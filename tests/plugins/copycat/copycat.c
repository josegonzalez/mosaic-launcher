// A plugin that provides services of its own and tries to take one the
// bundled playtime-captions provides first. copycat.echo replies with the
// x it was given and the me setting, which it reads as itself, so the
// caller sees it ran as copycat. copycat.nest calls itself, which is
// refused, and copycat.echo, which works, and replies with both results.
// It also tries a name its provides= doesn't list and an entry set's.

#include <stdio.h>
#include "mosaic_plugin.h"

#define ID "copycat"

static const MosaicHost* host;

static int echo(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	char x[64];
	if (host->event_value(args, "x", x, sizeof(x)) < 0) x[0] = '\0';
	snprintf(out, size, "me=%s\nx=%s", host->cfg_get("me", "nobody"), x);
	return 0;
}

static int nest(const char* name, const char* args, char* out, size_t size) {
	(void)args;
	char reply[MOSAIC_CALL_MAX];
	int same = host->call(name, NULL, reply, sizeof(reply));
	int other = host->call("copycat.echo", "x=inner", reply, sizeof(reply));
	char x[64];
	if (host->event_value(reply, "x", x, sizeof(x)) < 0) x[0] = '\0';
	snprintf(out, size, "same=%i\necho=%i\nx=%s", same, other, x);
	return 0;
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, call)) {
		host->log(ID, "no providers");
		return -1;
	}
	int captions = host->provide("captions.text", echo);
	int echoed = host->provide("copycat.echo", echo);
	int nested = host->provide("copycat.nest", nest);
	int undeclared = host->provide("copycat.undeclared", echo);
	int set = host->provide("sets.favorites", echo);
	char message[160];
	snprintf(message, sizeof(message), "provide captions %i echo %i nest %i undeclared %i set %i", captions, echoed, nested,
		undeclared, set);
	host->log(ID, message);
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
