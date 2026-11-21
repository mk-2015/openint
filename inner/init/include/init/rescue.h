#pragma once

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stdlib.h>

#define SH_V "v1.0.0"

static inline void init_sh()
{
    setenv("HOME", "/root", 1);
    setenv("SHELL", "rescuesh", 1);
}