#include <stdlib.h>
#include <string.h>
#include "defines.h"
#include "api.h"
#include "fallback.h"
#include "fallbackmodel.h"
#include "imgloader.h"
#include "thememodel.h"

// SDL_ttf's own calls, not fallbackttf.h's names for this file's. SDL_ttf.h
// was read with those names in place (uisizes.h includes it ahead of
// everything), so these are declared again here, as every SDL_ttf 2 has them.
#undef TTF_SizeUTF8
#undef TTF_RenderUTF8_Blended
#undef TTF_RenderUTF8_Blended_Wrapped
extern DECLSPEC int SDLCALL TTF_SizeUTF8(TTF_Font* font, const char* text, int* w, int* h);
extern DECLSPEC SDL_Surface* SDLCALL TTF_RenderUTF8_Blended(TTF_Font* font, const char* text, SDL_Color fg);
extern DECLSPEC SDL_Surface* SDLCALL TTF_RenderUTF8_Blended_Wrapped(TTF_Font* font, const char* text, SDL_Color fg, Uint32 wrapLength);

#define FONTS 5

// TTF_GlyphIsProvided32 came in SDL_ttf 2.0.18; before it only the Basic
// Multilingual Plane can be asked about, and the rest stays the UI font's.
#if SDL_VERSIONNUM(SDL_TTF_MAJOR_VERSION, SDL_TTF_MINOR_VERSION, SDL_TTF_PATCHLEVEL) >= SDL_VERSIONNUM(2, 0, 18)
#define HAS_GLYPH32 1
#endif

static char path[512];       // the fallback font, or "" for none
static TTF_Font* ui[FONTS];     // stock's five, while they're open
static TTF_Font* second[FONTS]; // the fallback font at the same sizes, while path is set

// What each font has a glyph for, by code point, up to the end of the
// CJK extension planes: 0 not asked yet, 1 no, 2 yes. Glyphs are the
// font file's, the same at every size. Pages never touched cost nothing.
#define COVER_MAX 0x30000
static unsigned char ui_has[COVER_MAX];
static unsigned char second_has[COVER_MAX];
static uint32_t touched; // past the highest code point either has asked

static void forget(void) {
	memset(ui_has, 0, touched);
	memset(second_has, 0, touched);
	touched = 0;
}

static int provided(TTF_Font* f, unsigned char* cache, uint32_t cp, int is_ui) {
	if (cp < COVER_MAX && cache[cp]) return cache[cp] == 2;
	int has;
#ifdef HAS_GLYPH32
	(void)is_ui;
	has = TTF_GlyphIsProvided32(f, cp) != 0;
#else
	if (cp > 0xFFFF) has = is_ui;
	else has = TTF_GlyphIsProvided(f, (Uint16)cp) != 0;
#endif
	if (cp < COVER_MAX) {
		cache[cp] = has ? 2 : 1;
		if (cp >= touched) touched = cp + 1;
	}
	return has;
}

// fallbackmodel.c's pick, for stock's font at *(int*)data
static int pick(uint32_t cp, void* data) {
	int i = *(int*)data;
	// control characters, and text the UI font has the glyph for
	if (cp < 0x20 || provided(ui[i], ui_has, cp, 1)) return FALLBACK_UI;
	return provided(second[i], second_has, cp, 0) ? FALLBACK_SECOND : FALLBACK_UI;
}

// Which of stock's fonts this is, while a fallback font is open, or -1.
static int slotOf(TTF_Font* f) {
	if (!f || !path[0]) return -1;
	for (int i = 0; i < FONTS; i++)
		if (ui[i] == f && second[i]) return i;
	return -1;
}

// 1 when the whole of text is one run of the UI font, which SDL_ttf then
// measures and draws itself.
static int plain(int slot, const char* text, size_t len) {
	if (slot < 0 || !len) return 1;
	int which;
	size_t n = FallbackModel_run(text, len, 0, pick, &slot, &which);
	return n == len && which == FALLBACK_UI;
}

// len bytes of s, ended: in buf when it fits, else allocated (free with
// done). NULL when there's no memory.
static char* piece(const char* s, size_t len, char* buf, size_t size) {
	char* out = len < size ? buf : malloc(len + 1);
	if (!out) return NULL;
	memcpy(out, s, len);
	out[len] = '\0';
	return out;
}
static void done(char* p, char* buf) {
	if (p != buf) free(p);
}

static int runWidth(TTF_Font* f, const char* s, size_t len) {
	char buf[256];
	char* p = piece(s, len, buf, sizeof(buf));
	if (!p) return -1;
	int w = 0;
	int ok = TTF_SizeUTF8(f, p, &w, NULL);
	done(p, buf);
	return ok == 0 ? w : -1;
}

// fallbackmodel.c's width, for stock's font at *(int*)data
static int width(int which, const char* text, size_t len, void* data) {
	int i = *(int*)data;
	return runWidth(which == FALLBACK_UI ? ui[i] : second[i], text, len);
}

// The width of text run by run, or -1.
static int widthOf(int slot, const char* text, size_t len) {
	return FallbackModel_width(text, len, pick, width, &slot);
}

int Fallback_sizeUTF8(TTF_Font* font, const char* text, int* w, int* h) {
	int slot = slotOf(font);
	size_t len = text ? strlen(text) : 0;
	if (plain(slot, text, len)) return TTF_SizeUTF8(font, text, w, h);
	int width = widthOf(slot, text, len);
	if (width < 0) return -1;
	if (w) *w = width;
	if (h) *h = TTF_FontHeight(font);
	return 0;
}

// len bytes of text, run by run, on a surface as SDL_ttf's: 32-bit ARGB,
// the UI font's height, transparent in fg's colour, each run's baseline on
// the UI font's.
static SDL_Surface* renderRuns(int slot, const char* text, size_t len, SDL_Color fg) {
	int width = widthOf(slot, text, len);
	if (width <= 0) return NULL;
	TTF_Font* f = ui[slot];
	SDL_Surface* out = SDL_CreateRGBSurface(SDL_SWSURFACE, width, TTF_FontHeight(f), 32,
		0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
	if (!out) return NULL;
	SDL_FillRect(out, NULL, SDL_MapRGBA(out->format, fg.r, fg.g, fg.b, 0));
	int x = 0;
	size_t at = 0;
	while (at < len) {
		int which;
		size_t n = FallbackModel_run(text, len, at, pick, &slot, &which);
		TTF_Font* rf = which == FALLBACK_UI ? f : second[slot];
		char buf[256];
		char* p = piece(text + at, n, buf, sizeof(buf));
		if (!p) break;
		int w = 0;
		TTF_SizeUTF8(rf, p, &w, NULL);
		SDL_Surface* run = TTF_RenderUTF8_Blended(rf, p, fg);
		done(p, buf);
		if (run) {
			// copied, not blended, so its pixels are SDL_ttf's own
			SDL_SetSurfaceBlendMode(run, SDL_BLENDMODE_NONE);
			int y = rf == f ? 0 : TTF_FontAscent(f) - TTF_FontAscent(rf);
			SDL_BlitSurface(run, NULL, out, &(SDL_Rect){ x, y, 0, 0 });
			SDL_FreeSurface(run);
		}
		x += w;
		at += n;
	}
	return out;
}

SDL_Surface* Fallback_renderUTF8(TTF_Font* font, const char* text, SDL_Color fg) {
	int slot = slotOf(font);
	size_t len = text ? strlen(text) : 0;
	if (plain(slot, text, len)) return TTF_RenderUTF8_Blended(font, text, fg);
	return renderRuns(slot, text, len, fg);
}

// fallbackmodel.c's measure, for stock's font at *(int*)data
static int measureLine(const char* text, size_t len, void* data) {
	return widthOf(*(int*)data, text, len);
}

SDL_Surface* Fallback_renderUTF8Wrapped(TTF_Font* font, const char* text, SDL_Color fg, Uint32 wrap) {
	int slot = slotOf(font);
	size_t len = text ? strlen(text) : 0;
	if (plain(slot, text, len)) return TTF_RenderUTF8_Blended_Wrapped(font, text, fg, wrap);

	// the lines, each drawn run by run, then stacked a line skip apart and
	// left aligned, as SDL_ttf's are
	int max_width = wrap > 0x7FFFFFFF ? 0 : (int)wrap;
	SDL_Surface* lines[64];
	int count = 0;
	int width = 0;
	size_t at = 0;
	while (at < len && count < (int)(sizeof(lines) / sizeof(lines[0]))) {
		size_t next;
		size_t n = FallbackModel_line(text + at, len - at, max_width, measureLine, &slot, &next);
		SDL_Surface* line = n ? renderRuns(slot, text + at, n, fg) : NULL;
		lines[count++] = line;
		if (line && line->w > width) width = line->w;
		at += next;
	}
	int skip = TTF_FontLineSkip(font);
	int height = TTF_FontHeight(font) + skip * (count - 1);
	SDL_Surface* out = width > 0 ? SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, 32,
		0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000) : NULL;
	if (out) SDL_FillRect(out, NULL, SDL_MapRGBA(out->format, fg.r, fg.g, fg.b, 0));
	for (int i = 0; i < count; i++) {
		if (!lines[i]) continue;
		if (out) {
			SDL_SetSurfaceBlendMode(lines[i], SDL_BLENDMODE_NONE);
			SDL_BlitSurface(lines[i], NULL, out, &(SDL_Rect){ 0, i * skip, 0, 0 });
		}
		SDL_FreeSurface(lines[i]);
	}
	return out;
}

int Fallback_check(const char* owner, const char* field, const char* given, char* out, size_t size) {
	out[0] = '\0';
	char copy[512];
	snprintf(copy, sizeof(copy), "%s", given);
	const char* why = NULL;
	if (ThemeModel_cardFile(SDCARD_PATH, copy, out, size, &why) != 0) {
		out[0] = '\0';
		LOG_warn("plugins: %s's %s (%s) %s, so none is used\n", owner, field, copy, why);
		return -1;
	}
	TTF_Font* test = TTF_OpenFont(out, SCALE1(FONT_LARGE));
	if (!test) {
		LOG_warn("plugins: %s's %s (%s) won't open, so none is used\n", owner, field, out);
		out[0] = '\0';
		return -1;
	}
	TTF_CloseFont(test);
	return 0;
}

static void closeSecond(void) {
	for (int i = 0; i < FONTS; i++) {
		if (second[i]) TTF_CloseFont(second[i]);
		second[i] = NULL;
	}
}

// The fallback font at stock's five sizes, in stock's style.
static void openSecond(void) {
	closeSecond();
	forget();
	if (!path[0] || !ui[0]) return;
	int sizes[FONTS] = { SCALE1(FONT_LARGE), SCALE1(FONT_MEDIUM), SCALE1(FONT_SMALL), SCALE1(FONT_TINY), SCALE1(FONT_MICRO) };
	for (int i = 0; i < FONTS; i++) {
		second[i] = TTF_OpenFont(path, sizes[i]);
		if (!second[i] || !ui[i]) {
			LOG_warn("plugins: the fallback font (%s) won't open at %d, so none is used\n", path, sizes[i]);
			closeSecond();
			path[0] = '\0';
			return;
		}
		TTF_SetFontStyle(second[i], TTF_GetFontStyle(ui[i]));
	}
}

void Fallback_fontsLoaded(void) {
	ui[0] = font.large;
	ui[1] = font.medium;
	ui[2] = font.small;
	ui[3] = font.tiny;
	ui[4] = font.micro;
	openSecond();
}

void Fallback_fontsClosed(void) {
	closeSecond();
	forget();
	memset(ui, 0, sizeof(ui));
}

void Fallback_set(const char* given, const char* owner) {
	if (!strcmp(given, path)) return;
	if (fontMutex) SDL_LockMutex(fontMutex);
	snprintf(path, sizeof(path), "%s", given);
	openSecond();
	if (fontMutex) SDL_UnlockMutex(fontMutex);
	if (path[0]) LOG_info("plugins: %s supplies the fallback font %s\n", owner, path);
	else if (given[0]) return; // openSecond logged why
	else LOG_info("plugins: no fallback font\n");
}

const char* Fallback_path(void) {
	return path;
}
