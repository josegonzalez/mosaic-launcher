// A plugin that owns the theme slot, and the transition slot to read the
// screen back. Its config's mode picks the theme it supplies:
//
//   good    colours (main 0xABCDEF, background 0x123456), a text step of 2,
//           pill 36, button 24, padding 8, and its font and wallpaper
//           settings (full paths, "" for none)
//   bad     values the launcher refuses: a pill of 200, a padding of -1, a
//           colour over 0xFFFFFF, a contrast of 5, and its font and
//           wallpaper settings, which the case points at bad paths
//   fail    returns an error
//   reload  good, then an error once the lists reload
//   empty   sets nothing
//
// It adds a <sd>/Themer folder to the root holding two games and an empty
// Inner folder. From tick, once the list shows, it logs the UI sizes,
// theme colours, a large text line's height and the selected row's
// height, opens Themer and then Inner, and logs the pixel at the left edge
// halfway down the old frame of that push, which the list leaves to the
// background. In reload mode it then reloads the lists, and logs the
// sizes and colours again. With still=1 it does none of that, so the
// screen can be looked at.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "mosaic_plugin.h"

#define ID "themer"

static const MosaicHost* host;
static char folder[512];
static char inner[600];
static const char* mode = "good";
static int still = 0;
static int asked = 0;
static int row_h = 0;
static int step = 0;
static long step_at = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[900];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static long ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void copyCfg(char* out, size_t size, const char* key) {
	snprintf(out, size, "%s", host->cfg_get(ID, key, ""));
}

static int theme(MosaicTheme* t) {
	asked++;
	say("asked %d, size %s", asked, t->size == sizeof(MosaicTheme) ? "matches" : "differs");
	if (!strcmp(mode, "fail") || (!strcmp(mode, "reload") && asked > 1)) return 7;
	if (!strcmp(mode, "empty")) return 0;
	if (!strcmp(mode, "bad")) {
		t->pill_size = 200;
		t->padding = -1;
		t->colors[1] = 0x1000000;
		t->contrast = 5;
	}
	else {
		t->colors[0] = 0xabcdef;
		t->colors[6] = 0x123456;
		t->text_step = 2;
		t->pill_size = 36;
		t->button_size = 24;
		t->padding = 8;
	}
	copyCfg(t->font, sizeof(t->font), "font");
	copyCfg(t->wallpaper, sizeof(t->wallpaper), "wallpaper");
	copyCfg(t->wallpaper_list, sizeof(t->wallpaper_list), "wallpaper_list");
	copyCfg(t->wallpaper_quick, sizeof(t->wallpaper_quick), "wallpaper_quick");
	return 0;
}

static void rootEntries(MosaicList* root) {
	host->list_push(root, host->entry_new(folder, MOSAIC_ENTRY_DIR, "Themer"));
}

static MosaicList* virtualDir(const char* path) {
	if (!strcmp(path, inner)) return host->list_new();
	if (strcmp(path, folder) != 0) return NULL;
	MosaicList* list = host->list_new();
	host->list_push(list, host->entry_new(inner, MOSAIC_ENTRY_DIR, "Inner"));
	static const char* names[] = { "Alpha", "Beta" };
	for (int i = 0; i < 2; i++) {
		char file[600];
		snprintf(file, sizeof(file), "%s/%s.gb", folder, names[i]);
		host->list_push(list, host->entry_new(file, MOSAIC_ENTRY_ROM, names[i]));
	}
	return list;
}

static int decorateRow(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRow* row) {
	(void)screen;
	(void)entry;
	if (row->selected) row_h = row->rect.h;
	return 0;
}

static int transitionBegin(MosaicTransition* t) {
	const char* to = host->current_path();
	if (t->kind == MOSAIC_TRANSITION_PUSH && to && !strcmp(to, inner)) {
		int w = 0, h = 0;
		MosaicColor c = {0, 0, 0, 0};
		host->surface_size(t->old_frame, &w, &h);
		host->read_pixel(t->old_frame, 2, h / 2, &c);
		say("frame %02X%02X%02X", c.r, c.g, c.b);
	}
	return MOSAIC_STYLE_OFF;
}

static void report(const char* when) {
	say("%s sizes pill %d button %d padding %d rows %d step %d", when, host->ui_size(MOSAIC_UI_PILL),
		host->ui_size(MOSAIC_UI_BUTTON), host->ui_size(MOSAIC_UI_PADDING), host->ui_size(MOSAIC_UI_ROWS),
		host->ui_size(MOSAIC_UI_TEXT_STEP));
	say("%s colors %06X %06X %06X %06X %06X %06X %06X, text %d, row %d, other %d", when, host->theme_color(1),
		host->theme_color(2), host->theme_color(3), host->theme_color(4), host->theme_color(5), host->theme_color(6),
		host->theme_color(7), host->text_height(MOSAIC_FONT_LARGE), row_h, host->ui_size(99));
}

static void tick(unsigned long now) {
	(void)now;
	if (still || !host->current_list()) return;
	host->request_redraw();
	long at = ms();
	if (step == 0) {
		step_at = at;
		step = 1;
	}
	else if (step == 1 && at >= step_at + 300) {
		report("start");
		host->open_path(folder, MOSAIC_ENTRY_DIR);
		step = 2;
		step_at = at;
	}
	// long enough for the background to load and the list to redraw over it
	else if (step == 2 && at >= step_at + 800) {
		host->select_index(1);
		step = 3;
		step_at = at;
	}
	else if (step == 3 && at >= step_at + 800) {
		host->open_path(inner, MOSAIC_ENTRY_DIR);
		step = 4;
		step_at = at;
	}
	else if (step == 4 && at >= step_at + 300) {
		if (!strcmp(mode, "reload")) {
			host->reload_lists();
			step = 5;
			step_at = at;
			return;
		}
		say("done");
		step = 6;
	}
	else if (step == 5 && at >= step_at + 300) {
		report("reloaded");
		say("done");
		step = 6;
	}
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, ui_size)) return 1;
	snprintf(folder, sizeof(folder), "%s/Themer", host->sdcard_path);
	snprintf(inner, sizeof(inner), "%s/Inner", folder);
	mode = host->cfg_get(ID, "mode", "good");
	still = !strcmp(host->cfg_get(ID, "still", ""), "1");
	// the theme isn't asked before init
	say("init %s, asked %d", mode, asked);
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.root_entries = rootEntries,
	.virtual_dir = virtualDir,
	.tick = tick,
	.decorate_row = decorateRow,
	.transition_begin = transitionBegin,
	.theme = theme,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
