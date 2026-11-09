#include <string.h>
#include "smooth.h"
#include "scalemodel.h"

// a theme's sizes need a few dozen pieces: three pills, their middles and
// the button's at two heights, the prompts it shows, and the status bar's
// twenty or so icons
#define SMOOTH_PIECES 96

typedef struct {
	SDL_Surface* sheet;
	SDL_Rect rect;
	int w, h;
	SDL_Surface* piece;
} Piece;

static Piece pieces[SMOOTH_PIECES];
static int next_piece = 0; // the slot to reuse once they're all taken

static SDL_Surface* make(SDL_Surface* sheet, const SDL_Rect* rect, int w, int h) {
	SDL_Surface* part = SDL_CreateRGBSurfaceWithFormat(0, rect->w, rect->h, 32, SDL_PIXELFORMAT_ARGB8888);
	SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!part || !out) {
		SDL_FreeSurface(part);
		SDL_FreeSurface(out);
		return NULL;
	}

	// the sheet's own pixels, untinted
	Uint8 r, g, b, a;
	SDL_BlendMode blend;
	SDL_GetSurfaceColorMod(sheet, &r, &g, &b);
	SDL_GetSurfaceAlphaMod(sheet, &a);
	SDL_GetSurfaceBlendMode(sheet, &blend);
	SDL_SetSurfaceColorMod(sheet, 255, 255, 255);
	SDL_SetSurfaceAlphaMod(sheet, 255);
	SDL_SetSurfaceBlendMode(sheet, SDL_BLENDMODE_NONE);
	SDL_Rect from = *rect;
	int copied = SDL_BlitSurface(sheet, &from, part, NULL);
	SDL_SetSurfaceColorMod(sheet, r, g, b);
	SDL_SetSurfaceAlphaMod(sheet, a);
	SDL_SetSurfaceBlendMode(sheet, blend);

	int scaled = -1;
	if (copied == 0 && SDL_LockSurface(part) == 0) {
		if (SDL_LockSurface(out) == 0) {
			scaled = ScaleModel_smooth(part->pixels, part->w, part->h, part->pitch / 4,
				out->pixels, w, h, out->pitch / 4);
			SDL_UnlockSurface(out);
		}
		SDL_UnlockSurface(part);
	}
	SDL_FreeSurface(part);
	if (scaled != 0) {
		SDL_FreeSurface(out);
		return NULL;
	}
	SDL_SetSurfaceBlendMode(out, SDL_BLENDMODE_BLEND);
	return out;
}

SDL_Surface* Smooth_piece(SDL_Surface* sheet, const SDL_Rect* rect, int w, int h) {
	if (!sheet || !rect || w < 1 || h < 1) return NULL;
	for (int i = 0; i < SMOOTH_PIECES; i++) {
		Piece* p = &pieces[i];
		if (p->piece && p->sheet == sheet && p->w == w && p->h == h && p->rect.x == rect->x && p->rect.y == rect->y
			&& p->rect.w == rect->w && p->rect.h == rect->h)
			return p->piece;
	}
	SDL_Surface* piece = make(sheet, rect, w, h);
	if (!piece) return NULL;
	Piece* p = &pieces[next_piece];
	next_piece = (next_piece + 1) % SMOOTH_PIECES;
	SDL_FreeSurface(p->piece);
	*p = (Piece){ sheet, *rect, w, h, piece };
	return piece;
}

void Smooth_flush(void) {
	for (int i = 0; i < SMOOTH_PIECES; i++) SDL_FreeSurface(pieces[i].piece);
	memset(pieces, 0, sizeof(pieces));
	next_piece = 0;
}
