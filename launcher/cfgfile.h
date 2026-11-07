#ifndef CFGFILE_H
#define CFGFILE_H

// key=value files, like a plugin's .userdata/<platform>/launcher/<id>.cfg.
// The last line for a key wins.

#include <stddef.h>

// Copies the value for key into out, or fallback when it isn't set. Returns
// 1 when the key was set.
int Cfg_get(const char* path, const char* key, const char* fallback, char* out, size_t size);

// Sets key, keeping the file's other lines in order. Returns 0 on success.
// Keys can't hold '=' or newlines, and values can't hold newlines.
int Cfg_set(const char* path, const char* key, const char* value);

#endif
