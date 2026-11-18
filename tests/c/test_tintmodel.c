// The screen tint's arithmetic: putting plugins' tints together and
// tinting a captured frame as it was shown.

#include "check.h"
#include "tintmodel.h"

int main(void) {
	// multiplying, rounded as the GPU blends
	CHECK(Tint_scale(255, 255) == 255);
	CHECK(Tint_scale(255, 127) == 127);
	CHECK(Tint_scale(0, 200) == 0);
	CHECK(Tint_scale(200, 0) == 0);
	CHECK(Tint_scale(128, 128) == 64);
	CHECK(Tint_scale(100, 191) == 75);

	// none is white, whatever its alpha
	CHECK(Tint_isNone(TINT_NONE));
	CHECK(Tint_isNone((MosaicColor){ 255, 255, 255, 0 }));
	CHECK(!Tint_isNone((MosaicColor){ 255, 254, 255, 255 }));

	// two tints multiply together, in either order
	MosaicColor a = TINT_NONE;
	Tint_mix(&a, (MosaicColor){ 255, 223, 127, 255 });
	CHECK(a.r == 255 && a.g == 223 && a.b == 127 && a.a == 255);
	Tint_mix(&a, (MosaicColor){ 128, 255, 0, 12 });
	CHECK(a.r == 128 && a.g == 223 && a.b == 0 && a.a == 255);
	MosaicColor b = TINT_NONE;
	Tint_mix(&b, (MosaicColor){ 128, 255, 0, 255 });
	Tint_mix(&b, (MosaicColor){ 255, 223, 127, 255 });
	CHECK(b.r == a.r && b.g == a.g && b.b == a.b);

	// a frame: opaque, and multiplied by the tint
	TintTable table;
	Tint_table((MosaicColor){ 255, 223, 127, 255 }, &table);
	uint32_t row[4] = { 0x00FFFFFFu, 0x80000000u, 0xFF808080u, 0x12345678u };
	Tint_row(row, 4, &table);
	CHECK(row[0] == 0xFFFFDF7Fu);
	CHECK(row[1] == 0xFF000000u);
	CHECK(row[2] == 0xFF807040u);
	CHECK(row[3] == 0xFF344B3Cu);
	// without one, only opaque
	uint32_t plain[2] = { 0x00FFFFFFu, 0x7F102030u };
	Tint_row(plain, 2, NULL);
	CHECK(plain[0] == 0xFFFFFFFFu && plain[1] == 0xFF102030u);
	// the table is the same arithmetic
	for (int i = 0; i < 256; i++) CHECK(table.g[i] == Tint_scale((uint8_t)i, 223));

	CHECK_DONE();
}
