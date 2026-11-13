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
