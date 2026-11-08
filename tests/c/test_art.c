// Each art path must stay byte-for-byte what stock nextui.c built inline.

#include "art.h"
#include "check.h"
#include "defines.h"

int main(void) {
	char out[1024];

	resolveArt(ART_THUMBNAIL, SDCARD_PATH "/Roms/Game Boy (GB)/Tetris (World).gb", out, sizeof(out));
	CHECK_STR(out, SDCARD_PATH "/Roms/Game Boy (GB)/.media/Tetris (World).png");

	// only the last extension is dropped
	resolveArt(ART_THUMBNAIL, SDCARD_PATH "/Roms/PS (PS)/Game.v1.1.cue", out, sizeof(out));
	CHECK_STR(out, SDCARD_PATH "/Roms/PS (PS)/.media/Game.v1.1.png");

	// folders have no extension to drop
	resolveArt(ART_THUMBNAIL, SDCARD_PATH "/Roms/Game Boy (GB)", out, sizeof(out));
	CHECK_STR(out, SDCARD_PATH "/Roms/.media/Game Boy (GB).png");

	resolveArt(ART_FOLDER_BG, SDCARD_PATH "/Roms/Game Boy (GB)", out, sizeof(out));
	CHECK_STR(out, SDCARD_PATH "/Roms/Game Boy (GB)/.media/bg.png");

	resolveArt(ART_LIST_BG, SDCARD_PATH "/Roms/Game Boy (GB)", out, sizeof(out));
	CHECK_STR(out, SDCARD_PATH "/Roms/Game Boy (GB)/.media/bglist.png");

	resolveArt(ART_DEFAULT_BG, NULL, out, sizeof(out));
	CHECK_STR(out, SDCARD_PATH "/bg.png");

	resolveArt(ART_QUICK_BG, "Wifi_off", out, sizeof(out));
	CHECK_STR(out, SDCARD_PATH "/.media/quick_Wifi_off.png");

	resolveArt(ART_QUICK_FALLBACK, NULL, out, sizeof(out));
	CHECK_STR(out, SDCARD_PATH "/.media/quick.png");

	char want[1024];
	snprintf(want, sizeof(want), SDCARD_PATH "/.system/res/Recents@%ix.png", FIXED_SCALE);
	resolveArt(ART_QUICK_ICON, "Recents", out, sizeof(out));
	CHECK_STR(out, want);

	// truncates instead of overflowing
	char small[16];
	resolveArt(ART_FOLDER_BG, SDCARD_PATH "/Roms/Game Boy (GB)", small, sizeof(small));
	CHECK(strlen(small) == sizeof(small) - 1);

	// the kinds only plugins supply have no place of their own
	int only_plugins[] = { ART_BOX, ART_BOX_3D, ART_LOGO, ART_SCREENSHOT, ART_BACKDROP };
	for (size_t i = 0; i < sizeof(only_plugins) / sizeof(only_plugins[0]); i++) {
		strcpy(out, "x");
		resolveArt(only_plugins[i], SDCARD_PATH "/Roms/Game Boy (GB)/Tetris (World).gb", out, sizeof(out));
		CHECK_STR(out, "");
	}

	// the built-in list's order: the thumbnail first, never a logo or backdrop
	CHECK(Art_listKind(0) == ART_THUMBNAIL);
	CHECK(Art_listKind(1) == ART_BOX);
	CHECK(Art_listKind(2) == ART_BOX_3D);
	CHECK(Art_listKind(3) == ART_SCREENSHOT);
	CHECK(Art_listKind(4) == -1);
	CHECK(Art_listKind(-1) == -1);

	CHECK_DONE();
}
