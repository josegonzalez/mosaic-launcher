#ifndef FNBINDS_H
#define FNBINDS_H

// The SDL-free half of the FN button calls (host->fn_bind and the rest):
// plugins' bindings of FN1 to FN3, the buttons stock's Settings lets the
// user assign. They're held here, in memory, and never written to stock's
// settings. The user's own choice in Settings always wins: a plugin can
// only bind a button the user left unassigned, and loses it once the user
// assigns it. The first plugin to bind a button keeps it until it
// releases it or is unloaded.

#include <stddef.h>

#define FN_COUNT 3          // FN1, FN2 and FN3, as stock's FN_BUTTON_COUNT
#define FN_ACTION_MAX 256   // a binding's bytes, its terminator included, as stock's fnAction
#define FN_OWNER_MAX 64     // a plugin id's bytes, as LoadedPlugin's
#define FN_NAME_MAX 8       // a button's name, eg. "HOME"

typedef struct FnBinds {
	char owner[FN_COUNT][FN_OWNER_MAX];   // the plugin holding each button, or "" for none
	char action[FN_COUNT][FN_ACTION_MAX]; // and what it bound it to
} FnBinds;

// What FnBinds_bind returns.
enum FnBindResult {
	FN_BIND_OK = 0,
	FN_BIND_BAD = -1,       // a bad index, owner or action
	FN_BIND_MISSING = -2,   // the device has no such button
	FN_BIND_USER = -3,      // the user assigned it in Settings
	FN_BIND_TAKEN = -4,     // another plugin bound it first
};

void FnBinds_clear(FnBinds* binds);

// 1 when action is one a button can run: "action:<id>" for a registered
// action, or "pak:<folder>" for a Tools pak, as stock's Settings writes
// it, with something after the prefix and short enough to keep.
int FnBinds_validAction(const char* action);

// Binds button index for owner, when the device has it (has_button) and
// the user's own binding, user_action, is unassigned ("" or NULL). The
// owner can bind its own button again, to a new action. Returns an enum
// FnBindResult.
int FnBinds_bind(FnBinds* binds, int index, const char* owner, const char* action, int has_button,
	const char* user_action);

// Releases owner's binding of button index. Returns 0, or -1 when owner
// doesn't hold it.
int FnBinds_release(FnBinds* binds, int index, const char* owner);

// Releases every binding owner holds, as it's unloaded. Returns how many.
int FnBinds_releaseAll(FnBinds* binds, const char* owner);

// Drops the plugin's binding of button index once the user assigned it in
// Settings (user_action isn't ""), copying the plugin's id to dropped
// (size bytes) when it isn't NULL. Returns 1 when one was dropped, else 0.
int FnBinds_yield(FnBinds* binds, int index, const char* user_action, char* dropped, size_t size);

// What button index runs: the user's binding when there is one, else the
// plugin's, else "". owner gets the plugin's id for a plugin's binding and
// NULL otherwise, when it isn't NULL.
const char* FnBinds_effective(const FnBinds* binds, int index, const char* user_action, const char** owner);

// The FN buttons' names from csv, eg. "L3,R3" or ",,HOME", in FN1 to FN3
// order, each "" when the field is empty or missing; a name too long to
// keep is "". For a desktop build standing in for a device in tests.
void FnBinds_parseNames(const char* csv, char names[FN_COUNT][FN_NAME_MAX]);

#endif
