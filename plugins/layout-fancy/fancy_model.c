#include "fancy_model.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

///////////////////////////////////////
// the list and its panel

static int bottom(const MosaicRect* r) {
	return r && r->w > 0 && r->h > 0 ? r->y + r->h : 0;
}

// the left edge of r when it's beside the row from y to y + h, else limit
static int leftOf(const MosaicRect* r, int y, int h, int limit) {
	if (!r || r->w <= 0 || r->h <= 0 || r->y >= y + h || r->y + r->h <= y) return limit;
	return r->x < limit ? r->x : limit;
}

int FL_layout(const MosaicRect* area, const MosaicRect* status, const MosaicRect* header, int scale, int panel,
		FLLayout* out) {
	memset(out, 0, sizeof(*out));
	if (scale < 1) scale = 1;
	int margin = FL_MARGIN * scale, padding = FL_PADDING * scale;
	out->row_x = area->x + margin;
	out->row_y = area->y + padding;
	out->row_h = FL_PILL * scale;
	out->name_pad = FL_NAME_PADDING * scale;
	if (panel) {
		// as stock's list beside its thumbnail: the list's share, rounded
		// down as stock's float is, less five margins, plus one, less two
		// paddings
		int list_w = area->w - (area->w * FL_PANEL_SHARE + 99) / 100;
		out->row_w = list_w - 4 * margin - 2 * padding;
		int panel_w = area->w * FL_PANEL_SHARE / 100;
		int top = area->y + padding;
		if (bottom(status) + padding > top) top = bottom(status) + padding;
		if (bottom(header) + padding > top) top = bottom(header) + padding;
		MosaicRect p = { area->x + area->w - 3 * margin - panel_w, top, panel_w, area->y + area->h - padding - top };
		if (p.w > 0 && p.h > 0) out->panel = p;
	} else {
		out->row_w = area->w - margin - 2 * padding;
	}
	if (out->row_w <= 2 * out->name_pad || out->row_y + out->row_h > area->y + area->h) return -1;

	int right = out->row_x + out->row_w;
	int limit = leftOf(status, out->row_y, out->row_h, right + padding);
	limit = leftOf(header, out->row_y, out->row_h, limit);
	out->top_w = limit - padding - out->row_x;
	if (out->top_w > out->row_w) out->top_w = out->row_w;
	if (out->top_w < 0) out->top_w = 0;
	return 0;
}

int FL_rows(const FLLayout* layout, const MosaicRect* area) {
	if (layout->row_h <= 0) return 1;
	int rows = (area->y + area->h - layout->row_y) / layout->row_h;
	return rows < 1 ? 1 : rows;
}

int FL_window(int start, int selected, int count, int rows) {
	if (count <= 0 || rows < 1) return 0;
	if (selected < 0) selected = 0;
	if (selected >= count) selected = count - 1;
	if (start > selected) start = selected;
	if (selected >= start + rows) start = selected - rows + 1;
	if (start > count - rows) start = count - rows;
	return start < 0 ? 0 : start;
}

MosaicRect FL_row(const FLLayout* layout, int slot) {
	if (slot < 0) return (MosaicRect){ 0, 0, 0, 0 };
	return (MosaicRect){ layout->row_x, layout->row_y + slot * layout->row_h, slot ? layout->row_w : layout->top_w,
		layout->row_h };
}

MosaicRect FL_name(const FLLayout* layout, const MosaicRect* row, int text_w) {
	int w = text_w < 0 ? 0 : text_w + 2 * layout->name_pad;
	if (w > row->w) w = row->w;
	return (MosaicRect){ row->x, row->y, w, row->h };
}

int FL_panelArt(int show_art, int prefer_preview, int has_box, int has_preview) {
	if (!show_art) return 0;
	if (has_preview && prefer_preview) return FL_SHOW_PREVIEW;
	return (has_box ? FL_SHOW_BOX : 0) | (has_preview ? FL_SHOW_PREVIEW : 0);
}

void FL_panelParts(const MosaicRect* panel, int show, int gap, MosaicRect* box, MosaicRect* preview) {
	MosaicRect none = { panel->x, panel->y, 0, 0 };
	*box = none;
	*preview = none;
	if ((show & FL_SHOW_BOX) && (show & FL_SHOW_PREVIEW)) {
		if (gap < 0) gap = 0;
		int h = (panel->h - gap) / 2;
		if (h <= 0) return;
		*box = (MosaicRect){ panel->x, panel->y, panel->w, h };
		*preview = (MosaicRect){ panel->x, panel->y + panel->h - h, panel->w, h };
	} else if (show & FL_SHOW_BOX) {
		*box = *panel;
	} else if (show & FL_SHOW_PREVIEW) {
		*preview = *panel;
	}
}

MosaicRect FL_contain(const MosaicRect* to, int w, int h) {
	if (w <= 0 || h <= 0) return *to;
	MosaicRect r = *to;
	if ((long)to->w * h > (long)to->h * w) {
		r.w = (int)((long)to->h * w / h);
		r.x += (to->w - r.w) / 2;
	} else {
		r.h = (int)((long)to->w * h / w);
		r.y += (to->h - r.h) / 2;
	}
	return r;
}

int FL_nameShown(int show_names, int hidden, int show_art, int selected, int has_art) {
	if (!show_names) return 0;
	if (!hidden || !show_art) return 1;
	return selected && !has_art;
}

///////////////////////////////////////
// save states

// The system folder's name in path below roms, or NULL for a path that
// isn't inside one.
static const char* systemFolder(const char* roms, const char* path, char* folder, size_t size) {
	size_t n = strlen(roms);
	if (!n || strncmp(path, roms, n) != 0 || path[n] != '/') return NULL;
	const char* start = path + n + 1;
	const char* slash = strchr(start, '/');
	if (!slash || slash == start || !slash[1] || (size_t)(slash - start) >= size) return NULL;
	memcpy(folder, start, (size_t)(slash - start));
	folder[slash - start] = '\0';
	return slash + 1;
}

int FL_tagOf(const char* roms, const char* path, char* out, size_t size) {
	char folder[512];
	if (size) out[0] = '\0';
	if (!roms || !path || !size || !systemFolder(roms, path, folder, sizeof(folder))) return 0;
	const char* tag = folder;
	char* open = strrchr(folder, '(');
	if (open) {
		tag = open + 1;
		char* close = strchr(open, ')');
		if (close) *close = '\0';
	}
	if (!*tag || strlen(tag) >= size) return 0;
	snprintf(out, size, "%s", tag);
	return 1;
}

static int endsWith(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcmp(s + a - b, suffix);
}

// "<dir>/<last part of dir><ext>"
static int named(const char* dir, size_t len, const char* ext, char* out, size_t size) {
	const char* name = dir;
	for (size_t i = 0; i < len; i++) {
		if (dir[i] == '/') name = dir + i + 1;
	}
	int namelen = (int)(dir + len - name);
	if (!namelen) return 0;
	return snprintf(out, size, "%.*s/%.*s%s", (int)len, dir, namelen, name, ext) < (int)size;
}

int FL_resumeFile(const char* path, int is_dir, int (*exists)(const char* path, void* ctx), void* ctx, char* out,
		size_t size) {
	char file[1024];
	if (size) out[0] = '\0';
	if (!path || !*path || !size || strlen(path) >= sizeof(file)) return 0;
	snprintf(file, sizeof(file), "%s", path);
	if (is_dir) {
		char cue[1024];
		size_t n = strlen(path);
		while (n > 1 && path[n - 1] == '/') n--;
		if (!named(path, n, ".cue", cue, sizeof(cue))) return 0;
		if (!exists(cue, ctx)) {
			if (!named(path, n, ".m3u", cue, sizeof(cue)) || !exists(cue, ctx)) return 0;
		}
		snprintf(file, sizeof(file), "%s", cue);
	}
	if (!endsWith(file, ".m3u")) {
		const char* slash = strrchr(file, '/');
		char m3u[1024];
		if (slash && slash > file && named(file, (size_t)(slash - file), ".m3u", m3u, sizeof(m3u)) && exists(m3u, ctx)) {
			snprintf(file, sizeof(file), "%s", m3u);
		}
	}
	if (strlen(file) >= size) return 0;
	snprintf(out, size, "%s", file);
	return 1;
}

int FL_slotPaths(const char* sdcard, const char* roms, const char* file, char* slot, char* stem, size_t size) {
	char tag[256];
	if (size) slot[0] = stem[0] = '\0';
	if (!sdcard || !file || !size || !FL_tagOf(roms, file, tag, sizeof(tag))) return 0;
	const char* name = strrchr(file, '/') + 1;
	if (snprintf(stem, size, "%s/.userdata/shared/.minui/%s/%s", sdcard, tag, name) >= (int)size ||
		snprintf(slot, size, "%s.txt", stem) >= (int)size) {
		slot[0] = stem[0] = '\0';
		return 0;
	}
	return 1;
}

int FL_slot(const char* text) {
	if (!text) return 0;
	while (isspace((unsigned char)*text)) text++;
	int negative = *text == '-';
	if (*text == '-' || *text == '+') text++;
	long n = 0;
	while (isdigit((unsigned char)*text)) {
		n = n * 10 + (*text++ - '0');
		if (n > 999) return -1;
	}
	if (negative && n) return -1;
	return (int)n;
}

int FL_previewPath(const char* stem, int slot, char* out, size_t size) {
	if (!size) return 0;
	out[0] = '\0';
	if (!stem || !*stem || slot < 0) return 0;
	if (snprintf(out, size, "%s.%d.bmp", stem, slot) >= (int)size) {
		out[0] = '\0';
		return 0;
	}
	return 1;
}
