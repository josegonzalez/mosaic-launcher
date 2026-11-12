// Binds an FN button (the button setting, 1 to 3, FN3 by default) to an
// action of its own from init, logging what fn_bind returned. With the
// release setting it then releases it, and with decline it declines to
// start, so the launcher unloads it. racer, loaded after it, tries for
// the same button.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "mosaic_plugin.h"

#define ID "binder"

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

static void runGo(void* data) {
	(void)data;
	say("go ran");
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, fn_release)) {
		say("no fn_release");
		return 0;
	}
	int button = MOSAIC_BUTTON_FN1 + atoi(host->cfg_get("button", "3")) - 1;
	host->register_action("binder.go", "GO", NULL, runGo, NULL);
	say("names %s,%s,%s", host->fn_name(MOSAIC_BUTTON_FN1), host->fn_name(MOSAIC_BUTTON_FN2),
		host->fn_name(MOSAIC_BUTTON_FN3));
	int rc = host->fn_bind(button, "action:binder.go");
	say("bind %i", rc);
	// binding its own button again, to something else, keeps it
	if (rc == 0) say("bind again %i", host->fn_bind(button, "action:binder.go"));
	if (atoi(host->cfg_get("release", "0"))) {
		int first = host->fn_release(button);
		int again = host->fn_release(button);
		say("release %i again %i", first, again);
	}
	return atoi(host->cfg_get("decline", "0"));
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
