#include <stdio.h>
#include <string.h>
#include "switcher_model.h"

// stock's sizes, at scale 1
#define PADDING 10
#define PILL_SIZE 30
#define BUTTON_PADDING 12

int SP_style(const char* value) {
	if (value && strcmp(value, "strip") == 0) return SP_STYLE_STRIP;
	if (value && strcmp(value, "list") == 0) return SP_STYLE_LIST;
	if (value && strcmp(value, "boxart") == 0) return SP_STYLE_BOXART;
	return SP_STYLE_FULL;
}

int SP_move(int selected, int count, int by, int fresh) {
	if (count <= 1 || selected < 0 || selected >= count || by == 0) return selected;
	if (by < 0) {
		if (selected > 0) return selected + by > 0 ? selected + by : 0;
		return fresh ? count - 1 : selected;
	}
	if (selected < count - 1) return selected + by < count - 1 ? selected + by : count - 1;
	return fresh ? 0 : selected;
}

int SP_afterRemove(int selected, int count) {
	if (count <= 0) return 0;
	int to = selected - 1;
	if (to < 0 || to >= count) to = count - 1;
	return to;
}

int SP_window(int first, int selected, int count, int rows) {
	if (count <= 0 || rows <= 0) return 0;
	if (selected < 0) selected = 0;
	if (selected >= count) selected = count - 1;
	if (selected < first) first = selected;
	if (selected >= first + rows) first = selected - rows + 1;
	if (first > count - rows) first = count - rows;
	return first < 0 ? 0 : first;
}

void SP_hints(int count, int can_resume, MosaicHint left[2], int* left_count, MosaicHint right[2], int* right_count) {
	*left_count = 0;
	*right_count = 0;
	if (count <= 0) {
		right[0] = (MosaicHint){ MOSAIC_BUTTON_B, "BACK" };
		*right_count = 1;
		return;
	}
	left[0] = (MosaicHint){ MOSAIC_BUTTON_B, "BACK" };
	*left_count = 1;
	if (can_resume) {
		right[0] = (MosaicHint){ MOSAIC_BUTTON_Y, "REMOVE" };
		right[1] = (MosaicHint){ MOSAIC_BUTTON_A, "RESUME" };
		*right_count = 2;
	} else {
		right[0] = (MosaicHint){ MOSAIC_BUTTON_A, "OPEN" };
		*right_count = 1;
	}
}

///////////////////////////////////////
// the recents

int SP_sync(SPRecents* recents, int count, SPPathFn path, SPSkipFn skip, void* data) {
	if (count < 0) count = 0;
	if (count > SP_MAX_RECENTS) count = SP_MAX_RECENTS;
	int changed = recents->count < 0 || count != recents->host_count;
	for (int i = 0; i < count; i++) {
		const char* p = path(i, data);
		if (!p) p = "";
		if (!changed && strncmp(recents->host_path[i], p, SP_PATH_MAX - 1) == 0) continue;
		changed = 1;
		snprintf(recents->host_path[i], SP_PATH_MAX, "%s", p);
	}
	if (!changed) return 0;
	recents->host_count = count;
	recents->count = 0;
	for (int i = 0; i < count; i++) {
		if (skip && skip(recents->host_path[i], data)) continue;
		memcpy(recents->path[recents->count], recents->host_path[i], SP_PATH_MAX);
		recents->index[recents->count++] = i;
	}
	memset(recents->known, 0, sizeof(recents->known));
	return 1;
}

const MosaicRecent* SP_info(SPRecents* recents, int index, SPInfoFn info, void* data) {
	if (index < 0 || index >= recents->count) return NULL;
	if (!recents->known[index]) {
		MosaicRecent* r = &recents->info[index];
		memset(r, 0, sizeof(*r));
		r->size = sizeof(*r);
		recents->known[index] = info(recents->index[index], r, data) == 0 ? 1 : -1;
	}
	return recents->known[index] > 0 ? &recents->info[index] : NULL;
}

///////////////////////////////////////
// names on two lines

// the start of the UTF-8 character at or before at in text
static size_t charStart(const char* text, size_t at) {
	while (at > 0 && ((unsigned char)text[at] & 0xC0) == 0x80) at--;
	return at;
}

// text's first n bytes, less spaces at the end, and then tail, into out
static void prefix(const char* text, size_t n, const char* tail, char* out) {
	while (n > 0 && text[n - 1] == ' ') n--;
	if (n > SP_NAME_MAX - 4) n = charStart(text, SP_NAME_MAX - 4);
	memcpy(out, text, n);
	snprintf(out + n, SP_NAME_MAX - n, "%s", tail);
}

int SP_wrap(const char* text, int max_w, SPMeasureFn measure, void* ctx, char lines[2][SP_NAME_MAX]) {
	lines[0][0] = lines[1][0] = '\0';
	if (!text) return 0;
	while (*text == ' ') text++;
	size_t len = strlen(text);
	if (len >= SP_NAME_MAX) len = charStart(text, SP_NAME_MAX - 1);
	if (len == 0) return 0;
	prefix(text, len, "", lines[0]);
	if (measure(lines[0], ctx) <= max_w) return 1;

	// the first line: up to the last space it fits to, or as many
	// characters as fit, at least one
	size_t cut = 0;
	for (size_t i = len; i > 0; i--) {
		if (text[i] != ' ' || text[i - 1] == ' ') continue;
		prefix(text, i, "", lines[0]);
		if (measure(lines[0], ctx) <= max_w) {
			cut = i;
			break;
		}
	}
	if (!cut) {
		for (size_t i = charStart(text, len - 1); i > 0; i = charStart(text, i - 1)) {
			prefix(text, i, "", lines[0]);
			if (measure(lines[0], ctx) <= max_w) {
				cut = i;
				break;
			}
		}
		if (!cut) cut = 1;
		while (cut < len && ((unsigned char)text[cut] & 0xC0) == 0x80) cut++;
		prefix(text, cut, "", lines[0]);
	}

	// the second: the rest, or as much of it as fits before "..."
	const char* rest = text + cut;
	while (*rest == ' ') rest++;
	size_t rest_len = len - (size_t)(rest - text);
	if (rest_len == 0) return 1;
	prefix(rest, rest_len, "", lines[1]);
	if (measure(lines[1], ctx) <= max_w) return 2;
	for (size_t i = charStart(rest, rest_len - 1); i > 0; i = charStart(rest, i - 1)) {
		prefix(rest, i, "...", lines[1]);
		if (measure(lines[1], ctx) <= max_w) return 2;
	}
	snprintf(lines[1], SP_NAME_MAX, "...");
	return 2;
}

int SP_pillHeight(int scale, int lines, int line_h) {
	if (scale < 1) scale = 1;
	return PILL_SIZE * scale + (lines > 1 && line_h > 0 ? line_h : 0);
}

///////////////////////////////////////
// layout

static int empty(const MosaicRect* r) {
	return !r || r->w <= 0 || r->h <= 0;
}

static int bottom(const MosaicRect* r) {
	return r->y + r->h;
}

int SP_avoid(const MosaicRect* r, const MosaicRect* hole, MosaicRect out[4]) {
	if (empty(r)) return 0;
	MosaicRect m = empty(hole) ? (MosaicRect){ 0, 0, 0, 0 } : SP_meet(r, hole);
	if (empty(&m)) {
		out[0] = *r;
		return 1;
	}
	int n = 0;
	if (m.y > r->y) out[n++] = (MosaicRect){ r->x, r->y, r->w, m.y - r->y };
	if (m.x > r->x) out[n++] = (MosaicRect){ r->x, m.y, m.x - r->x, m.h };
	if (m.x + m.w < r->x + r->w) out[n++] = (MosaicRect){ m.x + m.w, m.y, r->x + r->w - m.x - m.w, m.h };
	if (bottom(&m) < bottom(r)) out[n++] = (MosaicRect){ r->x, bottom(&m), r->w, bottom(r) - bottom(&m) };
	return n;
}

MosaicRect SP_hole(const MosaicRect* status, const MosaicRect* header) {
	int has_status = !empty(status), has_header = !empty(header);
	if (!has_status && !has_header) return (MosaicRect){ 0, 0, 0, 0 };
	if (!has_header) return *status;
	if (!has_status) return *header;
	int x0 = status->x < header->x ? status->x : header->x;
	int y0 = status->y < header->y ? status->y : header->y;
	int x1 = status->x + status->w > header->x + header->w ? status->x + status->w : header->x + header->w;
	int y1 = bottom(status) > bottom(header) ? bottom(status) : bottom(header);
	return (MosaicRect){ x0, y0, x1 - x0, y1 - y0 };
}

MosaicRect SP_meet(const MosaicRect* a, const MosaicRect* b) {
	int x0 = a->x > b->x ? a->x : b->x, y0 = a->y > b->y ? a->y : b->y;
	int x1 = a->x + a->w < b->x + b->w ? a->x + a->w : b->x + b->w;
	int y1 = bottom(a) < bottom(b) ? bottom(a) : bottom(b);
	if (empty(a) || empty(b) || x1 <= x0 || y1 <= y0) return (MosaicRect){ 0, 0, 0, 0 };
	return (MosaicRect){ x0, y0, x1 - x0, y1 - y0 };
}

MosaicRect SP_source(const MosaicRect* dst, const MosaicRect* part, int src_w, int src_h) {
	if (empty(dst) || src_w <= 0 || src_h <= 0) return (MosaicRect){ 0, 0, 0, 0 };
	MosaicRect r = {
		(int)((long)(part->x - dst->x) * src_w / dst->w),
		(int)((long)(part->y - dst->y) * src_h / dst->h),
		(int)((long)part->w * src_w / dst->w),
		(int)((long)part->h * src_h / dst->h),
	};
	if (r.w < 1) r.w = 1;
	if (r.h < 1) r.h = 1;
	return r;
}

MosaicRect SP_fit(int src_w, int src_h, const MosaicRect* box) {
	if (src_w <= 0 || src_h <= 0 || empty(box)) return (MosaicRect){ box ? box->x : 0, box ? box->y : 0, 0, 0 };
	MosaicRect r = *box;
	if ((long)src_w * box->h > (long)src_h * box->w) {
		r.h = (int)((long)box->w * src_h / src_w);
		r.y += (box->h - r.h) / 2;
	} else {
		r.w = (int)((long)box->h * src_w / src_h);
		r.x += (box->w - r.w) / 2;
	}
	return r;
}

MosaicRect SP_title(const MosaicRect* area, const MosaicRect* hole, int scale, int text_w) {
	if (scale < 1) scale = 1;
	int pad = PADDING * scale, pill = PILL_SIZE * scale, inner = BUTTON_PADDING * scale;
	int x = area->x + pad;
	int right = empty(hole) ? area->x + area->w - pad : hole->x;
	int w = right - x;
	if (text_w < 0) text_w = 0;
	if (text_w + 2 * inner < w) w = text_w + 2 * inner;
	if (w <= 2 * inner) return (MosaicRect){ x, area->y + pad, 0, 0 };
	return (MosaicRect){ x, area->y + pad, w, pill };
}

MosaicRect SP_room(const MosaicRect* area, const MosaicRect* hole, int scale) {
	if (scale < 1) scale = 1;
	int pad = PADDING * scale, pill = PILL_SIZE * scale;
	int top = area->y + pad;
	if (!empty(hole) && bottom(hole) + pad > top) top = bottom(hole) + pad;
	int end = bottom(area) - pad - pill - pad;
	MosaicRect r = { area->x + pad, top, area->w - 2 * pad, end - top };
	if (r.w <= 0 || r.h <= 0) return (MosaicRect){ r.x, r.y, 0, 0 };
	return r;
}

int SP_strip(const MosaicRect* room, int aspect_w, int aspect_h, int scale, int name_h, int min_w, SPStrip* out) {
	memset(out, 0, sizeof(*out));
	if (scale < 1) scale = 1;
	if (aspect_w <= 0 || aspect_h <= 0) aspect_w = aspect_h = 1;
	if (name_h < 0) name_h = 0;
	if (min_w < 1) min_w = 1;
	if (empty(room)) return -1;
	int gap = PADDING * scale;
	int below = name_h > 0 ? gap + name_h : 0;
	int h = room->h - below;
	int w = (int)((long)h * aspect_w / aspect_h);
	if (w > room->w * 2 / 5) {
		w = room->w * 2 / 5;
		h = (int)((long)w * aspect_h / aspect_w);
	}
	if (w < min_w || h < 1) return -1;

	out->room = *room;
	out->w[0] = w;
	out->h[0] = h;
	out->cx = room->x + room->w / 2;
	int top = room->y + (room->h - h - below) / 2;
	out->cy = top + h / 2;
	if (name_h > 0) out->name = (MosaicRect){ room->x, top + h + gap, room->w, name_h };

	for (int d = 1; d <= SP_MAX_SIDES; d++) {
		int dw = out->w[d - 1] * 3 / 5, dh = out->h[d - 1] * 3 / 5;
		int c = out->centre[d - 1] + out->w[d - 1] / 2 + gap + dw / 2;
		if (dw < min_w || dh < 1 || out->cx + c - dw / 2 + dw > room->x + room->w || out->cx - c - dw / 2 < room->x) break;
		out->w[d] = dw;
		out->h[d] = dh;
		out->centre[d] = c;
		out->sides = d;
	}
	return 0;
}

MosaicRect SP_stripCard(const SPStrip* strip, int offset) {
	int k = offset < 0 ? -offset : offset;
	if (strip->w[0] <= 0 || k > strip->sides) return (MosaicRect){ 0, 0, 0, 0 };
	int sign = offset < 0 ? -1 : 1;
	return (MosaicRect){ strip->cx + sign * strip->centre[k] - strip->w[k] / 2, strip->cy - strip->h[k] / 2, strip->w[k],
		strip->h[k] };
}

int SP_list(const MosaicRect* area, const MosaicRect* hole, int scale, SPList* out) {
	memset(out, 0, sizeof(*out));
	if (scale < 1) scale = 1;
	int pad = PADDING * scale, pill = PILL_SIZE * scale;
	int x = area->x + pad;
	int w = area->w * 2 / 5 - pad;
	int top = area->y + pad;
	if (!empty(hole) && hole->x < x + w && bottom(hole) + pad > top) top = bottom(hole) + pad;
	int end = bottom(area) - pad - pill - pad;
	int rows = w > 0 ? (end - top) / pill : 0;
	if (rows < 1) return -1;
	out->rows = (MosaicRect){ x, top, w, end - top };
	out->row_h = pill;
	out->count = rows;
	MosaicRect room = SP_room(area, hole, scale);
	int px = x + w + pad;
	MosaicRect panel = { px, room.y, room.x + room.w - px, room.h };
	if (!empty(&panel)) out->panel = panel;
	return 0;
}

int SP_windowRows(int first, int selected, int count, int h, int (*height)(int index, void* data), void* data,
	int* shown) {
	*shown = 0;
	if (count <= 0) return 0;
	if (selected < 0) selected = 0;
	if (selected >= count) selected = count - 1;
	if (first < 0) first = 0;
	if (selected < first) first = selected;
	int used = 0;
	for (int i = first; i <= selected; i++) used += height(i, data);
	while (first < selected && used > h) used -= height(first++, data);
	// down at the end, rows from before fill what's left
	int tail = 0;
	for (int i = first; i < count; i++) tail += height(i, data);
	while (first > 0 && tail + height(first - 1, data) <= h) tail += height(--first, data);
	used = 0;
	for (int i = first; i < count; i++) {
		used += height(i, data);
		if (used > h && i > first) break;
		(*shown)++;
	}
	return first;
}

///////////////////////////////////////
// where play time captions go

#define BUTTON_SIZE 20
#define BUTTON_MARGIN 5

int SP_boxart(const MosaicRect* room, int scale, int text_w, int name_h, int caption_h, SPBoxart* out) {
	memset(out, 0, sizeof(*out));
	if (scale < 1) scale = 1;
	int inner = BUTTON_PADDING * scale;
	if (caption_h < 0) caption_h = 0;
	int below = caption_h > 0 ? BUTTON_MARGIN * scale + caption_h : 0;
	if (empty(room) || name_h <= 0 || name_h + below > room->h || room->w <= 2 * inner) return -1;
	if (text_w < 0) text_w = 0;
	int w = text_w + 2 * inner < room->w ? text_w + 2 * inner : room->w;
	out->name = (MosaicRect){ room->x + (room->w - w) / 2, bottom(room) - below - name_h, w, name_h };
	if (caption_h > 0) out->caption = (MosaicRect){ room->x, bottom(room) - caption_h, room->w, caption_h };
	return 0;
}

int SP_mute(int fg, int bg, int percent) {
	if (percent < 0) percent = 0;
	if (percent > 100) percent = 100;
	int rgb = 0;
	for (int shift = 16; shift >= 0; shift -= 8) {
		int a = fg >> shift & 0xFF, b = bg >> shift & 0xFF;
		rgb |= (a + (b - a) * percent / 100) << shift;
	}
	return rgb;
}

MosaicRect SP_titleCaption(const MosaicRect* area, const MosaicRect* hole, const MosaicRect* title, int scale,
	int text_h) {
	if (scale < 1) scale = 1;
	MosaicRect none = { title ? title->x : 0, title ? bottom(title) : 0, 0, 0 };
	if (empty(area) || empty(title)) return none;
	int pad = PADDING * scale;
	int h = BUTTON_SIZE * scale > text_h ? BUTTON_SIZE * scale : text_h;
	int y = bottom(title) + BUTTON_MARGIN * scale;
	if (y + h > bottom(area) - pad - PILL_SIZE * scale - pad) return none;
	int right = area->x + area->w - pad;
	if (!empty(hole) && hole->y < y + h && bottom(hole) > y && hole->x < right) right = hole->x;
	if (right <= title->x) return none;
	return (MosaicRect){ title->x, y, right - title->x, h };
}

MosaicRect SP_stripCaption(const SPStrip* strip, int pill_h, int scale, int text_h) {
	if (scale < 1) scale = 1;
	int y = strip->name.y + pill_h + BUTTON_MARGIN * scale;
	if (empty(&strip->name) || text_h <= 0 || y + text_h > bottom(&strip->name)) return (MosaicRect){ 0, 0, 0, 0 };
	return (MosaicRect){ strip->name.x, y, strip->name.w, text_h };
}

MosaicRect SP_panelCaption(const MosaicRect* panel, int scale, int text_h, MosaicRect* preview) {
	if (scale < 1) scale = 1;
	if (preview) *preview = panel ? *panel : (MosaicRect){ 0, 0, 0, 0 };
	if (empty(panel) || text_h <= 0) return (MosaicRect){ 0, 0, 0, 0 };
	int above = panel->h - BUTTON_MARGIN * scale - text_h;
	if (above < panel->h / 2) return (MosaicRect){ 0, 0, 0, 0 };
	if (preview) preview->h = above;
	return (MosaicRect){ panel->x, bottom(panel) - text_h, panel->w, text_h };
}
