#ifndef CALLSITE_H
#define CALLSITE_H

// Which loaded library some code is in, for host calls that work from a
// plugin's own thread, where caller.h has no caller: the launcher matches
// the library a call came from against the plugins it loaded.

// The base address of the library holding addr, or NULL when it isn't in
// one the dynamic linker knows.
void* Callsite_base(const void* addr);

#endif
