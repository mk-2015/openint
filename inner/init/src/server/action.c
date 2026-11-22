#include <init/server.h>
#include <init/log.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void *action_impl(uint64_t argc, char *argv[]);

static int reserve_argv(char ***argv, size_t *capacity, size_t required)
{
    if (required > SIZE_MAX / sizeof(**argv))
    {
        errno = EOVERFLOW;
        return -1;
    }

    if (required <= *capacity)
        return 0;

    size_t new_capacity = *capacity == 0 ? 8 : *capacity;
    while (new_capacity < required)
    {
        if (new_capacity > (SIZE_MAX / sizeof(**argv)) / 2)
        {
            new_capacity = required;
            break;
        }
        new_capacity *= 2;
    }

    char **new_argv = realloc(*argv, new_capacity * sizeof(**argv));
    if (new_argv == NULL)
        return -1;

    *argv = new_argv;
    *capacity = new_capacity;
    return 0;
}

uint64_t parse_argv(size_t buflen, void *buffer, char ***argv_out)
{
    if (argv_out == NULL || buffer == NULL)
    {
        errno = EINVAL;
        return UINT64_MAX;
    }
    *argv_out = NULL;

    char *data = buffer;
    if (memchr(data, '\0', buflen) != NULL)
    {
        errno = EINVAL;
        return UINT64_MAX;
    }

    char **argv = NULL;
    size_t argv_capacity = 0;
    char *read = data;
    char *write = data;
    char *end = data + buflen;
    size_t argc = 0;

    while (read < end)
    {
        while (read < end && isspace((unsigned char)*read))
            read++;
        if (read == end)
            break;

        if (reserve_argv(&argv, &argv_capacity, argc + 1) < 0)
            goto fail;
        argv[argc++] = write;

        char quote = '\0';
        while (read < end)
        {
            char current = *read++;
            if (quote == '\0' && isspace((unsigned char)current))
                break;

            if (current == '\\' && quote != '\'')
            {
                if (read == end)
                {
                    errno = EINVAL;
                    goto fail;
                }
                *write++ = *read++;
                continue;
            }

            if (current == '\'' && quote != '"')
            {
                quote = quote == '\'' ? '\0' : '\'';
                continue;
            }
            if (current == '"' && quote != '\'')
            {
                quote = quote == '"' ? '\0' : '"';
                continue;
            }

            *write++ = current;
        }

        if (quote != '\0')
        {
            errno = EINVAL;
            goto fail;
        }
        *write++ = '\0';
    }

    if (reserve_argv(&argv, &argv_capacity, argc + 1) < 0)
        goto fail;
    argv[argc] = NULL;
    *argv_out = argv;
    return (uint64_t)argc;

fail:
    free(argv);
    return UINT64_MAX;
}

void *action(size_t length, void *buffer)
{
    if (buffer == NULL)
    {
        errno = EINVAL;
        return NULL;
    }
    if (length == SIZE_MAX)
    {
        errno = EOVERFLOW;
        return NULL;
    }

    char *parse_buffer = malloc(length + 1);
    if (parse_buffer == NULL)
    {
        log_message(LOG_ERROR, "Failed to allocate command parsing buffer: %s",
                    strerror(errno));
        return NULL;
    }
    memcpy(parse_buffer, buffer, length);
    parse_buffer[length] = '\0';

    char **argv = NULL;
    uint64_t argc = parse_argv(length, parse_buffer, &argv);
    if (argc == UINT64_MAX)
    {
        int parse_error = errno;
        free(parse_buffer);
        errno = parse_error;
        log_message(LOG_WARN, "Rejected malformed command request: %s",
                    strerror(errno));
        return NULL;
    }

    if (argc == 0)
    {
        free(argv);
        free(parse_buffer);
        errno = EINVAL;
        return NULL;
    }

    void *result = action_impl(argc, argv);

    /* Free buffers AFTER action_impl finishes using them */
    free(argv);
    free(parse_buffer);

    return result;
}

#include <stdbool.h>
extern void* powerctl(char* action, bool now);

void *action_impl(uint64_t argc, char *argv[])
{
    if (argc == 0 || argv == NULL)
    {
        errno = EINVAL;
        return NULL;
    }

    if (strcmp(argv[0], "powerctl") == 0)
    {
        if (argc < 2)
        {
            errno = EINVAL;
            return NULL;
        }

        bool now = (argc >= 3 && strcmp(argv[2], "now") == 0);
        return powerctl(argv[1], now);
    }

    errno = ENOSYS;
    return NULL;
}

