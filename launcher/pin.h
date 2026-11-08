#ifndef PIN_H
#define PIN_H

// The launcher's one PIN, which simple mode asks for to leave and the
// Launcher pak's Plugins page asks for before protected settings. It lives
// in a file of the launcher's own (PIN_PATH in plugins.h), not in a plugin's
// cfg, so no plugin reads it through cfg_get; plugins only ask the host to
// check what the user types. No SDL here, and every call takes the file's
// path, so the host tests can check it on its own.

#define PIN_LEN 64

// in with spaces, tabs and line breaks trimmed from both ends, cut to
// PIN_LEN - 1. NULL is empty.
void Pin_clean(const char* in, char out[PIN_LEN]);

// The PIN in the file at path, cleaned, or empty when there's none. Returns
// 1 when one is set.
int Pin_read(const char* path, char out[PIN_LEN]);

// 1 when the file at path holds a PIN.
int Pin_isSet(const char* path);

// 1 when typed, cleaned, is the PIN. With no PIN set, or a NULL typed (the
// keyboard was cancelled), it never matches.
int Pin_matches(const char* path, const char* typed);

// Saves pin, cleaned, or removes the file when that's empty. Returns 0 on
// success.
int Pin_write(const char* path, const char* pin);

// Simple mode kept its PIN as the pin key of its own cfg before the
// launcher had one. Moves a PIN found there to path, unless path already
// has one, and blanks the cfg's copy either way. Returns 1 when it moved
// one.
int Pin_adopt(const char* path, const char* cfg_path);

#endif
