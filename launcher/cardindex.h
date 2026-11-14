#ifndef CARDINDEX_H
#define CARDINDEX_H

// The card index for plugins (index_count and the rest in
// mosaic_plugin.h), over indexmodel.c: one copy the main thread hands
// out, and one worker thread that builds the next.
//
// Nothing happens until a plugin first asks. The first ask reads the copy
// kept on the card, if there is one, on the main thread, and starts the
// worker, which checks it against the card (or builds one from the card
// when there's none), reading again only the systems whose folders
// changed, and saves what it built. CardIndex_poll, on the main thread
// before each frame's ticks, puts a changed index in place. A refresh
// asked for while the worker runs starts another check once it's done.
// The main thread never waits on the worker except at CardIndex_quit.

#include "indexmodel.h"

// The card, the platform and the file the index is kept in, from
// plugins.c before anything asks. log (eg. LOG_info) gets what it does.
void CardIndex_setup(const char* sdcard, const char* platform, const char* path, void (*log)(const char* message));

// The index to answer from, or NULL before the first copy is ready. The
// first call starts it.
const CardIndex* CardIndex_get(void);

// Checks the card again in the background, once something has asked.
void CardIndex_refresh(void);

// Puts the worker's index in place when it changed. Returns 1 when it
// did (launcher.index_ready is due), else 0.
int CardIndex_poll(void);

// Stops the worker, waiting for it, and frees everything. Safe to call
// when it never started, and more than once.
void CardIndex_quit(void);

#endif
