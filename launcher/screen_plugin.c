#include "launcher.h"

// Plugins' screens and overlays, drawn over what main already put in the
// header this frame (see plugins.c for the stack and the hooks).

static SDL_Surface* overlay = NULL; // what overlays draw on, the screen's size

// The whole screen, and where the header main drew this frame is: none on
// a MOSAIC_SCREEN_FULL screen, where ow is 0.
static void screenView(MosaicScreenView* v) {
	memset(v, 0, sizeof(*v));
	v->size = sizeof(*v);
	LayoutView_area(screen->w, screen->h, screen->h, SCALE1(PADDING), ow, SCALE1(PILL_SIZE), &v->area, &v->status);
	v->area = (MosaicRect){ 0, 0, screen->w, screen->h };
	if (ow > 0 && header_w > 0) v->header = (MosaicRect){ v->status.x - header_w, v->status.y, header_w, v->status.h };
	v->screen = Plugins_currentScreen();
}

void PluginScreen_handleInput(void) {
	Plugins_screenInput();
}

void PluginScreen_render(void) {
	// what the screen under it left on the layers goes, as for a dialog
	GFX_clearLayers(LAYER_ALL);
	MosaicScreenView view;
	screenView(&view);
	Plugins_screenRender(screen, &view);
	lastScreen = SCREEN_PLUGIN;
}

void Overlays_render(void) {
	if (!Plugins_overlaysOver(currentScreen)) return;
	if (!overlay) {
		overlay = SDL_CreateRGBSurfaceWithFormat(0, screen->w, screen->h, 32, SDL_PIXELFORMAT_ARGB8888);
		if (!overlay) return;
		SDL_SetSurfaceBlendMode(overlay, SDL_BLENDMODE_BLEND);
	}
	SDL_FillRect(overlay, NULL, SDL_MapRGBA(overlay->format, 0, 0, 0, 0));
	MosaicScreenView view;
	screenView(&view);
	Plugins_overlays(overlay, &view);
	// the topmost layer, over the thumbnail and the scrolling name too
	GFX_clearLayers(LAYER_IDK2);
	GFX_drawOnLayer(overlay, 0, 0, screen->w, screen->h, 1.0f, 0, LAYER_IDK2);
}

void Overlays_quit(void) {
	if (overlay) SDL_FreeSurface(overlay);
	overlay = NULL;
}
