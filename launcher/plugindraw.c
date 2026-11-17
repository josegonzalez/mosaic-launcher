#include <sys/stat.h>
#include "launcher.h"
#include "drawmodel.h"
#include "plugindraw.h"
#include "shrinkmodel.h"

#define COORD_MAX DRAW_COORD_MAX
#define IMAGE_FILE_MAX (16 * 1024 * 1024)

static SDL_Surface* known(const MosaicSurface* surface) {
	return surface && DrawReg_owner(surface) ? (SDL_Surface*)surface : NULL;
}

// One a plugin can draw on: not one of the art cache's.
static SDL_Surface* drawable(const MosaicSurface* surface) {
	return DrawReg_writable(surface) ? (SDL_Surface*)surface : NULL;
}

static int inRange(int v) {
	return v >= -COORD_MAX && v <= COORD_MAX;
}

static int rectOk(const MosaicRect* r) {
	return r->w >= 0 && r->h >= 0 && r->w <= COORD_MAX && r->h <= COORD_MAX && inRange(r->x) && inRange(r->y);
}

static MosaicRect whole(const SDL_Surface* s) {
	return (MosaicRect){ 0, 0, s->w, s->h };
}

static int lesser(int a, int b) { return a < b ? a : b; }
static int greater(int a, int b) { return a > b ? a : b; }

///////////////////////////////////////
// the screen

int PluginDraw_screenWidth(void) {
	return screen ? screen->w : FIXED_WIDTH;
}
int PluginDraw_screenHeight(void) {
	return screen ? screen->h : FIXED_HEIGHT;
}
int PluginDraw_screenScale(void) {
	return FIXED_SCALE;
}

///////////////////////////////////////
// surfaces

// A transparent surface for owner, recorded against its budget.
static SDL_Surface* make(const char* owner, int w, int h) {
	if (!owner || !owner[0] || w <= 0 || h <= 0 || w > DRAW_MAX_SIDE || h > DRAW_MAX_SIDE) return NULL;
	size_t bytes = (size_t)w * h * 4;
	if (!DrawReg_fits(owner, bytes)) {
		LOG_warn("plugins: refused a %ix%i surface for %s, its surfaces would take over %i MiB\n",
			w, h, owner, DRAW_PLUGIN_BYTES / (1024 * 1024));
		return NULL;
	}
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!s) return NULL;
	SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
	if (DrawReg_add(s, owner, bytes) != 0) {
		SDL_FreeSurface(s);
		return NULL;
	}
	return s;
}

MosaicSurface* PluginDraw_surfaceNew(const char* owner, int w, int h) {
	return (MosaicSurface*)make(owner, w, h);
}

// The whole of a regular file, or NULL. Opened without blocking, so a FIFO
// or device can't stall the main thread.
void* PluginDraw_readImageFile(const char* path, size_t* size) {
	int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) return NULL;
	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > IMAGE_FILE_MAX) {
		close(fd);
		return NULL;
	}
	char* data = malloc(st.st_size);
	size_t got = 0;
	while (data && got < (size_t)st.st_size) {
		ssize_t n = read(fd, data + got, st.st_size - got);
		if (n <= 0) break;
		got += n;
	}
	close(fd);
	if (!data || got != (size_t)st.st_size) {
		free(data);
		return NULL;
	}
	*size = got;
	return data;
}

// Why Shrink_headerCheck's answer refuses an image, or NULL.
static const char* refusal(int rc, int w, int h, char* why, size_t size) {
	if (rc == SHRINK_UNKNOWN || (rc == 0 && Shrink_allowed(w, h))) return NULL;
	if (rc == 0) snprintf(why, size, "%ix%i is over %i a side or %i pixels", w, h, SHRINK_MAX_SIDE, SHRINK_MAX_PIXELS);
	else snprintf(why, size, "its PNG or JPEG header can't be read");
	return why;
}

const char* PluginDraw_imageRefusal(const void* data, size_t size, char* why, size_t why_size) {
	int w = 0, h = 0;
	return refusal(Shrink_headerCheck(data, size, &w, &h), w, h, why, why_size);
}

static size_t readRW(void* rw, void* buf, size_t n) {
	return SDL_RWread(rw, buf, 1, n);
}

SDL_Surface* PluginDraw_loadImage(const char* who, const char* path) {
	SDL_RWops* rw = SDL_RWFromFile(path, "rb");
	if (!rw) return NULL;
	int w = 0, h = 0;
	char why[96];
	const char* no = refusal(Shrink_readCheck(readRW, rw, &w, &h), w, h, why, sizeof(why));
	if (no || SDL_RWseek(rw, 0, RW_SEEK_SET) != 0) {
		if (no) LOG_warn("%s: can't load %s: %s\n", who, path, no);
		SDL_RWclose(rw);
		return NULL;
	}
	// as IMG_Load does: the extension names a format with no signature, such as TGA
	const char* ext = strrchr(path, '.');
	return IMG_LoadTyped_RW(rw, 1, ext ? ext + 1 : NULL);
}

MosaicSurface* PluginDraw_imageLoad(const char* owner, const char* path) {
	if (!owner || !owner[0] || !path) return NULL;
	size_t size = 0;
	void* data = PluginDraw_readImageFile(path, &size);
	if (!data) {
		LOG_warn("plugins: %s can't load %s: not a readable file up to %i MiB\n", owner, path, IMAGE_FILE_MAX / (1024 * 1024));
		return NULL;
	}
	// the size from its header, before it's decoded
	char why[96];
	const char* no = PluginDraw_imageRefusal(data, size, why, sizeof(why));
	if (no) {
		LOG_warn("plugins: %s can't load %s: %s\n", owner, path, no);
		free(data);
		return NULL;
	}
	SDL_Surface* decoded = IMG_Load_RW(SDL_RWFromConstMem(data, (int)size), 1);
	free(data);
	if (!decoded) {
		LOG_warn("plugins: %s can't load %s: %s\n", owner, path, IMG_GetError());
		return NULL;
	}
	SDL_Surface* image = NULL;
	if (decoded->w <= DRAW_MAX_SIDE && decoded->h <= DRAW_MAX_SIDE) {
		image = make(owner, decoded->w, decoded->h);
		if (image) {
			SDL_SetSurfaceBlendMode(decoded, SDL_BLENDMODE_NONE);
			SDL_BlitSurface(decoded, NULL, image, NULL);
		}
	}
	else {
		LOG_warn("plugins: %s can't load %s: %ix%i is over %i a side\n", owner, path, decoded->w, decoded->h, DRAW_MAX_SIDE);
	}
	SDL_FreeSurface(decoded);
	return (MosaicSurface*)image;
}

// Shrinks decoded into image a row at a time, each row converted to
// ARGB8888 as it's fed in.
static int shrinkInto(SDL_Surface* decoded, SDL_Surface* image) {
	SDL_Surface* src = decoded;
	SDL_Surface* converted = NULL;
	SDL_Palette* palette = decoded->format->palette;
	Uint32 colors[256];
	int paletted = decoded->format->format == SDL_PIXELFORMAT_INDEX8 && palette;
	if (paletted) {
		// SDL_ConvertPixels can't read a palette, so its colours are looked up
		for (int i = 0; i < 256; i++) {
			SDL_Color c = i < palette->ncolors ? palette->colors[i] : (SDL_Color){ 0, 0, 0, 255 };
			colors[i] = (Uint32)c.a << 24 | (Uint32)c.r << 16 | (Uint32)c.g << 8 | c.b;
		}
		Uint32 key;
		if (SDL_GetColorKey(decoded, &key) == 0 && key < 256) colors[key] &= 0x00FFFFFFu;
	}
	else if (SDL_ISPIXELFORMAT_INDEXED(decoded->format->format)) {
		// other paletted formats, converted whole
		converted = SDL_ConvertSurfaceFormat(decoded, SDL_PIXELFORMAT_ARGB8888, 0);
		if (!converted) return -1;
		src = converted;
	}
	Shrink* shrink = Shrink_new(src->w, src->h, image->w, image->h);
	Uint32* row = malloc(sizeof(Uint32) * src->w);
	int rc = shrink && row ? 0 : -1;
	if (rc == 0 && SDL_MUSTLOCK(src)) rc = SDL_LockSurface(src);
	for (int y = 0; rc == 0 && y < src->h; y++) {
		const Uint8* from = (const Uint8*)src->pixels + (size_t)y * src->pitch;
		if (paletted) {
			for (int x = 0; x < src->w; x++) row[x] = colors[from[x]];
		}
		else if (SDL_ConvertPixels(src->w, 1, src->format->format, from, src->pitch, SDL_PIXELFORMAT_ARGB8888, row,
				src->w * 4) != 0) {
			rc = -1;
			break;
		}
		rc = Shrink_row(shrink, row, image->pixels, image->pitch / 4);
	}
	if (SDL_MUSTLOCK(src)) SDL_UnlockSurface(src);
	if (rc == 0 && !Shrink_done(shrink)) rc = -1;
	free(row);
	Shrink_free(shrink);
	if (converted) SDL_FreeSurface(converted);
	return rc;
}

MosaicSurface* PluginDraw_imageLoadFit(const char* owner, const char* path, int max_w, int max_h) {
	if (!owner || !owner[0] || !path || max_w < 1 || max_h < 1 || max_w > DRAW_MAX_SIDE || max_h > DRAW_MAX_SIDE) return NULL;
	size_t size = 0;
	unsigned char* data = PluginDraw_readImageFile(path, &size);
	if (!data) {
		LOG_warn("plugins: %s can't load %s: not a readable file up to %i MiB\n", owner, path, IMAGE_FILE_MAX / (1024 * 1024));
		return NULL;
	}
	// the size from its header, before anything is decoded
	int sw = 0, sh = 0, dw = 0, dh = 0;
	if (Shrink_headerSize(data, size, &sw, &sh) != 0) {
		LOG_warn("plugins: %s can't load %s: not a PNG or JPEG\n", owner, path);
		free(data);
		return NULL;
	}
	if (!Shrink_allowed(sw, sh)) {
		LOG_warn("plugins: %s can't load %s: %ix%i is over %i a side or %i pixels\n", owner, path, sw, sh,
			SHRINK_MAX_SIDE, SHRINK_MAX_PIXELS);
		free(data);
		return NULL;
	}
	Shrink_fit(sw, sh, max_w, max_h, &dw, &dh);
	SDL_Surface* image = make(owner, dw, dh);
	if (!image) {
		free(data);
		return NULL;
	}
	SDL_Surface* decoded = IMG_Load_RW(SDL_RWFromConstMem(data, (int)size), 1);
	free(data);
	int rc = -1;
	if (!decoded) LOG_warn("plugins: %s can't load %s: %s\n", owner, path, IMG_GetError());
	else if (decoded->w != sw || decoded->h != sh) LOG_warn("plugins: %s can't load %s: it isn't the size its header says\n", owner, path);
	else if ((rc = shrinkInto(decoded, image)) != 0) LOG_warn("plugins: %s can't load %s: couldn't shrink it\n", owner, path);
	if (decoded) SDL_FreeSurface(decoded);
	if (rc != 0) {
		PluginDraw_surfaceFree(owner, (MosaicSurface*)image);
		return NULL;
	}
	return (MosaicSurface*)image;
}

MosaicSurface* PluginDraw_surfaceScale(const char* owner, const MosaicSurface* src, int w, int h, int fit) {
	SDL_Surface* s = known(src);
	MosaicRect from, to;
	if (!s || Draw_fit(s->w, s->h, w, h, fit, &from, &to) != 0) return NULL;
	SDL_Surface* scaled = make(owner, w, h);
	if (!scaled) return NULL;
	// a copy of its pixels, alpha and all, rather than blended onto nothing
	SDL_BlendMode mode;
	SDL_GetSurfaceBlendMode(s, &mode);
	SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_NONE);
	SDL_BlitScaled(s, &(SDL_Rect){ from.x, from.y, from.w, from.h }, scaled, &(SDL_Rect){ to.x, to.y, to.w, to.h });
	SDL_SetSurfaceBlendMode(s, mode);
	return (MosaicSurface*)scaled;
}

int PluginDraw_surfaceFree(const char* owner, MosaicSurface* surface) {
	if (!owner || !owner[0] || DrawReg_remove(surface, owner) != 0) return -1;
	SDL_FreeSurface((SDL_Surface*)surface);
	return 0;
}

int PluginDraw_freeAll(const char* owner) {
	if (!owner || !owner[0]) return 0;
	void* batch[64];
	int total = 0, n;
	while ((n = DrawReg_take(owner, batch, 64)) > 0) {
		for (int i = 0; i < n; i++) SDL_FreeSurface(batch[i]);
		total += n;
	}
	return total;
}

int PluginDraw_surfaceSize(const MosaicSurface* surface, int* w, int* h) {
	SDL_Surface* s = known(surface);
	if (!s || !w || !h) return -1;
	*w = s->w;
	*h = s->h;
	return 0;
}

int PluginDraw_readPixel(const MosaicSurface* surface, int x, int y, MosaicColor* out) {
	SDL_Surface* s = known(surface);
	if (!s || !out || x < 0 || y < 0 || x >= s->w || y >= s->h) return -1;
	int bpp = s->format->BytesPerPixel;
	if (bpp != 1 && bpp != 2 && bpp != 4) return -1;
	if (SDL_MUSTLOCK(s) && SDL_LockSurface(s) != 0) return -1;
	const Uint8* p = (const Uint8*)s->pixels + y * s->pitch + x * bpp;
	Uint32 pixel = bpp == 4 ? *(const Uint32*)p : bpp == 2 ? *(const Uint16*)p : *p;
	if (SDL_MUSTLOCK(s)) SDL_UnlockSurface(s);
	SDL_GetRGBA(pixel, s->format, &out->r, &out->g, &out->b, &out->a);
	return 0;
}

// RGBA8888 pixels into a surface owner made. SDL converts them into the
// surface's format (ARGB8888 for every plugin surface), and only copies
// rows when that's RGBA32 already; both run near memcpy's speed.
int PluginDraw_surfaceWrite(const char* owner, MosaicSurface* surface, const MosaicRect* rect, const void* pixels, int pitch) {
	if (!owner || !owner[0] || !pixels || !DrawReg_mine(surface, owner)) return -1;
	SDL_Surface* s = (SDL_Surface*)surface;
	MosaicRect area;
	size_t offset;
	int found = Draw_writeArea(s->w, s->h, rect, pitch, &area, &offset);
	if (found <= 0) return found;
	if (SDL_MUSTLOCK(s) && SDL_LockSurface(s) != 0) return -1;
	const Uint8* from = (const Uint8*)pixels + offset;
	int bpp = s->format->BytesPerPixel;
	Uint8* to = (Uint8*)s->pixels + (size_t)area.y * s->pitch + (size_t)area.x * bpp;
	int result = SDL_ConvertPixels(area.w, area.h, SDL_PIXELFORMAT_RGBA32, from, pitch, s->format->format, to, s->pitch);
	if (SDL_MUSTLOCK(s)) SDL_UnlockSurface(s);
	return result == 0 ? 0 : -1;
}

///////////////////////////////////////
// shapes and images

static int fill(MosaicSurface* target, const MosaicRect* rect, int radius, MosaicColor color) {
	SDL_Surface* dst = drawable(target);
	if (!dst) return -1;
	MosaicRect r = rect ? *rect : whole(dst);
	if (!rectOk(&r)) return -1;
	radius = Draw_clampRadius(radius, r.w, r.h);
	if (radius < 0) return -1;

	SDL_Rect area = { r.x, r.y, r.w, r.h }, clip, box;
	SDL_GetClipRect(dst, &clip);
	if (color.a == 0 || !SDL_IntersectRect(&area, &clip, &box)) return 0;

	// opaque straight onto dst, or else onto a layer blended on afterwards
	SDL_Surface* layer = dst;
	int ox = 0, oy = 0;
	if (color.a < 255) {
		layer = SDL_CreateRGBSurfaceWithFormat(0, box.w, box.h, 32, SDL_PIXELFORMAT_ARGB8888);
		if (!layer) return -1;
		ox = box.x;
		oy = box.y;
	}
	Uint32 pixel = SDL_MapRGBA(layer->format, color.r, color.g, color.b, color.a);
	int end = box.y + box.h;
	for (int y = box.y; y < end;) {
		int inset = Draw_roundInset(radius, r.h, y - r.y);
		// the rows between the corners in one go
		int rows = 1;
		if (radius == 0) rows = end - y;
		else if (inset == 0) {
			while (y + rows < end && Draw_roundInset(radius, r.h, y + rows - r.y) == 0) rows++;
		}
		int x0 = greater(r.x + inset, box.x);
		int x1 = lesser(r.x + r.w - inset, box.x + box.w);
		if (x1 > x0) SDL_FillRect(layer, &(SDL_Rect){ x0 - ox, y - oy, x1 - x0, rows }, pixel);
		y += rows;
	}
	if (layer != dst) {
		SDL_SetSurfaceBlendMode(layer, SDL_BLENDMODE_BLEND);
		SDL_BlitSurface(layer, NULL, dst, &box);
		SDL_FreeSurface(layer);
	}
	return 0;
}

int PluginDraw_fillRect(MosaicSurface* target, const MosaicRect* rect, MosaicColor color) {
	return fill(target, rect, 0, color);
}

int PluginDraw_fillRoundRect(MosaicSurface* target, const MosaicRect* rect, int radius, MosaicColor color) {
	return fill(target, rect, radius, color);
}

int PluginDraw_blit(MosaicSurface* target, const MosaicSurface* src, const MosaicRect* src_rect, const MosaicRect* dst_rect) {
	SDL_Surface* dst = drawable(target);
	SDL_Surface* s = known(src);
	if (!dst || !s || dst == s) return -1;
	MosaicRect from = src_rect ? *src_rect : whole(s);
	if (!rectOk(&from)) return -1;
	MosaicRect to = dst_rect ? *dst_rect : (MosaicRect){ 0, 0, from.w, from.h };
	if (!rectOk(&to)) return -1;
	if (from.w == 0 || from.h == 0 || to.w == 0 || to.h == 0) return 0;
	SDL_Rect sr = { from.x, from.y, from.w, from.h };
	SDL_Rect dr = { to.x, to.y, to.w, to.h };
	int result = from.w == to.w && from.h == to.h ? SDL_BlitSurface(s, &sr, dst, &dr) : SDL_BlitScaled(s, &sr, dst, &dr);
	return result == 0 ? 0 : -1;
}

int PluginDraw_blitAlpha(MosaicSurface* target, const MosaicSurface* src, const MosaicRect* src_rect, const MosaicRect* dst_rect, int alpha) {
	SDL_Surface* s = known(src);
	if (!s || alpha < 0 || alpha > 255) return -1;
	if (alpha == 255) return PluginDraw_blit(target, src, src_rect, dst_rect);
	// blended by alpha even from an opaque source, such as a captured frame
	SDL_BlendMode mode;
	Uint8 mod;
	SDL_GetSurfaceBlendMode(s, &mode);
	SDL_GetSurfaceAlphaMod(s, &mod);
	SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
	SDL_SetSurfaceAlphaMod(s, (Uint8)(mod * alpha / 255));
	int result = PluginDraw_blit(target, src, src_rect, dst_rect);
	SDL_SetSurfaceAlphaMod(s, mod);
	SDL_SetSurfaceBlendMode(s, mode);
	return result;
}

///////////////////////////////////////
// text

static TTF_Font* fontFor(int which) {
	switch (which) {
		case MOSAIC_FONT_LARGE: return font.large;
		case MOSAIC_FONT_MEDIUM: return font.medium;
		case MOSAIC_FONT_SMALL: return font.small;
		case MOSAIC_FONT_TINY: return font.tiny;
		case MOSAIC_FONT_MICRO: return font.micro;
		default: return NULL;
	}
}

// the animation worker renders text too
static void lockFonts(void) {
	if (fontMutex) SDL_LockMutex(fontMutex);
}
static void unlockFonts(void) {
	if (fontMutex) SDL_UnlockMutex(fontMutex);
}

static int measure(const char* text, void* data) {
	int w = 0;
	if (!text[0]) return 0;
	if (TTF_SizeUTF8((TTF_Font*)data, text, &w, NULL) != 0) return -1;
	return w;
}

// text's first line, fitted to max_width (0 for any), into a new string.
static char* fitLine(TTF_Font* f, const char* text, int max_width, int* width) {
	size_t size = strcspn(text, "\n") + 4; // room for "..."
	char* line = malloc(size);
	if (!line) return NULL;
	lockFonts();
	*width = Draw_truncate(text, max_width > 0 ? max_width : INT_MAX, measure, f, line, size);
	unlockFonts();
	if (*width < 0) {
		free(line);
		return NULL;
	}
	return line;
}

int PluginDraw_textWidth(int which, const char* text) {
	TTF_Font* f = fontFor(which);
	if (!f || !text) return -1;
	int width;
	char* line = fitLine(f, text, 0, &width);
	if (!line) return -1;
	free(line);
	return width;
}

int PluginDraw_textHeight(int which) {
	TTF_Font* f = fontFor(which);
	return f ? TTF_FontHeight(f) : -1;
}

int PluginDraw_textTruncate(int which, const char* text, int max_width, char* out, size_t size) {
	TTF_Font* f = fontFor(which);
	if (!f || !text || !out || size == 0 || max_width < 0) return -1;
	lockFonts();
	int width = Draw_truncate(text, max_width, measure, f, out, size);
	unlockFonts();
	return width;
}

int PluginDraw_drawText(MosaicSurface* target, int which, const char* text, int x, int y, MosaicColor color, int max_width) {
	SDL_Surface* dst = drawable(target);
	TTF_Font* f = fontFor(which);
	if (!dst || !f || !text || max_width < 0 || !inRange(x) || !inRange(y)) return -1;
	int width;
	char* line = fitLine(f, text, max_width, &width);
	if (!line) return -1;
	if (!line[0] || color.a == 0) {
		free(line);
		return width;
	}
	lockFonts();
	SDL_Surface* drawn = TTF_RenderUTF8_Blended(f, line, (SDL_Color){ color.r, color.g, color.b, 255 });
	unlockFonts();
	free(line);
	if (!drawn) return -1;
	SDL_SetSurfaceBlendMode(drawn, SDL_BLENDMODE_BLEND);
	SDL_SetSurfaceAlphaMod(drawn, color.a);
	SDL_BlitSurface(drawn, NULL, dst, &(SDL_Rect){ x, y, 0, 0 });
	SDL_FreeSurface(drawn);
	return width;
}

int PluginDraw_button(MosaicSurface* target, const char* glyph, int x, int y, MosaicRect* drawn) {
	SDL_Surface* dst = NULL;
	if (target && !(dst = drawable(target))) return -1;
	if (!glyph || !inRange(x) || !inRange(y)) return -1;
	// stock's own hint drawing, with no label, so the style, sizes and
	// colours are its hints'
	lockFonts();
	int w = GFX_getButtonWidth("", (char*)glyph);
	SDL_Rect at = { x, y, 0, 0 };
	if (dst) GFX_blitButton("", (char*)glyph, dst, &at);
	unlockFonts();
	if (drawn) *drawn = (MosaicRect){ x, y, w, SCALE1(BUTTON_SIZE) };
	return 0;
}

///////////////////////////////////////
// lending to render hooks

MosaicSurface* PluginDraw_lend(SDL_Surface* surface, const SDL_Rect* clip) {
	if (!surface || DrawReg_add(surface, DRAW_HOST, 0) != 0) return NULL;
	SDL_SetClipRect(surface, clip);
	return (MosaicSurface*)surface;
}

const MosaicSurface* PluginDraw_lendReadOnly(SDL_Surface* surface) {
	if (!surface || DrawReg_add(surface, DRAW_HOST, 0) != 0) return NULL;
	DrawReg_lock(surface);
	return (const MosaicSurface*)surface;
}

void PluginDraw_unlend(MosaicSurface* lent) {
	if (DrawReg_remove(lent, DRAW_HOST) == 0) SDL_SetClipRect((SDL_Surface*)lent, NULL);
}

void PluginDraw_clip(MosaicSurface* lent, const SDL_Rect* clip) {
	const char* owner = DrawReg_owner(lent);
	if (owner && !strcmp(owner, DRAW_HOST) && DrawReg_writable(lent)) SDL_SetClipRect((SDL_Surface*)lent, clip);
}
