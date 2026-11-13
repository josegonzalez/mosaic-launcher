#include <dlfcn.h>
#include <stddef.h>
#include "callsite.h"

// built with _GNU_SOURCE, for dladdr (see the makefile)
void* Callsite_base(const void* addr) {
	Dl_info info;
	if (!addr || !dladdr(addr, &info)) return NULL;
	return info.dli_fbase;
}
