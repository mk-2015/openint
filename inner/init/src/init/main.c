#define _GNU_SOURCE
#include <init/macros.h>
#include <init/log.h>
#include <init/system.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <dirent.h>
#include <stdio.h>

int rescue = 0;

void parse_args(int argc, char **argv)
{
  for (int idx = 1; idx < argc; idx++)
  {
    if (strcmp(argv[idx], "--rescue") == 0)
    {
      rescue = 1;
    }
  }
}

int start_init_system(int argc, char **argv)
{
  init_console();
  parse_args(argc, argv);

  if (rescue)
  {
    log_message(LOG_WARN, "Entering rescue mode...");
    sleep(1);
    clear_screen(STDOUT_FILENO);
    clear_screen(STDERR_FILENO);
    return enter_rescue_mode();
  }

  set_hostname();
  start_server((uint64_t)argc, argv);
  ensure();
  set_environment();

  return 0;
}