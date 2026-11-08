#include "launcher.h"
#include "background.h"

static SDL_Surface* drawn = NULL; // what the owner drew, the screen's size

// Stock's background as the layer shows it: folderbgbmp stretched to the
// screen, alpha and all, and clear where it has none, which the layer
// shows as the theme's background colour. With bgMutex held.
static void fillStock(SDL_Surface* s) {
	SDL_FillRect(s, NULL, SDL_MapRGBA(s->format, 0, 0, 0, 0));
	if (!folderbgbmp) return;
	SDL_BlendMode mode;
	SDL_GetSurfaceBlendMode(folderbgbmp, &mode);
	SDL_SetSurfaceBlendMode(folderbgbmp, SDL_BLENDMODE_NONE);
	if (folderbgbmp->w == s->w && folderbgbmp->h == s->h) SDL_BlitSurface(folderbgbmp, NULL, s, NULL);
	else SDL_BlitScaled(folderbgbmp, NULL, s, NULL);
	SDL_SetSurfaceBlendMode(folderbgbmp, mode);
}

void Background_render(void) {
	if (!Plugins_hasBackground()) return;
	if (!drawn) {
		drawn = SDL_CreateRGBSurfaceWithFormat(0, screen->w, screen->h, 32, SDL_PIXELFORMAT_ARGB8888);
		if (drawn) SDL_SetSurfaceBlendMode(drawn, SDL_BLENDMODE_BLEND);
	}
	if (!drawn) {
		Plugins_backgroundSkip();
		return;
	}
	SDL_LockMutex(bgMutex);
	unsigned serial = folderbgserial;
	fillStock(drawn);
	SDL_UnlockMutex(bgMutex);
	const Entry* selected = state.top->entries->count > 0 ? state.top->entries->items[state.top->selected] : NULL;
	Plugins_backgroundRender(drawn, selected, serial);
	// the layer takes it, or stock's again after an error
	folderbgchanged = 1;
}

int Background_layer(void) {
	int show = Plugins_backgroundShow(currentScreen == SCREEN_GAMELIST && drawn, folderbgserial);
	if (show != BACKGROUND_STOCK) {
		// cleared first, so where it's clear the theme's colour shows, as with stock's
		GFX_clearLayers(LAYER_BACKGROUND);
		GFX_drawOnLayer(drawn, 0, 0, screen->w, screen->h, 1.0f, 0, LAYER_BACKGROUND);
		if (show == BACKGROUND_PLUGIN) return 1;
		// until the owner draws over the new one
		state.dirty = 1;
		return 0;
	}
	if (folderbgbmp) GFX_drawOnLayer(folderbgbmp, 0, 0, screen->w, screen->h, 1.0f, 0, LAYER_BACKGROUND);
	else GFX_clearLayers(LAYER_BACKGROUND);
	return 1;
}

void Background_quit(void) {
	if (drawn) SDL_FreeSurface(drawn);
	drawn = NULL;
}
