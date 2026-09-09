#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <signal.h>
#include <ctype.h>
#include <strings.h>

#include <minit/api.h>
#include <minit/service.h>
#include <libunotifi/unotifi.h>
#include <libubookd/booker.h>

#define SOCKET_PATH "/run/minitd/contrl.sock"
#define MAX_EVENTS 16
#define MAX_ARGV 16

static pthread_t g_socket_thread;
static volatile int g_socket_running = 1;
static pthread_mutex_t g_service_mutex = PTHREAD_MUTEX_INITIALIZER;

extern volatile sig_atomic_t g_shutdown_requested;
extern volatile sig_atomic_t g_reboot_requested;

extern Book* g_Book;

/* Quote-aware string tokenizer */
static char *next_token(char **str) {
    if (!str || !*str) return NULL;

    char *p = *str;

    /* Skip leading whitespace */
    while (*p && isspace((unsigned char)*p)) p++;

    if (*p == '\0') {
        *str = NULL;
        return NULL;
    }

    char *start;
    if (*p == '"' || *p == '\'') {
        char quote = *p;
        p++; /* Skip opening quote */
        start = p;
        while (*p && *p != quote) p++;
        if (*p == quote) {
            *p = '\0'; /* Null-terminate at closing quote */
            p++;
        }
    } else {
        start = p;
        while (*p && !isspace((unsigned char)*p)) {
            if (*p == ':' && (p[1] == '"' || p[1] == '\'')) {
                char quote = p[1];
                p += 2;
                while (*p && *p != quote) p++;
                if (*p == quote) p++;
                continue;
            }
            p++;
        }
        if (*p) {
            *p = '\0'; /* Null-terminate at space */
            p++;
        }
    }

    for (char *value = start; *value; value++) {
        if (*value == ':' && (value[1] == '"' || value[1] == '\'')) {
            char quote = value[1];
            memmove(value + 1, value + 2, strlen(value + 2) + 1);
            char *end = strchr(value + 1, quote);
            if (end) {
                memmove(end, end + 1, strlen(end + 1) + 1);
            }
            break;
        }
    }

    *str = p;
    return start;
}

/* Trim extra \r or \n to \0 */
static void trim_cmd(char *str) {
    if (!str) return;
    int len = strlen(str);
    while (len > 0 && (str[len - 1] == '\r' || str[len - 1] == '\n' || str[len - 1] == ' ')) {
        str[--len] = '\0';
    }
}

/* Parse buffer into an argv array using next_token (quote-aware) */
static int tokenize_cmd(char *buf, char *argv[], int max_args) {
    int argc = 0;
    char *cursor = buf;

    trim_cmd(buf);

    while (cursor && *cursor && argc < max_args - 1) {
        char *token = next_token(&cursor);
        if (token && *token) {
            argv[argc++] = token;
        }
    }
    argv[argc] = NULL;

    return argc;
}

static int parse_log_level(const char *value, LogLevel *level) {
    if (strcasecmp(value, "debug") == 0) {
        *level = LOG_LEVEL_DEBUG;
    } else if (strcasecmp(value, "info") == 0) {
        *level = LOG_LEVEL_INFO;
    } else if (strcasecmp(value, "warning") == 0 || strcasecmp(value, "warn") == 0) {
        *level = LOG_LEVEL_WARNING;
    } else if (strcasecmp(value, "error") == 0) {
        *level = LOG_LEVEL_ERROR;
    } else if (strcasecmp(value, "critical") == 0 || strcasecmp(value, "crit") == 0) {
        *level = LOG_LEVEL_CRITICAL;
    } else {
        return -1;
    }

    return 0;
}

/* Process command using libunotifi */
static void __proc_cmd_unotifi(int fd, const char *raw_buf) {
    if (!raw_buf) {
        unotifi_send_response(fd, "ERR EMPTY_PAYLOAD");
        return;
    }

    char *cmd_copy = strdup(raw_buf);
    if (!cmd_copy) {
        unotifi_send_response(fd, "ERR MEMORY_ALLOC");
        return;
    }

    char *argv[MAX_ARGV];
    int argc = tokenize_cmd(cmd_copy, argv, MAX_ARGV);

    if (argc == 0) {
        unotifi_send_response(fd, "ERR EMPTY_COMMAND");
        free(cmd_copy);
        return;
    }

    char resp_buf[2048] = {0};

    /* ---------------- POWERCTL HANDLER ---------------- */
    if (strcmp(argv[0], "POWERCTL") == 0) {
        if (argc < 2) {
            unotifi_send_response(fd, "ERR USAGE: POWERCTL <reboot|poweroff|halt>");
            free(cmd_copy);
            return;
        }

        const char *action = argv[1];
        if (strcmp(action, "reboot") == 0) {
            unotifi_send_response(fd, "OK REBOOTING");
            g_reboot_requested = 1;
        } else if (strcmp(action, "poweroff") == 0 || strcmp(action, "halt") == 0) {
            unotifi_send_response(fd, "OK SHUTTING_DOWN");
            g_shutdown_requested = 1;
        } else {
            snprintf(resp_buf, sizeof(resp_buf), "ERR UNKNOWN_POWER_ACTION '%s'", action);
            unotifi_send_response(fd, resp_buf);
        }
    }

    /* ---------------- LOG HANDLER ---------------- */
    else if (strcmp(argv[0], "LOG") == 0) {
        if (argc < 2) {
            unotifi_send_response(fd, "ERR USAGE: LOG <ADD|QUERY|SUB>");
            free(cmd_copy);
            return;
        }

        if (strcasecmp(argv[1], "ADD") == 0) {
            const char *level_value = NULL;
            const char *message = NULL;
            LogLevel level;

            for (int i = 2; i < argc; i++) {
                char *separator = strchr(argv[i], ':');
                if (!separator || separator == argv[i] || separator[1] == '\0') {
                    unotifi_send_response(fd,
                                          "ERR USAGE: LOG ADD level:<level> message:<text>");
                    free(cmd_copy);
                    return;
                }

                *separator = '\0';
                if (strcasecmp(argv[i], "level") == 0) {
                    level_value = separator + 1;
                } else if (strcasecmp(argv[i], "message") == 0) {
                    message = separator + 1;
                } else {
                    snprintf(resp_buf, sizeof(resp_buf), "ERR UNKNOWN_ARGUMENT '%s'", argv[i]);
                    unotifi_send_response(fd, resp_buf);
                    free(cmd_copy);
                    return;
                }
            }

            if (!level_value || !message) {
                unotifi_send_response(fd, "ERR USAGE: LOG ADD level:<level> message:<text>");
                free(cmd_copy);
                return;
            }

            if (parse_log_level(level_value, &level) != 0) {
                snprintf(resp_buf, sizeof(resp_buf), "ERR UNKNOWN_LOG_LEVEL '%s'", level_value);
                unotifi_send_response(fd, resp_buf);
                free(cmd_copy);
                return;
            }

            if (BookWriteLog(g_Book, "%s", level, message) < 0) {
                unotifi_send_response(fd, "ERR LOG_ADD_FAILED");
            } else {
                unotifi_send_response(fd, "OK LOG ADDED");
            }
        } else if (strcasecmp(argv[1], "QUERY") == 0 || strcasecmp(argv[1], "SEARCH") == 0) {
            const char *query = NULL;
            unsigned int matches = 0;

            for (int i = 2; i < argc; i++) {
                char *separator = strchr(argv[i], ':');
                if (!separator || strcasecmp(argv[i], "query") != 0) {
                    unotifi_send_response(fd, "ERR USAGE: LOG QUERY query:<text>");
                    free(cmd_copy);
                    return;
                }
                query = separator + 1;
            }

            if (!query || *query == '\0') {
                unotifi_send_response(fd, "ERR USAGE: LOG QUERY query:<text>");
                free(cmd_copy);
                return;
            }

            int result = BookQueryLog(g_Book, query, resp_buf, sizeof(resp_buf), &matches);
            if (result == -2) {
                unotifi_send_response(fd, "ERR LOG_QUERY_TOO_LARGE");
            } else if (result < 0) {
                unotifi_send_response(fd, "ERR LOG_QUERY_FAILED");
            } else if (matches == 0) {
                unotifi_send_response(fd, "OK LOG QUERY count=0");
            } else {
                char header[64];
                snprintf(header, sizeof(header), "OK LOG QUERY count=%u\n", matches);
                size_t header_length = strlen(header);
                size_t result_length = strlen(resp_buf);
                if (header_length + result_length < sizeof(resp_buf)) {
                    memmove(resp_buf + header_length, resp_buf, result_length + 1);
                    memcpy(resp_buf, header, header_length);
                    unotifi_send_response(fd, resp_buf);
                } else {
                    unotifi_send_response(fd, "ERR LOG_QUERY_TOO_LARGE");
                }
            }
        } else if (strcasecmp(argv[1], "SUB") == 0 || strcasecmp(argv[1], "REMOVE") == 0) {
            const char *query = NULL;
            unsigned int removed = 0;

            for (int i = 2; i < argc; i++) {
                char *separator = strchr(argv[i], ':');
                if (!separator || strcasecmp(argv[i], "query") != 0) {
                    unotifi_send_response(fd, "ERR USAGE: LOG SUB query:<text>");
                    free(cmd_copy);
                    return;
                }
                query = separator + 1;
            }

            if (!query || *query == '\0') {
                unotifi_send_response(fd, "ERR USAGE: LOG SUB query:<text>");
                free(cmd_copy);
                return;
            }

            if (BookRemoveLog(g_Book, query, &removed) < 0) {
                unotifi_send_response(fd, "ERR LOG_SUB_FAILED");
            } else {
                snprintf(resp_buf, sizeof(resp_buf), "OK LOG SUB count=%u", removed);
                unotifi_send_response(fd, resp_buf);
            }
        } else {
            snprintf(resp_buf, sizeof(resp_buf), "ERR UNKNOWN_LOG_COMMAND '%s'", argv[1]);
            unotifi_send_response(fd, resp_buf);
        }
    }

    /* ---------------- SERVICE HANDLER ---------------- */
    else if (strcmp(argv[0], "SERVICE") == 0) {
        if (argc < 2) {
            unotifi_send_response(fd, "ERR USAGE: SERVICE <LIST|START|STOP|STOPALL|UNREGISTER>");
            free(cmd_copy);
            return;
        }

        const char *subcmd = argv[1];

        if (strcmp(subcmd, "LIST") == 0) {
            pthread_mutex_lock(&g_service_mutex);
            size_t offset = snprintf(resp_buf, sizeof(resp_buf), "OK COUNT %zu\n", ServiceTableCount);
            for (size_t i = 0; i < ServiceTableCount && offset < sizeof(resp_buf); i++) {
                ServiceIndex *s = &ServiceTable[i];
                offset += snprintf(resp_buf + offset, sizeof(resp_buf) - offset,
                                   "SVC id=%d pid=%d policy=%s name=%s path=%s\n",
                                   s->serviceid,
                                   s->pid,
                                   (s->sp == SERVICE_ALWAYS) ? "ALWAYS" : "ONCE",
                                   s->name ? s->name : "null",
                                   s->exec_file_path ? s->exec_file_path : "null");
            }
            pthread_mutex_unlock(&g_service_mutex);
            unotifi_send_response(fd, resp_buf);
        }

        else if (strcmp(subcmd, "START") == 0) {
            if (argc < 4) {
                unotifi_send_response(fd, "ERR USAGE: SERVICE START <exec_path> <name> [ONCE|ALWAYS]");
                free(cmd_copy);
                return;
            }

            const char *path = argv[2];
            const char *name = argv[3];
            ServicePolicy policy = SERVICE_ONCE;

            if (argc >= 5 && strcmp(argv[4], "ALWAYS") == 0) {
                policy = SERVICE_ALWAYS;
            }

            pthread_mutex_lock(&g_service_mutex);
            ServiceIndex si = CreateService(policy, path, name, NULL, NULL, NULL, NULL, NULL, NULL, SERVICE_TYPE_SIMPLE, RESTART_NO);
            if (RegisterService(&si) == 0) {
                pthread_mutex_unlock(&g_service_mutex);
                unotifi_send_response(fd, "ERR REGISTER_FAILED");
                FreeServiceIndex(&si);
                free(cmd_copy);
                return;
            }

            if (StartService(&si) < 0) {
                pthread_mutex_unlock(&g_service_mutex);
                unotifi_send_response(fd, "ERR START_FAILED");
            } else {
                snprintf(resp_buf, sizeof(resp_buf), "OK SERVICE_STARTED id=%d pid=%d", si.serviceid, si.pid);
                pthread_mutex_unlock(&g_service_mutex);
                unotifi_send_response(fd, resp_buf);
            }
        }

        else if (strcmp(subcmd, "STOP") == 0) {
            if (argc < 3) {
                unotifi_send_response(fd, "ERR USAGE: SERVICE STOP <serviceid>");
                free(cmd_copy);
                return;
            }

            int sid = atoi(argv[2]);
            pthread_mutex_lock(&g_service_mutex);
            if (StopService(sid) == 0) {
                snprintf(resp_buf, sizeof(resp_buf), "OK SERVICE_STOPPED id=%d", sid);
            } else {
                snprintf(resp_buf, sizeof(resp_buf), "ERR STOP_FAILED id=%d", sid);
            }
            pthread_mutex_unlock(&g_service_mutex);
            unotifi_send_response(fd, resp_buf);
        }

        else if (strcmp(subcmd, "STOPALL") == 0) {
            pthread_mutex_lock(&g_service_mutex);
            StopAllServices();
            pthread_mutex_unlock(&g_service_mutex);
            unotifi_send_response(fd, "OK ALL_SERVICES_STOPPED");
        }

        else if (strcmp(subcmd, "UNREGISTER") == 0) {
            if (argc < 3) {
                unotifi_send_response(fd, "ERR USAGE: SERVICE UNREGISTER <serviceid>");
                free(cmd_copy);
                return;
            }

            int sid = atoi(argv[2]);
            pthread_mutex_lock(&g_service_mutex);
            if (UnregisterService(sid) == 0) {
                snprintf(resp_buf, sizeof(resp_buf), "OK SERVICE_UNREGISTERED id=%d", sid);
            } else {
                snprintf(resp_buf, sizeof(resp_buf), "ERR UNREGISTER_FAILED id=%d", sid);
            }
            pthread_mutex_unlock(&g_service_mutex);
            unotifi_send_response(fd, resp_buf);
        }

        else {
            snprintf(resp_buf, sizeof(resp_buf), "ERR UNKNOWN_SERVICE_SUBCMD '%s'", subcmd);
            unotifi_send_response(fd, resp_buf);
        }
    }

    else {
        snprintf(resp_buf, sizeof(resp_buf), "ERR UNKNOWN_COMMAND '%s'", argv[0]);
        unotifi_send_response(fd, resp_buf);
    }

    free(cmd_copy);
}

/* Worker thread function */
void *__unix_socket_minit(void *arg) {
    (void)arg;

    while(!g_Book || !g_Book->inited) {
        usleep(100000); // Spawned in another thread
        // wont block execution, but we need to wait for the book to be initialized before we can log anything
    }

    int server_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (server_fd < 0) {
        return NULL;
    }

    mkdir("/run/minitd", 0755);
    unlink(SOCKET_PATH);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    socklen_t addr_len = offsetof(struct sockaddr_un, sun_path) + strlen(addr.sun_path) + 1;

    if (bind(server_fd, (struct sockaddr *)&addr, addr_len) < 0) {
        close(server_fd);
        return NULL;
    }

    chmod(SOCKET_PATH, 0660);

    if (listen(server_fd, 16) < 0) {
        close(server_fd);
        unlink(SOCKET_PATH);
        return NULL;
    }

    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0) {
        close(server_fd);
        unlink(SOCKET_PATH);
        return NULL;
    }

    struct epoll_event ev, events[MAX_EVENTS];
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);

    while (g_socket_running) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 500);

        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;

            if (fd == server_fd) {
                int client_fd = accept4(server_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
                if (client_fd >= 0) {
                    int flags = fcntl(client_fd, F_GETFL, 0);
                    if (flags < 0 || fcntl(client_fd, F_SETFL, flags & ~O_NONBLOCK) < 0) {
                        close(client_fd);
                        continue;
                    }

                    char *client_info = NULL;
                    if (unotifi_recv_handshake(client_fd, &client_info) != UNOTIFI_OK ||
                        unotifi_send_handshake_response(client_fd, "minitd") != UNOTIFI_OK) {
                        free(client_info);
                        close(client_fd);
                        continue;
                    }
                    free(client_info);

                    if (fcntl(client_fd, F_SETFL, flags) < 0) {
                        close(client_fd);
                        continue;
                    }

                    ev.events = EPOLLIN | EPOLLONESHOT;
                    ev.data.fd = client_fd;
                    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);
                }
            } else {
                char *payload = NULL;

                if (unotifi_recv_data(fd, &payload) == UNOTIFI_OK && payload) {
                    __proc_cmd_unotifi(fd, payload);
                    free(payload);

                    ev.events = EPOLLIN | EPOLLONESHOT;
                    ev.data.fd = fd;
                    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
                } else {
                    unotifi_send_response(fd, "ERR RECV_FAILED");
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                }
            }
        }
    }

    close(server_fd);
    close(epoll_fd);
    unlink(SOCKET_PATH);
    return NULL;
}

/* Exported main server launcher */
void __main_server(void) {
    if (pthread_create(&g_socket_thread, NULL, __unix_socket_minit, NULL) != 0) {
        perror("[ FAIL ] Failed to create socket thread");
        return;
    }

    pthread_detach(g_socket_thread);
}