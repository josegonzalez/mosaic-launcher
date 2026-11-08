// A plugin that reads the bundled art-backdrop's background back. It owns
// the transition slot, and adds a <sd>/Dimmer folder to the root holding
// three games and an empty Inner folder. Its art_path gives Art a backdrop
// (the file its backdrop setting names) and Shot a screenshot (its shot
// setting), and Bare gets none.
//
// From tick it opens Dimmer, and then for each game in turn selects it,
// waits for its art to come in and opens Inner, then opens Dimmer again.
// Each push into Inner logs pixels down the left edge of the old frame,
// where the list leaves the background clear, and one at the top right,
// with the game that was selected. Pushes are drawn with no transition.
// With probe=<x>,<y> in its config, it logs the pixel there too, on a
// line of its own.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "mosaic_plugin.h"

#define ID "dimmer"

static const MosaicHost* host;
static char folder[512];
static char inner[600];
static const char* backdrop = "";
static const char* shot = "";
static int probe_x = -1, probe_y = -1;
static const char* names[] = { "Art", "Shot", "Bare" };
static int game = 0;   // the one to select next
static int step = 0;
static long step_at = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[700];
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

// "RRGGBBAA" at x, y on s, or "-"
static const char* pixel(const MosaicSurface* s, int x, int y) {
	static char out[8][12];
	static int next = 0;
	char* text = out[next++ % 8];
	MosaicColor c;
	if (host->read_pixel(s, x, y, &c) != 0) return "-";
	snprintf(text, 12, "%02X%02X%02X%02X", c.r, c.g, c.b, c.a);
	return text;
}

static void rootEntries(MosaicList* root) {
	host->list_push(root, host->entry_new(folder, MOSAIC_ENTRY_DIR, "Dimmer"));
}

static MosaicList* virtualDir(const char* path) {
	if (!strcmp(path, inner)) return host->list_new();
	if (strcmp(path, folder) != 0) return NULL;
	MosaicList* list = host->list_new();
	for (int i = 0; i < 3; i++) {
		char file[600];
		snprintf(file, sizeof(file), "%s/%s.gb", folder, names[i]);
		host->list_push(list, host->entry_new(file, MOSAIC_ENTRY_ROM, names[i]));
	}
	host->list_push(list, host->entry_new(inner, MOSAIC_ENTRY_DIR, "Inner"));
	return list;
}

static int artPath(const MosaicEntry* entry, int kind, char* out, size_t size) {
	const char* name = host->entry_name(entry);
	if (kind == MOSAIC_ART_BACKDROP && !strcmp(name, "Art")) snprintf(out, size, "%s", backdrop);
	else if (kind == MOSAIC_ART_SCREENSHOT && !strcmp(name, "Shot")) snprintf(out, size, "%s", shot);
	else return 0;
	return 1;
}

static int transitionBegin(MosaicTransition* t) {
	const char* to = host->current_path();
	if (t->kind == MOSAIC_TRANSITION_PUSH && to && !strcmp(to, inner) && game > 0) {
		int w = 0, h = 0;
		host->surface_size(t->old_frame, &w, &h);
		int bg = host->theme_color(MOSAIC_COLOR_BACKGROUND);
		say("from %s top %s quarter %s fade %s low %s right %s, theme %06X", names[game - 1], pixel(t->old_frame, 2, 2),
			pixel(t->old_frame, 2, h / 4), pixel(t->old_frame, 2, h * 5 / 8), pixel(t->old_frame, 2, h * 7 / 8),
			pixel(t->old_frame, w - 2, 2), bg < 0 ? 0 : (unsigned)bg);
		if (probe_x >= 0) say("probe %s %s", names[game - 1], pixel(t->old_frame, probe_x, probe_y));
	}
	return MOSAIC_STYLE_OFF;
}

static void tick(unsigned long now) {
	(void)now;
	if (!host->current_list()) return;
	host->request_redraw();
	long at = ms();
	if (step == 0) {
		step_at = at;
		step = 1;
	}
	else if (step == 1 && at >= step_at + 300) {
		say("open folder %i", host->open_path(folder, MOSAIC_ENTRY_DIR));
		step = 2;
		step_at = at;
	}
	else if (step == 2 && at >= step_at + 300) {
		if (game == 3) {
			say("done");
			step = 5;
			return;
		}
		say("select %s %i", names[game], host->select_index(game));
		game++;
		step = 3;
		step_at = at;
	}
	// long enough for the art to load and the list to redraw with it
	else if (step == 3 && at >= step_at + 800) {
		host->open_path(inner, MOSAIC_ENTRY_DIR);
		step = 4;
		step_at = at;
	}
	else if (step == 4 && at >= step_at + 200) {
		host->open_path(folder, MOSAIC_ENTRY_DIR);
		step = 2;
		step_at = at;
	}
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, plugin_art)) return 1;
	snprintf(folder, sizeof(folder), "%s/Dimmer", host->sdcard_path);
	snprintf(inner, sizeof(inner), "%s/Inner", folder);
	backdrop = host->cfg_get(ID, "backdrop", "");
	shot = host->cfg_get(ID, "shot", "");
	if (sscanf(host->cfg_get(ID, "probe", ""), "%d,%d", &probe_x, &probe_y) != 2) probe_x = probe_y = -1;
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
	.art_path = artPath,
	.transition_begin = transitionBegin,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
