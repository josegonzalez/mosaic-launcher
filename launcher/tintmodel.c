#include "tintmodel.h"

uint8_t Tint_scale(uint8_t value, uint8_t by) {
	return (uint8_t)(((unsigned)value * by + 127) / 255);
}

void Tint_mix(MosaicColor* into, MosaicColor c) {
	into->r = Tint_scale(into->r, c.r);
	into->g = Tint_scale(into->g, c.g);
	into->b = Tint_scale(into->b, c.b);
	into->a = 255;
}

int Tint_isNone(MosaicColor c) {
	return c.r == 255 && c.g == 255 && c.b == 255;
}

void Tint_table(MosaicColor c, TintTable* table) {
	for (int i = 0; i < 256; i++) {
		table->r[i] = Tint_scale((uint8_t)i, c.r);
		table->g[i] = Tint_scale((uint8_t)i, c.g);
		table->b[i] = Tint_scale((uint8_t)i, c.b);
	}
}

void Tint_row(uint32_t* row, int count, const TintTable* table) {
	for (int x = 0; x < count; x++) {
		uint32_t p = row[x];
		if (table) {
			p = (uint32_t)table->r[(p >> 16) & 0xFF] << 16 | (uint32_t)table->g[(p >> 8) & 0xFF] << 8
				| table->b[p & 0xFF];
		}
		row[x] = p | 0xFF000000u;
	}
}
