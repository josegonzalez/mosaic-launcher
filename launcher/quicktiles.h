#ifndef QUICKTILES_H
#define QUICKTILES_H

// Plugins' quick menu tiles, and the tiles plugins hide. A plugin tile is an
// ENTRY_DIP entry with an "action:<id>" path, named by its action's label;
// activating it runs the action.

#include "containers.h"
#include "actions.h"
#include "library.h"

enum QuickRow {
	QUICK_ROW_TOP,    // state.quick: Recents, Collections, Games, Tools
	QUICK_ROW_BOTTOM, // state.quickActions: pak shortcuts and toggles
};

typedef struct QuickTile {
	const Action* action;
	int row;
	char* icon;     // a PNG, or NULL for none
	char* icon_off; // drawn while isOn returns 0; NULL keeps icon
	int (*isOn)(void* data); // NULL for tiles without an on/off state
} QuickTile;

// Both return 0, or -1 for an unknown action or a bad argument. Adding a
// tile for an action that already has one replaces it.
int QuickTiles_add(const char* action_id, int row, const char* icon, const char* icon_off, int (*isOn)(void* data));
// Hides a built-in tile by name, or a plugin tile by its action id.
int QuickTiles_hide(const char* name, int hidden);

// Relabels a built-in top row tile (Recents, Collections, Games, Tools), or
// restores it with NULL or "". Returns 0, or -1 for any other name.
int QuickTiles_rename(const char* name, const char* label);

// The label to draw for a tile: a plugin's new label, or its name.
const char* QuickTiles_label(const Entry* entry);

// True when plugins have added, hidden or renamed any tile.
int QuickTiles_any(void);

// The plugin tile an entry stands for, or NULL for a built-in one.
const QuickTile* QuickTiles_forEntry(const Entry* entry);
int QuickTile_isOff(const QuickTile* tile);

// Drops hidden tiles from a row's entries (freeing them), then appends the
// row's plugin tiles whose actions are available, in the order added.
void QuickTiles_apply(Array* entries, int row);

// Forgets every tile and hidden name.
void QuickTiles_reset(void);

#endif
