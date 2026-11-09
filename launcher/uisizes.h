#ifndef LAUNCHER_UISIZES_H
#define LAUNCHER_UISIZES_H

// Stock fixes its UI sizes at compile time in defines.h and platform.h.
// The launcher's makefile includes this header ahead of every file of
// nextui.elf (its own and the vendored ones it compiles), so the theme
// slot's owner can change them at runtime: each size macro becomes a call
// into theme.c, which answers stock's value until a theme sets one. Only
// nextui.elf is built this way; minarch, the Settings pak and the vendored
// tree itself keep stock's constants.
//
// Every macro here is before scaling, as stock's are.

#include "defines.h"

// h700's PADDING and MAIN_ROW_COUNT read it, and api.h, which declares it,
// isn't included yet
extern int hdmi_active;

// Stock's values, captured before they're redefined: each expands to
// stock's own expression (on tg5040 and h700 one that depends on the
// device or HDMI), worked out at the time of the call.
static inline int UI_stockPill(void) { return PILL_SIZE; }
static inline int UI_stockButton(void) { return BUTTON_SIZE; }
static inline int UI_stockPadding(void) { return PADDING; }
static inline int UI_stockRows(void) { return MAIN_ROW_COUNT; }

// What theme.c answers: stock's above unless the theme slot's owner set
// a size, then the theme's and what's derived from it.
int UI_pillSize(void);
int UI_buttonSize(void);
int UI_padding(void);
int UI_rowCount(void);
int UI_textStep(void); // points added to every one of stock's text sizes

#undef PILL_SIZE
#undef BUTTON_SIZE
#undef BUTTON_MARGIN
#undef PADDING
#undef MAIN_ROW_COUNT
#define PILL_SIZE (UI_pillSize())
#define BUTTON_SIZE (UI_buttonSize())
// stock's literal 5 is this, as its comment says
#define BUTTON_MARGIN ((PILL_SIZE - BUTTON_SIZE) / 2)
#define PADDING (UI_padding())
#define MAIN_ROW_COUNT (UI_rowCount())

// stock's text sizes, then each plus the theme's step
static inline int UI_stockFontLarge(void) { return FONT_LARGE; }
static inline int UI_stockFontMedium(void) { return FONT_MEDIUM; }
static inline int UI_stockFontSmall(void) { return FONT_SMALL; }
static inline int UI_stockFontTiny(void) { return FONT_TINY; }
static inline int UI_stockFontMicro(void) { return FONT_MICRO; }
#undef FONT_LARGE
#undef FONT_MEDIUM
#undef FONT_SMALL
#undef FONT_TINY
#undef FONT_MICRO
#define FONT_LARGE (UI_stockFontLarge() + UI_textStep())
#define FONT_MEDIUM (UI_stockFontMedium() + UI_textStep())
#define FONT_SMALL (UI_stockFontSmall() + UI_textStep())
#define FONT_TINY (UI_stockFontTiny() + UI_textStep())
#define FONT_MICRO (UI_stockFontMicro() + UI_textStep())

#endif
