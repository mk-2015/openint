#include <init/log.h>
#include <init/system.h>
#include <init/power.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <time.h> 

#define MREBOOT    0
#define MSHUTDOWN  1
#define MHALT      2

#define MAX_WAIT   30

void* powerctl(char* action, bool now)
{
    int int_action = MREBOOT;

    if (strcmp(action, "reboot") == 0)
    {
        int_action = MREBOOT;
    }
    else if (strcmp(action, "shutdown") == 0)
    {
        int_action = MSHUTDOWN;
    }
    else if (strcmp(action, "halt") == 0)
    {
        #ifdef _DISALLOW_HALT
        log_message(LOG_DEBUG, "Halting is not supported! or Not set!");
        return (void*)"Halting is not supported";
        #else
        int_action = MHALT;
        #endif
    }
    else
    {
        log_message(LOG_WARN, "Cannot execute power action %s, defaulting to reboot.", action);
    }

    if (!now)
    {
        log_message(LOG_INFO, "You have %d seconds to save your work!", MAX_WAIT);
        struct timespec rem = { .tv_sec = MAX_WAIT, .tv_nsec = 0 };
        while (nanosleep(&rem, &rem) == -1 && errno == EINTR)
            ;
    }

    switch (int_action)
    {
    case MSHUTDOWN:
        mshutdown();
        break;

    case MHALT:
        mhalt();
        break;

    case MREBOOT:
        [[fallthrough]];
    default:
        mreboot();
    }

    return (void*)"Doing specified action.";
}
