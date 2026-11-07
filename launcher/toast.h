#ifndef TOAST_H
#define TOAST_H

// A short message at the bottom of the screen, eg. "Added to favorites".
// One at a time; a new one replaces the last.

#define TOAST_DURATION_MS 1500

void Toast_show(const char* message);

// Call every frame. Asks for a redraw when the message should disappear.
void Toast_tick(unsigned long now);

// Draws the message, if one is showing. Call after the screen renders.
void Toast_render(void);

#endif
