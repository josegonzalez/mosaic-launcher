#include "launcher.h"
#include "tintmodel.h"
#include "tint.h"

// SDL's own, which Tint_present calls (tintpresent.h sends every other
// file's calls here)
#undef SDL_RenderPresent
extern DECLSPEC void SDLCALL SDL_RenderPresent(SDL_Renderer* renderer);

static MosaicColor wanted = TINT_NONE; // for the frames shown from now on
static MosaicColor shown = TINT_NONE;  // on the frame on screen

void Tint_set(int on, MosaicColor c) {
	wanted = on ? c : TINT_NONE;
	wanted.a = 255;
}

int Tint_shown(MosaicColor* out) {
	if (out) *out = shown;
	return !Tint_isNone(shown);
}

// Every frame the platform shows, multiplied by the tint: one fill over
// the whole frame on the GPU, blended by multiplying, so it costs the same
// whatever's on screen.
void Tint_present(SDL_Renderer* renderer) {
	shown = wanted;
	if (!Tint_isNone(shown)) {
		SDL_BlendMode mode;
		Uint8 r, g, b, a;
		SDL_GetRenderDrawBlendMode(renderer, &mode);
		SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_MOD);
		SDL_SetRenderDrawColor(renderer, shown.r, shown.g, shown.b, 255);
		SDL_RenderFillRect(renderer, NULL);
		SDL_SetRenderDrawColor(renderer, r, g, b, a);
		SDL_SetRenderDrawBlendMode(renderer, mode);
	}
	SDL_RenderPresent(renderer);
}

SDL_Surface* Tint_captureShown(void) {
	// what's on screen has the tint, so put the layers together again
	// without it, as GFX_flipHidden does for a transition's new frame
	if (Tint_shown(NULL)) GFX_flipHidden();
	return GFX_captureRendererToSurface();
}

void Tint_capture(SDL_Surface* frame) {
	TintTable table;
	int tinted = Tint_shown(NULL);
	if (tinted) Tint_table(shown, &table);
	for (int y = 0; y < frame->h; y++) {
		Tint_row((Uint32*)((Uint8*)frame->pixels + y * frame->pitch), frame->w, tinted ? &table : NULL);
	}
}
