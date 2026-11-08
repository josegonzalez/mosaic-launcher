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
