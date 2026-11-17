#ifndef GUEST_MODEL_H
#define GUEST_MODEL_H

// The guest profile plugin's logic, without SDL or the host: its button
// setting and the header mark's size.

#define GP_PROFILE "guest" // the profile's name, as plugin.txt's profile.guest provides it

// The game list button the button setting names (MOSAIC_BUTTON_L2, R2 or
// SELECT), or -1 for none or anything else.
int GP_button(const char* name);

// 1 when the live profile's name is the guest's.
int GP_isGuest(const char* live);

// The header mark's width: the text's width with pad either side, or 0
// when that's wider than max_width, so a mark is never shown cut short.
int GP_markWidth(int text_width, int pad, int max_width);

#endif
