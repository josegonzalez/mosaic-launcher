#ifndef BACKGROUND_H
#define BACKGROUND_H

// The game list's background layer: stock's folder background, or what the
// background slot's owner draws over it (layoutmodel.h has the slot).

// Before the game list draws, on each frame it redraws: lends the owner a
// screen-sized surface filled with stock's background and keeps what it
// draws for the background layer. Does nothing without an owner.
void Background_render(void);

// Puts the background on LAYER_BACKGROUND, with bgMutex held: the owner's
// on the game list once it has drawn, else folderbgbmp, or a cleared
// layer without one. Returns 1, or 0 when stock's background changed
// under the owner's, which shows until the game list redraws, so the
// caller keeps folderbgchanged set until then.
int Background_layer(void);

// After the video has stopped.
void Background_quit(void);

#endif
