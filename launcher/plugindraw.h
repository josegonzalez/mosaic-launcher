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
int PluginDraw_fillRect(MosaicSurface* target, const MosaicRect* rect, MosaicColor color);
int PluginDraw_fillRoundRect(MosaicSurface* target, const MosaicRect* rect, int radius, MosaicColor color);
int PluginDraw_blit(MosaicSurface* target, const MosaicSurface* src, const MosaicRect* src_rect, const MosaicRect* dst_rect);

int PluginDraw_textWidth(int font, const char* text);
int PluginDraw_textHeight(int font);
int PluginDraw_textTruncate(int font, const char* text, int max_width, char* out, size_t size);
int PluginDraw_drawText(MosaicSurface* target, int font, const char* text, int x, int y, MosaicColor color, int max_width);

// For render hooks: lends surface (such as the screen) to plugins, who can
// draw on it only inside clip (NULL for all of it), until
// PluginDraw_unlend. Plugins can't free it. Returns NULL if it's already
// lent.
MosaicSurface* PluginDraw_lend(SDL_Surface* surface, const SDL_Rect* clip);
void PluginDraw_unlend(MosaicSurface* lent);

#endif
