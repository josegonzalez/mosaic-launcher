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

int SP_sync(SPRecents* recents, int count, SPPathFn path, void* data) {
	if (count < 0) count = 0;
	if (count > SP_MAX_RECENTS) count = SP_MAX_RECENTS;
	int changed = count != recents->count;
	for (int i = 0; i < count; i++) {
		const char* p = path(i, data);
		if (!p) p = "";
		if (!changed && strncmp(recents->path[i], p, SP_PATH_MAX - 1) == 0) continue;
		changed = 1;
		snprintf(recents->path[i], SP_PATH_MAX, "%s", p);
	}
	if (!changed) return 0;
	recents->count = count;
	memset(recents->known, 0, sizeof(recents->known));
	return 1;
}

const MosaicRecent* SP_info(SPRecents* recents, int index, SPInfoFn info, void* data) {
	if (index < 0 || index >= recents->count) return NULL;
	if (!recents->known[index]) {
		MosaicRecent* r = &recents->info[index];
		memset(r, 0, sizeof(*r));
		r->size = sizeof(*r);
		recents->known[index] = info(index, r, data) == 0 ? 1 : -1;
	}
	return recents->known[index] > 0 ? &recents->info[index] : NULL;
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
	out->rows = (MosaicRect){ x, top, w, rows * pill };
	out->row_h = pill;
	out->count = rows;
	MosaicRect room = SP_room(area, hole, scale);
	int px = x + w + pad;
	MosaicRect panel = { px, room.y, room.x + room.w - px, room.h };
	if (!empty(&panel)) out->panel = panel;
	return 0;
}
