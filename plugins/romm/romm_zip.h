// The romm plugin's unpacker for games RomM serves as a zip of their
// files, with miniz (vendor/miniz). The zip is untrusted: every entry is
// checked (romm_model.h's RM_zipCheck) before anything is written, each
// file goes to a temporary name in a temporary folder, created with
// O_EXCL|O_NOFOLLOW through folders opened without following links, and
// only a folder whose every file is in is moved into place. A cancel or
// error removes only what it made.

#ifndef ROMM_ZIP_H
#define ROMM_ZIP_H

#include "romm_model.h"

typedef struct RZJob {
	const char* zip; // the downloaded zip
	const char* dir; // the system's folder it unpacks into, eg. <sd>/Roms/Sony PlayStation (PS)
	const char* game; // the game's folder there, RomM's fs_name
	const char* temp; // a folder to unpack in first, in dir, that isn't there yet
	long long expect; // the size RomM gave for the game's files, or -1
	volatile int* cancel; // set to 1 to stop
	volatile long long* done; // bytes written so far, or NULL
	volatile long long* total; // bytes to write, or NULL

	// what came of it
	char launch[RM_PATH_MAX]; // the file to launch, relative to dir: "<game>/<game>.m3u", "<game>/<disc>" or "<file>"
	int files; // files written, the plugin's .m3u among them
	int discs;
	char error[256];
} RZJob;

// Unpacks the zip: a zip of one file (besides RomM's own <game>.m3u) lands
// as that file in dir, as a single-file rom does; any other into
// dir/<game>/, with <game>.m3u listing the discs (RM_discs) when there are
// two or more. Refuses to replace a file or folder that's there. Returns 0,
// or -1 with job->error set; the zip is left for the caller either way.
int RZ_unpack(RZJob* job);
// Removes a folder the plugin made and what's in it, never following a
// link (a link is removed, not what it points at), up to 16 folders deep.
// Returns 0, or -1 when something was left.
int RZ_removeTree(const char* path);

#endif
