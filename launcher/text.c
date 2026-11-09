#include "launcher.h"
#include "drawmodel.h"

static int measure(const char* text, void* data) {
	int w = 0;
	if (!text[0]) return 0;
	if (TTF_SizeUTF8((TTF_Font*)data, text, &w, NULL) != 0) return -1;
	return w;
}

int Text_truncate(TTF_Font* font, const char* text, char* out, size_t size, int max_width, int padding) {
	if (fontMutex) SDL_LockMutex(fontMutex);
	int width = Draw_truncateChars(text ? text : "", max_width, padding, measure, font, out, size);
	if (fontMutex) SDL_UnlockMutex(fontMutex);
	if (width < 0) {
		if (size) out[0] = '\0';
		return padding;
	}
	return width;
}

void Text_hardwareHints(SDL_Surface* dst, int show_setting) {
	if (show_setting == 1)
		GFX_blitButtonGroup((char*[]){ BRIGHTNESS_BUTTON_LABEL, TR("BRIGHTNESS"), NULL }, 0, dst, 0);
	else if (show_setting == 3)
		GFX_blitButtonGroup((char*[]){ BRIGHTNESS_BUTTON_LABEL, TR("COLOR TEMP"), NULL }, 0, dst, 0);
	else
		GFX_blitButtonGroup((char*[]){ "MNU", TR("BRGHT"), "SEL", TR("CLTMP"), NULL }, 0, dst, 0);
}
