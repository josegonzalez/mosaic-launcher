// Registers two actions for the action paths case: pather.show, with
// MOSAIC_ACTION_PATH, on Y on the game list (its button setting, which its
// plugin.txt marks button:pather.show), and pather.plain, without it, on
// FN1. Each logs the path it got and the probe setting, read as it runs to
// show it runs as pather:
//
//   ran <n> <path> as <probe>   pather.show's nth run
//   plain <path> as <probe>     pather.plain's run, (null) for no path
//   bind <rc> fn <rc>           bind_button and fn_bind from init

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "pather"

static const MosaicHost* host;
static int runs = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[1200];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static int canShow(const char* path, void* data) {
	(void)data;
	return path && path[0];
}

static void show(const char* path, void* data) {
	(void)data;
	say("ran %d %s as %s", ++runs, path ? path : "(null)", host->cfg_get("probe", "nobody"));
}

static void plain(const char* path, void* data) {
	(void)data;
	say("plain %s as %s", path ? path : "(null)", host->cfg_get("probe", "nobody"));
}

static int init(const MosaicHost* h) {
	host = h;
	host->register_action("pather.show", "SHOW", MOSAIC_ACTION_PATH, canShow, show, NULL);
	host->register_action("pather.plain", "PLAIN", 0, NULL, plain, NULL);
	int bind = strcmp(host->cfg_get("button", "Y"), "Y") ? -1
		: host->bind_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_Y, MOSAIC_TRIGGER_PRESS, "pather.show");
	int fn = MOSAIC_HOST_HAS(host, fn_bind) ? host->fn_bind(MOSAIC_BUTTON_FN1, "action:pather.plain") : -2;
	say("bind %d fn %d", bind, fn);
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
