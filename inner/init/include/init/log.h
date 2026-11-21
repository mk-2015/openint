#pragma once
#include <signal.h>

enum log_level {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    MAX_LOGLEVEL = LOG_ERROR
};

extern int filter;
extern int console_init;

void init_console(void);
void set_filter(int filterin);
void log_message(int level, const char *format, ...);
void load(volatile sig_atomic_t *timeout, const char *format, ...);
void clear_screen(int fd);