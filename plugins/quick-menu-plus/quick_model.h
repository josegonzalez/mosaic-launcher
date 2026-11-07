#ifndef QUICK_MODEL_H
#define QUICK_MODEL_H

#include <stddef.h>

// What quick menu plus reads from its settings and how it finds manage.sh.
// No SDL and no host here, so the host tests can check it on its own.

#define QMP_MAX 16
#define QMP_NAME_LEN 64

// The built-in tile a name stands for, ignoring case and spaces around it,
// eg. "pak store" is "Pak Store". With top_only, only the top row's
// Recents, Collections, Games and Tools count. NULL when it's none of them.
const char* Qmp_builtinName(const char* name, int top_only);

typedef struct QmpLabel {
	char name[QMP_NAME_LEN];  // a built-in top row tile
	char label[QMP_NAME_LEN];
} QmpLabel;

// Reads the labels setting, comma-separated name=label pairs such as
// "Recents=Played, Games=Library", into out. Names are top row tiles, in
// any case; a later pair for the same tile replaces the earlier one.
// Pairs without a known name or a label are left out and counted in
// skipped, when it isn't NULL. Returns how many are in out.
int Qmp_parseLabels(const char* setting, QmpLabel* out, int max, int* skipped);

// Reads the hide setting, comma-separated tile names, into out, without
// repeats. A built-in tile's name is written the way the launcher spells
// it; anything else, such as a plugin tile's action id, is kept as typed.
// Returns how many are in out.
int Qmp_parseHidden(const char* setting, char out[][QMP_NAME_LEN], int max);

// A stock NextUI setting with a quick menu tile: the MOSAIC_TOGGLE_* name
// the host takes, the short word the toggles setting uses, and the tile's
// label.
typedef struct QmpToggle {
	const char* name;
	const char* word;
	const char* label;
} QmpToggle;

// The toggle a word or a MOSAIC_TOGGLE_* name stands for, ignoring case and
// spaces around it, or NULL.
const QmpToggle* Qmp_toggleFor(const char* word);

// Reads the toggles setting, comma-separated words such as "save, clock",
// into out, in the order given and without repeats. Words that aren't a
// toggle are counted in skipped, when it isn't NULL. Returns how many are
// in out.
int Qmp_parseToggles(const char* setting, const QmpToggle** out, int max, int* skipped);

// The pak a launcher binary at <pak>/bin/<platform>/nextui.elf belongs
// to. Returns 1 and writes it, or 0 for a path that isn't laid out so.
int Qmp_pakFromExe(const char* exe, char* out, size_t size);

// The shell command that runs <pak>/bin/manage.sh with verb, single-quoted.
// Returns 1, or 0 when it doesn't fit.
int Qmp_manageCommand(const char* pak, const char* verb, char* out, size_t size);

#endif
