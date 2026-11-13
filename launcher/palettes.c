#include <stdio.h>
#include <string.h>
#include "config.h"
#include "palette.h"
#include "palettes.h"

_Static_assert(PALETTE_COLOR_COUNT == MOSAIC_THEME_COLORS, "stock's palettes changed their colour count");
_Static_assert(PALETTE_NAME_MAX <= sizeof(((MosaicPalette*)0)->name), "stock's palette names grew");

static ColorPalette list[PALETTES_MAX];
static int count = -1; // -1 until first read

int Palettes_count(void) {
	count = PALETTE_enumerate(list, PALETTES_MAX);
	return count;
}

static void readOnce(void) {
	if (count < 0) Palettes_count();
}

int Palettes_rgb(uint32_t rgba) {
	return (int)(rgba >> 8);
}

int Palettes_info(int index, MosaicPalette* out) {
	readOnce();
	if (!out || out->size < sizeof(out->size) || index < 0 || index >= count) return -1;
	const ColorPalette* p = &list[index];
	MosaicPalette full;
	memset(&full, 0, sizeof(full));
	full.size = out->size;
	snprintf(full.name, sizeof(full.name), "%s", p->name);
	for (int i = 0; i < MOSAIC_THEME_COLORS; i++) full.colors[i] = Palettes_rgb(p->colors[i]);
	full.builtin = p->builtin ? 1 : 0;
	snprintf(full.path, sizeof(full.path), "%s", p->path);
	// only the fields the plugin knows about
	memcpy(out, &full, out->size < sizeof(full) ? out->size : sizeof(full));
	return 0;
}

int Palettes_find(const char* const* names, int n, const char* current) {
	if (!current || !current[0]) return -1;
	for (int i = 0; i < n; i++) {
		if (strcmp(names[i], current) == 0) return i;
	}
	return -1;
}

int Palettes_selected(void) {
	readOnce();
	const char* names[PALETTES_MAX];
	for (int i = 0; i < count; i++) names[i] = list[i].name;
	return Palettes_find(names, count, CFG_getPaletteName());
}
