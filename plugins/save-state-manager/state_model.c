#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "state_model.h"

// stock's sizes, at scale 1
#define PADDING 10
#define PILL_SIZE 30
#define BUTTON_PADDING 12

///////////////////////////////////////
// names

// The system folder's name in path below roms, or 0 for a path that isn't
// inside one.
static int systemFolder(const char* roms, const char* path, char* folder, size_t size) {
	size_t n = strlen(roms);
	if (!n || strncmp(path, roms, n) != 0 || path[n] != '/') return 0;
	const char* start = path + n + 1;
	const char* slash = strchr(start, '/');
	if (!slash || slash == start || !slash[1] || (size_t)(slash - start) >= size) return 0;
	memcpy(folder, start, (size_t)(slash - start));
	folder[slash - start] = '\0';
	return 1;
}

int SM_tagOf(const char* roms, const char* path, char* out, size_t size) {
	char folder[SM_NAME_MAX];
	if (size) out[0] = '\0';
	if (!roms || !path || !size || !systemFolder(roms, path, folder, sizeof(folder))) return 0;
	const char* tag = folder;
	char* open = strrchr(folder, '(');
	if (open) {
		tag = open + 1;
		char* close = strchr(open, ')');
		if (close) *close = '\0';
	}
	if (!*tag || strlen(tag) >= size) return 0;
	snprintf(out, size, "%s", tag);
	return 1;
}

static int endsWith(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && strcmp(s + a - b, suffix) == 0;
}

// "<dir>/<last part of dir><ext>", for the first len characters of dir
static int named(const char* dir, size_t len, const char* ext, char* out, size_t size) {
	const char* name = dir;
	for (size_t i = 0; i < len; i++) {
		if (dir[i] == '/') name = dir + i + 1;
	}
	int namelen = (int)(dir + len - name);
	if (namelen <= 0) return 0;
	return snprintf(out, size, "%.*s/%.*s%s", (int)len, dir, namelen, name, ext) < (int)size;
}

int SM_gameFile(const char* path, int is_dir, int (*exists)(const char* path, void* ctx), void* ctx, char* out,
		size_t size) {
	char file[SM_PATH_MAX];
	if (size) out[0] = '\0';
	if (!path || !*path || !size || strlen(path) >= sizeof(file)) return 0;
	snprintf(file, sizeof(file), "%s", path);
	if (is_dir) {
		size_t n = strlen(path);
		while (n > 1 && path[n - 1] == '/') n--;
		if (!named(path, n, ".cue", file, sizeof(file))) return 0;
		if (!exists(file, ctx) && (!named(path, n, ".m3u", file, sizeof(file)) || !exists(file, ctx))) return 0;
	}
	if (!endsWith(file, ".m3u")) {
		const char* slash = strrchr(file, '/');
		char m3u[SM_PATH_MAX];
		if (slash && slash > file && named(file, (size_t)(slash - file), ".m3u", m3u, sizeof(m3u)) && exists(m3u, ctx)) {
			snprintf(file, sizeof(file), "%s", m3u);
		}
	}
	if (strlen(file) >= size) return 0;
	snprintf(out, size, "%s", file);
	return 1;
}

// the dot starting an extension stock cuts: one to four characters after it
static char* cutDot(char* name) {
	char* dot = strrchr(name, '.');
	if (!dot) return NULL;
	size_t len = strlen(dot);
	return len > 2 && len <= 5 ? dot : NULL;
}

void SM_cut(const char* name, char* out, size_t size) {
	if (!size) return;
	snprintf(out, size, "%s", name ? name : "");
	char* dot = cutDot(out);
	if (dot) *dot = '\0';
}

void SM_displayName(const char* name, char* out, size_t size) {
	char work[SM_NAME_MAX];
	if (!size) return;
	snprintf(work, sizeof(work), "%s", name ? name : "");
	char* dot;
	while ((dot = cutDot(work)) != NULL) *dot = '\0';
	char kept[SM_NAME_MAX];
	snprintf(kept, sizeof(kept), "%s", work);
	for (;;) {
		char* open = strrchr(work, '(');
		if (!open) open = strrchr(work, '[');
		if (!open || open == work) break;
		*open = '\0';
	}
	if (!work[0]) snprintf(work, sizeof(work), "%s", kept);
	size_t n = strlen(work);
	while (n > 1 && isspace((unsigned char)work[n - 1])) work[--n] = '\0';
	snprintf(out, size, "%s", work);
}

void SM_slotLabel(int slot, char* out, size_t size) {
	if (slot == SM_DEFAULT_SLOT) snprintf(out, size, "Default");
	else if (slot == SM_AUTO_SLOT) snprintf(out, size, "Auto");
	else snprintf(out, size, "Slot %d", slot + 1);
}

int SM_slotOf(const char* text) {
	if (!text) return 0;
	while (isspace((unsigned char)*text)) text++;
	int negative = *text == '-';
	if (*text == '-' || *text == '+') text++;
	long n = 0;
	while (isdigit((unsigned char)*text) && n < 100000) n = n * 10 + (*text++ - '0');
	return (int)(negative ? -n : n);
}

void SM_date(time_t when, char* out, size_t size) {
	if (!size) return;
	out[0] = '\0';
	struct tm tm;
	if (when <= 0 || !localtime_r(&when, &tm)) return;
	strftime(out, size, "%Y-%m-%d %H:%M", &tm);
}

///////////////////////////////////////
// the card

// 1 for a real folder at path, not a link
static int isFolder(const char* path) {
	struct stat st;
	return lstat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// 1 for a regular file at path, not a link; *mtime gets its time
static int isFile(const char* path, time_t* mtime) {
	struct stat st;
	if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode)) return 0;
	if (mtime) *mtime = st.st_mtime;
	return 1;
}

// a name that can be one file or folder name
static int plainName(const char* name) {
	return name && name[0] && strcmp(name, ".") != 0 && strcmp(name, "..") != 0 && !strchr(name, '/')
		&& strlen(name) < SM_NAME_MAX;
}

void SM_cardInit(SMCard* card, const char* sdcard) {
	snprintf(card->shared, sizeof(card->shared), "%s/.userdata/shared", sdcard ? sdcard : "");
}

// shared itself, and the .userdata it's in, are real folders
static int sharedReal(const SMCard* card) {
	char userdata[SM_PATH_MAX];
	snprintf(userdata, sizeof(userdata), "%s", card->shared);
	char* slash = strrchr(userdata, '/');
	if (!slash) return 0;
	*slash = '\0';
	return isFolder(userdata) && isFolder(card->shared);
}

// "<shared>/.minui/<tag>" when it and every folder on the way is real
static int minuiFolder(const SMCard* card, const char* tag, char* out, size_t size) {
	char minui[SM_PATH_MAX];
	snprintf(minui, sizeof(minui), "%s/.minui", card->shared);
	if (!sharedReal(card) || !isFolder(minui)) return 0;
	if (snprintf(out, size, "%s/%s", minui, tag) >= (int)size) return 0;
	return isFolder(out);
}

typedef struct Folders {
	char paths[SM_MAX_FILES][SM_PATH_MAX];
	int count;
} Folders;

static int comparePaths(const void* a, const void* b) {
	return strcmp(a, b);
}

// shared's real folders named <tag>-<core>, sorted
static void coreFolders(const SMCard* card, const char* tag, Folders* out) {
	out->count = 0;
	if (!sharedReal(card)) return;
	DIR* d = opendir(card->shared);
	if (!d) return;
	size_t len = strlen(tag);
	struct dirent* e;
	while (out->count < SM_MAX_FILES && (e = readdir(d))) {
		if (strncmp(e->d_name, tag, len) != 0 || e->d_name[len] != '-' || !e->d_name[len + 1]) continue;
		// the core's name has no dash, so GB-gambatte is GB's and not G's
		if (strchr(e->d_name + len + 1, '-')) continue;
		char* path = out->paths[out->count];
		if (snprintf(path, SM_PATH_MAX, "%s/%s", card->shared, e->d_name) >= SM_PATH_MAX || !isFolder(path)) continue;
		out->count++;
	}
	closedir(d);
	qsort(out->paths, (size_t)out->count, SM_PATH_MAX, comparePaths);
}

// The file names a slot's state can have, in the formats stock writes.
static int stateNames(const SMGame* game, int slot, char names[4][SM_NAME_MAX + 16]) {
	int n = 0;
	snprintf(names[n++], sizeof(names[0]), "%s.st%d", game->name, slot);
	if (!game->use_cut) return n;
	char cut[SM_NAME_MAX];
	SM_cut(game->name, cut, sizeof(cut));
	if (slot == SM_AUTO_SLOT) {
		snprintf(names[n++], sizeof(names[0]), "%s.state.auto", cut);
		return n;
	}
	snprintf(names[n++], sizeof(names[0]), "%s.state.%d", cut, slot);
	if (slot == 0) snprintf(names[n++], sizeof(names[0]), "%s.state", cut);
	else snprintf(names[n++], sizeof(names[0]), "%s.state%d", cut, slot);
	return n;
}

static int stateFilesIn(const SMGame* game, const Folders* folders, int slot, char out[][SM_PATH_MAX], int max,
		time_t* newest) {
	char names[4][SM_NAME_MAX + 16];
	int count = stateNames(game, slot, names);
	int found = 0;
	if (newest) *newest = 0;
	for (int f = 0; f < folders->count; f++) {
		for (int i = 0; i < count; i++) {
			char path[SM_PATH_MAX];
			time_t when = 0;
			if (snprintf(path, sizeof(path), "%s/%s", folders->paths[f], names[i]) >= (int)sizeof(path)) continue;
			if (!isFile(path, &when)) continue;
			if (newest && when > *newest) *newest = when;
			if (found < max) snprintf(out[found], SM_PATH_MAX, "%s", path);
			found++;
		}
	}
	return found < max ? found : max;
}

int SM_stateFiles(const SMCard* card, const SMGame* game, int slot, char out[][SM_PATH_MAX], int max) {
	Folders folders;
	coreFolders(card, game->tag, &folders);
	return stateFilesIn(game, &folders, slot, out, max, NULL);
}

// The name a file in .minui/<tag> is about: <name>.txt (the last slot),
// <name>.<slot>.bmp (a preview) or <name>.<slot>.txt (the disc), the
// second reading kept in other when both fit. Returns how many.
static int minuiNames(const char* file, char* name, char* other, size_t size) {
	if (file[0] == '.') return 0;
	size_t len = strlen(file);
	int n = 0;
	if (len > 6 && (endsWith(file, ".bmp") || endsWith(file, ".txt"))) {
		const char* d = file + len - 6;
		if (d[0] == '.' && isdigit((unsigned char)d[1]) && (size_t)(d - file) < size) {
			snprintf(name, size, "%.*s", (int)(d - file), file);
			n++;
		}
	}
	if (len > 4 && endsWith(file, ".txt") && len - 4 < size) {
		snprintf(n ? other : name, size, "%.*s", (int)(len - 4), file);
		n++;
	}
	return n;
}

// The name a .st<slot> file is for, or 0.
static int stName(const char* file, char* name, size_t size) {
	size_t len = strlen(file);
	if (file[0] == '.' || len < 5) return 0;
	const char* d = file + len - 4;
	if (strncmp(d, ".st", 3) != 0 || !isdigit((unsigned char)d[3]) || (size_t)(d - file) >= size) return 0;
	snprintf(name, size, "%.*s", (int)(d - file), file);
	return 1;
}

// Calls found for each name a state, preview or slot file under tag is for.
static void tagNames(const SMCard* card, const char* tag, void (*found)(const char* name, void* ctx), void* ctx) {
	char name[SM_NAME_MAX], other[SM_NAME_MAX];
	Folders folders;
	coreFolders(card, tag, &folders);
	for (int f = 0; f < folders.count; f++) {
		DIR* d = opendir(folders.paths[f]);
		if (!d) continue;
		struct dirent* e;
		while ((e = readdir(d))) {
			if (stName(e->d_name, name, sizeof(name))) found(name, ctx);
		}
		closedir(d);
	}
	char minui[SM_PATH_MAX];
	if (!minuiFolder(card, tag, minui, sizeof(minui))) return;
	DIR* d = opendir(minui);
	if (!d) return;
	struct dirent* e;
	while ((e = readdir(d))) {
		int n = minuiNames(e->d_name, name, other, sizeof(name));
		if (n > 0) found(name, ctx);
		if (n > 1) found(other, ctx);
	}
	closedir(d);
}

typedef struct CutCheck {
	const char* name;
	char cut[SM_NAME_MAX];
	int shared;
} CutCheck;

static void cutFound(const char* name, void* ctx) {
	CutCheck* check = ctx;
	if (check->shared || strcmp(name, check->name) == 0) return;
	char cut[SM_NAME_MAX];
	SM_cut(name, cut, sizeof(cut));
	if (strcmp(cut, check->cut) == 0) check->shared = 1;
}

static void gameSetup(const char* tag, const char* name, const char* display, SMGame* game) {
	memset(game, 0, sizeof(*game));
	snprintf(game->tag, sizeof(game->tag), "%s", tag);
	snprintf(game->name, sizeof(game->name), "%s", name);
	if (display && display[0]) snprintf(game->display, sizeof(game->display), "%s", display);
	else SM_displayName(name, game->display, sizeof(game->display));
}

int SM_gameInit(const SMCard* card, const char* tag, const char* name, const char* display, const char* rom_dir,
		SMGame* game) {
	memset(game, 0, sizeof(*game));
	if (!plainName(tag) || !plainName(name)) return -1;
	gameSetup(tag, name, display, game);
	CutCheck check = { name, "", 0 };
	SM_cut(name, check.cut, sizeof(check.cut));
	if (rom_dir) {
		DIR* d = opendir(rom_dir);
		if (d) {
			struct dirent* e;
			while (!check.shared && (e = readdir(d))) {
				if (e->d_name[0] != '.') cutFound(e->d_name, &check);
			}
			closedir(d);
		}
	}
	if (!check.shared) tagNames(card, tag, cutFound, &check);
	game->use_cut = !check.shared;
	return 0;
}

static int compareSlots(const void* a, const void* b) {
	return ((const SMSlot*)a)->slot - ((const SMSlot*)b)->slot;
}

static void newestOf(SMGame* game) {
	game->newest = 0;
	for (int i = 0; i < game->count; i++) {
		if (game->slots[i].saved > game->newest) game->newest = game->slots[i].saved;
	}
}

int SM_gameScan(const SMCard* card, SMGame* game) {
	game->count = 0;
	game->newest = 0;
	Folders folders;
	coreFolders(card, game->tag, &folders);
	char minui[SM_PATH_MAX];
	int has_minui = minuiFolder(card, game->tag, minui, sizeof(minui));
	for (int slot = 0; slot < SM_SLOTS; slot++) {
		char files[SM_MAX_FILES][SM_PATH_MAX];
		time_t saved = 0;
		int n = stateFilesIn(game, &folders, slot, files, SM_MAX_FILES, &saved);
		if (n <= 0) continue;
		SMSlot* s = &game->slots[game->count++];
		memset(s, 0, sizeof(*s));
		s->slot = slot;
		s->saved = saved;
		s->files = n;
		if (has_minui && snprintf(s->preview, sizeof(s->preview), "%s/%s.%d.bmp", minui, game->name, slot)
				< (int)sizeof(s->preview)) {
			s->has_preview = isFile(s->preview, NULL);
		}
		if (!s->has_preview) s->preview[0] = '\0';
	}
	qsort(game->slots, (size_t)game->count, sizeof(SMSlot), compareSlots);
	newestOf(game);
	return game->count;
}

// the slot a last-slot file names, or -1 when it can't be read
static int readSlot(const char* path) {
	if (!isFile(path, NULL)) return -1;
	int fd = open(path, O_RDONLY | O_NOFOLLOW);
	if (fd < 0) return -1;
	char text[32];
	ssize_t got = read(fd, text, sizeof(text) - 1);
	close(fd);
	if (got < 0) return -1;
	text[got] = '\0';
	return SM_slotOf(text);
}

static int removeFile(const char* path) {
	return isFile(path, NULL) && unlink(path) == 0;
}

int SM_remove(const SMCard* card, SMGame* game, int index) {
	if (index < 0 || index >= game->count) return -1;
	int slot = game->slots[index].slot;
	char files[SM_MAX_FILES][SM_PATH_MAX];
	int n = SM_stateFiles(card, game, slot, files, SM_MAX_FILES);
	int removed = 0;
	for (int i = 0; i < n; i++) removed += removeFile(files[i]);
	if (!removed) return -1;
	char minui[SM_PATH_MAX];
	if (minuiFolder(card, game->tag, minui, sizeof(minui))) {
		char path[SM_PATH_MAX];
		if (snprintf(path, sizeof(path), "%s/%s.%d.bmp", minui, game->name, slot) < (int)sizeof(path)) {
			removed += removeFile(path);
		}
		if (snprintf(path, sizeof(path), "%s/%s.%d.txt", minui, game->name, slot) < (int)sizeof(path)) {
			removed += removeFile(path);
		}
		if (snprintf(path, sizeof(path), "%s/%s.txt", minui, game->name) < (int)sizeof(path) && readSlot(path) == slot) {
			removed += removeFile(path);
		}
	}
	memmove(&game->slots[index], &game->slots[index + 1], (size_t)(game->count - index - 1) * sizeof(SMSlot));
	game->count--;
	newestOf(game);
	return removed;
}

///////////////////////////////////////
// every game with states

void SM_listInit(SMList* list) {
	memset(list, 0, sizeof(*list));
}

void SM_listFree(SMList* list) {
	free(list->games);
	SM_listInit(list);
}

void SM_listDrop(SMList* list, int index) {
	if (index < 0 || index >= list->count) return;
	memmove(&list->games[index], &list->games[index + 1], (size_t)(list->count - index - 1) * sizeof(SMGame));
	list->count--;
}

// A game name found under a tag, before it's scanned.
typedef struct Candidate {
	char tag[SM_NAME_MAX];
	char name[SM_NAME_MAX];
	char cut[SM_NAME_MAX];
} Candidate;

typedef struct Candidates {
	Candidate* items;
	int count;
	int cap;
	const char* tag;
	int start; // where the tag's names start
} Candidates;

// at most, counting a name again for each of its files until compacted
#define MAX_CANDIDATES (SM_MAX_GAMES * 4)

static int compareCandidates(const void* a, const void* b);

// sorts the tag's names and drops the same name found again
static void compact(Candidates* c) {
	int n = c->count - c->start;
	if (n < 2) return;
	Candidate* seg = c->items + c->start;
	qsort(seg, (size_t)n, sizeof(Candidate), compareCandidates);
	int kept = 1;
	for (int i = 1; i < n; i++) {
		if (compareCandidates(&seg[i], &seg[kept - 1]) != 0) seg[kept++] = seg[i];
	}
	c->count = c->start + kept;
}

static void candidateFound(const char* name, void* ctx) {
	Candidates* c = ctx;
	if (!plainName(name)) return;
	if (c->count >= MAX_CANDIDATES) compact(c);
	if (c->count >= MAX_CANDIDATES) return;
	if (c->count == c->cap) {
		int cap = c->cap ? c->cap * 2 : 64;
		Candidate* grown = realloc(c->items, (size_t)cap * sizeof(Candidate));
		if (!grown) return;
		c->items = grown;
		c->cap = cap;
	}
	Candidate* it = &c->items[c->count++];
	snprintf(it->tag, sizeof(it->tag), "%s", c->tag);
	snprintf(it->name, sizeof(it->name), "%s", name);
	SM_cut(name, it->cut, sizeof(it->cut));
}

// by tag, then cut, then name, so names cutting the same sit together
static int compareCandidates(const void* a, const void* b) {
	const Candidate* x = a;
	const Candidate* y = b;
	int c = strcmp(x->tag, y->tag);
	if (!c) c = strcmp(x->cut, y->cut);
	return c ? c : strcmp(x->name, y->name);
}

// newest first, then by name
static int compareGames(const void* a, const void* b) {
	const SMGame* x = a;
	const SMGame* y = b;
	if (x->newest != y->newest) return x->newest > y->newest ? -1 : 1;
	int c = strcmp(x->display, y->display);
	return c ? c : strcmp(x->tag, y->tag);
}

typedef struct Tags {
	char names[256][SM_NAME_MAX];
	int count;
} Tags;

static void tagAdd(Tags* tags, const char* tag) {
	if (!plainName(tag) || tags->count >= 256) return;
	for (int i = 0; i < tags->count; i++) {
		if (strcmp(tags->names[i], tag) == 0) return;
	}
	snprintf(tags->names[tags->count++], SM_NAME_MAX, "%s", tag);
}

// the tags with a .minui folder or a <tag>-<core> folder
static void listTags(const SMCard* card, Tags* tags) {
	tags->count = 0;
	if (!sharedReal(card)) return;
	DIR* d = opendir(card->shared);
	if (d) {
		struct dirent* e;
		while ((e = readdir(d))) {
			if (e->d_name[0] == '.') continue;
			char* dash = strrchr(e->d_name, '-');
			if (!dash || dash == e->d_name || !dash[1]) continue;
			char tag[SM_NAME_MAX];
			snprintf(tag, sizeof(tag), "%.*s", (int)(dash - e->d_name), e->d_name);
			tagAdd(tags, tag);
		}
		closedir(d);
	}
	char minui[SM_PATH_MAX];
	snprintf(minui, sizeof(minui), "%s/.minui", card->shared);
	if (!isFolder(minui)) return;
	d = opendir(minui);
	if (!d) return;
	struct dirent* e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		char path[SM_PATH_MAX];
		if (snprintf(path, sizeof(path), "%s/%s", minui, e->d_name) < (int)sizeof(path) && isFolder(path)) {
			tagAdd(tags, e->d_name);
		}
	}
	closedir(d);
}

// Lists into list, stopping at the first game when first is 1.
static int scan(const SMCard* card, SMList* list, int first) {
	list->count = 0;
	Tags* tags = malloc(sizeof(Tags));
	if (!tags) return 0;
	listTags(card, tags);
	Candidates c = { 0 };
	for (int t = 0; t < tags->count; t++) {
		c.tag = tags->names[t];
		c.start = c.count;
		tagNames(card, tags->names[t], candidateFound, &c);
		compact(&c);
	}
	free(tags);
	if (c.count > 1) qsort(c.items, (size_t)c.count, sizeof(Candidate), compareCandidates);
	SMGame* game = malloc(sizeof(SMGame));
	for (int i = 0; game && i < c.count; i++) {
		const Candidate* it = &c.items[i];
		if (i > 0 && compareCandidates(it, &c.items[i - 1]) == 0) continue; // the same name again
		gameSetup(it->tag, it->name, NULL, game);
		// another name cutting the same, before or after
		int shared = 0;
		for (int j = i - 1; j >= 0 && !strcmp(c.items[j].tag, it->tag) && !strcmp(c.items[j].cut, it->cut); j--) {
			if (strcmp(c.items[j].name, it->name) != 0) shared = 1;
		}
		for (int j = i + 1; j < c.count && !strcmp(c.items[j].tag, it->tag) && !strcmp(c.items[j].cut, it->cut); j++) {
			if (strcmp(c.items[j].name, it->name) != 0) shared = 1;
		}
		game->use_cut = !shared;
		if (SM_gameScan(card, game) <= 0) continue;
		if (list->count == list->cap) {
			if (list->cap >= SM_MAX_GAMES) {
				// full: keep the newest, putting this one over the oldest
				int oldest = 0;
				for (int k = 1; k < list->count; k++) {
					if (compareGames(&list->games[k], &list->games[oldest]) > 0) oldest = k;
				}
				if (compareGames(game, &list->games[oldest]) < 0) list->games[oldest] = *game;
				continue;
			}
			int cap = list->cap ? list->cap * 2 : 16;
			if (cap > SM_MAX_GAMES) cap = SM_MAX_GAMES;
			SMGame* grown = realloc(list->games, (size_t)cap * sizeof(SMGame));
			if (!grown) break;
			list->games = grown;
			list->cap = cap;
		}
		list->games[list->count++] = *game;
		if (first) break;
	}
	free(game);
	free(c.items);
	if (list->count > 1) qsort(list->games, (size_t)list->count, sizeof(SMGame), compareGames);
	return list->count;
}

int SM_scanAll(const SMCard* card, SMList* list) {
	return scan(card, list, 0);
}

int SM_any(const SMCard* card) {
	SMList list;
	SM_listInit(&list);
	int found = scan(card, &list, 1) > 0;
	SM_listFree(&list);
	return found;
}

///////////////////////////////////////
// moving

int SM_step(int selected, int count, int by, int fresh) {
	if (count <= 1 || selected < 0 || selected >= count || by == 0) return selected;
	if (by < 0) {
		if (selected > 0) return selected + by > 0 ? selected + by : 0;
		return fresh ? count - 1 : selected;
	}
	if (selected < count - 1) return selected + by < count - 1 ? selected + by : count - 1;
	return fresh ? 0 : selected;
}

int SM_row(int selected, int count, int cols, int dy) {
	if (count <= 0 || cols <= 0 || selected < 0 || selected >= count) return selected;
	if (dy < 0) return selected - cols >= 0 ? selected - cols : selected;
	if (dy > 0) {
		if (selected / cols >= (count - 1) / cols) return selected;
		return selected + cols < count ? selected + cols : count - 1;
	}
	return selected;
}

int SM_afterRemove(int selected, int count) {
	if (count <= 0 || selected < 0) return 0;
	return selected < count ? selected : count - 1;
}

int SM_firstRow(int first, int selected, int count, int cols, int rows) {
	if (count <= 0 || cols <= 0 || rows <= 0) return 0;
	if (selected < 0) selected = 0;
	if (selected >= count) selected = count - 1;
	int row = selected / cols;
	if (row < first) first = row;
	if (row >= first + rows) first = row - rows + 1;
	int total = (count + cols - 1) / cols;
	if (first > total - rows) first = total - rows;
	return first < 0 ? 0 : first;
}

void SM_hints(int view, int count, int can_delete, MosaicHint left[2], int* left_count, MosaicHint right[2],
		int* right_count) {
	*left_count = 0;
	*right_count = 0;
	if (count <= 0) {
		right[0] = (MosaicHint){ MOSAIC_BUTTON_B, "BACK" };
		*right_count = 1;
		return;
	}
	left[0] = (MosaicHint){ MOSAIC_BUTTON_B, "BACK" };
	*left_count = 1;
	if (view == SM_VIEW_GAMES) {
		right[(*right_count)++] = (MosaicHint){ MOSAIC_BUTTON_A, "OPEN" };
		return;
	}
	if (can_delete) right[(*right_count)++] = (MosaicHint){ MOSAIC_BUTTON_Y, "DELETE" };
	if (view == SM_VIEW_SLOTS) right[(*right_count)++] = (MosaicHint){ MOSAIC_BUTTON_A, "VIEW" };
}

///////////////////////////////////////
// layout

static int empty(const MosaicRect* r) {
	return !r || r->w <= 0 || r->h <= 0;
}

int SM_avoid(const MosaicRect* r, const MosaicRect* hole, MosaicRect out[4]) {
	if (empty(r)) return 0;
	int x1 = r->x, y1 = r->y, x2 = r->x + r->w, y2 = r->y + r->h;
	if (empty(hole) || hole->x >= x2 || hole->y >= y2 || hole->x + hole->w <= x1 || hole->y + hole->h <= y1) {
		out[0] = *r;
		return 1;
	}
	int hx1 = hole->x > x1 ? hole->x : x1, hy1 = hole->y > y1 ? hole->y : y1;
	int hx2 = hole->x + hole->w < x2 ? hole->x + hole->w : x2, hy2 = hole->y + hole->h < y2 ? hole->y + hole->h : y2;
	int n = 0;
	if (hy1 > y1) out[n++] = (MosaicRect){ x1, y1, r->w, hy1 - y1 };
	if (hx1 > x1) out[n++] = (MosaicRect){ x1, hy1, hx1 - x1, hy2 - hy1 };
	if (hx2 < x2) out[n++] = (MosaicRect){ hx2, hy1, x2 - hx2, hy2 - hy1 };
	if (hy2 < y2) out[n++] = (MosaicRect){ x1, hy2, r->w, y2 - hy2 };
	return n;
}

MosaicRect SM_hole(const MosaicRect* status, const MosaicRect* header) {
	MosaicRect none = { 0, 0, 0, 0 };
	int has_status = !empty(status), has_header = !empty(header);
	if (!has_status && !has_header) return none;
	if (!has_header) return *status;
	if (!has_status) return *header;
	int x1 = status->x < header->x ? status->x : header->x;
	int y1 = status->y < header->y ? status->y : header->y;
	int x2 = status->x + status->w > header->x + header->w ? status->x + status->w : header->x + header->w;
	int y2 = status->y + status->h > header->y + header->h ? status->y + status->h : header->y + header->h;
	return (MosaicRect){ x1, y1, x2 - x1, y2 - y1 };
}

MosaicRect SM_fit(int src_w, int src_h, const MosaicRect* box) {
	MosaicRect none = { 0, 0, 0, 0 };
	if (src_w <= 0 || src_h <= 0 || empty(box)) return none;
	int w = box->w, h = (int)((long long)src_h * box->w / src_w);
	if (h > box->h) {
		h = box->h;
		w = (int)((long long)src_w * box->h / src_h);
	}
	if (w < 1) w = 1;
	if (h < 1) h = 1;
	return (MosaicRect){ box->x + (box->w - w) / 2, box->y + (box->h - h) / 2, w, h };
}

MosaicRect SM_source(const MosaicRect* dst, const MosaicRect* part, int src_w, int src_h) {
	MosaicRect none = { 0, 0, 0, 0 };
	if (empty(dst) || empty(part)) return none;
	int x1 = (int)((long long)(part->x - dst->x) * src_w / dst->w);
	int y1 = (int)((long long)(part->y - dst->y) * src_h / dst->h);
	int x2 = (int)((long long)(part->x + part->w - dst->x) * src_w / dst->w);
	int y2 = (int)((long long)(part->y + part->h - dst->y) * src_h / dst->h);
	if (x2 <= x1) x2 = x1 + 1;
	if (y2 <= y1) y2 = y1 + 1;
	return (MosaicRect){ x1, y1, x2 - x1, y2 - y1 };
}

MosaicRect SM_titlePill(const MosaicRect* area, const MosaicRect* hole, int scale, int text_w) {
	MosaicRect none = { 0, 0, 0, 0 };
	int pad = PADDING * scale, inner = BUTTON_PADDING * scale;
	int x = area->x + pad, y = area->y + pad, h = PILL_SIZE * scale;
	int right = area->x + area->w - pad;
	if (!empty(hole) && hole->y < y + h && hole->y + hole->h > y && hole->x - pad < right) right = hole->x - pad;
	int w = text_w + 2 * inner;
	if (w > right - x) w = right - x;
	if (text_w <= 0 || w <= 2 * inner) return none;
	return (MosaicRect){ x, y, w, h };
}

int SM_grid(const MosaicRect* area, const MosaicRect* hole, int scale, int aspect_w, int aspect_h, int lines_h,
		SMGrid* out) {
	memset(out, 0, sizeof(*out));
	if (empty(area) || scale < 1 || aspect_w <= 0 || aspect_h <= 0 || lines_h < 0) return -1;
	int pad = PADDING * scale, pill = PILL_SIZE * scale;
	int top = area->y + pad + pill + pad;
	if (!empty(hole) && hole->y + hole->h + pad > top) top = hole->y + hole->h + pad;
	int bottom = area->y + area->h - pad - pill - pad;
	MosaicRect room = { area->x + pad, top, area->w - 2 * pad, bottom - top };
	if (empty(&room)) return -1;
	out->gap = pad;
	out->lines_h = lines_h;
	// the fewest columns from 3 that fit two rows
	for (int cols = 3; cols <= SM_MAX_COLS; cols++) {
		int cell_w = (room.w - pad * (cols - 1)) / cols;
		if (cell_w <= 0) break;
		int thumb_h = (int)((long long)cell_w * aspect_h / aspect_w);
		int rows = (room.h + pad) / (thumb_h + lines_h + pad);
		out->cols = cols;
		out->cell_w = cell_w;
		out->thumb_h = thumb_h;
		out->rows = rows;
		if (rows >= 2) break;
	}
	if (out->cols <= 0 || out->cell_w <= 0) return -1;
	if (out->rows < 1) {
		// one row, the previews as tall as it leaves
		out->rows = 1;
		out->thumb_h = room.h - lines_h;
		if (out->thumb_h <= 0) return -1;
	}
	out->cell_h = out->thumb_h + lines_h;
	int used_w = out->cols * out->cell_w + (out->cols - 1) * pad;
	int used_h = out->rows * out->cell_h + (out->rows - 1) * pad;
	out->room = (MosaicRect){ room.x + (room.w - used_w) / 2, room.y + (room.h - used_h) / 2, used_w, used_h };
	return 0;
}

MosaicRect SM_cell(const SMGrid* grid, int slot) {
	MosaicRect none = { 0, 0, 0, 0 };
	if (grid->cols <= 0 || slot < 0 || slot >= grid->cols * grid->rows) return none;
	int col = slot % grid->cols, row = slot / grid->cols;
	return (MosaicRect){ grid->room.x + col * (grid->cell_w + grid->gap), grid->room.y + row * (grid->cell_h + grid->gap),
		grid->cell_w, grid->cell_h };
}

///////////////////////////////////////
// scaled copies

SMCopy* SM_copyFind(SMCopy* copies, int count, const char* path, int w, int h) {
	for (int i = 0; i < count; i++) {
		SMCopy* c = &copies[i];
		if (c->surface && c->w == w && c->h == h && strcmp(c->path, path) == 0) return c;
	}
	return NULL;
}

SMCopy* SM_copyPick(SMCopy* copies, int count) {
	SMCopy* pick = NULL;
	for (int i = 0; i < count; i++) {
		SMCopy* c = &copies[i];
		if (!c->surface) return c;
		if (!pick || c->used < pick->used) pick = c;
	}
	return pick;
}
