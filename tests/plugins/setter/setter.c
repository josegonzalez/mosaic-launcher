// Reads the bundled favorites and hidden plugins' entry sets through the
// host, and registers one of its own, for the entry sets case. Its
// settings name the paths to ask about: fav (a favorite), hid (a hidden
// game) and plain (neither). It logs:
//
//   register <name> <rc>      set_register from init, for favorites (taken),
//                              random (reserved) and setter.mine (its own)
//   has <set> <a> <b>         set_has for fav and plain, or hid and plain
//   list favorites <n> <paths> set_list, the paths comma-separated
//   count favorites <n>       set_list with no callback
//   list missing <n>          set_list on a set nobody registered
//   list mine <n> nested <a> <b>
//                              its own set, asking favorites (another set)
//                              and setter.mine (the one being listed) from
//                              inside the walk
//   filtered <a> <b>          is_filtered for hid and plain
//   unregister <rc> <rc>      set_unregister of its own set, then again
//   done                      once, from the first list_ready

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "setter"

static const MosaicHost* host;
static char fav[512], hid[512], plain[512];
static int done = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[2048];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static int mineHas(const char* name, const char* path) {
	(void)name;
	return !strcmp(path, plain);
}

static void mineList(const char* name, void (*add)(const char* path, void* ctx), void* ctx) {
	(void)name;
	add(plain, ctx);
	add(fav, ctx);
	add("", ctx); // not counted
}

static char listed[1500];
static void collect(const char* path, void* data) {
	(void)data;
	size_t len = strlen(listed);
	snprintf(listed + len, sizeof(listed) - len, "%s%s", len ? "," : "", path);
}

static int nested_fav = -1, nested_mine = -1;
static void nested(const char* path, void* data) {
	(void)data;
	if (!strcmp(path, fav)) {
		nested_fav = host->set_has("favorites", path);
		nested_mine = host->set_has("setter.mine", plain);
	}
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, is_filtered)) {
		say("no set calls");
		return 1;
	}
	snprintf(fav, sizeof(fav), "%s", host->cfg_get("fav", ""));
	snprintf(hid, sizeof(hid), "%s", host->cfg_get("hid", ""));
	snprintf(plain, sizeof(plain), "%s", host->cfg_get("plain", ""));
	say("register favorites %d", host->set_register("favorites", mineHas, mineList));
	say("register random %d", host->set_register("random", mineHas, mineList));
	say("register setter.mine %d", host->set_register("setter.mine", mineHas, mineList));
	return 0;
}

static void listReady(const MosaicList* list, const char* dir_path) {
	(void)list, (void)dir_path;
	if (done) return;
	done = 1;
	say("has favorites %d %d", host->set_has("favorites", fav), host->set_has("favorites", plain));
	say("has hidden %d %d", host->set_has("hidden", hid), host->set_has("hidden", plain));
	listed[0] = '\0';
	int n = host->set_list("favorites", collect, NULL);
	say("list favorites %d %s", n, listed);
	say("count favorites %d", host->set_list("favorites", NULL, NULL));
	say("list missing %d", host->set_list("tags:Nope", collect, NULL));
	n = host->set_list("setter.mine", nested, NULL);
	say("list mine %d nested %d %d", n, nested_fav, nested_mine);
	say("filtered %d %d", host->is_filtered(hid), host->is_filtered(plain));
	int first = host->set_unregister("setter.mine");
	say("unregister %d %d", first, host->set_unregister("setter.mine"));
	say("done");
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.list_ready = listReady,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
