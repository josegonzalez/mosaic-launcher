#ifndef _GNU_SOURCE
#define _GNU_SOURCE // dladdr
#endif
#include <dlfcn.h>
#include <stddef.h>
#include "callsite.h"

void* Callsite_base(const void* addr) {
	Dl_info info;
	if (!addr || !dladdr(addr, &info)) return NULL;
	return info.dli_fbase;
}
