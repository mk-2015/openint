#include <init/log.h>
#include <init/macros.h>
#include <init/system.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

void ensure(void) {
    if (mkdir(CONFIG_ROOT, 0700) == -1) {
        if (errno != EEXIST) {
            log_message(LOG_WARN, "Cannot create config root, sticking with defaults.");
            return;
        }
    } else {
        log_message(LOG_DEBUG, "Created Configuration Root successfully. at=%s", CONFIG_ROOT);
    }
    
    FILE* fp = fopen(CONFIG_ENV, "w");
    if (fp) {
        fprintf(fp, "# Auto-generated\nenv:\nHOME: \"/root\"\n");
        fclose(fp);
        log_message(LOG_DEBUG, "Created environment file successfully!");
    } else {
        log_message(LOG_WARN, "Cannot create '%s'", CONFIG_ENV);
    }
}
