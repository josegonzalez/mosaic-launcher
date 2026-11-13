#ifndef FAKELOADED_H
#define FAKELOADED_H

// A fake host's plugin_loaded, for testing a plugin that asks which other
// plugins are loaded: the ids in fake_loaded, comma-separated, are.

#include <stddef.h>
#include <string.h>

static const char* fake_loaded = "";

static int fakePluginLoaded(const char* id) {
	size_t len = strlen(id);
	for (const char* p = fake_loaded; *p;) {
		const char* end = strchr(p, ',');
		if (!end) end = p + strlen(p);
		if ((size_t)(end - p) == len && !strncmp(p, id, len)) return 1;
		p = *end ? end + 1 : end;
	}
	return 0;
}

// A host table from before plugin_loaded, as an older launcher's.
#define FAKE_HOST_BEFORE_LOADED offsetof(MosaicHost, plugin_loaded)

#endif
