#ifndef ACTIONLIST_H
#define ACTIONLIST_H

// The SDL-free half of host->action_info: which buttons each action is on,
// from the game list's own bindings, plugins' overrides and bind_button
// ones, and the FN buttons, as MosaicActionInfo lists them.

#include "mosaic_plugin.h"

// One button an action is on, before an override takes it.
typedef struct ActionBind {
	const char* action_id;
	int by;                // enum MosaicBindBy
	int screen;            // enum MosaicScreen, or -1 for an FN button
	int button;            // enum MosaicButton
	int trigger;           // enum MosaicTrigger
	const char* plugin_id; // the plugin that bound it, or NULL
	const char* setting;   // the key of its setting it follows, or NULL
} ActionBind;

// Leaves out the screen's own and bind_button binds whose screen and
// button an override takes, whatever their trigger, as the game list
// does, keeping the order. Returns how many are left.
int ActionList_dropOverridden(ActionBind* binds, int count);

// Fills info, a whole MosaicActionInfo, for the action id: its label,
// owner (NULL for the launcher's own) and whether it takes a path, and the
// binds for it, in order, counting them all and keeping the first
// MOSAIC_ACTION_BINDINGS.
void ActionList_fill(MosaicActionInfo* info, const char* id, const char* label, const char* plugin_id,
	int takes_path, const ActionBind* binds, int count);

// Copies info into out, only as much as out->size says the plugin knows,
// keeping its size. Returns 0, or -1 for a NULL out or one too small for
// its size.
int ActionList_copy(const MosaicActionInfo* info, MosaicActionInfo* out);

#endif
