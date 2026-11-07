// Path repair: keeps recents and collections pointing at games after
// they're renamed, eg. "Pokémon Red.gb" to "Pokemon Red.gb", or a change of
// case or punctuation.
//
// When an entry's file is gone, it looks in the same folder for the one file
// whose name matches with accents, case, spaces and punctuation folded (see
// repair_model.h), and points the entry at that. A renamed folder below the
// system folder is matched the same way, so a multi-disc game's folder can
// be renamed too. When no file matches, or more than one does, the entry is
// left alone. Only the folders on a broken entry's path are listed.
//
// Collections (every Collections/*.txt, so favorites too) are repaired at
// start, before any list is built; a file is rewritten only when an entry in
// it changed, through a temporary file so a power cut never leaves half a
// list. An entry whose match is already in the same collection is dropped.
// Recents are repaired as the launcher loads them, at start and when the
// lists reload, through repair_path; an older launcher never asks.

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "mosaic_plugin.h"
#include "repair_model.h"

#define ID "path-repair"
#define PATH_LEN 1024
#define MAX_LISTED 8

static const MosaicHost* host;
static int recents_on = 1;     // the "recents" setting
static int collections_on = 1; // the "collections" setting

static void say(const char* message) {
	host->log(ID, message);
}

static int exists(const char* path) {
	struct stat st;
	return stat(path, &st) == 0;
}

///////////////////////////////////////
// folders listed for this pass, so several broken entries in one folder
// list it once

static struct {
	char dir[PATH_LEN];
	char** names;
	int count;
} listed[MAX_LISTED];
static int listed_count = 0;
static int listed_next = 0; // the slot to reuse once they're all taken

static void forget(void) {
	for (int i = 0; i < listed_count; i++) {
		for (int k = 0; k < listed[i].count; k++) free(listed[i].names[k]);
		free(listed[i].names);
		listed[i].names = NULL;
		listed[i].count = 0;
	}
	listed_count = 0;
	listed_next = 0;
}

// The names in dir, leaving out hidden ones, as stock does.
static int names(const char* dir, const char* const** out) {
	for (int i = 0; i < listed_count; i++) {
		if (!strcmp(listed[i].dir, dir)) {
			*out = (const char* const*)listed[i].names;
			return listed[i].count;
		}
	}
	int slot;
	if (listed_count < MAX_LISTED) slot = listed_count++;
	else {
		slot = listed_next;
		listed_next = (listed_next + 1) % MAX_LISTED;
		for (int k = 0; k < listed[slot].count; k++) free(listed[slot].names[k]);
		free(listed[slot].names);
	}
	snprintf(listed[slot].dir, PATH_LEN, "%s", dir);
	listed[slot].names = NULL;
	listed[slot].count = 0;
	int cap = 0;
	DIR* dh = opendir(dir);
	if (dh) {
		struct dirent* dp;
		while ((dp = readdir(dh)) != NULL) {
			if (dp->d_name[0] == '.') continue;
			if (listed[slot].count == cap) {
				cap = cap ? cap * 2 : 64;
				char** grown = realloc(listed[slot].names, (size_t)cap * sizeof(char*));
				if (!grown) break;
				listed[slot].names = grown;
			}
			listed[slot].names[listed[slot].count++] = strdup(dp->d_name);
		}
		closedir(dh);
	}
	*out = (const char* const*)listed[slot].names;
	return listed[slot].count;
}

///////////////////////////////////////
// repairing one path

// Writes the card-relative path a missing one now matches and returns 1,
// or returns 0 when there's no single match.
static int repairRel(const char* rel, char* out, size_t size) {
	size_t fixed = Repair_fixedLength(rel);
	if (!fixed) return 0;
	char path[PATH_LEN]; // the full path repaired so far
	int len = snprintf(path, sizeof(path), "%s%.*s", host->sdcard_path, (int)fixed, rel);
	if (len < 0 || (size_t)len >= sizeof(path) || !exists(path)) return 0;

	const char* rest = rel + fixed; // "/<name>/<name>..."
	while (*rest == '/') {
		const char* name = rest + 1;
		const char* end = strchr(name, '/');
		size_t name_len = end ? (size_t)(end - name) : strlen(name);
		if (!name_len || name_len >= 256) return 0;
		char want[256];
		snprintf(want, sizeof(want), "%.*s", (int)name_len, name);

		size_t at = strlen(path);
		len = snprintf(path + at, sizeof(path) - at, "/%s", want);
		if (len < 0 || (size_t)len >= sizeof(path) - at) return 0;
		if (!exists(path)) {
			path[at] = '\0';
			const char* const* in;
			int count = names(path, &in);
			int pick = Repair_pick(want, in, count);
			if (pick < 0) return 0;
			len = snprintf(path + at, sizeof(path) - at, "/%s", in[pick]);
			if (len < 0 || (size_t)len >= sizeof(path) - at) return 0;
		}
		rest = name + name_len;
	}
	const char* repaired = path + strlen(host->sdcard_path);
	// stock reads recents and collections 255 bytes a line
	if (!strcmp(repaired, rel) || strlen(repaired) >= size || strlen(repaired) >= 256) return 0;
	snprintf(out, size, "%s", repaired);
	return 1;
}

///////////////////////////////////////
// collections

typedef struct Lines {
	char** items;
	int count, cap;
} Lines;

static void linesFree(Lines* lines) {
	for (int i = 0; i < lines->count; i++) free(lines->items[i]);
	free(lines->items);
	lines->items = NULL;
	lines->count = lines->cap = 0;
}

static int linesPush(Lines* lines, const char* line) {
	if (lines->count == lines->cap) {
		int cap = lines->cap ? lines->cap * 2 : 32;
		char** grown = realloc(lines->items, (size_t)cap * sizeof(char*));
		if (!grown) return -1;
		lines->items = grown;
		lines->cap = cap;
	}
	lines->items[lines->count++] = strdup(line);
	return 0;
}

static int linesHas(const Lines* lines, const char* line) {
	for (int i = 0; i < lines->count; i++) {
		if (lines->items[i] && !strcmp(lines->items[i], line)) return 1;
	}
	return 0;
}

static int save(const char* list_path, const Lines* lines) {
	char tmp[PATH_LEN * 2 + 8];
	snprintf(tmp, sizeof(tmp), "%s.tmp", list_path);
	FILE* f = fopen(tmp, "w");
	if (!f) return -1;
	for (int i = 0; i < lines->count; i++) {
		if (lines->items[i]) fprintf(f, "%s\n", lines->items[i]);
	}
	if (fclose(f) != 0) {
		remove(tmp);
		return -1;
	}
	return rename(tmp, list_path); // so a power cut never leaves half a list
}

// Repairs one collection file. Returns how many entries changed.
static int repairCollection(const char* list_path) {
	FILE* f = fopen(list_path, "r");
	if (!f) return 0;
	Lines lines = {0};
	char line[PATH_LEN];
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = '\0';
		if (line[0] && linesPush(&lines, line) != 0) break;
	}
	fclose(f);

	int changed = 0;
	for (int i = 0; i < lines.count; i++) {
		const char* rel = lines.items[i];
		char full[PATH_LEN * 2];
		snprintf(full, sizeof(full), "%s%s", host->sdcard_path, rel);
		if (rel[0] != '/' || exists(full)) continue;
		char repaired[PATH_LEN];
		if (!repairRel(rel, repaired, sizeof(repaired))) continue;

		char message[PATH_LEN * 2 + 32];
		if (linesHas(&lines, repaired)) {
			snprintf(message, sizeof(message), "dropped %s, already listed as %s", rel, repaired);
			free(lines.items[i]);
			lines.items[i] = NULL;
		}
		else {
			snprintf(message, sizeof(message), "repaired %s to %s", rel, repaired);
			free(lines.items[i]);
			lines.items[i] = strdup(repaired);
		}
		say(message);
		changed++;
	}
	if (changed && save(list_path, &lines) != 0) {
		char message[PATH_LEN * 2 + 32];
		snprintf(message, sizeof(message), "couldn't save %s", list_path);
		say(message);
		changed = 0;
	}
	linesFree(&lines);
	return changed;
}

static int endsWith(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcmp(s + a - b, suffix);
}

// Every Collections/*.txt but map.txt, which names games instead of
// listing them.
static void repairCollections(void) {
	char dir[PATH_LEN];
	snprintf(dir, sizeof(dir), "%s/Collections", host->sdcard_path);
	DIR* dh = opendir(dir);
	if (!dh) return;
	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL) {
		if (dp->d_name[0] == '.' || !endsWith(dp->d_name, ".txt") || !strcmp(dp->d_name, "map.txt")) continue;
		char list_path[PATH_LEN * 2];
		snprintf(list_path, sizeof(list_path), "%s/%s", dir, dp->d_name);
		repairCollection(list_path);
	}
	closedir(dh);
	forget();
}

///////////////////////////////////////
// hooks

// A recent the launcher found missing, as a full path.
static int repairPath(const char* missing, char* out, size_t size) {
	if (!recents_on || !missing) return 0;
	size_t len = strlen(host->sdcard_path);
	if (strncmp(missing, host->sdcard_path, len) != 0 || missing[len] != '/') return 0;
	char repaired[PATH_LEN];
	if (!repairRel(missing + len, repaired, sizeof(repaired))) return 0;
	int written = snprintf(out, size, "%s%s", host->sdcard_path, repaired);
	if (written < 0 || (size_t)written >= size) return 0;
	char message[PATH_LEN * 2 + 32];
	snprintf(message, sizeof(message), "repaired recent %s to %s", missing + len, repaired);
	say(message);
	return 1;
}

// The launcher repairs recents in a burst as it loads them; drop the
// folders listed for them on the next frame.
static void tick(unsigned long now) {
	(void)now;
	if (listed_count) forget();
}

static int init(const MosaicHost* h) {
	host = h;
	recents_on = strcmp(host->cfg_get(ID, "recents", "on"), "off") != 0;
	collections_on = strcmp(host->cfg_get(ID, "collections", "on"), "off") != 0;
	if (collections_on) repairCollections();
	return 0;
}

static void quit(void) {
	forget();
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.tick = tick,
	.repair_path = repairPath,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
