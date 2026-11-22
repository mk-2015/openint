#define _GNU_SOURCE
#include <init/log.h>
#include <init/macros.h>
#include <init/power.h>
#include <init/system.h>
#include <sys/reboot.h>
#include <sys/syscall.h>
#include <dirent.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>

static void kill_thread_handler(int sig)
{
    (void)sig;
    pthread_exit(NULL);
}

static void kill_other_init_threads(void)
{
    struct sigaction sa = {0};
    sa.sa_handler = kill_thread_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    pid_t self_tid = syscall(SYS_gettid);

    for (int attempt = 0; attempt < 50; attempt++) {
        DIR *dir = opendir("/proc/1/task");
        if (!dir) return;

        int other_threads = 0;
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') continue;

            pid_t tid = (pid_t)atoi(entry->d_name);
            if (tid > 0 && tid != self_tid) {
                other_threads++;
                syscall(SYS_tgkill, 1, tid, SIGUSR1);
            }
        }
        closedir(dir);

        if (other_threads == 0) break;

        struct timespec ts = { .tv_sec = 0, .tv_nsec = 10000000 }; // 10ms
        nanosleep(&ts, NULL);
    }
}

void __NO_RETURN mpower(int cmd)
{
    unsigned int fcmd = cmd;
    if (fcmd == RB_POWER_OFF)
        log_message(LOG_INFO, "Powering off system...");
    else if (fcmd == RB_AUTOBOOT)
        log_message(LOG_INFO, "Rebooting system....");
    else if (fcmd == RB_HALT_SYSTEM)
        log_message(LOG_INFO, "Halting system...");
    else {
        log_message(LOG_WARN, "Invalid cmd to mpowerctl. Doing default reboot.");
        fcmd = RB_AUTOBOOT;
    }

    kill_other_init_threads();

    kill(-1, SIGTERM);
    struct timespec rem = {
        .tv_sec = 1,
        .tv_nsec = 250000000
    };
    while (nanosleep(&rem, &rem) == -1 && errno == EINTR)
        ;
    kill(-1, SIGKILL);

    sync();
    unmount_system();
    sync();

    reboot(fcmd);

    log_message(LOG_ERROR, "Reboot syscall failed. Freezing!");
    static const char banner[] =
        "\n"
        "                        ██ ██████   ██ ▄█████   ▄█████ ▄████▄ ██████ ██████\n"
        "                        ██   ██     ██ ▀▀▀▄▄▄   ▀▀▀▄▄▄ ██▄▄██ ██▄▄   ██▄▄\n"
        "                        ██   ██     ██ █████▀   █████▀ ██  ██ ██     ██▄▄▄▄\n"
        "\n"
        "                ██████ ▄████▄   ██████ ██  ██ █████▄  ███  ██   ▄████▄ ██████ ██████\n"
        "                  ██   ██  ██     ██   ██  ██ ██▄▄██▄ ██ ▀▄██   ██  ██ ██▄▄   ██▄▄\n"
        "                  ██   ▀████▀     ██   ▀████▀ ██   ██ ██   ██   ▀████▀ ██     ██\n"
        "\n"
        "                                ██████ ██  ██ ██████   █████▄ ▄█████\n"
        "                                  ██   ██████ ██▄▄     ██▄▄█▀ ██\n"
        "                                  ██   ██  ██ ██▄▄▄▄   ██     ▀█████\n";

    ssize_t written = write(STDOUT_FILENO, banner, sizeof(banner) - 1);
    (void)written;
    fflush(stdout);
    fflush(stderr);
    for(;;){pause();}
}

void __NO_RETURN mshutdown(void)
{
    mpower(RB_POWER_OFF);
}

void __NO_RETURN mreboot(void)
{
    mpower(RB_AUTOBOOT);
}

void __NO_RETURN mhalt(void)
{
    mpower(RB_HALT_SYSTEM);
}
