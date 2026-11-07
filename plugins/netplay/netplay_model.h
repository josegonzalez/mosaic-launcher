#ifndef NETPLAY_MODEL_H
#define NETPLAY_MODEL_H

#include <stddef.h>

// Netplay's model: the address the user types, the pending host or join
// choice, and the arguments it adds to a launch. No SDL and no host here,
// so the host tests can check it on its own.

#define NP_PATH_LEN 512
#define NP_HOST_LEN 254 // a DNS name is at most 253 characters
#define NP_ARG_LEN 256
#define NP_ARGS_MAX 4
#define NP_DEFAULT_PORT 55435

enum NPMode {
	NP_NONE,
	NP_HOST, // wait for another device to connect
	NP_JOIN, // connect to a device that's hosting
};

// A host or join the user picked, waiting for its launch.
typedef struct NPPending {
	int mode; // an enum NPMode
	char path[NP_PATH_LEN];
	char host[NP_HOST_LEN];
	int port;
} NPPending;

// A port from 1 to 65535 in text, or fallback for anything else.
int NP_port(const char* text, int fallback);

// Reads a typed address: a host name or IPv4 address, optionally followed
// by :port, with spaces around it left out. The port is port_fallback
// when there isn't one. Returns 0, or -1 for an empty address, a character
// a host name can't have, a name too long or a bad port.
int NP_parseAddress(const char* text, int port_fallback, char* host, size_t size, int* port);

// Sets pending to a host or join of path. Returns 0, or -1 for a bad mode,
// a missing path or one too long.
int NP_setPending(NPPending* pending, int mode, const char* path, const char* host, int port);

// Takes the pending choice for a launch: when it's for path and the
// launch is a game, copies it to out and returns its mode, and otherwise
// returns NP_NONE. Either way pending is cleared, so a choice never
// carries over to a later launch.
int NP_take(NPPending* pending, const char* path, int is_rom, NPPending* out);

// The arguments netplay adds before the game's path, from the pending
// choice:
//
//   host: --host --port <port>
//   join: --connect <host> --port <port>
//
// Returns how many it wrote, or -1 for NP_NONE or one that doesn't fit.
int NP_args(const NPPending* pending, char args[][NP_ARG_LEN], int max);

// The emulator tag of a game under roms (eg. "GB" for
// "<roms>/Game Boy (GB)/Tetris.gb"): the text in the last parentheses of
// its system folder's name, or the whole name without any, as stock picks
// the emulator. Returns 0, or -1 for a path that isn't in a system folder.
int NP_emuTag(const char* roms, const char* path, char* out, size_t size);

#endif
