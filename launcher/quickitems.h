#ifndef QUICKITEMS_H
#define QUICKITEMS_H

// The quick menu's bottom row: pak shortcuts and toggles, in display order.
// Everything that used to compare item names (which entries exist, their
// icons, the on/off art, and what activating one does) reads this table.

#include "containers.h"

enum QuickItemKind {
	QUICK_PAK,    // opens a Tools or Emus pak of this name, if it's installed
	QUICK_TOGGLE, // acts in place (ENTRY_DIP)
};

typedef struct QuickItem {
	const char* name;     // entry name; also the key for its quick_<name>.png art
	int kind;
	int (*supported)(void); // QUICK_TOGGLE only; NULL means always shown
	int (*isOn)(void);      // NULL for items without an on/off state
	int asset;              // icon, or the "on" icon when isOn is set
	int asset_off;          // icon when isOn returns 0
	void (*activate)(void); // QUICK_TOGGLE only
} QuickItem;

const QuickItem* QuickItem_find(const char* name);

// The bottom row's entries, in order. Replaces getQuickToggles().
Array* QuickItem_entries(void);

// Icon to draw for an item; ASSET_WIFI_OFF for names not in the table,
// as stock did.
int QuickItem_asset(const char* name);

// True when the item has an on/off state and is off, for picking
// quick_<name>_off.png.
int QuickItem_isOff(const char* name);

// Runs a toggle's action. Does nothing for paks or unknown names.
void QuickItem_activate(const char* name);

#endif
