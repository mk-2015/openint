#define _DEFAULT_SOURCE
#define _GNU_SOURCE
#include <sys/syscall.h>
#include <init/rescue.h>
#include <init/system.h>
#include <init/macros.h>
#include <init/log.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/klog.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <sys/wait.h>

extern char **environ;

#define MAX_RESCUE_ARGS 128

typedef struct
{
  pthread_mutex_t io_lock;
  _Atomic bool stop;
  _Atomic int latest_errno;
  char tty_path[64];
} rescue_ui_t;

static void format_tty_name(char *buf, size_t size)
{
  if (ttyname_r(STDOUT_FILENO, buf, size) != 0)
  {
    snprintf(buf, size, "unknown");
  }
}

static void draw_rescue_header_locked(rescue_ui_t *ui)
{
  int err_val = atomic_load(&ui->latest_errno);

  printf("\033[44m\033[97m\033[2J"
         "\033[1;1HRESCUE MODE"
         "\033[2;1HRESCUESH %s"
         "\033[3;1H\033[2KTTY=%s ERRNO=%d. Press F2 To reboot with kexec"
         "\033[4;r\033[4;1H",
         SH_V, ui->tty_path, err_val);
  fflush(stdout);
}

static void update_status_line_locked(rescue_ui_t *ui)
{
  int err_val = atomic_load(&ui->latest_errno);

  printf("\033[s\033[3;1H\033[2K\033[44m\033[97m"
         "TTY=%s ERRNO=%d. Press F2 To reboot with kexec"
         "\033[u",
         ui->tty_path, err_val);
  fflush(stdout);
}

static void *rescue_status_thread(void *arg)
{
  rescue_ui_t *ui = arg;

  while (!atomic_load(&ui->stop))
  {
    struct timespec ts = { .tv_sec = 1, .tv_nsec = 0 };
    nanosleep(&ts, NULL);

    if (atomic_load(&ui->stop))
      break;

    pthread_mutex_lock(&ui->io_lock);
    format_tty_name(ui->tty_path, sizeof(ui->tty_path));
    update_status_line_locked(ui);
    pthread_mutex_unlock(&ui->io_lock);
  }

  return NULL;
}

static void stop_rescue_ui(rescue_ui_t *ui, pthread_t *tid, bool *started)
{
  if (*started)
  {
    atomic_store(&ui->stop, true);
    pthread_join(*tid, NULL);
    *started = false;
  }
}

void parse_input(char *input, char **iargv)
{
  int argc = 0;
  char *token = strtok(input, " \t");

  while (token != NULL && argc < MAX_RESCUE_ARGS)
  {
    iargv[argc++] = token;
    token = strtok(NULL, " \t");
  }

  iargv[argc] = NULL;
}

static int mkdir_p(const char *path, mode_t mode)
{
  if (path == NULL || path[0] == '\0')
  {
    errno = EINVAL;
    return -1;
  }

  char tmp[PATH_MAX];
  int n = snprintf(tmp, sizeof(tmp), "%s", path);
  if (n < 0 || (size_t)n >= sizeof(tmp))
  {
    errno = ENAMETOOLONG;
    return -1;
  }

  size_t len = (size_t)n;
  while (len > 1 && tmp[len - 1] == '/')
  {
    tmp[--len] = '\0';
  }

  for (char *p = tmp + 1; *p != '\0'; p++)
  {
    if (*p == '/')
    {
      *p = '\0';
      if (mkdir(tmp, mode) == -1 && errno != EEXIST)
        return -1;
      *p = '/';
    }
  }

  if (mkdir(tmp, mode) == -1 && errno != EEXIST)
    return -1;

  return 0;
}

static int rm_recursive(const char *path, bool recursive, bool force)
{
  struct stat st;
  if (lstat(path, &st) == -1)
  {
    if (force && errno == ENOENT)
      return 0;
    return -1;
  }

  if (S_ISDIR(st.st_mode))
  {
    if (!recursive)
    {
      errno = EISDIR;
      return -1;
    }

    DIR *dir = opendir(path);
    if (dir == NULL)
      return -1;

    size_t base_len = strlen(path);
    bool has_trailing_slash = (base_len > 0 && path[base_len - 1] == '/');

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        continue;

      char child[PATH_MAX];
      int n = snprintf(child, sizeof(child),
                       has_trailing_slash ? "%s%s" : "%s/%s",
                       path, entry->d_name);
      if (n < 0 || (size_t)n >= sizeof(child))
      {
        closedir(dir);
        errno = ENAMETOOLONG;
        return -1;
      }

      if (rm_recursive(child, recursive, force) == -1)
      {
        int saved_errno = errno;
        closedir(dir);
        errno = saved_errno;
        return -1;
      }
    }

    if (closedir(dir) == -1)
      return -1;

    return rmdir(path);
  }

  return unlink(path);
}

static unsigned long parse_mount_options(const char *opts)
{
  unsigned long flags = 0;
  if (opts == NULL || *opts == '\0')
    return flags;

  char buf[256];
  size_t len = strlen(opts);
  if (len >= sizeof(buf))
    len = sizeof(buf) - 1;
  memcpy(buf, opts, len);
  buf[len] = '\0';

  char *saveptr = NULL;
  for (char *tok = strtok_r(buf, ",", &saveptr); tok != NULL; tok = strtok_r(NULL, ",", &saveptr))
  {
    if (strcmp(tok, "ro") == 0)
      flags |= MS_RDONLY;
    else if (strcmp(tok, "remount") == 0)
      flags |= MS_REMOUNT;
    else if (strcmp(tok, "bind") == 0)
      flags |= MS_BIND;
  }

  return flags;
}

static void print_dmesg(void)
{
  int fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK);
  if (fd >= 0)
  {
    char line[2048];
    ssize_t n;
    while ((n = read(fd, line, sizeof(line) - 1)) > 0)
    {
      line[n] = '\0';
      // /dev/kmsg format: "priority,seq,timestamp,-;message\n"
      char *msg = strchr(line, ';');
      if (msg != NULL)
        printf("%s", msg + 1);
      else
        printf("%s", line);
    }
    close(fd);
    return;
  }

  // Fallback to klogctl syscall if /dev/kmsg is unavailable
  int size = klogctl(10, NULL, 0); // SYSLOG_ACTION_SIZE_BUFFER
  if (size <= 0)
    size = 16384;

  char *buf = malloc((size_t)size + 1);
  if (buf == NULL)
  {
    printf("dmesg: %s\n", strerror(errno));
    return;
  }

  int len = klogctl(3, buf, size); // SYSLOG_ACTION_READ_ALL
  if (len < 0)
  {
    printf("dmesg: %s\n", strerror(errno));
    free(buf);
    return;
  }

  buf[len] = '\0';
  fwrite(buf, 1, (size_t)len, stdout);
  if (len > 0 && buf[len - 1] != '\n')
    putchar('\n');
  free(buf);
}

static int do_insmod(int argc, char **argv)
{
  if (argc < 2 || argv[1] == NULL)
  {
    printf("insmod: missing module file operand\n");
    return -1;
  }

  const char *path = argv[1];
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
  {
    printf("insmod: %s: %s\n", path, strerror(errno));
    return -1;
  }

  // Join optional module parameters into a single "k=v k2=v2" string
  char params[512] = {0};
  size_t used = 0;
  for (int i = 2; i < argc && argv[i] != NULL; i++)
  {
    size_t arg_len = strlen(argv[i]);
    size_t need = arg_len + (used > 0 ? 1 : 0);
    if (used + need >= sizeof(params))
    {
      close(fd);
      printf("insmod: module parameters exceed %zu bytes\n", sizeof(params) - 1);
      errno = E2BIG;
      return -1;
    }
    if (used > 0)
      params[used++] = ' ';
    memcpy(params + used, argv[i], arg_len);
    used += arg_len;
    params[used] = '\0';
  }

  long rc = syscall(SYS_finit_module, fd, params, 0);
  int saved_errno = errno;
  close(fd);

  if (rc != 0)
  {
    errno = saved_errno;
    printf("insmod: failed to load '%s': %s\n", path, strerror(errno));
    return -1;
  }

  return 0;
}

static int do_rmmod(int argc, char **argv)
{
  if (argc < 2 || argv[1] == NULL)
  {
    printf("rmmod: missing module name operand\n");
    return -1;
  }

  const char *name = argv[1];
  long rc = syscall(SYS_delete_module, name, O_NONBLOCK);
  if (rc != 0)
  {
    printf("rmmod: failed to unload '%s': %s\n", name, strerror(errno));
    return -1;
  }

  return 0;
}

static void do_lsmod(void)
{
  FILE *fp = fopen("/proc/modules", "r");
  if (fp == NULL)
  {
    printf("lsmod: /proc/modules: %s\n", strerror(errno));
    return;
  }

  char line[512];
  while (fgets(line, sizeof(line), fp) != NULL)
    fputs(line, stdout);

  fclose(fp);
}

static int do_setenv(int argc, char **argv)
{
  if (argc < 3 || argv[1] == NULL || argv[2] == NULL)
  {
    printf("setenv: usage: setenv <name> <value>\n");
    return -1;
  }

  if (argv[1][0] == '\0' || strchr(argv[1], '=') != NULL)
  {
    errno = EINVAL;
    printf("setenv: invalid variable name '%s'\n", argv[1]);
    return -1;
  }

  if (setenv(argv[1], argv[2], 1) == -1)
  {
    printf("setenv: %s: %s\n", argv[1], strerror(errno));
    return -1;
  }

  return 0;
}

static int do_getenv(int argc, char **argv)
{
  if (argc < 2 || argv[1] == NULL)
  {
    printf("getenv: usage: getenv <name>\n");
    return -1;
  }

  const char *val = getenv(argv[1]);
  if (val == NULL)
  {
    errno = ENOENT;
    printf("getenv: %s: not set\n", argv[1]);
    return -1;
  }

  printf("%s\n", val);
  return 0;
}

static void do_lsenv(void)
{
  if (environ == NULL)
    return;

  for (size_t i = 0; environ[i] != NULL; i++)
  {
    printf("%s\n", environ[i]);
  }
}

static int do_startw(int argc, char **argv, rescue_ui_t *ui, pthread_t *tid, bool *started)
{
  if (argc < 2 || argv[1] == NULL)
  {
    printf("startw: usage: startw <abs_path_to_program> [arguments...]\n");
    return -1;
  }

  const char *prog_path = argv[1];
  if (prog_path[0] != '/')
  {
    errno = EINVAL;
    printf("startw: '%s' must be an absolute path starting with '/'\n", prog_path);
    return -1;
  }

  // Pause the background status line updater while the external program owns the TTY
  stop_rescue_ui(ui, tid, started);
  printf("\033[r\033[0m");
  fflush(stdout);

  pid_t pid = fork();
  if (pid < 0)
  {
    int saved_errno = errno;
    pthread_mutex_lock(&ui->io_lock);
    format_tty_name(ui->tty_path, sizeof(ui->tty_path));
    draw_rescue_header_locked(ui);
    pthread_mutex_unlock(&ui->io_lock);
    *started = (pthread_create(tid, NULL, rescue_status_thread, ui) == 0);
    errno = saved_errno;
    printf("startw: fork failed: %s\n", strerror(errno));
    return -1;
  }

  if (pid == 0)
  {
    // Restore default signal handling before execve
    execv(prog_path, &argv[1]);
    fprintf(stderr, "startw: execv '%s': %s\n", prog_path, strerror(errno));
    _exit(127);
  }

  int status = 0;
  while (waitpid(pid, &status, 0) < 0)
  {
    if (errno != EINTR)
    {
      int saved_errno = errno;
      pthread_mutex_lock(&ui->io_lock);
      format_tty_name(ui->tty_path, sizeof(ui->tty_path));
      draw_rescue_header_locked(ui);
      pthread_mutex_unlock(&ui->io_lock);
      *started = (pthread_create(tid, NULL, rescue_status_thread, ui) == 0);
      errno = saved_errno;
      printf("startw: waitpid failed: %s\n", strerror(errno));
      return -1;
    }
  }

  // Restore rescue UI header, scroll region, and status thread after child exits
  pthread_mutex_lock(&ui->io_lock);
  format_tty_name(ui->tty_path, sizeof(ui->tty_path));
  draw_rescue_header_locked(ui);
  pthread_mutex_unlock(&ui->io_lock);
  *started = (pthread_create(tid, NULL, rescue_status_thread, ui) == 0);

  if (WIFEXITED(status))
  {
    int code = WEXITSTATUS(status);
    printf("startw: '%s' exited with status %d\n", prog_path, code);
    return (code == 0) ? 0 : -1;
  }

  if (WIFSIGNALED(status))
  {
    int sig = WTERMSIG(status);
    printf("startw: '%s' terminated by signal %d\n", prog_path, sig);
    return -1;
  }

  return 0;
}

int enter_rescue_mode(void)
{
  int boot_errno = errno;

  init_console();
  init_sh();
  set_filter(MAX_LOGLEVEL);

  rescue_ui_t ui = {
    .io_lock = PTHREAD_MUTEX_INITIALIZER,
    .stop = false,
    .latest_errno = boot_errno,
  };
  format_tty_name(ui.tty_path, sizeof(ui.tty_path));

  pthread_mutex_lock(&ui.io_lock);
  draw_rescue_header_locked(&ui);
  pthread_mutex_unlock(&ui.io_lock);

  pthread_t status_tid;
  bool status_started = (pthread_create(&status_tid, NULL, rescue_status_thread, &ui) == 0);

  char input[4096] = {0};
  char *iargv[MAX_RESCUE_ARGS + 1] = {NULL};

  while (true)
  {
    char hostname[HOST_NAME_MAX + 1] = {0};

    pthread_mutex_lock(&ui.io_lock);
    if (gethostname(hostname, sizeof(hostname)) == 0 &&
        hostname[0] != '\0' && strcmp(hostname, "(none)") != 0)
    {
      printf("\n(%s) RESCUE$ ", hostname);
    }
    else
    {
      printf("\n(RESCUE)$ ");
    }
    fflush(stdout);
    pthread_mutex_unlock(&ui.io_lock);

    if (fgets(input, sizeof(input), stdin) == NULL)
    {
      clearerr(stdin);
      continue;
    }

    input[strcspn(input, "\n")] = '\0';

    if (input[0] == '\0')
      continue;

    parse_input(input, iargv);

    if (iargv[0] == NULL)
      continue;

    pthread_mutex_lock(&ui.io_lock);

    if (strcmp(iargv[0], "reboot") == 0)
    {
      pthread_mutex_unlock(&ui.io_lock);
      break;
    }
    else if (strcmp(iargv[0], "clear") == 0)
    {
      clear_screen(STDOUT_FILENO);
      format_tty_name(ui.tty_path, sizeof(ui.tty_path));
      draw_rescue_header_locked(&ui);
    }
    else if (strcmp(iargv[0], "sync") == 0)
    {
      sync();
    }
    else if (strcmp(iargv[0], "shutdown") == 0)
    {
      sync();
      if (reboot(RB_POWER_OFF) == -1)
      {
        atomic_store(&ui.latest_errno, errno);
        printf("shutdown: %s\n", strerror(errno));
        fflush(stdout);
        pthread_mutex_unlock(&ui.io_lock);
        continue;
      }
      pthread_mutex_unlock(&ui.io_lock);
      stop_rescue_ui(&ui, &status_tid, &status_started);
      pthread_mutex_destroy(&ui.io_lock);
      return 0;
    }
    else if (strcmp(iargv[0], "hostname") == 0)
    {
      if (iargv[1] == NULL)
      {
        char cur[HOST_NAME_MAX + 1] = {0};
        if (gethostname(cur, sizeof(cur)) == 0)
          printf("%s\n", cur);
        else
        {
          atomic_store(&ui.latest_errno, errno);
          printf("hostname: %s\n", strerror(errno));
        }
      }
      else
      {
        if (sethostname(iargv[1], strlen(iargv[1])) == -1)
        {
          atomic_store(&ui.latest_errno, errno);
          printf("hostname: %s\n", strerror(errno));
        }
      }
      fflush(stdout);
    }
    else if (strcmp(iargv[0], "mkdir") == 0)
    {
      bool parents = false;
      int start = 1;

      if (iargv[1] != NULL && strcmp(iargv[1], "-p") == 0)
      {
        parents = true;
        start = 2;
      }

      if (iargv[start] == NULL)
      {
        printf("mkdir: missing operand\n");
        fflush(stdout);
        pthread_mutex_unlock(&ui.io_lock);
        continue;
      }

      for (int idx = start; idx < MAX_RESCUE_ARGS && iargv[idx] != NULL; idx++)
      {
        int rc = parents ? mkdir_p(iargv[idx], 0755) : mkdir(iargv[idx], 0755);
        if (rc == -1)
        {
          atomic_store(&ui.latest_errno, errno);
          printf("mkdir: cannot create directory '%s': %s\n", iargv[idx], strerror(errno));
        }
      }
      fflush(stdout);
    }
    else if (strcmp(iargv[0], "rm") == 0)
    {
      bool recursive = false;
      bool force = false;
      int idx = 1;

      while (idx < MAX_RESCUE_ARGS && iargv[idx] != NULL && iargv[idx][0] == '-')
      {
        if (strcmp(iargv[idx], "--") == 0)
        {
          idx++;
          break;
        }
        for (size_t c = 1; iargv[idx][c] != '\0'; c++)
        {
          if (iargv[idx][c] == 'r' || iargv[idx][c] == 'R')
            recursive = true;
          else if (iargv[idx][c] == 'f')
            force = true;
        }
        idx++;
      }

      if (iargv[idx] == NULL)
      {
        if (!force)
          printf("rm: missing operand\n");
        fflush(stdout);
        pthread_mutex_unlock(&ui.io_lock);
        continue;
      }

      for (; idx < MAX_RESCUE_ARGS && iargv[idx] != NULL; idx++)
      {
        if (strcmp(iargv[idx], "/") == 0 || strcmp(iargv[idx], "/*") == 0)
        {
          printf("rm: refusing to operate recursively on '%s'\n", iargv[idx]);
          continue;
        }
        if (strcmp(iargv[idx], ".") == 0 || strcmp(iargv[idx], "..") == 0)
        {
          printf("rm: refusing to remove '.' or '..'\n");
          continue;
        }
        if (rm_recursive(iargv[idx], recursive, force) == -1)
        {
          atomic_store(&ui.latest_errno, errno);
          printf("rm: cannot remove '%s': %s\n", iargv[idx], strerror(errno));
        }
      }
      fflush(stdout);
    }
    else if (strcmp(iargv[0], "mount") == 0)
    {
      // Usage: mount [-t fstype] [-o opts] <source> <target>
      const char *fstype = NULL;
      const char *source = NULL;
      const char *target = NULL;
      unsigned long flags = 0;

      for (int idx = 1; idx < MAX_RESCUE_ARGS && iargv[idx] != NULL; idx++)
      {
        if (strcmp(iargv[idx], "-t") == 0 && iargv[idx + 1] != NULL)
        {
          fstype = iargv[++idx];
        }
        else if (strcmp(iargv[idx], "-o") == 0 && iargv[idx + 1] != NULL)
        {
          flags |= parse_mount_options(iargv[++idx]);
        }
        else if (source == NULL)
        {
          source = iargv[idx];
        }
        else if (target == NULL)
        {
          target = iargv[idx];
        }
      }

      if (source == NULL && target == NULL)
      {
        FILE *fp = fopen("/proc/mounts", "r");
        if (fp != NULL)
        {
          char line[512];
          while (fgets(line, sizeof(line), fp) != NULL)
            fputs(line, stdout);
          fclose(fp);
        }
        else
        {
          atomic_store(&ui.latest_errno, errno);
          printf("mount: /proc/mounts: %s\n", strerror(errno));
        }
      }
      else if (target == NULL)
      {
        printf("mount: usage: mount [-t fstype] [-o opts] <source> <target>\n");
      }
      else
      {
        if (mount(source, target, fstype, flags, NULL) == -1)
        {
          atomic_store(&ui.latest_errno, errno);
          printf("mount: %s on %s failed: %s\n", source, target, strerror(errno));
        }
      }
      fflush(stdout);
    }
    else if (strcmp(iargv[0], "umount") == 0)
    {
      if (iargv[1] == NULL)
      {
        printf("umount: missing target operand\n");
        fflush(stdout);
        pthread_mutex_unlock(&ui.io_lock);
        continue;
      }

      int flags = 0;
      const char *target = iargv[1];
      if (strcmp(iargv[1], "-f") == 0 && iargv[2] != NULL)
      {
        flags = MNT_FORCE;
        target = iargv[2];
      }
      else if (strcmp(iargv[1], "-l") == 0 && iargv[2] != NULL)
      {
        flags = MNT_DETACH;
        target = iargv[2];
      }

      int rc = (flags != 0) ? umount2(target, flags) : umount(target);
      if (rc == -1)
      {
        atomic_store(&ui.latest_errno, errno);
        printf("umount: %s: %s\n", target, strerror(errno));
      }
      fflush(stdout);
    }
    else if (strcmp(iargv[0], "dmesg") == 0)
    {
      print_dmesg();
      fflush(stdout);
    }
    else if (strcmp(iargv[0], "getcwd") == 0 || strcmp(iargv[0], "pwd") == 0)
    {
      char cwd[PATH_MAX];
      if (getcwd(cwd, sizeof(cwd)) != NULL)
        printf("%s\n", cwd);
      else
      {
        atomic_store(&ui.latest_errno, errno);
        printf("getcwd: %s\n", strerror(errno));
      }
      fflush(stdout);
    }
    else if (strcmp(iargv[0], "chdir") == 0 || strcmp(iargv[0], "cd") == 0)
    {
      const char *target = (iargv[1] != NULL) ? iargv[1] : "/";
      if (chdir(target) == -1)
      {
        atomic_store(&ui.latest_errno, errno);
        printf("chdir: %s: %s\n", target, strerror(errno));
      }
      fflush(stdout);
    }
    else if (strcmp(iargv[0], "ls") == 0)
    {
      bool show_all = false;
      const char *target = ".";

      for (int idx = 1; idx < MAX_RESCUE_ARGS && iargv[idx] != NULL; idx++)
      {
        if (strcmp(iargv[idx], "-a") == 0)
          show_all = true;
        else
          target = iargv[idx];
      }

      DIR *dir = opendir(target);
      if (dir == NULL)
      {
        atomic_store(&ui.latest_errno, errno);
        printf("ls: %s: %s\n", target, strerror(errno));
        fflush(stdout);
        pthread_mutex_unlock(&ui.io_lock);
        continue;
      }

      size_t base_len = strlen(target);
      bool has_trailing_slash = (base_len > 0 && target[base_len - 1] == '/');

      struct dirent *entry;
      while ((entry = readdir(dir)) != NULL)
      {
        if (!show_all && entry->d_name[0] == '.')
          continue;

        char full_path[PATH_MAX];
        int n = snprintf(full_path, sizeof(full_path),
                         has_trailing_slash ? "%s%s" : "%s/%s",
                         target, entry->d_name);
        struct stat st;
        if (n >= 0 && (size_t)n < sizeof(full_path) &&
            stat(full_path, &st) == 0 && S_ISDIR(st.st_mode))
        {
          printf("%s/  ", entry->d_name);
        }
        else
        {
          printf("%s  ", entry->d_name);
        }
      }
      putchar('\n');
      closedir(dir);
      fflush(stdout);
    }
    else if (strcmp(iargv[0], "echo") == 0)
    {
      int nline = 1;
      int start = 1;
      if (iargv[1] != NULL && strcmp(iargv[1], "-n") == 0)
      {
        nline = 0;
        start = 2;
      }
      for (int idx = start; idx < MAX_RESCUE_ARGS && iargv[idx] != NULL; idx++)
      {
        if (idx > start)
          putchar(' ');
        printf("%s", iargv[idx]);
      }
      if (nline)
        putchar('\n');
      fflush(stdout);
    }
    else if (strcmp(iargv[0], "cat") == 0)
    {
      if (iargv[1] == NULL)
      {
        printf("cat: missing file operand\n");
        fflush(stdout);
        pthread_mutex_unlock(&ui.io_lock);
        continue;
      }
      for (int idx = 1; idx < MAX_RESCUE_ARGS && iargv[idx] != NULL; idx++)
      {
        const char *filename = iargv[idx];
        FILE *fileptr = fopen(filename, "r");
        if (fileptr == NULL)
        {
          atomic_store(&ui.latest_errno, errno);
          printf("cat: %s: %s\n", filename, strerror(errno));
          continue;
        }
        char buffer[128];
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), fileptr)) > 0)
        {
          fwrite(buffer, 1, bytes_read, stdout);
        }
        fclose(fileptr);
      }
      fflush(stdout);
    }
    else if (strcmp(iargv[0], "panic") == 0)
    {
      pthread_mutex_unlock(&ui.io_lock);
      stop_rescue_ui(&ui, &status_tid, &status_started);
      pthread_mutex_destroy(&ui.io_lock);
      exit(1);
    }
    else if (strcmp(iargv[0], "insmod") == 0)
    {
      int argc = 0;
      while (argc < MAX_RESCUE_ARGS && iargv[argc] != NULL)
        argc++;
      if (do_insmod(argc, iargv) != 0)
        atomic_store(&ui.latest_errno, errno);
      fflush(stdout);
    }
    else if (strcmp(iargv[0], "rmmod") == 0)
    {
      int argc = 0;
      while (argc < MAX_RESCUE_ARGS && iargv[argc] != NULL)
        argc++;
      if (do_rmmod(argc, iargv) != 0)
        atomic_store(&ui.latest_errno, errno);
      fflush(stdout);
    }
    else if (strcmp(iargv[0], "lsmod") == 0)
    {
      do_lsmod();
      fflush(stdout);
    }
else if (strcmp(iargv[0], "setenv") == 0)
    {
      int argc = 0;
      while (argc < MAX_RESCUE_ARGS && iargv[argc] != NULL)
        argc++;
      if (do_setenv(argc, iargv) != 0)
        atomic_store(&ui.latest_errno, errno);
      fflush(stdout);
    }
    else if (strcmp(iargv[0], "getenv") == 0)
    {
      int argc = 0;
      while (argc < MAX_RESCUE_ARGS && iargv[argc] != NULL)
        argc++;
      if (do_getenv(argc, iargv) != 0)
        atomic_store(&ui.latest_errno, errno);
      fflush(stdout);
    }
    else if (strcmp(iargv[0], "lsenv") == 0 || strcmp(iargv[0], "env") == 0)
    {
      do_lsenv();
      fflush(stdout);
    }
    else if (strcmp(iargv[0], "startw") == 0)
    {
      int argc = 0;
      while (argc < MAX_RESCUE_ARGS && iargv[argc] != NULL)
        argc++;
      pthread_mutex_unlock(&ui.io_lock);
      int rc = do_startw(argc, iargv, &ui, &status_tid, &status_started);
      pthread_mutex_lock(&ui.io_lock);
      if (rc != 0)
        atomic_store(&ui.latest_errno, errno);
      fflush(stdout);
    }
    else
    {
      printf("%s: command not found\n", iargv[0]);
      fflush(stdout);
    }

    pthread_mutex_unlock(&ui.io_lock);
  }

  stop_rescue_ui(&ui, &status_tid, &status_started);
  pthread_mutex_destroy(&ui.io_lock);

  printf("\033[r\033[0m\n");
  fflush(stdout);
  sync();
  reboot(RB_AUTOBOOT);
  return 0;
}