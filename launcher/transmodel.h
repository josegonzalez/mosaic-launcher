#ifndef TRANSMODEL_H
#define TRANSMODEL_H

// The parts of plugins' transition styles that don't need SDL or the
// loader: which slot's owner draws each kind, checking what
// transition_begin chose, the timing caps, easing, and stepping through the
// frames by the clock. plugins.c calls the owner; transitions.c draws.

#include "mosaic_plugin.h"

#define TRANSITION_MAX_MS 1000 // the longest a plugin's transition runs
#define TRANSITION_FRAME_MS 16 // about 60 frames a second
// past this many frames the next is the last, even if the clock stalls
#define TRANSITION_MAX_FRAMES (TRANSITION_MAX_MS / TRANSITION_FRAME_MS * 2)

// Who draws transitions: the transition slot's owner for pushes, pops and
// screen changes, and the launch slot's owner for launches, each a loaded
// plugin's index or -1. An owner that fails is off until the launcher
// restarts, for every kind it owns.
typedef struct TransitionSlots {
	int transition;
	int launch;
	int transition_off;
	int launch_off;
} TransitionSlots;

void TransitionSlots_clear(TransitionSlots* slots);

// The owner asked about kind (an enum MosaicTransitionKind), or -1 for none,
// one that's off, or an unknown kind.
int TransitionSlots_owner(const TransitionSlots* slots, int kind);

// The owner asked about kind with the stock Menu transitions setting at
// menu_transitions: as TransitionSlots_owner, but none for pushes, pops and
// screen changes while it's off, since off means no menu transitions at all.
int TransitionSlots_asked(const TransitionSlots* slots, int kind, int menu_transitions);

// Turns owner off for every kind it owns.
void TransitionSlots_fail(TransitionSlots* slots, int owner);

// What transition_begin returned, as an enum MosaicTransitionStyle, or -1
// for an error: a value that isn't one, or MOSAIC_STYLE_CUSTOM without a
// transition_frame.
int Transition_checkStyle(int rc, int has_frame);

// A duration_ms cut to 0 to max.
int Transition_capDuration(int ms, int max);

// t (cut to 0 to 1) along an enum MosaicEase; an unknown one is linear.
float Transition_ease(int ease, float t);

// How far through a duration ms long transition elapsed ms is, from 0 to 1.
// 1 when duration is 0 or less.
float Transition_progress(unsigned long elapsed, int duration);

// Stepping through a transition's frames.
typedef struct TransitionRun {
	unsigned long start;
	int duration;
	int frames; // drawn so far
	int done;   // the last, at progress 1, has been handed out
} TransitionRun;

void TransitionRun_start(TransitionRun* run, unsigned long now, int duration);

// The next frame's linear progress at now, in *progress. Returns 1 while
// there's a frame to draw: frames follow the clock until duration has
// passed, and the last is exactly 1, or 0 once that one's been handed out.
// After TRANSITION_MAX_FRAMES the next is the last.
int TransitionRun_next(TransitionRun* run, unsigned long now, float* progress);

#endif
