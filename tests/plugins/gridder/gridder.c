// A plugin that owns the layout slot and draws the game list as a grid of
// four columns, logging the views it gets and what it drew. It adds a
// <sd>/Gridder folder of seven games to the root and opens it. Its input
// moves the selection down a row there once, with select_index, so the
// launcher keeps the selection. It reads back the screen it's lent to see
// its drawing land inside the area and not past it. Each cell it draws is
// passed to host->decorate_cell, for row decorators such as badger's.
// With art=1 in its config, the top half of the selected cell is its art,
// passed to host->decorate_art, for art decorators such as bander's.
// With swap=1, a plugin's art_replace surface for the selected cell, from
// host->art_replacement, fills that cell, and it logs each change in what
// art_replacement answers, with the cell's colour read back.
//
// With fail=init in its config, layout_init declines; with fail=input or
// fail=render, that hook returns an error the third time it's called; and
// with fail=crash its first render crashes.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "gridder"
#define COLUMNS 4

static const MosaicHost* host;
static char folder[512];
static const char* fail = "";
static int art = 0;
static int swap = 0;
static int swap_rc = -2; // what art_replacement last answered for the selected cell
static int ticks = 0;
static int inputs = 0;
static int renders = 0;
static int moved = 0;
static int folder_drawn = 0; // a render has shown the folder
static MosaicSurface* lent = NULL; // the screen from the last render, to try after it

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[700];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static void rootEntries(MosaicList* root) {
	host->list_push(root, host->entry_new(folder, MOSAIC_ENTRY_DIR, "Gridder"));
}

static MosaicList* virtualDir(const char* path) {
	if (strcmp(path, folder) != 0) return NULL;
	static const char* names[] = { "Apple", "Banana", "Cherry", "Date", "Elder", "Fig", "Grape" };
	MosaicList* list = host->list_new();
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		char file[600];
		snprintf(file, sizeof(file), "%s/%s.gb", folder, names[i]);
		host->list_push(list, host->entry_new(file, MOSAIC_ENTRY_ROM, names[i]));
	}
	return list;
}

static int inside(const MosaicRect* outer, const MosaicRect* r) {
	return r->x >= outer->x && r->y >= outer->y && r->x + r->w <= outer->x + outer->w && r->y + r->h <= outer->y + outer->h;
}

static int layoutInit(const MosaicListView* view) {
	say("layout_init count %i selected %i depth %i, area %i,%i %ix%i, status inside %i, view size %i",
		view->count, view->selected, view->depth, view->area.x, view->area.y, view->area.w, view->area.h,
		inside(&view->area, &view->status), view->size == sizeof(MosaicListView));
	if (!strcmp(fail, "init")) {
		say("declining the layout");
		return 1;
	}
	return 0;
}

static int layoutInput(const MosaicInput* input, const MosaicListView* view, unsigned int* consumed) {
	inputs++;
	if (inputs == 1) {
		say("input first: pressed %u repeated %u released %u held %u consumed %u, input size %i",
			input->pressed, input->repeated, input->released, input->held, *consumed, input->size == sizeof(MosaicInput));
	}
	if (!strcmp(fail, "input") && inputs == 3) {
		say("input failing");
		return -1;
	}
	// a grid's down: the same column, one row on, through the launcher
	if (!moved && folder_drawn && view->depth == 2 && !strcmp(view->path, folder)) {
		moved = 1;
		int target = view->selected + COLUMNS;
		say("input move %i to %i: %i, bad %i, now %i", view->selected, target, host->select_index(target),
			host->select_index(view->count), host->selected_index());
		*consumed = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_DOWN);
	}
	return 0;
}

// "RRGGBB" at x, y on s, or "-"
static const char* pixel(const MosaicSurface* s, int x, int y) {
	static char out[4][8];
	static int next = 0;
	char* text = out[next++ % 4];
	MosaicColor c;
	if (host->read_pixel(s, x, y, &c) != 0) return "-";
	snprintf(text, 8, "%02X%02X%02X", c.r, c.g, c.b);
	return text;
}

static int layoutRender(MosaicSurface* screen, const MosaicListView* view) {
	renders++;
	if (!strcmp(fail, "crash")) {
		say("crashing in render");
		abort();
	}
	if (!strcmp(fail, "render") && renders == 3) {
		say("render failing");
		return -1;
	}
	lent = screen;

	// cells, the selected one lit, with names when the launcher shows them
	MosaicRect area = view->area;
	int scale = host->screen_scale();
	int cell_w = area.w / COLUMNS;
	int cell_h = 60 * scale;
	int first_row = view->selected / COLUMNS > 1 ? view->selected / COLUMNS - 1 : 0;
	host->fill_rect(screen, &area, mosaic_color(0x000080, 255));
	int lit_x = -1, lit_y = -1;
	int decorated = 0;
	for (int i = first_row * COLUMNS; i < view->count; i++) {
		int row = i / COLUMNS - first_row;
		MosaicRect cell = { area.x + (i % COLUMNS) * cell_w + 4, area.y + row * cell_h + 4, cell_w - 8, cell_h - 8 };
		if (cell.y >= area.y + area.h) break;
		int lit = i == view->selected;
		host->fill_round_rect(screen, &cell, 8, mosaic_color(lit ? 0xFF0000 : 0x00FF00, 255));
		if (lit) {
			lit_x = cell.x + cell.w / 2;
			lit_y = cell.y + 2;
		}
		if (view->show_names) {
			const MosaicEntry* entry = host->list_get(view->list, i);
			host->draw_text(screen, MOSAIC_FONT_SMALL, host->entry_name(entry), cell.x + 4, cell.y + cell.h / 2,
				mosaic_color(0xFFFFFF, 255), cell.w - 8);
		}
		if (host->decorate_cell(screen, i, &cell, NULL) != 0) decorated = -1;
		if (lit && swap) {
			const MosaicSurface* replaced = NULL;
			int rc = host->art_replacement(i, &replaced);
			if (rc == 1) host->blit(screen, replaced, NULL, &cell);
			if (rc != swap_rc) {
				swap_rc = rc;
				const MosaicSurface* other = NULL;
				const MosaicEntry* entry = host->list_get(view->list, i);
				say("render replacement %s %i lit %s, other %i, bad index %i %i, no out %i", host->entry_name(entry), rc,
					pixel(screen, cell.x + cell.w / 2, cell.y + 2), host->art_replacement(i == 0 ? 1 : 0, &other),
					host->art_replacement(-1, &other), host->art_replacement(view->count, &other),
					host->art_replacement(i, NULL));
			}
		}
		if (lit && art) {
			MosaicRect top = { cell.x, cell.y, cell.w, cell.h / 2 };
			int rc = host->decorate_art(screen, i, &top);
			if (renders == 1) say("render art %i", rc);
		}
	}

	if (view->depth == 2 && !strcmp(view->path, folder)) folder_drawn = 1;
	static int last_depth = -1, last_selected = -1;
	if (view->depth != last_depth || view->selected != last_selected) {
		last_depth = view->depth;
		last_selected = view->selected;
		const MosaicEntry* entry = host->list_get(view->list, view->selected);
		say("render depth %i count %i selected %i %s, window %i-%i of %i rows, letter %i of %i, names %i, art %i \"%s\"",
			view->depth, view->count, view->selected, host->entry_name(entry), view->start, view->end, view->rows,
			view->letter, view->letter_count, view->show_names, view->show_art, view->art);
		say("render lit %s, background %s", lit_x >= 0 ? pixel(screen, lit_x, lit_y) : "none", pixel(screen, area.x + 1, area.y + 1));
		say("render header %ix%i, beside status %i", view->header.w, view->header.h,
			view->header.w == 0 || (view->header.x + view->header.w == view->status.x && view->header.y == view->status.y));
	}

	if (renders == 1) {
		// past the area, where the hints are, nothing lands
		int below = area.y + area.h;
		const char* before = pixel(screen, 1, below);
		int filled = host->fill_rect(screen, &(MosaicRect){ 0, below, 10, 10 }, mosaic_color(0xFFFF00, 255));
		say("render clipped %i (fill %i), select %i, free %i", !strcmp(before, pixel(screen, 1, below)), filled,
			host->select_index(0), host->surface_free(ID, screen));
		MosaicRect cell = { area.x, area.y, 10, 10 };
		say("render decorated %i, bad index %i %i, no rect %i, outside %i, not the screen %i", decorated,
			host->decorate_cell(screen, -1, &cell, NULL), host->decorate_cell(screen, view->count, &cell, NULL),
			host->decorate_cell(screen, 0, NULL, NULL),
			host->decorate_cell(screen, 0, &(MosaicRect){ 0, below, 10, 10 }, NULL),
			host->decorate_cell(NULL, 0, &cell, NULL));
		if (art) {
			say("render art bad index %i %i, no rect %i, outside %i, not the screen %i, at the bottom %i",
				host->decorate_art(screen, -1, &cell), host->decorate_art(screen, view->count, &cell),
				host->decorate_art(screen, 0, NULL), host->decorate_art(screen, 0, &(MosaicRect){ 0, below, 10, 10 }),
				host->decorate_art(NULL, 0, &cell), host->decorate_art(screen, 0, &(MosaicRect){ 0, below - 10, 10, 10 }));
		}
	}
	return 0;
}

static void tick(unsigned long now) {
	(void)now;
	if (!host->current_list()) return;
	ticks++;
	if (ticks == 2) {
		say("open folder %i", host->open_path(folder, MOSAIC_ENTRY_DIR));
	}
	if (ticks == 3 && lent) {
		say("after render fill %i, decorate %i", host->fill_rect(lent, NULL, mosaic_color(0xFFFFFF, 255)),
			host->decorate_cell(lent, 0, &(MosaicRect){ 0, 0, 10, 10 }, NULL));
		if (art) say("after render art %i", host->decorate_art(lent, 0, &(MosaicRect){ 0, 0, 10, 10 }));
	}
	if (swap && ticks == 3) {
		const MosaicSurface* replaced = NULL;
		say("after render replacement %i", host->art_replacement(host->selected_index(), &replaced));
	}
	// keep frames coming, so a failed layout is seen to stay off
	if (ticks < 60) host->request_redraw();
	if (ticks == 60) say("renders %i, inputs %i", renders, inputs);
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, draw_text) || !MOSAIC_HOST_HAS(host, select_index) || !MOSAIC_HOST_HAS(host, decorate_cell)) return 1;
	snprintf(folder, sizeof(folder), "%s/Gridder", host->sdcard_path);
	fail = host->cfg_get(ID, "fail", "");
	art = atoi(host->cfg_get(ID, "art", "0")) && MOSAIC_HOST_HAS(host, decorate_art);
	swap = atoi(host->cfg_get(ID, "swap", "0")) && MOSAIC_HOST_HAS(host, art_replacement);
	return 0;
}

static void quit(void) {
	say("quit");
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.root_entries = rootEntries,
	.virtual_dir = virtualDir,
	.tick = tick,
	.layout_init = layoutInit,
	.layout_input = layoutInput,
	.layout_render = layoutRender,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
