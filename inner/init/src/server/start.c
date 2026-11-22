#include <pthread.h>
#include <init/log.h>
#include <init/system.h>
#include <init/macros.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

extern void server_thread(void *);

static void *server_entry(void *argument)
{
  server_thread(argument);
  free(argument);
  return NULL;
}

void start_server(uint64_t argc, char **argv)
{
  args_t *arguments = malloc(sizeof(*arguments));
  if (arguments == NULL)
  {
    log_message(LOG_ERROR, "Failed to allocate server thread arguments.");
    return;
  }
  arguments->argc = argc;
  arguments->argv = argv;

  pthread_attr_t attributes;
  int error = pthread_attr_init(&attributes);
  if (error != 0)
  {
    free(arguments);
    log_message(LOG_ERROR, "Failed to initialize server thread: %s",
                strerror(error));
    return;
  }

  error = pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
  if (error != 0)
  {
    int destroy_error = pthread_attr_destroy(&attributes);
    free(arguments);
    if (destroy_error != 0)
      log_message(LOG_WARN, "Failed to release server thread attributes: %s",
                  strerror(destroy_error));
    log_message(LOG_ERROR, "Failed to configure server thread: %s",
                strerror(error));
    return;
  }

  pthread_t thread;
  error = pthread_create(&thread, &attributes, server_entry, arguments);
  int destroy_error = pthread_attr_destroy(&attributes);
  if (error != 0)
  {
    free(arguments);
    log_message(LOG_ERROR, "Failed to create server thread: %s",
                strerror(error));
    return;
  }
  if (destroy_error != 0)
    log_message(LOG_WARN, "Failed to release server thread attributes: %s",
                strerror(destroy_error));

  log_message(LOG_INFO, "Started server thread.");
}