// Not a plugin: a library scripts/screenshots.sh preloads into the desktop
// build beside presser, so a shot can show a device's sample values in place
// of the computer's own, such as system-info's IP address and storage. It
// changes nothing unless its variables are set:
//
//   DEVICE_IP       an interface and IPv4 address, eg. "wlan0 192.168.1.50":
//                   getifaddrs lists only it
//   DEVICE_STORAGE  free and total bytes, eg. "13207024435 63887638528":
//                   statvfs answers them for every path
//   DEVICE_MEMINFO  text /proc/meminfo reads as
//   DEVICE_UPTIME   text /proc/uptime reads as, eg. "9240.00 0.00"

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dlfcn.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/statvfs.h>

static struct ifaddrs* fake_list = NULL; // the list handed out, for freeIfaddrs

static int fakeIfaddrs(struct ifaddrs** out) {
	const char* spec = getenv("DEVICE_IP");
	char name[IFNAMSIZ] = "", addr[64] = "";
	if (!spec || sscanf(spec, "%15s %63s", name, addr) != 2) return -1;
	struct ifaddrs* entry = calloc(1, sizeof(*entry) + sizeof(struct sockaddr_in) + strlen(name) + 1);
	if (!entry) return -1;
	struct sockaddr_in* sin = (struct sockaddr_in*)(entry + 1);
	char* copy = (char*)(sin + 1);
	strcpy(copy, name);
	sin->sin_family = AF_INET;
	if (inet_pton(AF_INET, addr, &sin->sin_addr) != 1) {
		free(entry);
		return -1;
	}
	entry->ifa_name = copy;
	entry->ifa_flags = IFF_UP | IFF_RUNNING;
	entry->ifa_addr = (struct sockaddr*)sin;
	fake_list = entry;
	*out = entry;
	return 0;
}

static int fakeStatvfs(struct statvfs* st) {
	const char* spec = getenv("DEVICE_STORAGE");
	unsigned long long free_bytes = 0, total = 0;
	if (!spec || sscanf(spec, "%llu %llu", &free_bytes, &total) != 2) return -1;
	memset(st, 0, sizeof(*st));
	st->f_bsize = st->f_frsize = 4096;
	st->f_blocks = (fsblkcnt_t)(total / 4096);
	st->f_bfree = st->f_bavail = (fsblkcnt_t)(free_bytes / 4096);
	return 0;
}

// The text a /proc file reads as, or NULL to read the real one.
static const char* fakeProc(const char* path) {
	if (!path) return NULL;
	if (!strcmp(path, "/proc/meminfo")) return getenv("DEVICE_MEMINFO");
	if (!strcmp(path, "/proc/uptime")) return getenv("DEVICE_UPTIME");
	return NULL;
}

#ifdef __APPLE__
// dyld's interposing, as in presser
static int getIfaddrs(struct ifaddrs** out) {
	if (getenv("DEVICE_IP")) return fakeIfaddrs(out);
	return getifaddrs(out);
}
static void freeIfaddrs(struct ifaddrs* list) {
	if (list && list == fake_list) {
		fake_list = NULL;
		free(list);
		return;
	}
	freeifaddrs(list);
}
static int statVfs(const char* path, struct statvfs* st) {
	if (getenv("DEVICE_STORAGE")) return fakeStatvfs(st);
	return statvfs(path, st);
}
static FILE* fOpen(const char* path, const char* mode) {
	const char* text = fakeProc(path);
	if (text) return fmemopen((void*)text, strlen(text), "r");
	return fopen(path, mode);
}

#define INTERPOSE(replacement, replacee) \
	__attribute__((used)) static const struct { const void* a; const void* b; } interpose_##replacee \
	__attribute__((section("__DATA,__interpose"))) = { (const void*)&replacement, (const void*)&replacee }
INTERPOSE(getIfaddrs, getifaddrs);
INTERPOSE(freeIfaddrs, freeifaddrs);
INTERPOSE(statVfs, statvfs);
INTERPOSE(fOpen, fopen);
#else
// LD_PRELOAD: these come before libc's, which they find with RTLD_NEXT.
int getifaddrs(struct ifaddrs** out) {
	if (getenv("DEVICE_IP")) return fakeIfaddrs(out);
	int (*real)(struct ifaddrs**) = (int (*)(struct ifaddrs**))dlsym(RTLD_NEXT, "getifaddrs");
	return real ? real(out) : -1;
}
void freeifaddrs(struct ifaddrs* list) {
	if (list && list == fake_list) {
		fake_list = NULL;
		free(list);
		return;
	}
	void (*real)(struct ifaddrs*) = (void (*)(struct ifaddrs*))dlsym(RTLD_NEXT, "freeifaddrs");
	if (real) real(list);
}
int statvfs(const char* path, struct statvfs* st) {
	if (getenv("DEVICE_STORAGE")) return fakeStatvfs(st);
	int (*real)(const char*, struct statvfs*) = (int (*)(const char*, struct statvfs*))dlsym(RTLD_NEXT, "statvfs");
	return real ? real(path, st) : -1;
}
FILE* fopen(const char* path, const char* mode) {
	const char* text = fakeProc(path);
	if (text) return fmemopen((void*)text, strlen(text), "r");
	FILE* (*real)(const char*, const char*) = (FILE * (*)(const char*, const char*)) dlsym(RTLD_NEXT, "fopen");
	return real ? real(path, mode) : NULL;
}
#endif
