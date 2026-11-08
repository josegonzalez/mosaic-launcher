#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include "defines.h"
#include "art.h"

// <rom dir>/.media/<rom file name without its last extension>.png
static void thumbnailPath(const char* rom_path, char* out, size_t size) {
	char path_copy[1024];
	strncpy(path_copy, rom_path, sizeof(path_copy) - 1);
	path_copy[sizeof(path_copy) - 1] = '\0';
	char* rompath = dirname(path_copy);

	const char* res_name = strrchr(rom_path, '/');
	res_name = res_name ? res_name + 1 : rom_path;

	char res_copy[1024];
	strncpy(res_copy, res_name, sizeof(res_copy) - 1);
	res_copy[sizeof(res_copy) - 1] = '\0';
	char* dot = strrchr(res_copy, '.');
	if (dot) *dot = '\0';

	snprintf(out, size, "%s/.media/%s.png", rompath, res_copy);
}

void resolveArt(int kind, const char* subject, char* out, size_t size) {
	switch (kind) {
		case ART_THUMBNAIL:
			thumbnailPath(subject, out, size);
			break;
		case ART_FOLDER_BG:
			snprintf(out, size, "%s/.media/bg.png", subject);
			break;
		case ART_LIST_BG:
			snprintf(out, size, "%s/.media/bglist.png", subject);
			break;
		case ART_DEFAULT_BG:
			snprintf(out, size, SDCARD_PATH "/bg.png");
			break;
		case ART_QUICK_BG:
			snprintf(out, size, SDCARD_PATH "/.media/quick_%s.png", subject);
			break;
		case ART_QUICK_FALLBACK:
			snprintf(out, size, SDCARD_PATH "/.media/quick.png");
			break;
		case ART_QUICK_ICON:
			snprintf(out, size, SDCARD_PATH "/.system/res/%s@%ix.png", subject, FIXED_SCALE);
			break;
		default:
			if (size) out[0] = '\0';
			break;
	}
}

int Art_listKind(int i) {
	static const int kinds[] = { ART_THUMBNAIL, ART_BOX, ART_BOX_3D, ART_SCREENSHOT };
	return i >= 0 && i < (int)(sizeof(kinds) / sizeof(kinds[0])) ? kinds[i] : -1;
}
