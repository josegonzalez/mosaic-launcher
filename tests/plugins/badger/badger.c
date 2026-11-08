// A plugin that decorates rows and adds a header widget, logging what it's
// handed and reading the screen back to see its drawing land where it's
// allowed and nowhere else. It adds a <sd>/Badger folder of seven games to
// the root and opens it, and logs the rows of that folder. With open=0 and
// folder=<name> in its config, it adds and opens nothing and logs the rows
// of <sd>/<name> instead, such as gridder's folder. Each row gets a red dot
// at its right end; the widget is a blue box width wide (60 by default,
// from width= in its config; 0 asks for none).
//
// With fail=row in its config, decorate_row returns an error the third time
// it's called; with fail=width, header_width does the second time; and with
// fail=widget, header_widget does. With BADGER_NO_WIDTH set in the
// environment, it describes itself without header_width.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "badger"

static const MosaicHost* host;
static char folder[512];
static const char* fail = "";
static int open_folder = 1;
static int width = 60;
static int ticks = 0;
static int rows = 0, widths = 0, widgets = 0;
static MosaicRect widget = { 0, 0, 0, 0 }; // where the widget went this frame
static unsigned int seen = 0; // rows of the folder logged, by index

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
	if (open_folder) host->list_push(root, host->entry_new(folder, MOSAIC_ENTRY_DIR, "Badger"));
}

static MosaicList* virtualDir(const char* path) {
	if (!open_folder || strcmp(path, folder) != 0) return NULL;
	static const char* names[] = { "Apple", "Banana", "Cherry", "Date", "Elder", "Fig", "Grape" };
	MosaicList* list = host->list_new();
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		char file[600];
		snprintf(file, sizeof(file), "%s/%s.gb", folder, names[i]);
		host->list_push(list, host->entry_new(file, MOSAIC_ENTRY_ROM, names[i]));
	}
	return list;
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

static int inFolder(const MosaicEntry* entry) {
	const char* path = host->entry_path(entry);
	size_t n = strlen(folder);
	return path && !strncmp(path, folder, n) && path[n] == '/';
}

static int decorateRow(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRow* row) {
	rows++;
	if (!strcmp(fail, "row") && rows == 3) {
		say("row failing");
		return -1;
	}
	const MosaicRect* r = &row->rect;
	MosaicRect dot = { r->x + r->w - 12, r->y + r->h / 2 - 3, 6, 6 };
	int filled = host->fill_rect(screen, &dot, mosaic_color(0xFF0000, 255));
	if (!inFolder(entry) || row->index < 0 || row->index >= 32) return 0;

	unsigned int bit = 1u << row->index;
	if (seen & bit) return 0;
	seen |= bit;
	say("row %s index %i selected %i rect %i,%i %ix%i name %i,%i %ix%i, row size %i",
		host->entry_name(entry), row->index, row->selected, r->x, r->y, r->w, r->h,
		row->name.x, row->name.y, row->name.w, row->name.h, row->size == sizeof(MosaicRow));
	// drawing lands in the row, and stops at its edges
	const char* before = pixel(screen, r->x + 1, r->y + r->h);
	int outside = host->fill_rect(screen, &(MosaicRect){ r->x, r->y + r->h, r->w, 4 }, mosaic_color(0xFFFF00, 255));
	say("row %s dot %i %s, clipped %i (fill %i), select %i", host->entry_name(entry), filled,
		pixel(screen, dot.x + 1, dot.y + 1), !strcmp(before, pixel(screen, r->x + 1, r->y + r->h)), outside,
		host->select_index(0));
	// the top row stops short of the widget, and sees it drawn
	if (widget.w > 0 && r->y < widget.y + widget.h && widget.y < r->y + r->h) {
		say("row %s clear of the widget %i, widget %s", host->entry_name(entry), r->x + r->w <= widget.x,
			pixel(screen, widget.x + widget.w / 2, widget.y + widget.h / 2));
	}
	return 0;
}

static int headerWidth(int max_width, int height) {
	widths++;
	if (widths == 1) say("width first: room %i height %i", max_width, height);
	if (!strcmp(fail, "width") && widths == 2) {
		say("width failing");
		return -1;
	}
	widget = (MosaicRect){ 0, 0, 0, 0 };
	return width;
}

static int headerWidget(MosaicSurface* screen, const MosaicRect* rect) {
	widgets++;
	if (!strcmp(fail, "widget") && widgets == 2) {
		say("widget failing");
		return 1;
	}
	widget = *rect;
	int filled = host->fill_rect(screen, rect, mosaic_color(0x0000FF, 255));
	if (widgets == 1) {
		// past its right edge is the status pill, which stays as it is
		int px = rect->x + rect->w + 20, py = rect->y + rect->h / 2;
		const char* drawn = pixel(screen, rect->x + 1, rect->y + 1);
		const char* before = pixel(screen, px, py);
		int outside = host->fill_rect(screen, NULL, mosaic_color(0xFFFF00, 255));
		say("widget rect %i,%i %ix%i fill %i %s, clipped %i (fill %i), select %i", rect->x, rect->y, rect->w, rect->h,
			filled, drawn, !strcmp(before, pixel(screen, px, py)), outside, host->select_index(0));
		host->fill_rect(screen, rect, mosaic_color(0x0000FF, 255));
	}
	return 0;
}

static void tick(unsigned long now) {
	(void)now;
	if (!host->current_list()) return;
	ticks++;
	if (ticks == 2 && open_folder) say("open folder %i", host->open_path(folder, MOSAIC_ENTRY_DIR));
	if (ticks == 3) say("decorate outside a layout %i", host->decorate_cell(NULL, 0, &(MosaicRect){ 0, 0, 10, 10 }, NULL));
	// keep frames coming, so a failed hook is seen to stay off
	if (ticks < 60) host->request_redraw();
	if (ticks == 60) say("rows %i, widths %i, widgets %i", rows > 3 ? 4 : rows, widths > 2 ? 3 : widths, widgets > 2 ? 3 : widgets);
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, decorate_cell)) return 1;
	snprintf(folder, sizeof(folder), "%s/%s", host->sdcard_path, host->cfg_get(ID, "folder", "Badger"));
	fail = host->cfg_get(ID, "fail", "");
	open_folder = atoi(host->cfg_get(ID, "open", "1"));
	width = atoi(host->cfg_get(ID, "width", "60"));
	return 0;
}

static MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.root_entries = rootEntries,
	.virtual_dir = virtualDir,
	.tick = tick,
	.decorate_row = decorateRow,
	.header_width = headerWidth,
	.header_widget = headerWidget,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	if (getenv("BADGER_NO_WIDTH")) plugin.header_width = NULL;
	return &plugin;
}
