#define _POSIX_C_SOURCE 200809L

#include <init/log.h>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <termios.h>

int filter = LOG_DEBUG;
int console_init = 0;

static char *format_message(const char *format, va_list args)
{
  va_list copy;
  va_copy(copy, args);
  int length = vsnprintf(NULL, 0, format, copy);
  va_end(copy);

  if (length < 0)
    return NULL;

  char *message = malloc((size_t)length + 1);
  if (message == NULL)
    return NULL;

  if (vsnprintf(message, (size_t)length + 1, format, args) < 0)
  {
    free(message);
    return NULL;
  }

  return message;
}

static int open_active_console(void)
{
  char active[128] = {0};
  char dev_path[160] = "/dev/console";

  FILE *fp = fopen("/sys/class/tty/console/active", "r");
  if (fp != NULL)
  {
    if (fgets(active, sizeof(active), fp) != NULL)
    {
      active[strcspn(active, "\n")] = '\0';
      char *last = strrchr(active, ' ');
      char *name = (last != NULL) ? (last + 1) : active;

      if (strcmp(name, "tty0") == 0)
        snprintf(dev_path, sizeof(dev_path), "/dev/tty1");
      else if (name[0] != '\0')
        snprintf(dev_path, sizeof(dev_path), "/dev/%s", name);
    }
    fclose(fp);
  }

  int fd = open(dev_path, O_RDWR);
  if (fd < 0)
    fd = open("/dev/console", O_RDWR);

  return fd;
}

void init_console(void)
{
  if (!console_init)
  {
    (void)setsid();
    int fd = open_active_console();

    if (fd >= 0)
    {
      if (ioctl(fd, TIOCSCTTY, 1) == -1 && errno != ENOTTY)
        perror("log: ioctl active console");

      for (int target = STDIN_FILENO; target <= STDERR_FILENO; ++target)
      {
        if (dup2(fd, target) == -1)
          perror("log: dup2 active console");
      }

      if (fd > STDERR_FILENO)
        close(fd);

      console_init = 1;
    }
  }

  tcsetpgrp(STDIN_FILENO, getpgrp());
  struct termios tty;
  if (tcgetattr(STDIN_FILENO, &tty) == 0)
  {
    tty.c_lflag |= (ECHO | ECHOE | ECHOK | ICANON | ISIG);
    tty.c_iflag |= ICRNL;
    tty.c_oflag |= (OPOST | ONLCR);
    tcsetattr(STDIN_FILENO, TCSANOW, &tty);
  }
}

void set_filter(int filterin)
{
  if (filterin < LOG_DEBUG || filterin > MAX_LOGLEVEL)
  {
    errno = EINVAL;
    perror("log: invalid filter");
    return;
  }

  filter = filterin;
}

void clear_screen(int fd)
{
  const char *clear_seq = "\033[2J\033[3J\033[H";
  write(fd, clear_seq, 11);
}

void log_message(int level, const char *format, ...)
{
  if (level < LOG_DEBUG || level > MAX_LOGLEVEL)
  {
    errno = EINVAL;
    perror("log: invalid level");
    return;
  }
  if (level < filter)
    return;

  va_list args;
  va_start(args, format);
  char *message = format_message(format, args);
  va_end(args);
  if (message == NULL)
  {
    perror("log: formatting message");
    return;
  }

  static const char *const names[] = {"DEBUG", "INFO", "WARN", "ERROR"};
  flockfile(stdout);
  if (level == LOG_DEBUG)
    fprintf(stdout, "%s\n", message);
  else
    fprintf(stdout, "[    %s    ] %s\n", names[level], message);
  fflush(stdout);
  funlockfile(stdout);
  free(message);
}

void load(volatile sig_atomic_t *timeout, const char *format, ...)
{
  if (timeout == NULL)
  {
    errno = EINVAL;
    perror("log: load requires a timeout flag");
    return;
  }

  va_list args;
  va_start(args, format);
  char *message = format_message(format, args);
  va_end(args);
  if (message == NULL)
  {
    perror("log: formatting load message");
    return;
  }

  static const char spinner[] = "|/-\\";
  struct timespec delay = {.tv_sec = 0, .tv_nsec = 100000000};
  size_t frame = 0;
  while (*timeout != 1)
  {
    flockfile(stdout);
    fprintf(stdout, "\r%s %c", message, spinner[frame++ % (sizeof(spinner) - 1)]);
    fflush(stdout);
    funlockfile(stdout);

    while (nanosleep(&delay, &delay) == -1 && errno == EINTR)
    {
      if (*timeout == 1)
        break;
    }
    delay.tv_sec = 0;
    delay.tv_nsec = 100000000;
  }
  flockfile(stdout);
  fprintf(stdout, "\r%s done\n", message);
  fflush(stdout);
  funlockfile(stdout);
  free(message);
}
