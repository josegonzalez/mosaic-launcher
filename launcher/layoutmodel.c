#include <string.h>
#include "layoutmodel.h"

void LayoutSlot_claim(LayoutSlot* slot, int owner, int has_render) {
	slot->owner = owner;
	slot->state = owner >= 0 && has_render ? LAYOUT_WAITING : LAYOUT_NONE;
}

int LayoutSlot_active(const LayoutSlot* slot) {
	return slot->state == LAYOUT_WAITING || slot->state == LAYOUT_ON;
}

int LayoutSlot_needsInit(const LayoutSlot* slot) {
	return slot->state == LAYOUT_WAITING;
}

int LayoutSlot_result(LayoutSlot* slot, int rc) {
	if (!LayoutSlot_active(slot)) return 0;
	slot->state = rc == 0 ? LAYOUT_ON : LAYOUT_OFF;
	return slot->state == LAYOUT_ON;
}

void LayoutSlot_clear(LayoutSlot* slot) {
	slot->state = LAYOUT_NONE;
	slot->owner = -1;
}

unsigned int LayoutInput_bits(const LayoutButton* map, int count, int pad_bits) {
	unsigned int bits = 0;
	for (int i = 0; i < count; i++) {
		if (map[i].mosaic < 0 || map[i].mosaic >= 32) continue;
		if (pad_bits & map[i].pad) bits |= MOSAIC_BUTTON_BIT(map[i].mosaic);
	}
	return bits;
}

int LayoutInput_padBits(const LayoutButton* map, int count, unsigned int bits) {
	int pad = 0;
	for (int i = 0; i < count; i++) {
		if (map[i].mosaic < 0 || map[i].mosaic >= 32) continue;
		if (bits & MOSAIC_BUTTON_BIT(map[i].mosaic)) pad |= map[i].pad;
	}
	return pad;
}

void LayoutInput_fill(MosaicInput* out, const LayoutButton* map, int count,
		int pressed, int repeated, int released, int held) {
	memset(out, 0, sizeof(*out));
	out->size = sizeof(*out);
	out->pressed = LayoutInput_bits(map, count, pressed);
	// stock's repeat starts on the press itself
	out->repeated = LayoutInput_bits(map, count, repeated | pressed);
	out->released = LayoutInput_bits(map, count, released);
	out->held = LayoutInput_bits(map, count, held);
}

void LayoutView_area(int w, int h, int hints_top, int padding, int status_w, int status_h,
		MosaicRect* area, MosaicRect* status) {
	if (w < 0) w = 0;
	if (hints_top > h) hints_top = h;
	if (hints_top < 0) hints_top = 0;
	*area = (MosaicRect){ 0, 0, w, hints_top };
	if (status_w <= 0) *status = (MosaicRect){ 0, 0, 0, 0 };
	else *status = (MosaicRect){ w - padding - status_w, padding, status_w, status_h };
}

int LayoutRect_clip(const MosaicRect* a, const MosaicRect* b, MosaicRect* out) {
	int x1 = a->x > b->x ? a->x : b->x;
	int y1 = a->y > b->y ? a->y : b->y;
	long ax2 = (long)a->x + a->w, bx2 = (long)b->x + b->w;
	long ay2 = (long)a->y + a->h, by2 = (long)b->y + b->h;
	long x2 = ax2 < bx2 ? ax2 : bx2;
	long y2 = ay2 < by2 ? ay2 : by2;
	if (a->w <= 0 || a->h <= 0 || b->w <= 0 || b->h <= 0 || x2 <= x1 || y2 <= y1) {
		*out = (MosaicRect){ 0, 0, 0, 0 };
		return 0;
	}
	*out = (MosaicRect){ x1, y1, (int)(x2 - x1), (int)(y2 - y1) };
	return 1;
}

void ListRow_rects(int row, int margin, int padding, int pill_h, int width, int name_w, int show_names,
		MosaicRow* out) {
	if (width < 0) width = 0;
	if (name_w < 0 || !show_names) name_w = 0;
	if (name_w > width) name_w = width;
	out->rect = (MosaicRect){ margin, padding + row * pill_h, width, pill_h };
	out->name = (MosaicRect){ margin, out->rect.y, name_w, pill_h };
}

int ListRow_nameShown(int show_names, int hidden, int show_art, int selected, int has_art) {
	if (!show_names) return 0;
	if (!hidden || !show_art) return 1;
	return selected && !has_art;
}

void ListSelection_clear(ListSelection* sel) {
	sel->row = (MosaicRect){ 0, 0, 0, 0 };
	sel->pill = sel->row;
}

void ListSelection_row(ListSelection* sel, int row, int margin, int padding, int pill_h, int width, int name_w,
		int show_names) {
	MosaicRow rects;
	ListRow_rects(row, margin, padding, pill_h, width, name_w, show_names, &rects);
	ListSelection_clear(sel);
	if (rects.rect.w <= 0 || rects.rect.h <= 0) return;
	sel->row = rects.rect;
	if (rects.name.w > 0) sel->pill = rects.name;
}

void ListSelection_cell(ListSelection* sel, int index, int selected, const MosaicRect* rect, const MosaicRect* area) {
	if (index != selected || !rect || !area) return;
	MosaicRect clip;
	if (!LayoutRect_clip(rect, area, &clip)) return;
	sel->row = clip;
	sel->pill = (MosaicRect){ 0, 0, 0, 0 };
}

void HeaderRow_init(HeaderRow* row, int screen_w, int padding, int gap, int status_w, int pill_h) {
	if (status_w < 0) status_w = 0;
	if (gap < 0) gap = 0;
	row->y = padding;
	row->h = pill_h;
	row->status_x = screen_w - padding - status_w;
	row->gap = gap;
	row->next = row->status_x - (status_w > 0 ? gap : 0);
	// half of what's left of the pill, past the screen's own padding
	int left = row->status_x - padding;
	row->limit = row->status_x - (left > 0 ? left / 2 : 0);
	row->count = 0;
}

int HeaderRow_room(const HeaderRow* row) {
	int room = row->next - row->limit;
	return room > 0 ? room : 0;
}

int HeaderRow_place(HeaderRow* row, int want, MosaicRect* out) {
	int room = HeaderRow_room(row);
	if (want <= 0 || room <= 0) return 0;
	int w = want < room ? want : room;
	*out = (MosaicRect){ row->next - w, row->y, w, row->h };
	row->next = out->x - row->gap;
	row->count++;
	return 1;
}

int HeaderRow_width(const HeaderRow* row) {
	return row->count ? row->status_x - (row->next + row->gap) : 0;
}

void HeaderRow_rect(const HeaderRow* row, MosaicRect* out) {
	int w = HeaderRow_width(row);
	if (w <= 0) *out = (MosaicRect){ 0, 0, 0, 0 };
	else *out = (MosaicRect){ row->status_x - w, row->y, w, row->h };
}

int HeaderRow_topRowWidth(int available, int screen_w, int padding, int margin, int status_w, int header_w) {
	if (header_w <= 0) return available;
	int clear = screen_w - padding - status_w - header_w - padding - margin;
	if (clear < 0) clear = 0;
	return available < clear ? available : clear;
}

///////////////////////////////////////
// the background slot

void BackgroundSlot_claim(BackgroundSlot* slot, int owner) {
	slot->owner = owner;
	slot->off = 0;
	slot->drawn = 0;
	slot->built = 0;
}

void BackgroundSlot_clear(BackgroundSlot* slot) {
	BackgroundSlot_claim(slot, -1);
}

int BackgroundSlot_active(const BackgroundSlot* slot) {
	return slot->owner >= 0 && !slot->off;
}

int BackgroundSlot_result(BackgroundSlot* slot, int rc, unsigned serial) {
	if (!BackgroundSlot_active(slot)) return 0;
	if (rc != 0) {
		slot->off = 1;
		slot->drawn = 0;
		return 0;
	}
	slot->drawn = 1;
	slot->built = serial;
	return 1;
}

void BackgroundSlot_skip(BackgroundSlot* slot) {
	slot->drawn = 0;
}

int BackgroundSlot_show(const BackgroundSlot* slot, int on_gamelist, unsigned serial) {
	if (!on_gamelist || !BackgroundSlot_active(slot) || !slot->drawn) return BACKGROUND_STOCK;
	return slot->built == serial ? BACKGROUND_PLUGIN : BACKGROUND_REDRAW;
}
