#include "launcher.h"
#include "hintmodel.h"

// Plugins' screens and overlays, drawn over what main already put in the
// header this frame (see plugins.c for the stack and the hooks).

static SDL_Surface* overlay = NULL; // what overlays draw on, the screen's size

#define HINT_PILLS_MAX 2 // a group each side, since a group can't go over another
static MosaicRect hint_pills[HINT_PILLS_MAX]; // the hints the screen drew this frame
static int hint_pill_count = 0;

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
	hint_pill_count = 0;
	Plugins_screenRender(screen, &view);
	lastScreen = SCREEN_PLUGIN;
}

int PluginScreen_drawHints(SDL_Surface* surface, const MosaicScreenView* view, int group, const MosaicHint* hints,
	int count, MosaicRect* drawn) {
	const char* glyphs[HINTS_MAX];
	if (!surface || !view || Hints_check(group, hints, count, glyphs) != 0) return -1;
	char labels[HINTS_MAX][HINT_LABEL_MAX];
	char* pairs[HINTS_MAX * 2 + 1];
	int widths[HINTS_MAX];
	for (int i = 0; i < count; i++) {
		// labels are UI text, so they go through TR() as stock's do
		if (Hints_label(TR(hints[i].label), labels[i], sizeof(labels[i])) < 0) return -1;
		pairs[i * 2] = (char*)glyphs[i];
		pairs[i * 2 + 1] = labels[i];
	}
	pairs[count * 2] = NULL;
	if (fontMutex) SDL_LockMutex(fontMutex);
	for (int i = 0; i < count; i++) widths[i] = GFX_getButtonWidth(labels[i], (char*)glyphs[i]);
	HintMetrics m = { SCALE1(PADDING), SCALE1(PILL_SIZE), SCALE1(BUTTON_MARGIN) };
	MosaicRect pill;
	// clear of the header, and of the other group's pill this frame
	MosaicRect avoid[2 + HINT_PILLS_MAX];
	int avoid_count = 0;
	avoid[avoid_count++] = view->status;
	avoid[avoid_count++] = view->header;
	for (int i = 0; i < hint_pill_count; i++) avoid[avoid_count++] = hint_pills[i];
	int ok = hint_pill_count < HINT_PILLS_MAX && Hints_place(group, surface->w, surface->h, &m, widths, count, &pill) == 0
		&& Hints_clear(&pill, surface->w, surface->h, avoid, avoid_count);
	// stock's own call, so the pill, fonts and placement are the built-in screens'
	if (ok) GFX_blitButtonGroup(pairs, Hints_primary(group, count), surface, group == MOSAIC_HINTS_RIGHT);
	if (fontMutex) SDL_UnlockMutex(fontMutex);
	if (!ok) return -1;
	hint_pills[hint_pill_count++] = pill;
	if (drawn) *drawn = pill;
	return 0;
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
