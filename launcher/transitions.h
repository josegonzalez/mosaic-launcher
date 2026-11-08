#ifndef TRANSITIONS_H
#define TRANSITIONS_H

// Animated hand-offs between what was on screen and what replaces it.

#include "api.h"

// Slides between oldScreen and the freshly rendered frame, SLIDE_LEFT when
// opening a folder and SLIDE_RIGHT when closing one. Captures the new frame
// itself. The transition slot's owner can draw it, or any of these, in
// place of stock's, or turn it off.
void Transition_slide(SDL_Surface* oldScreen, int direction);

// Brings in the game switcher's save state preview, drawn at dst, according
// to which screen it came from. slide is the switcher's own left/right
// movement when going between recents.
void Transition_showPreview(SDL_Surface* bmp, SDL_Rect dst, int from, int slide);

// The same, for the full-screen placeholder shown when there's no preview.
void Transition_showPlaceholder(SDL_Surface* placeholder, int from, int slide);

// Keeps what's on screen as a launch transition's first frame, for the
// launch slot's owner. Call before the frame starts drawing.
void Transition_captureLaunch(void);

// The launch slot's owner's transition from the kept frame into black.
// Returns 1 when it drew it or turned it off, or 0 when stock's fade runs.
int Transition_launch(void);

#endif
