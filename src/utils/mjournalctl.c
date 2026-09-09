#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libunotifi/unotifi.h>

#define SOCKET_PATH "/run/minitd/contrl.sock"
#define COMMAND_SIZE 4096

static void print_usage(const char *program) {
    fprintf(stderr,
            "Usage:\n"
            "  %s add <level> <message>\n"
            "  %s query <text>\n"
            "  %s sub <text>\n",
            program, program, program);
}

static int append_quoted(char *command, size_t size, size_t *used,
                         const char *value) {
    if (*used + 2 >= size) return -1;
    command[(*used)++] = '"';
    for (const char *cursor = value; *cursor; cursor++) {
        if (*cursor == '"' || *cursor == '\\') {
            if (*used + 2 >= size) return -1;
            command[(*used)++] = '\\';
        }
        if (*used + 1 >= size) return -1;
        command[(*used)++] = *cursor;
    }
    command[(*used)++] = '"';
    command[*used] = '\0';
    return 0;
}

static int append_words(char *command, size_t size, size_t *used,
                        int argc, char **argv, int first) {
    for (int i = first; i < argc; i++) {
        if (i > first) {
            if (*used + 1 >= size) return -1;
            command[(*used)++] = ' ';
        }
        if (append_quoted(command, size, used, argv[i]) != 0) return -1;
    }
    return 0;
}

static int send_command(const char *command) {
    int fd = openCommunicationChannel(SOCKET_PATH);
    if (fd < 0) {
        perror("connect");
        return 1;
    }

    if (unotifi_send_handshake(fd, getpid(), "mjournalctl") != UNOTIFI_OK) {
        fprintf(stderr, "Handshake failed\n");
        close(fd);
        return 1;
    }

    char *handshake_info = NULL;
    if (unotifi_recv_handshake(fd, &handshake_info) != UNOTIFI_OK) {
        fprintf(stderr, "Handshake response failed\n");
        close(fd);
        return 1;
    }
    free(handshake_info);

    int exit_code = 0;
    if (unotifi_send_data(fd, command) != UNOTIFI_OK) {
        fprintf(stderr, "Failed to send command\n");
        exit_code = 1;
    } else {
        char *response = NULL;
        if (unotifi_recv_data(fd, &response) == UNOTIFI_OK && response) {
            puts(response);
            if (strncmp(response, "ERR ", 4) == 0) exit_code = 1;
            free(response);
        } else {
            fprintf(stderr, "Failed to receive response\n");
            exit_code = 1;
        }
    }

    unotifi_send_unshake_close(fd);
    close(fd);
    return exit_code;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 2;
    }

    int is_add = strcmp(argv[1], "add") == 0;
    int is_query = strcmp(argv[1], "query") == 0;
    int is_sub = strcmp(argv[1], "sub") == 0;
    if (!is_add && !is_query && !is_sub) {
        print_usage(argv[0]);
        return 2;
    }

    char command[COMMAND_SIZE];
    int written = snprintf(command, sizeof(command), "LOG %s ",
                           is_add ? "ADD" : (is_query ? "QUERY" : "SUB"));
    if (written < 0 || (size_t)written >= sizeof(command)) return 2;
    size_t used = (size_t)written;

    if (is_add) {
        if (argc < 4) {
            print_usage(argv[0]);
            return 2;
        }
        written = snprintf(command + used, sizeof(command) - used,
                           "level:%s message:", argv[2]);
        if (written < 0 || (size_t)written >= sizeof(command) - used) return 2;
        used += (size_t)written;
        if (append_words(command, sizeof(command), &used, argc, argv, 3) != 0) {
            fprintf(stderr, "Message is too long\n");
            return 2;
        }
    } else {
        written = snprintf(command + used, sizeof(command) - used, "query:");
        if (written < 0 || (size_t)written >= sizeof(command) - used) return 2;
        used += (size_t)written;
        if (append_words(command, sizeof(command), &used, argc, argv, 2) != 0) {
            fprintf(stderr, "Query is too long\n");
            return 2;
        }
    }

    return send_command(command);
}