#ifndef STOCKTOGGLES_H
#define STOCKTOGGLES_H

// The stock NextUI settings plugins may read and change, by the names in
// mosaic_plugin.h (MOSAIC_TOGGLE_*). Everything goes through CFG_get* and
// CFG_set*, so minuisettings.txt stays as stock writes it and the launcher
// sees a change at once.

// 1 when the toggle is on, 0 when it's off, -1 for a name off the list.
int StockToggles_get(const char* name);

// Turns a toggle on (1) or off (0), saving minuisettings.txt when it
// changes. Returns 0, or -1 for a name off the list or another value.
int StockToggles_set(const char* name, int on);

#endif
