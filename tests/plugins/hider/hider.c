// A plugin that hides entries' names and looks at how they're drawn. It
// adds a <sd>/Hider folder of three games to the root and opens it, gives
// each of them <sd>/Hider/art.png as its thumbnail, and hides the names of
// Apple and Cherry. As a row decorator it reads each row of that folder
// back from the screen, logging whether its name was hidden, how wide its
// name is and whether anything is drawn across the left of the row, where
// the built-in list draws names (ink), each time that changes. As the
// transition slot's owner it logs where the push into the folder has the
// selected row and its pill, and turns the slide off.
//
// With hide=none in its config it hides nothing, and with art=none it
// gives no thumbnails. With slots=layout in its plugin.txt it draws the
// list itself: it logs the view's names_hidden for the folder and passes
// each entry to decorate_cell, one cell a row, without drawing names.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "hider"

static const MosaicHost* host;
static char folder[512];
static char art[600];
static int hide = 1, give_art = 1;
static int ticks = 0;
static int asks = 0;
static char last[3][160]; // the last line logged for each game
static char last_view[64];

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[700];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static const char* names[] = { "Apple", "Banana", "Cherry" };

static int game(const MosaicEntry* entry) {
	const char* path = host->entry_path(entry);
	size_t n = strlen(folder);
	if (!path || strncmp(path, folder, n) != 0 || path[n] != '/') return -1;
	for (int i = 0; i < 3; i++) {
		if (!strcmp(host->entry_name(entry), names[i])) return i;
	}
	return -1;
}

static void rootEntries(MosaicList* root) {
	host->list_push(root, host->entry_new(folder, MOSAIC_ENTRY_DIR, "Hider"));
}

static MosaicList* virtualDir(const char* path) {
	if (strcmp(path, folder) != 0) return NULL;
	MosaicList* list = host->list_new();
	for (int i = 0; i < 3; i++) {
		char file[600];
		snprintf(file, sizeof(file), "%s/%s.gb", folder, names[i]);
		host->list_push(list, host->entry_new(file, MOSAIC_ENTRY_ROM, names[i]));
	}
	return list;
}

static int artPath(const MosaicEntry* entry, int kind, char* out, size_t size) {
	if (!give_art || kind != MOSAIC_ART_THUMBNAIL || game(entry) < 0) return 0;
	snprintf(out, size, "%s", art);
	return 1;
}

static int hideName(const MosaicEntry* entry, const char* dir_path) {
	int g = game(entry);
	if (g >= 0 && strcmp(dir_path, folder) == 0) asks++;
	return hide && (g == 0 || g == 2);
}

static unsigned int pixel(const MosaicSurface* s, int x, int y) {
	MosaicColor c;
	if (host->read_pixel(s, x, y, &c) != 0) return 0x1000000;
	return (unsigned int)c.r << 16 | (unsigned int)c.g << 8 | c.b;
}

static int decorateRow(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRow* row) {
	int g = game(entry);
	if (g < 0) return 0;
	const MosaicRect* r = &row->rect;
	// anything across the left of the row, where its name would be
	unsigned int blank = pixel(screen, r->x + 1, r->y + r->h / 2);
	int ink = 0;
	for (int y = r->y + r->h / 4; y < r->y + r->h * 3 / 4 && !ink; y += 2) {
		for (int x = r->x + 2; x < r->x + r->w / 2 && x < r->x + 300; x++) {
			if (pixel(screen, x, y) != blank) ink = 1;
		}
	}
	char line[160];
	snprintf(line, sizeof(line), "row %s selected %i hidden %i named %i ink %i", names[g], row->selected,
		MOSAIC_ROW_HAS(row, name_hidden) ? row->name_hidden : -1, row->name.w > 0, ink);
	if (strcmp(line, last[g]) != 0) {
		snprintf(last[g], sizeof(last[g]), "%s", line);
		say("%s", line);
	}
	return 0;
}

static int transitionBegin(MosaicTransition* t) {
	if (t->kind == MOSAIC_TRANSITION_PUSH) {
		const char* path = host->current_path();
		say("push to %s row %i pill %i", path && !strcmp(path, folder) ? "hider" : "other", t->new_row.w > 0,
			t->new_pill.w > 0);
	}
	return MOSAIC_STYLE_OFF;
}

static int layoutInit(const MosaicListView* view) {
	(void)view;
	return 0;
}

static int layoutRender(MosaicSurface* screen, const MosaicListView* view) {
	if (!view->path || strcmp(view->path, folder) != 0) return 0;
	char flags[64];
	if (!MOSAIC_VIEW_HAS(view, names_hidden)) snprintf(flags, sizeof(flags), "missing");
	else if (!view->names_hidden) snprintf(flags, sizeof(flags), "none");
	else {
		int n = 0;
		for (int i = 0; i < view->count && n < (int)sizeof(flags) - 1; i++) flags[n++] = view->names_hidden[i] ? '1' : '0';
		flags[n] = '\0';
	}
	if (strcmp(flags, last_view) != 0) {
		snprintf(last_view, sizeof(last_view), "%s", flags);
		say("view names_hidden %s", flags);
	}
	int h = view->area.h / 4;
	for (int i = 0; i < view->count; i++) {
		MosaicRect cell = { view->area.x, view->area.y + i * h, view->area.w / 2, h };
		host->decorate_cell(screen, i, &cell, NULL);
	}
	return 0;
}

static void tick(unsigned long now) {
	(void)now;
	if (!host->current_list()) return;
	ticks++;
	if (ticks == 2) say("open folder %i", host->open_path(folder, MOSAIC_ENTRY_DIR));
	if (ticks < 60) host->request_redraw();
	if (ticks == 60) say("done, asked %i", asks);
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, hide_names)) return 1;
	snprintf(folder, sizeof(folder), "%s/Hider", host->sdcard_path);
	snprintf(art, sizeof(art), "%s/art.png", folder);
	hide = strcmp(host->cfg_get("hide", "some"), "none") != 0;
	give_art = strcmp(host->cfg_get("art", "some"), "none") != 0;
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
	.layout_init = layoutInit,
	.layout_render = layoutRender,
	.decorate_row = decorateRow,
	.transition_begin = transitionBegin,
	.hide_name = hideName,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
