#include "launcher.h"

static char message[128];
static unsigned long expires = 0; // 0: nothing showing
static int pending = 0;           // shown before the first tick timed it

void Toast_show(const char* text) {
	if (!text || !text[0]) return;
	snprintf(message, sizeof(message), "%s", text);
	pending = 1;
	state.dirty = 1;
}

void Toast_tick(unsigned long now) {
	if (pending) {
		expires = now + TOAST_DURATION_MS;
		pending = 0;
	}
	else if (expires && now >= expires) {
		expires = 0;
		message[0] = '\0';
		state.dirty = 1; // redraw without it
	}
}

void Toast_render(void) {
	if (!message[0]) return;

	int text_w, text_h;
	GFX_sizeText(font.small, message, SCALE1(FONT_SMALL), &text_w, &text_h);
	int max_w = screen->w - SCALE1(PADDING * 2);
	int pill_w = MIN(text_w + SCALE1(BUTTON_PADDING * 2), max_w);
	int pill_h = SCALE1(PILL_SIZE);

	// over the button hints, centred
	SDL_Rect pill = { (screen->w - pill_w) / 2, screen->h - SCALE1(PADDING) - pill_h, pill_w, pill_h };
	GFX_blitPillDark(ASSET_WHITE_PILL, screen, &pill);

	SDL_Rect text = { pill.x + SCALE1(BUTTON_PADDING), pill.y + (pill_h - text_h) / 2,
		pill_w - SCALE1(BUTTON_PADDING * 2), text_h };
	GFX_blitText(font.small, message, SCALE1(FONT_SMALL), uintToColour(THEME_COLOR5_255), screen, &text);
}
