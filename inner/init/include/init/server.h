#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Tokenizes a writable buflen-byte string with one extra byte available.
 * Supports single/double quotes and backslash escaping, but no shell expansion.
 * Returns argc and stores a NULL-terminated argv vector in argv_out. The caller
 * frees the vector, not its strings. On failure, returns UINT64_MAX and sets errno.
 */
uint64_t parse_argv(size_t buflen, void *buffer, char ***argv_out);
