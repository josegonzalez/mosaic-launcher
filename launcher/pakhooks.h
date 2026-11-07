#ifndef PAKHOOKS_H
#define PAKHOOKS_H

// Hook scripts that ship inside a pak, as an alternative to dropping files
// into .userdata/<platform>/.hooks/: Tools/<platform>/<name>.pak/pre-launch.sh
// runs before every game or pak launch, in pak name order.
//
// The script gets HOOK_PHASE=pre, HOOK_TYPE (rom or pak), HOOK_ROM_PATH and
// HOOK_PAK_PATH. Exiting 1 cancels the launch; any other failure is logged
// and the launch goes ahead.

#define PAK_HOOK_CANCEL 1

// Finds the scripts. Call once at startup.
void PakHooks_init(void);

// Runs them. Returns 1 to go ahead, 0 if one cancelled.
int PakHooks_preLaunch(const char* path, int type);

#endif
