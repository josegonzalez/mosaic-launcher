// Bug report: one zip in Reports/ on the card with the launcher's and
// plugins' logs, settings, device and build details and a game list
// summary, with private details taken out, to attach to a bug report.
//
// The bug-report.make action makes one, from a Bug report tile on the
// quick menu (the "tile" setting), a game list button (the "button"
// setting), or button-remap or an FN button (action:bug-report.make). It
// opens a wait screen, and the tick after the screen has drawn asks the
// launcher for the report (host->bug_report), closes the screen and shows
// a toast naming the file. The "keep_network" setting keeps IP and MAC
// addresses, Wi-Fi names and Syncthing device IDs in.
//
// It asks for the card index from init, so the game list summary is
// ready by the time a report is made.
//
// The launcher writes the zip itself, so it can make one while plugins
// are paused after a crash too: plugin.txt provides report.crash, which
// asks it to, with this plugin's keep_network setting.

#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"
#include "bug_report_model.h"

#define ID "bug-report"
#define ACTION_ID ID ".make"
#define SCREEN_ID ID ".wait"

static const MosaicHost* host;
static int waiting = 0; // the wait screen is open, the report not made yet
static int drawn = 0;   // and has drawn, so the report is made on the next tick

static void say(const char* message) {
	host->log(ID, message);
}

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static void toast(const char* message) {
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(message);
}

// Makes the report and says where it went.
static void make(void) {
	char path[256] = "";
	int rc = host->bug_report(BR_flags(host->cfg_get("keep_network", "off")), path, sizeof(path));
	char message[320];
	BR_toast(rc, path, tr("Saved %s"), tr("Couldn't make a bug report"), message, sizeof(message));
	char line[400];
	if (rc == 0) snprintf(line, sizeof(line), "saved %s", path);
	else snprintf(line, sizeof(line), "couldn't make a bug report");
	say(line);
	toast(message);
}

///////////////////////////////////////
// the wait screen

static MosaicColor themeColor(int index, unsigned int fallback) {
	int rgb = host->theme_color(index);
	return mosaic_color(rgb < 0 ? fallback : (unsigned int)rgb, 255);
}

static void centred(MosaicSurface* screen, int font, const char* text, int y, int width, MosaicColor color) {
	char cut[256];
	int w = host->text_truncate(font, text, width - 40, cut, sizeof(cut));
	if (w < 0) return;
	host->draw_text(screen, font, cut, (width - w) / 2, y, color, width);
}

static int screenEnter(void* data) {
	(void)data;
	waiting = 1;
	drawn = 0;
	return 0;
}

static void screenExit(void* data) {
	(void)data;
	waiting = 0;
}

static int screenInput(const MosaicInput* in, void* data) {
	(void)in;
	(void)data;
	return MOSAIC_SCREEN_STAY;
}

static int screenRender(MosaicSurface* screen, const MosaicScreenView* v, void* data) {
	(void)data;
	// all but the status pill and header widgets, already drawn
	MosaicColor bg = themeColor(MOSAIC_COLOR_BACKGROUND, 0x000000);
	int top = 0, left = v->area.w;
	if (v->status.w > 0 && v->status.y + v->status.h > top) top = v->status.y + v->status.h;
	if (v->header.w > 0 && v->header.y + v->header.h > top) top = v->header.y + v->header.h;
	if (v->status.w > 0 && v->status.x < left) left = v->status.x;
	if (v->header.w > 0 && v->header.x < left) left = v->header.x;
	MosaicRect below = { 0, top, v->area.w, v->area.h - top };
	host->fill_rect(screen, &below, bg);
	MosaicRect band = { 0, 0, left, top };
	if (band.w > 0 && band.h > 0) host->fill_rect(screen, &band, bg);
	int line = host->text_height(MOSAIC_FONT_LARGE);
	int scale = host->screen_scale();
	if (scale < 1) scale = 1;
	int y = v->area.h / 2 - line;
	centred(screen, MOSAIC_FONT_LARGE, tr("Making a bug report..."), y, v->area.w, themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF));
	centred(screen, MOSAIC_FONT_SMALL, tr("It goes in Reports on the card"), y + line + 4 * scale, v->area.w,
		themeColor(MOSAIC_COLOR_MAIN, 0xFFFFFF));
	if (!drawn) say("wait screen drawn");
	drawn = 1;
	return 0;
}

///////////////////////////////////////
// the action

static int always(const char* path, void* data) {
	(void)path;
	(void)data;
	return 1;
}

static void run(const char* path, void* data) {
	(void)path;
	(void)data;
	if (waiting) return;
	if (host->open_screen(SCREEN_ID) != 0) {
		// such as from a dialog's callback: without the screen, at once
		say("couldn't open the wait screen, so making it at once");
		make();
	}
}

static void tick(unsigned long now) {
	(void)now;
	if (!waiting || !drawn) return;
	make();
	waiting = 0;
	if (host->close_screen(SCREEN_ID) != 0) say("couldn't close the wait screen");
}

static int init(const MosaicHost* h) {
	host = h;
	waiting = drawn = 0;
	if (!MOSAIC_HOST_HAS(host, bug_report) || !MOSAIC_HOST_HAS(host, open_screen) || !MOSAIC_HOST_HAS(host, draw_text)) {
		say("unsupported: this launcher can't make bug reports");
		return 1;
	}
	static const MosaicScreenHooks hooks = {
		.size = sizeof(MosaicScreenHooks),
		.enter = screenEnter,
		.exit = screenExit,
		.input = screenInput,
		.render = screenRender,
	};
	if (host->register_screen(SCREEN_ID, &hooks) != 0) {
		say("couldn't register its wait screen");
		return 1;
	}
	if (host->register_action(ACTION_ID, "Bug report", 0, always, run, NULL) != 0) {
		say("couldn't register " ACTION_ID);
		return 1;
	}
	if (strcmp(host->cfg_get("tile", "on"), "off") != 0
			&& (!MOSAIC_HOST_HAS(host, quick_tile) || host->quick_tile(ACTION_ID, MOSAIC_QUICK_TOP, "quick/bug-report.png", NULL, NULL) != 0)) {
		say("couldn't add the Bug report tile");
	}
	int button = BR_button(host->cfg_get("button", "none"));
	if (button >= 0 && host->bind_button(MOSAIC_SCREEN_GAMELIST, button, MOSAIC_TRIGGER_PRESS, ACTION_ID) != 0) {
		say("couldn't bind the Bug report button");
	}
	// the game list summary comes from the card index, read in the background from now
	if (MOSAIC_HOST_HAS(host, index_count)) host->index_count(NULL);
	say(BR_flags(host->cfg_get("keep_network", "off")) ? "ready, keeping network details" : "ready");
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.tick = tick,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
