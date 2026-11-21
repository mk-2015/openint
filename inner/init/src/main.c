#include <stdio.h>
#include <init/macros.h>
#include <init/log.h>

int main(int argc, char const *argv[])
{
    /*
    mkdir("/dev", 0755);
    mkdir("/proc", 0555);
    mkdir("/sys", 0555);
    mkdir("/run", 0755);

    mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
    mount("proc", "/proc", "proc", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL);
    mount("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL);
    mount("tmpfs", "/run", "tmpfs", MS_NOSUID | MS_NODEV, "mode=755");
    */

    if (getpid() != 1)
    {
        log_message(LOG_ERROR, "System not booted as init.");
        exit(EXIT_FAILURE);
    }

    init_console();
    set_filter(LOG_INFO);

    clear_screen(STDOUT_FILENO);
    clear_screen(STDERR_FILENO);
    log_message(LOG_INFO, "Hello, Init!");

    log_message(LOG_DEBUG, "Starting Init System!");

    log_message(LOG_DEBUG, "Finished Init System");

    while (true) {/* Do nothing */}
}
