// Transition styles, without the host: the settings, what transition_begin
// answers for each style and kind, and where each frame goes at a
// progress. No SDL, so it's tested on the host.
//
// Each frame of a transition is two layers, the bottom one drawn first:
// the old frame (what was on screen) and the new one (what replaces it),
// each at an offset from the screen's top left and an alpha. The styles:
//
//   slide       a push slides both frames left, the new one in from the
//               right; a pop slides them right, the new one in from the
//               left; a screen change slides the new frame (the game
//               switcher) down from the top over the old one
//   fade        the new frame fades in over the old one
//   slide_fade  a push and a screen change fade the new frame in over the
//               old one as it moves a quarter of the screen into place,
//               from the right or the top; a pop moves the old frame a
//               quarter of the screen to the right as it fades out,
//               showing the new one under it
//   stock       stock's transition
//   off         none: the new frame shows at once
//
// A pop is a push played backwards, with the frames the other way round.

#ifndef TRANSITIONS_MODEL_H
#define TRANSITIONS_MODEL_H

#include "mosaic_plugin.h"

enum TRStyle {
	TR_STYLE_STOCK,
	TR_STYLE_SLIDE,
	TR_STYLE_FADE,
	TR_STYLE_SLIDE_FADE,
	TR_STYLE_OFF,
};

// The style a setting's value names, or slide for anything else.
int TR_style(const char* value);

// How long the speed setting's value, fast, normal or slow, runs, in
// milliseconds, or normal's for anything else.
#define TR_FAST_MS 150
#define TR_NORMAL_MS 250
#define TR_SLOW_MS 500
int TR_speed(const char* value);

// The ease setting's value, linear, in, out or in_out, as an enum
// MosaicEase, or MOSAIC_EASE_OUT, as stock's slides, for anything else.
int TR_ease(const char* value);

// What transition_begin returns for a style and an enum
// MosaicTransitionKind: MOSAIC_STYLE_OFF for off, MOSAIC_STYLE_CUSTOM for
// a push, pop or screen change in a style the plugin draws, and
// MOSAIC_STYLE_STOCK for everything else, as stock, a launch or a kind it
// doesn't know.
int TR_begin(int style, int kind);

// A transition's duration_ms for a speed's ms and the launcher's longest,
// max_ms: ms, cut to max_ms when max_ms is above 0.
int TR_duration(int ms, int max_ms);

enum TRFrame {
	TR_OLD,
	TR_NEW,
};

typedef struct TRLayer {
	int frame; // an enum TRFrame
	int x, y;  // where the frame's top left goes
	int alpha; // 0 (not drawn) to 255
} TRLayer;

// The two layers, bottom then top, for a style the plugin draws and a
// push, pop or screen change, at progress (0 to 1, cut to that) on a w by
// h screen. Returns 0, or -1 for another style or kind, or a w or h below
// 1. At 0 the screen shows the old frame and at 1 the new one.
int TR_layers(int style, int kind, float progress, int w, int h, TRLayer out[2]);

#endif
