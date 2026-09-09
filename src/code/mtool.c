#include <minit/api.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

#define OKPRINT     "[   OK    ] "
#define FAILPRINT   "[  FAIL   ] "
#define INFOPRINT   "[  INFO   ] "
#define WARNPRINT   "[  WARN   ] "

static void print_prefixed(const char* prefix, const char* str, va_list args)
{
    fputs(prefix, stdout);
    vprintf(str, args);
    fputc('\n', stdout);
    fflush(stdout);
}

void print_ok(const char* str, ...)
{
    va_list args;
    va_start(args, str);
    print_prefixed(OKPRINT, str, args);
    va_end(args);
}

void print_fail(const char* str, ...)
{
    va_list args;
    va_start(args, str);
    print_prefixed(FAILPRINT, str, args);
    va_end(args);
}

void print_info(const char* str, ...)
{
    va_list args;
    va_start(args, str);
    print_prefixed(INFOPRINT, str, args);
    va_end(args);
}

void print_warn(const char* str, ...)
{
    va_list args;
    va_start(args, str);
    print_prefixed(WARNPRINT, str, args);
    va_end(args);
}

static void sleep_micros(unsigned int micros)
{
    struct timespec req;
    req.tv_sec = micros / 1000000;
    req.tv_nsec = (long)(micros % 1000000) * 1000L;
    nanosleep(&req, NULL);
}

void spinner(unsigned int delaymicros, const bool *condition, const char* str, ...)
{
    static const char* frames[] = {
        "-*-    ",
        " -*-   ",
        "  -*-  ",
        "   -*- ",
        "    -*-",
        "-    -*",
        "*-    -"
    };
    size_t num_frames = sizeof(frames) / sizeof(frames[0]);
    size_t i = 0;
    va_list args;

    // Hide cursor (\033[?25l)
    fputs("\033[?25l", stdout);

    while (1) {
        // Break out immediately if condition becomes true
        if (condition && *condition) {
            break;
        }

        // Carriage return (\r) and clear line (\033[K)
        fputs("\r\033[K", stdout);

        printf("[%s] ", frames[i]);

        va_start(args, str);
        vprintf(str, args);
        va_end(args);

        fflush(stdout);

        sleep_micros(delaymicros);

        // Advance to next frame and wrap around
        i = (i + 1) % num_frames;
    }

    // Restore cursor (\033[?25h) and clear active spinner line
    fputs("\r\033[K\033[?25h", stdout);
    fflush(stdout);
}