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
// background, and the alpha across the end of the bottom right hint pill
// (see edge), and the battery's box in the status pill (see icon). In
// reload mode it then reloads the lists, and logs the sizes and colours
// again. With still=1 it does none of that, so the
// screen can be looked at.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
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

// The alpha of the 12 pixels from the right edge of the bottom right hint
// pill in, a quarter of the way down it, as "edge pill <h> scale <s>
// alpha <a>...". Each is worked out from the background beside the pill
// and the pill's colour, on the channel where they differ most.
static void edge(const MosaicSurface* frame, int w, int h) {
	int s = host->screen_scale();
	int pill = s * host->ui_size(MOSAIC_UI_PILL);
	int pad = s * host->ui_size(MOSAIC_UI_PADDING);
	int right = w - pad, y = h - pad - pill + pill / 4;
	MosaicColor bg;
	if (host->read_pixel(frame, w - 1, y, &bg) != 0) return;
	int fill = host->theme_color(2);
	int d[3] = { (fill >> 16 & 0xff) - bg.r, (fill >> 8 & 0xff) - bg.g, (fill & 0xff) - bg.b };
	int c = 0;
	for (int i = 1; i < 3; i++)
		if (abs(d[i]) > abs(d[c])) c = i;
	if (!d[c]) return;
	char line[128] = "";
	size_t n = 0;
	for (int k = 1; k <= 12; k++) {
		MosaicColor p;
		if (host->read_pixel(frame, right - k, y, &p) != 0) return;
		int v[3] = { p.r - bg.r, p.g - bg.g, p.b - bg.b };
		int dv = d[c] < 0 ? -v[c] : v[c], dd = abs(d[c]);
		int a = (dv * 255 + dd / 2) / dd;
		n += snprintf(line + n, sizeof(line) - n, " %d", a < 0 ? 0 : a > 255 ? 255 : a);
	}
	say("edge pill %d scale %d alpha%s", pill, s, line);
}

// The box of the status pill's last icon, the battery, as "icon pill <h>
// button <h> scale <s> box <gap> <top> <w> <h>": the columns from the
// pill's right end in, after the gap, that have a pixel at least 110/255
// of the way from the pill's colour to the icons' (theme colour 6), and
// the rows those pixels take, from the pill's top.
static void icon(const MosaicSurface* frame, int w) {
	int s = host->screen_scale();
	int pill = s * host->ui_size(MOSAIC_UI_PILL);
	int pad = s * host->ui_size(MOSAIC_UI_PADDING);
	int fill = host->theme_color(2), ink = host->theme_color(6);
	int f[3] = { fill >> 16 & 0xff, fill >> 8 & 0xff, fill & 0xff };
	int d[3] = { (ink >> 16 & 0xff) - f[0], (ink >> 8 & 0xff) - f[1], (ink & 0xff) - f[2] };
	int dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
	if (!dd) return;
	int right = w - pad, x0 = -1, x1 = -1, y0 = pill, y1 = -1;
	for (int x = right - 1; x > right - pill * 4 && x >= 0; x--) {
		int any = 0;
		for (int y = 0; y < pill; y++) {
			MosaicColor p;
			if (host->read_pixel(frame, x, pad + y, &p) != 0) return;
			int v = (p.r - f[0]) * d[0] + (p.g - f[1]) * d[1] + (p.b - f[2]) * d[2];
			if (v * 255 < 110 * dd) continue;
			any = 1;
			if (y < y0) y0 = y;
			if (y > y1) y1 = y;
		}
		if (any) {
			if (x1 < 0) x1 = x;
			x0 = x;
		}
		else if (x1 >= 0)
			break;
	}
	if (x1 < 0) return;
	say("icon pill %d button %d scale %d box %d %d %d %d", pill, s * host->ui_size(MOSAIC_UI_BUTTON), s,
		right - 1 - x1, y0, x1 - x0 + 1, y1 - y0 + 1);
}

static int transitionBegin(MosaicTransition* t) {
	const char* to = host->current_path();
	if (t->kind == MOSAIC_TRANSITION_PUSH && to && !strcmp(to, inner)) {
		int w = 0, h = 0;
		MosaicColor c = {0, 0, 0, 0};
		host->surface_size(t->old_frame, &w, &h);
		host->read_pixel(t->old_frame, 2, h / 2, &c);
		say("frame %02X%02X%02X", c.r, c.g, c.b);
		edge(t->old_frame, w, h);
		icon(t->old_frame, w);
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
