#include "launcher.h"

#define MAX_TILES 32
#define MAX_HIDDEN 32

static QuickTile tiles[MAX_TILES];
static int tile_count = 0;
static char* hidden[MAX_HIDDEN];
static int hidden_count = 0;

// The top row's built-in tiles, and the labels plugins gave them.
static const char* const renamable[] = { "Recents", "Collections", "Games", "Tools" };
#define RENAMABLE_COUNT (int)(sizeof(renamable) / sizeof(renamable[0]))
static char* labels[RENAMABLE_COUNT];

static char* copy(const char* s) {
	return s && s[0] ? strdup(s) : NULL;
}

int QuickTiles_add(const char* action_id, int row, const char* icon, const char* icon_off, int (*isOn)(void* data)) {
	const Action* action = Actions_find(action_id);
	if (!action || (row != QUICK_ROW_TOP && row != QUICK_ROW_BOTTOM)) return -1;
	QuickTile* tile = NULL;
	for (int i = 0; i < tile_count; i++) {
		if (tiles[i].action == action) tile = &tiles[i];
	}
	if (!tile) {
		if (tile_count >= MAX_TILES) return -1;
		tile = &tiles[tile_count++];
	}
	else {
		free(tile->icon);
		free(tile->icon_off);
	}
	tile->action = action;
	tile->row = row;
	tile->icon = copy(icon);
	tile->icon_off = copy(icon_off);
	tile->isOn = isOn;
	return 0;
}

static int hiddenAt(const char* name) {
	for (int i = 0; i < hidden_count; i++) {
		if (!strcmp(hidden[i], name)) return i;
	}
	return -1;
}

int QuickTiles_hide(const char* name, int hide) {
	if (!name || !name[0]) return -1;
	int at = hiddenAt(name);
	if (hide && at < 0) {
		if (hidden_count >= MAX_HIDDEN) return -1;
		hidden[hidden_count++] = strdup(name);
	}
	else if (!hide && at >= 0) {
		free(hidden[at]);
		hidden[at] = hidden[--hidden_count];
	}
	return 0;
}

static int renamableAt(const char* name) {
	for (int i = 0; name && i < RENAMABLE_COUNT; i++) {
		if (!strcmp(renamable[i], name)) return i;
	}
	return -1;
}

int QuickTiles_rename(const char* name, const char* label) {
	int at = renamableAt(name);
	if (at < 0) return -1;
	free(labels[at]);
	labels[at] = copy(label);
	return 0;
}

const char* QuickTiles_label(const Entry* entry) {
	// plugin tiles are named by their action's label already
	int at = QuickTiles_forEntry(entry) ? -1 : renamableAt(entry->name);
	return at >= 0 && labels[at] ? labels[at] : entry->name;
}

int QuickTiles_any(void) {
	if (tile_count > 0 || hidden_count > 0) return 1;
	for (int i = 0; i < RENAMABLE_COUNT; i++) {
		if (labels[i]) return 1;
	}
	return 0;
}

const QuickTile* QuickTiles_forEntry(const Entry* entry) {
	if (!entry || entry->type != ENTRY_DIP || !prefixMatch(FN_ACTION_ACTION_PREFIX, entry->path)) return NULL;
	const char* id = entry->path + strlen(FN_ACTION_ACTION_PREFIX);
	for (int i = 0; i < tile_count; i++) {
		if (!strcmp(tiles[i].action->id, id)) return &tiles[i];
	}
	return NULL;
}

int QuickTile_isOff(const QuickTile* tile) {
	return tile && tile->isOn && !tile->isOn(tile->action->data);
}

void QuickTiles_apply(Array* entries, int row) {
	if (!QuickTiles_any()) return;
	for (int i = entries->count - 1; i >= 0; i--) {
		Entry* entry = entries->items[i];
		if (hiddenAt(entry->name) < 0) continue;
		Array_remove(entries, entry);
		Entry_free(entry);
	}
	for (int i = 0; i < tile_count; i++) {
		const QuickTile* tile = &tiles[i];
		if (tile->row != row || hiddenAt(tile->action->id) >= 0 || !Action_available(tile->action)) continue;
		char path[256];
		snprintf(path, sizeof(path), FN_ACTION_ACTION_PREFIX "%s", tile->action->id);
		Array_push(entries, Entry_newNamed(path, ENTRY_DIP, (char*)tile->action->label));
	}
}

void QuickTiles_reset(void) {
	for (int i = 0; i < tile_count; i++) {
		free(tiles[i].icon);
		free(tiles[i].icon_off);
	}
	tile_count = 0;
	for (int i = 0; i < hidden_count; i++) free(hidden[i]);
	hidden_count = 0;
	for (int i = 0; i < RENAMABLE_COUNT; i++) {
		free(labels[i]);
		labels[i] = NULL;
	}
}

struct QuickTilesSaved {
	QuickTile tiles[MAX_TILES];
	int tile_count;
	char* hidden[MAX_HIDDEN];
	int hidden_count;
	char* labels[RENAMABLE_COUNT];
};

static char* copyExact(const char* s) {
	return s ? strdup(s) : NULL;
}

QuickTilesSaved* QuickTiles_save(void) {
	QuickTilesSaved* saved = calloc(1, sizeof(*saved));
	for (int i = 0; i < tile_count; i++) {
		saved->tiles[i] = tiles[i];
		saved->tiles[i].icon = copyExact(tiles[i].icon);
		saved->tiles[i].icon_off = copyExact(tiles[i].icon_off);
	}
	saved->tile_count = tile_count;
	for (int i = 0; i < hidden_count; i++) saved->hidden[i] = strdup(hidden[i]);
	saved->hidden_count = hidden_count;
	for (int i = 0; i < RENAMABLE_COUNT; i++) saved->labels[i] = copyExact(labels[i]);
	return saved;
}

void QuickTiles_restore(QuickTilesSaved* saved) {
	if (!saved) return;
	QuickTiles_reset();
	// the copies move back in, so only the struct is freed
	for (int i = 0; i < saved->tile_count; i++) tiles[i] = saved->tiles[i];
	tile_count = saved->tile_count;
	for (int i = 0; i < saved->hidden_count; i++) hidden[i] = saved->hidden[i];
	hidden_count = saved->hidden_count;
	for (int i = 0; i < RENAMABLE_COUNT; i++) labels[i] = saved->labels[i];
	free(saved);
}

void QuickTiles_discard(QuickTilesSaved* saved) {
	if (!saved) return;
	for (int i = 0; i < saved->tile_count; i++) {
		free(saved->tiles[i].icon);
		free(saved->tiles[i].icon_off);
	}
	for (int i = 0; i < saved->hidden_count; i++) free(saved->hidden[i]);
	for (int i = 0; i < RENAMABLE_COUNT; i++) free(saved->labels[i]);
	free(saved);
}
