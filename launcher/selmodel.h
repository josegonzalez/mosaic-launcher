#ifndef SELMODEL_H
#define SELMODEL_H

// The parts of selection events that don't need SDL or the loader: seeing
// that the game list's selection moved, and deciding when it has settled.
// The art cache uses SelKey too, to drop loads asked for before a move.

// How long the selection stays put, with no scrolling button held, before
// plugins hear of it.
#define SEL_SETTLE_MS 250

#define SEL_PATH 1024

// The selection as the launcher holds it: the open folder, its list and the
// selected index. Pointers are only compared, never read.
typedef struct SelKey {
	const void* top;
	const void* entries;
	int selected;
	int seen; // 0 until the first look
} SelKey;

void SelKey_reset(SelKey* key);

// 1 when the selection differs from the last look, and always on the first,
// remembering it. A reload gives the folder a new list, so it counts as a
// move even when the same entry stays selected.
int SelKey_changed(SelKey* key, const void* top, const void* entries, int selected);

// When a moved selection has settled, and whether it's news.
typedef struct SelSettle {
	int moving;             // moved since it last settled
	unsigned long moved_at; // when it last moved
	int sent;               // an event has gone out
	char folder[SEL_PATH];  // the folder and entry the last event was about,
	char entry[SEL_PATH];   // entry "" for an empty folder
	char now_folder[SEL_PATH]; // and the last frame's
	char now_entry[SEL_PATH];
} SelSettle;

void SelSettle_init(SelSettle* s);

// One frame at now: moved is 1 when the selection changed since the last
// frame (from SelKey_changed; a new folder or entry path counts too, in
// case a freed list's memory came back for the next one), held is 1 while
// a button that scrolls the list is down, and folder and entry are the
// open folder's path and the selected entry's (entry NULL in an empty
// folder). The selection settles once it has gone SEL_SETTLE_MS without
// moving and nothing is held. Returns
// 1 when it settles on another folder or entry than the last event's, or
// on anything when there's been no event yet, and remembers it; 0 otherwise,
// including when it settles back where the last event was.
int SelSettle_step(SelSettle* s, unsigned long now, int moved, int held, const char* folder, const char* entry);

#endif
