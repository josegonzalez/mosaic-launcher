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

int FL_position(const char* value) {
	if (value && !strcmp(value, "left")) return FL_ART_LEFT;
	if (value && !strcmp(value, "top")) return FL_ART_TOP;
	return FL_ART_RIGHT;
}

const char* FL_positionName(int position) {
	return position == FL_ART_LEFT ? "left" : position == FL_ART_TOP ? "top" : "right";
}

// whether r is beside x to x + w
static int across(const MosaicRect* r, int x, int w) {
	return r && r->w > 0 && r->h > 0 && r->x < x + w && r->x + r->w > x;
}

// how far in from either side x to x + w goes to stay padding clear of r
// where r is beside y0 to y1, at least inset
static int clearOf(const MosaicRect* r, int x, int w, int y0, int y1, int padding, int inset) {
	if (!across(r, x, w) || r->y >= y1 || r->y + r->h <= y0) return inset;
	int in = r->x + r->w / 2 >= x + w / 2 ? x + w - (r->x - padding) : r->x + r->w + padding - x;
	return in > inset ? in : inset;
}

int FL_layout(const MosaicRect* area, const MosaicRect* status, const MosaicRect* header, int scale, int panel,
		FLLayout* out) {
	return FL_layoutAt(area, status, header, scale, panel, FL_ART_RIGHT, out);
}

int FL_layoutAt(const MosaicRect* area, const MosaicRect* status, const MosaicRect* header, int scale, int panel,
		int position, FLLayout* out) {
	memset(out, 0, sizeof(*out));
	if (scale < 1) scale = 1;
	int margin = FL_MARGIN * scale, padding = FL_PADDING * scale;
	out->row_x = area->x + margin;
	out->row_y = area->y + padding;
	out->row_h = FL_PILL * scale;
	out->name_pad = FL_NAME_PADDING * scale;
	if (panel && position == FL_ART_TOP) {
		// the art across the top half, the rows under it as wide as without art
		int split = area->y + area->h / 2;
		out->row_y = split;
		out->row_w = area->w - margin - 2 * padding;
		int x = area->x + 3 * margin, w = area->w - 6 * margin;
		// from the top, centred and kept clear of status and header beside it
		int top = area->y + padding, inset = 0;
		inset = clearOf(status, x, w, top, split - padding, padding, inset);
		inset = clearOf(header, x, w, top, split - padding, padding, inset);
		if (2 * inset <= w / 2) {
			x += inset;
			w -= 2 * inset;
		} else {
			// too narrow that way: across, under them
			if (across(status, x, w) && bottom(status) + padding > top) top = bottom(status) + padding;
			if (across(header, x, w) && bottom(header) + padding > top) top = bottom(header) + padding;
		}
		MosaicRect p = { x, top, w, split - padding - top };
		if (p.w > 0 && p.h > 0) out->panel = p;
	} else if (panel) {
		// as stock's list beside its thumbnail: the list's share, rounded
		// down as stock's float is, less five margins, plus one, less two
		// paddings
		int list_w = area->w - (area->w * FL_PANEL_SHARE + 99) / 100;
		out->row_w = list_w - 4 * margin - 2 * padding;
		int panel_w = area->w * FL_PANEL_SHARE / 100;
		int x = area->x + area->w - 3 * margin - panel_w;
		if (position == FL_ART_LEFT) {
			// mirrored: the panel as far in from the left, the rows ending a margin short of the right
			x = area->x + 3 * margin;
			out->row_x = area->x + area->w - margin - out->row_w;
		}
		int top = area->y + padding;
		// on the right, below status and header as before; on the left, only below those above the panel
		int left = position == FL_ART_LEFT;
		if ((!left || across(status, x, panel_w)) && bottom(status) + padding > top) top = bottom(status) + padding;
		if ((!left || across(header, x, panel_w)) && bottom(header) + padding > top) top = bottom(header) + padding;
		MosaicRect p = { x, top, panel_w, area->y + area->h - padding - top };
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

int FL_replacedArt(int show_art, int prefer_preview, int has_preview) {
	if (!show_art) return 0;
	return FL_SHOW_BOX | (has_preview && !prefer_preview ? FL_SHOW_PREVIEW : 0);
}

MosaicRect FL_around(const MosaicRect* a, const MosaicRect* b) {
	int a_in = a->w > 0 && a->h > 0, b_in = b->w > 0 && b->h > 0;
	if (!a_in) return b_in ? *b : (MosaicRect){ a->x, a->y, 0, 0 };
	if (!b_in) return *a;
	int x0 = a->x < b->x ? a->x : b->x, y0 = a->y < b->y ? a->y : b->y;
	int x1 = a->x + a->w > b->x + b->w ? a->x + a->w : b->x + b->w;
	int y1 = a->y + a->h > b->y + b->h ? a->y + a->h : b->y + b->h;
	return (MosaicRect){ x0, y0, x1 - x0, y1 - y0 };
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
// the description

void FL_descParts(const MosaicRect* panel, int position, int gap, MosaicRect* art, MosaicRect* desc) {
	if (gap < 0) gap = 0;
	*art = *panel;
	*desc = *panel;
	if (position == FL_ART_TOP) {
		int w = (panel->w - gap) / 2;
		if (w < 0) w = 0;
		art->w = w;
		desc->x = panel->x + panel->w - w;
		desc->w = w;
	} else {
		int h = (panel->h - gap) / 2;
		if (h < 0) h = 0;
		art->h = h;
		desc->y = panel->y + panel->h - h;
		desc->h = h;
	}
}

MosaicRect FL_descRest(const MosaicRect* panel, const MosaicRect* drawn, int position, int gap) {
	if (!drawn || drawn->w <= 0 || drawn->h <= 0) return *panel;
	if (gap < 0) gap = 0;
	MosaicRect r = *panel;
	if (position == FL_ART_TOP) {
		int x = drawn->x + drawn->w + gap;
		if (x < panel->x) x = panel->x;
		r.x = x;
		r.w = panel->x + panel->w - x;
	} else {
		int y = drawn->y + drawn->h + gap;
		if (y < panel->y) y = panel->y;
		r.y = y;
		r.h = panel->y + panel->h - y;
	}
	if (r.w < 0) r.w = 0;
	if (r.h < 0) r.h = 0;
	return r;
}

MosaicRect FL_pin(const MosaicRect* fitted, const MosaicRect* region, int position) {
	MosaicRect r = *fitted;
	if (position == FL_ART_TOP) r.x = region->x;
	else r.y = region->y;
	return r;
}

// the end of the UTF-8 character starting at i
static int charEnd(const char* text, int i, int n) {
	i++;
	while (i < n && ((unsigned char)text[i] & 0xC0) == 0x80) i++;
	return i;
}

int FL_wrap(const char* text, int max_w, int (*width)(const char* text, int len, void* ctx), void* ctx, FLLine* lines,
		int max) {
	if (!text || max_w <= 0 || !width || !lines || max <= 0) return 0;
	int n = (int)strlen(text), count = 0, pos = 0;
	while (count < max) {
		while (pos < n && text[pos] == ' ') pos++;
		if (pos >= n) break;
		int fit = -1, cursor = pos;
		// as many whole words as fit
		while (cursor < n) {
			int end = cursor;
			while (end < n && text[end] != ' ') end++;
			if (width(text + pos, end - pos, ctx) > max_w) break;
			fit = end;
			cursor = end;
			while (cursor < n && text[cursor] == ' ') cursor++;
		}
		if (fit < 0) {
			// a word wider than a line, cut between characters, at least one
			fit = charEnd(text, pos, n);
			while (fit < n && text[fit] != ' ') {
				int next = charEnd(text, fit, n);
				if (width(text + pos, next - pos, ctx) > max_w) break;
				fit = next;
			}
		}
		lines[count].start = pos;
		lines[count].len = fit - pos;
		count++;
		pos = fit;
	}
	return count;
}

int FL_scrollOffset(long since_ms, int content_h, int view_h, int gap, int speed) {
	if (content_h <= view_h || since_ms < FL_DESC_WAIT_MS || speed <= 0) return 0;
	if (gap < 0) gap = 0;
	long moved = (since_ms - FL_DESC_WAIT_MS) * speed / 1000;
	return (int)(moved % (content_h + gap));
}

int FL_scrollParts(const MosaicRect* view, int content_h, int gap, int offset, MosaicRect src[2], MosaicRect dst[2]) {
	if (view->w <= 0 || view->h <= 0 || content_h <= 0) return 0;
	if (gap < 0) gap = 0;
	int period = content_h + gap;
	if (offset < 0 || offset >= period) offset = 0;
	int count = 0;
	if (offset < content_h) {
		int h = content_h - offset < view->h ? content_h - offset : view->h;
		src[count] = (MosaicRect){ 0, offset, view->w, h };
		dst[count] = (MosaicRect){ view->x, view->y, view->w, h };
		count++;
	}
	// the next copy, a gap under it, once the text scrolls
	int next = period - offset;
	if (content_h > view->h && next < view->h) {
		int h = view->h - next < content_h ? view->h - next : content_h;
		src[count] = (MosaicRect){ 0, 0, view->w, h };
		dst[count] = (MosaicRect){ view->x, view->y + next, view->w, h };
		count++;
	}
	return count;
}

int FL_pages(int y, int h, int page_h, FLPiece* out, int max) {
	if (y < 0 || h <= 0 || page_h <= 0) return 0;
	int count = 0;
	while (h > 0 && count < max) {
		int page = y / page_h, in = y - page * page_h;
		int part = page_h - in < h ? page_h - in : h;
		out[count++] = (FLPiece){ page, in, part };
		y += part;
		h -= part;
	}
	return count;
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
