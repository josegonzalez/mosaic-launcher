#ifndef FAKESETS_H
#define FAKESETS_H

// A fake host's entry sets, for testing a plugin that registers them: the
// sets it registered and unregistered, and what each holds when asked.

#include <stdio.h>
#include <string.h>

#define FAKE_SETS_MAX 256

static char fake_set_names[FAKE_SETS_MAX][80];
static int (*fake_set_has[FAKE_SETS_MAX])(const char* name, const char* path);
static void (*fake_set_list[FAKE_SETS_MAX])(const char* name, void (*add)(const char* path, void* ctx), void* ctx);
static int fake_set_count = 0;

static int fakeSetFind(const char* name) {
	for (int i = 0; i < fake_set_count; i++) {
		if (!strcmp(fake_set_names[i], name)) return i;
	}
	return -1;
}

static int fakeSetRegister(const char* name, int (*has)(const char* name, const char* path),
		void (*list)(const char* name, void (*add)(const char* path, void* ctx), void* ctx)) {
	int i = fakeSetFind(name);
	if (i < 0) {
		if (fake_set_count >= FAKE_SETS_MAX) return -1;
		i = fake_set_count++;
		snprintf(fake_set_names[i], sizeof(fake_set_names[0]), "%s", name);
	}
	fake_set_has[i] = has;
	fake_set_list[i] = list;
	return 0;
}

static int fakeSetUnregister(const char* name) {
	int i = fakeSetFind(name);
	if (i < 0) return -1;
	for (int j = i; j < fake_set_count - 1; j++) {
		memcpy(fake_set_names[j], fake_set_names[j + 1], sizeof(fake_set_names[0]));
		fake_set_has[j] = fake_set_has[j + 1];
		fake_set_list[j] = fake_set_list[j + 1];
	}
	fake_set_count--;
	return 0;
}

static int fakeIsFiltered(const char* path) {
	(void)path;
	return 0;
}

// set_has on a registered set, or -1 for none.
static int fakeSetHas(const char* name, const char* path) {
	int i = fakeSetFind(name);
	return i < 0 ? -1 : fake_set_has[i](name, path) != 0;
}

static void fakeSetAdd(const char* path, void* ctx) {
	char* out = ctx;
	size_t len = strlen(out);
	snprintf(out + len, 2048 - len, "%s\n", path);
}

// What a registered set lists, one path a line, or "none" for no set.
static const char* fakeSetList(const char* name) {
	static char out[2048];
	out[0] = '\0';
	int i = fakeSetFind(name);
	if (i < 0) return "none";
	fake_set_list[i](name, fakeSetAdd, out);
	return out;
}

#endif
