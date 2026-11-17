#ifndef PLUGINDRAW_H
#define PLUGINDRAW_H

// The drawing calls in mosaic_plugin.h, over SDL and stock's fonts. A
// MosaicSurface is an SDL_Surface the launcher has recorded in drawmodel.c:
// one a plugin made, which it owns, or one lent to a render hook. Every
// call refuses a surface that isn't recorded, so a stale or made-up pointer
// is never drawn on. Plugins call these on the main thread; text locks
// fontMutex, as the launcher's own text does.

#include "api.h"
#include "mosaic_plugin.h"

int PluginDraw_screenWidth(void);
int PluginDraw_screenHeight(void);
int PluginDraw_screenScale(void);

// Surfaces owned by owner, a plugin id the loader has checked.
MosaicSurface* PluginDraw_surfaceNew(const char* owner, int w, int h);
MosaicSurface* PluginDraw_imageLoad(const char* owner, const char* path);
MosaicSurface* PluginDraw_imageLoadFit(const char* owner, const char* path, int max_w, int max_h);
MosaicSurface* PluginDraw_surfaceScale(const char* owner, const MosaicSurface* src, int w, int h, int fit);
int PluginDraw_surfaceFree(const char* owner, MosaicSurface* surface);

// Frees every surface owner still has, after its quit or when its init
// declines. Returns how many.
int PluginDraw_freeAll(const char* owner);

int PluginDraw_surfaceSize(const MosaicSurface* surface, int* w, int* h);
int PluginDraw_readPixel(const MosaicSurface* surface, int x, int y, MosaicColor* out);
int PluginDraw_surfaceWrite(const char* owner, MosaicSurface* surface, const MosaicRect* rect, const void* pixels, int pitch);
int PluginDraw_fillRect(MosaicSurface* target, const MosaicRect* rect, MosaicColor color);
int PluginDraw_fillRoundRect(MosaicSurface* target, const MosaicRect* rect, int radius, MosaicColor color);
int PluginDraw_blit(MosaicSurface* target, const MosaicSurface* src, const MosaicRect* src_rect, const MosaicRect* dst_rect);
int PluginDraw_blitAlpha(MosaicSurface* target, const MosaicSurface* src, const MosaicRect* src_rect, const MosaicRect* dst_rect, int alpha);

int PluginDraw_textWidth(int font, const char* text);
int PluginDraw_textHeight(int font);
int PluginDraw_textTruncate(int font, const char* text, int max_width, char* out, size_t size);
int PluginDraw_drawText(MosaicSurface* target, int font, const char* text, int x, int y, MosaicColor color, int max_width);

// host->draw_button for a button stock names glyph (see Hint_glyph), drawn
// by stock's hint code without a label; target NULL only measures.
int PluginDraw_button(MosaicSurface* target, const char* glyph, int x, int y, MosaicRect* drawn);

// The whole of a regular file of up to 16 MiB, to free, or NULL. Opened
// without blocking, so a FIFO or device can't stall the caller; safe off
// the main thread.
void* PluginDraw_readImageFile(const char* path, size_t* size);

// Images are checked by their header before they're decoded (see
// shrinkmodel.h), so a small file can't decode to more than the launcher
// allows: a PNG or JPEG over SHRINK_MAX_SIDE a side or SHRINK_MAX_PIXELS,
// or one whose header can't be read, is refused, and other formats decode
// as before. PluginDraw_imageRefusal checks data, the whole file: NULL
// when it can be decoded, or why not, written in why.
const char* PluginDraw_imageRefusal(const void* data, size_t size, char* why, size_t why_size);

// IMG_Load(path) after checking the file's header, for images the launcher
// didn't make. NULL, as IMG_Load returns, for a file it can't open or
// decode, logging one it refuses as "<who>: can't load <path>: <why>".
// Safe off the main thread.
SDL_Surface* PluginDraw_loadImage(const char* who, const char* path);

// For render hooks: lends surface (such as the screen) to plugins, who can
// draw on it only inside clip (NULL for all of it), until
// PluginDraw_unlend. Plugins can't free it. Returns NULL if it's already
// lent.
MosaicSurface* PluginDraw_lend(SDL_Surface* surface, const SDL_Rect* clip);
void PluginDraw_unlend(MosaicSurface* lent);

// Lends surface for plugins to draw and read but not draw on, such as a
// transition's frames, until PluginDraw_unlend. Returns NULL if it's
// already lent.
const MosaicSurface* PluginDraw_lendReadOnly(SDL_Surface* surface);

// Changes what a lent surface can be drawn on inside, such as one cell of
// a layout for its row decorators.
void PluginDraw_clip(MosaicSurface* lent, const SDL_Rect* clip);

#endif
