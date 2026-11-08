// Launch animation, without the host: which of the selection's rects
// glides, where it is partway through the launch, how strongly the old
// frame and the pill show at each point, and the settings. No SDL and no
// file access, so it's tested on the host.

#ifndef LAUNCH_MODEL_H
#define LAUNCH_MODEL_H

#include "mosaic_plugin.h"

// progress, in thousandths of the way through the launch
#define LA_UNIT 1000
// how far through the launch the pill reaches the centre, after which it fades out
#define LA_GLIDE_END 750
// the duration setting's default, in ms
#define LA_DURATION 350

// The rect to glide: the selection pill when it's known (w over 0), or
// else the selected row or cell. Returns 1 with it in out, or 0 when
// neither is known.
int LA_pick(const MosaicRect* pill, const MosaicRect* row, MosaicRect* out);

// transition_frame's progress, from 0 to 1, in LA_UNIT steps, kept to
// 0 to LA_UNIT.
int LA_progress(float progress);

// Where from is at p (0 to LA_UNIT): from its own place at 0 to the
// middle of a screen_w by screen_h screen at LA_GLIDE_END, in proportion,
// staying the same size, and resting there after.
MosaicRect LA_glide(const MosaicRect* from, int screen_w, int screen_h, int p);

// How much of the old frame shows at p while the pill glides, from 255
// at 0 to none at LA_GLIDE_END.
int LA_dim(int p);

// How much of the pill shows at p: all of it until LA_GLIDE_END, then
// fading out to none at LA_UNIT.
int LA_pillAlpha(int p);

// How much of the old frame shows at p in the plain fade, for a launch
// whose selection isn't known: from 255 at 0 to none at LA_UNIT.
int LA_fade(int p);

// The duration setting, in ms: a whole number from 0 up, at most max
// (the launcher's cap), or LA_DURATION for anything else.
int LA_duration(const char* value, int max);

// The ease setting as an enum MosaicEase: linear, in, out or in-out, and
// out for anything else.
int LA_ease(const char* value);

#endif
