#include <init/system.h>
#include <init/log.h>
#include <init/macros.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdlib.h>

#define MAX_RETRIES 128
#ifndef SERVER_SOCKET_PATH
#define SERVER_SOCKET_PATH "/run/oinit.sock"
#endif

#define RECEIVE_CHUNK_SIZE 32u

extern void *action(size_t length, void *buffer);

static char *receive_request(int client_fd, size_t *length)
{
    char chunk[RECEIVE_CHUNK_SIZE];
    char *buffer = NULL;
    size_t used = 0;
    size_t capacity = 0;

    for (;;)
    {
        ssize_t received = recv(client_fd, chunk, sizeof(chunk), 0);
        if (received < 0)
        {
            if (errno == EINTR)
                continue;
            log_message(LOG_WARN, "Failed to receive client data: %s",
                        strerror(errno));
            free(buffer);
            return NULL;
        }
        if (received == 0)
            break;

        size_t chunk_len = (size_t)received;
        if (used == SIZE_MAX || chunk_len > SIZE_MAX - used - 1)
        {
            errno = EOVERFLOW;
            free(buffer);
            return NULL;
        }

        size_t required = used + chunk_len + 1;
        if (required > capacity)
        {
            size_t new_capacity = capacity == 0 ? RECEIVE_CHUNK_SIZE : capacity;
            while (new_capacity < required)
            {
                if (new_capacity > SIZE_MAX / 2)
                {
                    new_capacity = required;
                    break;
                }
                new_capacity *= 2;
            }

            char *new_buffer = realloc(buffer, new_capacity);
            if (new_buffer == NULL)
            {
                free(buffer);
                return NULL;
            }
            buffer = new_buffer;
            capacity = new_capacity;
        }

        memcpy(buffer + used, chunk, chunk_len);
        used += chunk_len;
    }

    if (buffer == NULL)
    {
        buffer = malloc(1);
        if (buffer == NULL)
            return NULL;
    }
    buffer[used] = '\0';
    *length = used;
    return buffer;
}

static int send_response(int client_fd, const char *response)
{
    size_t remaining = strlen(response);
    const char *next = response;
    while (remaining > 0)
    {
        ssize_t sent = send(client_fd, next, remaining, MSG_NOSIGNAL);
        if (sent < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (sent == 0)
        {
            errno = EPIPE;
            return -1;
        }
        next += sent;
        remaining -= (size_t)sent;
    }
    return 0;
}

static int handle_client(int client_fd)
{
    size_t length;
    char *buffer = receive_request(client_fd, &length);
    if (buffer == NULL)
        return -1;

    void *response = action(length, buffer);
    int result = 0;
    if (response != NULL)
        result = send_response(client_fd, response);
    free(buffer);
    return result;
}

static int server_main(uint64_t argc, char **argv)
{
    (void)argc;
    (void)argv;

    int listener_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener_fd < 0)
    {
        log_message(LOG_ERROR, "Failed to create server socket: %s",
                    strerror(errno));
        return -1;
    }

    struct sockaddr_un address = {0};
    address.sun_family = AF_UNIX;
    size_t path_len = sizeof(SERVER_SOCKET_PATH);
    if (path_len > sizeof(address.sun_path))
    {
        close(listener_fd);
        errno = ENAMETOOLONG;
        log_message(LOG_ERROR, "Server socket path is too long.");
        return -1;
    }
    memcpy(address.sun_path, SERVER_SOCKET_PATH, path_len);

    socklen_t address_len =
        (socklen_t)(offsetof(struct sockaddr_un, sun_path) + path_len);
    int bound = 0;
    if (bind(listener_fd, (struct sockaddr *)&address, address_len) < 0)
    {
        log_message(LOG_ERROR, "Failed to bind %s: %s", SERVER_SOCKET_PATH,
                    strerror(errno));
        goto fail;
    }
    bound = 1;

    if (listen(listener_fd, 16) < 0)
    {
        log_message(LOG_ERROR, "Failed to listen on %s: %s",
                    SERVER_SOCKET_PATH, strerror(errno));
        goto fail;
    }
    log_message(LOG_INFO, "Listening on %s.", SERVER_SOCKET_PATH);

    for (;;)
    {
        int client_fd = accept(listener_fd, NULL, NULL);
        if (client_fd < 0)
        {
            if (errno == EINTR)
                continue;
            log_message(LOG_ERROR, "Accept on %s failed: %s",
                        SERVER_SOCKET_PATH, strerror(errno));
            goto fail;
        }

        if (handle_client(client_fd) < 0)
            log_message(LOG_WARN, "Failed to handle client request: %s",
                        strerror(errno));

        if (close(client_fd) < 0)
            log_message(LOG_WARN, "Failed to close accepted client: %s",
                        strerror(errno));
    }

fail:
    {
        int saved_errno = errno;
        close(listener_fd);
        if (bound && unlink(SERVER_SOCKET_PATH) < 0 && errno != ENOENT)
            log_message(LOG_WARN, "Failed to remove %s: %s",
                        SERVER_SOCKET_PATH, strerror(errno));
        errno = saved_errno;
    }
    return -1;
}

void server_thread(void *args)
{
    if (args == NULL)
    {
        log_message(LOG_ERROR, "Server thread received no arguments.");
        return;
    }

    const args_t *arguments = args;
    int retry = 0;

    while (true)
    {
        if (retry >= MAX_RETRIES)
        {
            log_message(LOG_ERROR, "Server undexpectedly %d quit times, Limit reached. Server failure.",retry);
            break;
        }

        int code = server_main(arguments->argc, arguments->argv);
        if (code != 0)
        {
            log_message(LOG_DEBUG, "Server unexpectedly quit, restarting...");
            retry++;
            sleep(1);
        } else {
            break;
        }
    }

    return;
}