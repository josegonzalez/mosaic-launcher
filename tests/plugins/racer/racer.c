// Loaded after binder, it tries for FN3 too, and logs whose binding FN3
// runs before and after, along with refusals for bad arguments.

#include <stdarg.h>
#include <stdio.h>
#include "mosaic_plugin.h"

#define ID "racer"

static const MosaicHost* host;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[700];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static void sayBinding(const char* when) {
	MosaicFnBinding b = { .size = sizeof(b) };
	int rc = host->fn_binding(MOSAIC_BUTTON_FN3, &b);
	say("%s %i by %i action '%s' plugin '%s'", when, rc, b.by, b.action, b.plugin_id);
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, fn_release)) {
		say("no fn_release");
		return 0;
	}
	sayBinding("before");
	say("bind FN3 %i", host->fn_bind(MOSAIC_BUTTON_FN3, "pak:Clock.pak"));
	sayBinding("after");
	MosaicFnBinding none = { .size = sizeof(none) };
	say("refused A %i bad %i empty %i null %i release %i binding A %i null %i name A '%s'",
		host->fn_bind(MOSAIC_BUTTON_A, "pak:Clock.pak"),
		host->fn_bind(MOSAIC_BUTTON_FN1, "Clock.pak"),
		host->fn_bind(MOSAIC_BUTTON_FN1, "action:"),
		host->fn_bind(MOSAIC_BUTTON_FN1, NULL),
		host->fn_release(MOSAIC_BUTTON_FN1),
		host->fn_binding(MOSAIC_BUTTON_A, &none),
		host->fn_binding(MOSAIC_BUTTON_FN3, NULL),
		host->fn_name(MOSAIC_BUTTON_A));
	say("done");
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
