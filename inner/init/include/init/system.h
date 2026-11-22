#pragma once

#include <stdint.h>

int enter_rescue_mode(void);
int start_init_system(int argc, char** argv);
void mount_system(void);
void unmount_system(void);
void ensure(void);

void set_hostname();
void start_server(uint64_t argc, char **argv);
void set_environment(void);

typedef struct {
  uint64_t argc;
  char **argv;
} args_t;

#define CONFIG_ROOT    "/etc/oinit.d"
#define CONFIG_ENV     CONFIG_ROOT "/env.yaml"