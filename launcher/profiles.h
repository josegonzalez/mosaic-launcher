#ifndef PROFILES_H
#define PROFILES_H

// Profiles: a separate set of the launcher's data, such as a guest's, that
// a plugin switches to and back (host->profile_switch). The owner's data is
// the default profile, named "". No SDL here, and every call takes its
// paths in a ProfileConfig, so the host tests can check it on its own.
//
// Data stock reads and writes at fixed paths (recent.txt, the play
// tracker's game_logs.sqlite and the Collections folder) is swapped in
// place: the live profile's items are at their usual paths, and every
// other profile's are parked in <parks_dir>/<name>/, the owner's in
// <parks_dir>/owner/. Switching parks the live items, saves the new name
// in state_path, then moves the new profile's parked items in, so a power
// cut between any two steps leaves nothing lost: Profile_settle, run at
// start and before each switch, moves back what a cut left parked.
// Plugins' own files are never moved: each profile's live in a folder of
// their own (Profile_filePath).

#include <stddef.h>

#define PROFILE_NAME_MAX 32     // a name's bytes, its terminator included
#define PROFILE_OWNER_DIR "owner" // where the owner's items are parked
#define PROFILE_ITEMS_MAX 16     // the most items a ProfileConfig swaps

typedef struct ProfileItem {
	const char* live; // its usual path
	const char* park; // its name in a profile's parked folder
} ProfileItem;

typedef struct ProfileConfig {
	const char* state_path;   // the live profile's name; none for the owner's
	const char* parks_dir;    // parked items, <parks_dir>/<name>/<park>
	const ProfileItem* items;
	int item_count;
	const char* userdata_dir; // .userdata, whose */launcher/profiles/<name> Profile_clear removes
	void (*log)(const char* message); // or NULL
} ProfileConfig;

// 1 for "" (the owner's) or 1 to 31 lower case letters, digits and -,
// not starting with -, and not "owner".
int Profile_nameValid(const char* name);

// The live profile's name from state_path, or "" for none or one that
// isn't a valid name.
void Profile_current(const ProfileConfig* config, char out[PROFILE_NAME_MAX]);

// Moves the live profile's parked items to their usual paths where those
// are missing, as a switch a power cut stopped would have. Returns how
// many it moved, or -1 when one couldn't be.
int Profile_settle(const ProfileConfig* config);

// Makes name the live profile: parks the live one's items, saves name,
// and moves name's parked items in. An item whose park is taken (only
// after a cut) is kept beside it with a .<time> suffix rather than
// replacing it. Returns 0, also when name is live already, or -1 for an
// invalid name, more than PROFILE_ITEMS_MAX items, or an item that
// couldn't be moved: one that couldn't be parked, after moving back what
// it had parked, with the profile unchanged, or one of name's that
// couldn't be moved in, with name live (Profile_current says which).
int Profile_switch(const ProfileConfig* config, const char* name);

// Removes a profile's data, never the owner's: its parked items, or its
// live ones when it's live, and every <userdata_dir>/*/launcher/profiles/
// <name> folder (plugins' files, and the parks when parks_dir is one of
// them). Returns 0, or -1 for "" or an invalid name, or when something
// couldn't be removed.
int Profile_clear(const ProfileConfig* config, const char* name);

// Removes path, a folder with everything in it, without following links.
// 0 when it's gone or wasn't there.
int Profile_removeTree(const char* path);

// A plugin's file in a profile: <launcher_dir>/<file> for the owner's
// ("") and <launcher_dir>/profiles/<name>/<file> for another, making that
// profile's folder. file is relative, with no empty, . or .. parts.
// Returns 0, or -1 for a bad name or file, or a path that doesn't fit.
int Profile_filePath(const char* launcher_dir, const char* name, const char* file, char* out, size_t size);

#endif
