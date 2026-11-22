#define _DEFAULT_SOURCE
#include <init/log.h>
#include <init/macros.h>
#include <init/system.h>
#include <unistd.h>
#include <limits.h>
#include <stdio.h>


#define DEFAULT_HOSTNAME "system"

void set_hostname(void)
{
    char hostname[HOST_NAME_MAX + 1];

    FILE *fphostname = fopen("/etc/hostname", "r");
    if (!fphostname)
    {
        log_message(LOG_WARN, "Cannot open /etc/hostname, defaulting to system.");
        sethostname(DEFAULT_HOSTNAME, strlen(DEFAULT_HOSTNAME));
        return;
    }

    size_t len = fread(hostname, 1, HOST_NAME_MAX, fphostname);
    
    int extra_char = fgetc(fphostname);
    fclose(fphostname);

    if (len == 0 || (extra_char != EOF && extra_char != '\n' && extra_char != '\r'))
    {
        log_message(LOG_WARN, "Invalid or oversized hostname, defaulting to system.");
        sethostname(DEFAULT_HOSTNAME, strlen(DEFAULT_HOSTNAME));
        return;
    }

    hostname[len] = '\0';

    while (len > 0 && (hostname[len - 1] == '\n' || hostname[len - 1] == '\r'))
    {
        hostname[--len] = '\0';
    }

    if (len == 0 || sethostname(hostname, len) < 0)
    {
        log_message(LOG_WARN, "Failed to set hostname, defaulting to system.");
        sethostname(DEFAULT_HOSTNAME, strlen(DEFAULT_HOSTNAME));
    }

    log_message(LOG_INFO, "Set hostname to %s", hostname);
}
