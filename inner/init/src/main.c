#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <init/macros.h>
#include <init/log.h>
#include <init/system.h>

int main(int argc, char *argv[])
{
  if (getpid() != 1)
  {
    log_message(LOG_ERROR, "System not booted as init.");
    exit(EXIT_FAILURE);
  }

  mount_system();
  init_console();
  set_filter(LOG_INFO);

  clear_screen(STDOUT_FILENO);
  clear_screen(STDERR_FILENO);
  log_message(LOG_INFO, "Hello, Init!");

  log_message(LOG_DEBUG, "Starting Init System!");
  int code = start_init_system(argc, argv);
  (void)code;
  log_message(LOG_DEBUG, "Finished Init System");

  while (true)
  {
    /* Do nothing */
  }
}
