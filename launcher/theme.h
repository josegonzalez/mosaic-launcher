#ifndef LAUNCHER_THEME_H
#define LAUNCHER_THEME_H

// The theme slot over stock's look: its owner's colours, background
// colour, font, text size, UI sizes and wallpapers, kept in memory only.
// Stock's settings are never written. With no theme, everything here
// answers stock's own values. The UI_* calls uisizes.h makes are here too.

#include <stddef.h>
#include "mosaic_plugin.h"
#include "thememodel.h"

// Checks theme, puts what it can use in place and logs (as owner) each
// value it refuses. Returns how many it refused.
int Theme_set(const char* owner, const MosaicTheme* theme);

// Stock's theme back.
void Theme_clear(void);

// 1 while a theme is in place.
int Theme_active(void);

// A MosaicTheme with every field at its "stock's" value, for asking.
void Theme_blank(MosaicTheme* theme);

// For a spot (THEME_WALL_*) whose stock file is path: when path doesn't
// exist and the theme has a wallpaper there, replaces path with it and
// returns 1. Otherwise leaves path alone and returns 0.
int Theme_fillWallpaper(int spot, char* path, size_t size);

// host->ui_size: an enum MosaicUISize's size, or -1.
int Theme_uiSize(int which);

// Called by the launcher's build of api.c (patches/api.c.patch).
const char* Theme_fontPath(const char* stock);
void Theme_colors(void);
void GFX_themeChanged(void);

#endif
