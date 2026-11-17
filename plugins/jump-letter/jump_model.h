#ifndef JUMP_MODEL_H
#define JUMP_MODEL_H

// The SDL-free half of jump-letter: an entry's letter, the sections a list
// has in its own order, the hold that opens the strip, and which of the
// strip's sections fit on screen.

#include <stddef.h>

#define JL_KEY_LEN 64     // a section's key, with its terminator
#define JL_SECTIONS_MAX 256
#define JL_HOLD_MS 300    // past stock's 250 ms tap, which SELECT's game switcher keeps

// The section of a name by its first character: A to Z for a Latin
// letter, accents and case folded (so É is E and ß is S), or # for
// anything else, a digit, a symbol, another script or an empty name.
void JL_letter(const char* name, char* out, size_t size);

// The sections of a list, in its own order: each key once, where it's
// first seen, with that entry's index.
typedef struct JLSections {
	char keys[JL_SECTIONS_MAX][JL_KEY_LEN];
	int first[JL_SECTIONS_MAX]; // the first entry with the key
	int count;
} JLSections;

// Writes entry i's key into out, as a plugin's section_key or JL_letter
// gives it.
typedef void (*JLKeyOf)(int i, char* out, size_t size, void* data);

// The sections of a list of count entries, each entry's key from key_of.
// An entry whose key is past the first JL_SECTIONS_MAX goes in the last
// section, so every entry still has one. Where at_entry is an index in
// the list, *at_section is set to its section's index (or -1 elsewhere).
void JL_sections(JLSections* s, int count, JLKeyOf key_of, void* data, int at_entry, int* at_section);

// A step of delta from at, kept in 0 to count - 1.
int JL_step(int at, int delta, int count);

// Which of the strip's items, widths[0] to widths[count - 1] with gap
// between each, fit in avail with item at among them: the window grows
// from at a step right then a step left while the next fits, so at stays
// near its middle. Sets *first and *last (*last below *first for none).
void JL_window(const int* widths, int count, int gap, int avail, int at, int* first, int* last);

// The hold that opens the strip. A press arms it; held past JL_HOLD_MS
// it opens; letting go then jumps, and B closes it until the button is
// let go. Opened sticky (from an FN button or the quick menu, with no
// button held), A jumps and B closes it.
enum JLHoldState {
	JL_IDLE,
	JL_ARMED,     // pressed, not held long enough yet
	JL_OPEN,      // the strip shows
	JL_CANCELLED, // B closed it while the button is still held
};
typedef struct JLHold {
	int state; // enum JLHoldState
	unsigned long armed_at;
	int sticky;
} JLHold;

void JL_holdArm(JLHold* h, unsigned long now);
void JL_holdOpenSticky(JLHold* h);
// A frame with the button held or not, and whether any other button is
// held with it (a chord, such as the lock's SELECT and START, which
// doesn't open the strip). Returns 1 on the frame the strip opens.
int JL_holdFrame(JLHold* h, int held, int chord, unsigned long now);
// The button let go: returns 1 when the strip was open, so it jumps.
int JL_holdRelease(JLHold* h);
// B: closes the strip without jumping.
void JL_holdCancel(JLHold* h);

#endif
