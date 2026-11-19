#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "actions_model.h"

#define MAX_FILE_BYTES (1024 * 1024) // the most of a collection or names file read
#define MAX_DEPTH 8                  // how deep a system folder is searched or a game folder deleted

static GAWords words; // none set: English

void GA_setWords(const GAWords* set) {
	if (set) words = *set;
	else memset(&words, 0, sizeof(words));
}

// Fixed text and a count's form in the caller's words, for make pot
static const char* tr(const char* text) {
	return words.tr ? words.tr(text) : text;
}

static const char* trPlural(const char* singular, const char* plural, unsigned long n) {
	if (words.tr_plural) return words.tr_plural(singular, plural, n);
	return n == 1 ? singular : plural;
}

///////////////////////////////////////
// paths

// snprintf, through a va_list so the compiler doesn't warn about paths
// that might not fit; callers check the length where it matters.
static int fmt(char* out, size_t size, const char* format, ...) {
	va_list args;
	va_start(args, format);
	int n = vsnprintf(out, size, format, args);
	va_end(args);
	return n;
}

static int endsWithCase(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcasecmp(s + a - b, suffix);
}

static const char* baseName(const char* path) {
	const char* slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

// The folder a path is in. Returns 0 when it has none.
static int dirName(const char* path, char* out, size_t size) {
	const char* slash = strrchr(path, '/');
	if (!slash || slash == path || (size_t)(slash - path) >= size) return 0;
	memcpy(out, path, (size_t)(slash - path));
	out[slash - path] = '\0';
	return 1;
}

// True for an absolute path with no empty, "." or ".." parts and no
// trailing slash.
static int cleanPath(const char* path) {
	if (path[0] != '/' || !path[1]) return 0;
	const char* p = path;
	while (*p) {
		p++; // past a slash
		const char* end = strchr(p, '/');
		size_t len = end ? (size_t)(end - p) : strlen(p);
		if (len == 0) return 0;
		if (len == 1 && p[0] == '.') return 0;
		if (len == 2 && p[0] == '.' && p[1] == '.') return 0;
		p += len;
	}
	return 1;
}

static int isUnder(const char* root, const char* path) {
	size_t len = strlen(root);
	return !strncmp(root, path, len) && path[len] == '/';
}

// True when the folder path is in resolves, links and all, to somewhere
// inside root (itself resolved).
static int parentInside(const char* root_real, const char* path) {
	char dir[GA_PATH_LEN];
	char real[PATH_MAX];
	if (!dirName(path, dir, sizeof(dir)) || !realpath(dir, real)) return 0;
	return !strcmp(real, root_real) || isUnder(root_real, real);
}

static int resolve(const char* path, char* out) {
	char real[PATH_MAX];
	if (!realpath(path, real) || strlen(real) >= GA_PATH_LEN) return 0;
	strcpy(out, real);
	return 1;
}

static int isFile(const char* path) {
	struct stat st;
	return lstat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int isRealDir(const char* path) {
	struct stat st;
	return lstat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// <dir>/<dir name><ext> as a regular file.
static int namedAfter(const char* dir, const char* ext, char* out, size_t size) {
	int n = fmt(out, size, "%s/%s%s", dir, baseName(dir), ext);
	return n > 0 && (size_t)n < size && isFile(out);
}

static int isGameFolder(const char* dir) {
	char path[GA_PATH_LEN];
	return isRealDir(dir) && (namedAfter(dir, ".m3u", path, sizeof(path)) || namedAfter(dir, ".cue", path, sizeof(path)));
}

void GA_tagOf(const char* folder, char* out, size_t size) {
	const char* open = strrchr(folder, '(');
	if (!open) {
		fmt(out, size, "%s", folder);
		return;
	}
	open++;
	const char* close = strchr(open, ')');
	size_t len = close ? (size_t)(close - open) : strlen(open);
	if (len >= size) len = size - 1;
	memcpy(out, open, len);
	out[len] = '\0';
}

// The name minarch saves under with its extension cut, as it cuts it: only
// an extension of two to four characters.
static void stripped(const char* name, char* out, size_t size) {
	fmt(out, size, "%s", name);
	char* dot = strrchr(out, '.');
	if (dot && strlen(dot) > 2 && strlen(dot) <= 5) *dot = '\0';
}

// The name the launcher looks for art under: cut at the last dot.
static void artName(const char* name, char* out, size_t size) {
	fmt(out, size, "%s", name);
	char* dot = strrchr(out, '.');
	if (dot) *dot = '\0';
}

int GA_gameFor(const char* sdcard, const char* path, char* out, size_t size, int* is_dir) {
	if (!sdcard || !path || !cleanPath(path) || strlen(path) >= GA_PATH_LEN) return 0;
	char roms[GA_PATH_LEN];
	fmt(roms, sizeof(roms), "%s/Roms", sdcard);
	if (!isUnder(roms, path)) return 0;
	const char* rest = path + strlen(roms) + 1;
	const char* item = strchr(rest, '/');
	if (!item || item == rest || !item[1]) return 0; // a system folder, not a game in one
	if (rest[0] == '.' || strstr(rest, "/.")) return 0; // nothing stock hides, such as .media
	if (endsWithCase(path, ".pak")) return 0;

	char roms_real[GA_PATH_LEN];
	if (!resolve(roms, roms_real)) return 0;
	char dir[GA_PATH_LEN];
	char dir_real[PATH_MAX];
	if (!dirName(path, dir, sizeof(dir)) || !realpath(dir, dir_real) || !isUnder(roms_real, dir_real)) return 0;

	struct stat st;
	if (lstat(path, &st) != 0) return 0;
	int in_system = strchr(item + 1, '/') == NULL; // directly in Roms/<system>/
	if (S_ISDIR(st.st_mode)) {
		if (!isGameFolder(path)) return 0;
		fmt(out, size, "%s", path);
		*is_dir = 1;
		return 1;
	}
	if (!S_ISREG(st.st_mode) && !S_ISLNK(st.st_mode)) return 0;
	if (!in_system && isGameFolder(dir)) {
		fmt(out, size, "%s", dir);
		*is_dir = 1;
		return 1;
	}
	fmt(out, size, "%s", path);
	*is_dir = 0;
	return 1;
}

///////////////////////////////////////
// string sets, for names other games use

typedef struct Names {
	char** items;
	int count;
	int capacity;
} Names;

static void namesFree(Names* set) {
	for (int i = 0; i < set->count; i++) free(set->items[i]);
	free(set->items);
	memset(set, 0, sizeof(*set));
}

static int namesAdd(Names* set, const char* name) {
	if (set->count == set->capacity) {
		int capacity = set->capacity ? set->capacity * 2 : 64;
		char** items = realloc(set->items, sizeof(char*) * (size_t)capacity);
		if (!items) return -1;
		set->items = items;
		set->capacity = capacity;
	}
	set->items[set->count] = strdup(name);
	if (!set->items[set->count]) return -1;
	set->count++;
	return 0;
}

static int namesHas(const Names* set, const char* name) {
	for (int i = 0; i < set->count; i++) {
		if (!strcmp(set->items[i], name)) return 1;
	}
	return 0;
}

// Every file name under dir, except the paths in skip and anything in
// them, and the names with minarch's extension cut.
static int collectNames(const char* dir, const Names* skip, int depth, Names* full, Names* cut) {
	if (depth > MAX_DEPTH) return 0;
	DIR* dh = opendir(dir);
	if (!dh) return 0;
	struct dirent* dp;
	int result = 0;
	while (!result && (dp = readdir(dh)) != NULL) {
		if (dp->d_name[0] == '.') continue;
		char path[GA_PATH_LEN];
		int n = fmt(path, sizeof(path), "%s/%s", dir, dp->d_name);
		if (n < 0 || (size_t)n >= sizeof(path)) continue;
		if (namesHas(skip, path)) continue;
		struct stat st;
		if (lstat(path, &st) != 0) continue;
		if (S_ISDIR(st.st_mode)) {
			result = collectNames(path, skip, depth + 1, full, cut);
			continue;
		}
		char short_name[GA_PATH_LEN];
		stripped(dp->d_name, short_name, sizeof(short_name));
		if (namesAdd(full, dp->d_name) != 0 || namesAdd(cut, short_name) != 0) result = -1;
	}
	closedir(dh);
	return result;
}

// Names used by every other game in the system tag, across every Roms/
// folder stock gives that tag.
static int systemNames(const char* roms, const char* tag, const Names* skip, Names* full, Names* cut) {
	DIR* dh = opendir(roms);
	if (!dh) return 0;
	struct dirent* dp;
	int result = 0;
	while (!result && (dp = readdir(dh)) != NULL) {
		if (dp->d_name[0] == '.') continue;
		char folder_tag[GA_PATH_LEN];
		GA_tagOf(dp->d_name, folder_tag, sizeof(folder_tag));
		if (strcmp(folder_tag, tag) != 0) continue;
		char path[GA_PATH_LEN];
		int n = fmt(path, sizeof(path), "%s/%s", roms, dp->d_name);
		if (n < 0 || (size_t)n >= sizeof(path) || !isRealDir(path)) continue;
		result = collectNames(path, skip, 0, full, cut);
	}
	closedir(dh);
	return result;
}

///////////////////////////////////////
// files a .cue or .m3u lists

// The plain file names (no folders) a .cue's FILE lines or a .m3u's lines
// name, added to refs.
static int readRefs(const char* list, Names* refs) {
	int is_cue = endsWithCase(list, ".cue");
	FILE* f = fopen(list, "r");
	if (!f) return 0;
	char line[GA_PATH_LEN];
	int result = 0;
	while (!result && fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = '\0';
		char* name = line;
		while (*name == ' ' || *name == '\t') name++;
		if (is_cue) {
			if (strncasecmp(name, "FILE", 4) != 0 || (name[4] != ' ' && name[4] != '\t')) continue;
			name += 5;
			while (*name == ' ' || *name == '\t') name++;
			if (*name == '"') {
				name++;
				char* close = strchr(name, '"');
				if (!close) continue;
				*close = '\0';
			}
			else {
				char* space = strpbrk(name, " \t");
				if (space) *space = '\0';
			}
		}
		else if (*name == '#') continue;
		if (!name[0] || strchr(name, '/') || !strcmp(name, ".") || !strcmp(name, "..")) continue;
		if (!namesHas(refs, name)) result = namesAdd(refs, name);
	}
	fclose(f);
	return result;
}

// What a loose .cue or .m3u lists beside it, and what a .cue it lists
// lists in turn.
static int listedFiles(const char* list, Names* refs) {
	if (readRefs(list, refs) != 0) return -1;
	if (!endsWithCase(list, ".m3u")) return 0;
	char dir[GA_PATH_LEN];
	if (!dirName(list, dir, sizeof(dir))) return 0;
	int listed = refs->count;
	for (int i = 0; i < listed; i++) {
		if (!endsWithCase(refs->items[i], ".cue")) continue;
		char cue[GA_PATH_LEN];
		int n = fmt(cue, sizeof(cue), "%s/%s", dir, refs->items[i]);
		if (n > 0 && (size_t)n < sizeof(cue) && isFile(cue) && readRefs(cue, refs) != 0) return -1;
	}
	return 0;
}

// Files the other .cue and .m3u in dir list, so the game doesn't take a
// file it shares.
static int othersListed(const char* dir, const char* skip, Names* refs) {
	DIR* dh = opendir(dir);
	if (!dh) return 0;
	struct dirent* dp;
	int result = 0;
	while (!result && (dp = readdir(dh)) != NULL) {
		if (!endsWithCase(dp->d_name, ".cue") && !endsWithCase(dp->d_name, ".m3u")) continue;
		char path[GA_PATH_LEN];
		int n = fmt(path, sizeof(path), "%s/%s", dir, dp->d_name);
		if (n < 0 || (size_t)n >= sizeof(path) || !strcmp(path, skip) || !isFile(path)) continue;
		result = readRefs(path, refs);
	}
	closedir(dh);
	return result;
}

///////////////////////////////////////
// the plan

void GA_planInit(GAPlan* plan) {
	memset(plan, 0, sizeof(*plan));
}

void GA_planFree(GAPlan* plan) {
	free(plan->items);
	GA_planInit(plan);
}

static int planHas(const GAPlan* plan, const char* path) {
	for (int i = 0; i < plan->count; i++) {
		if (!strcmp(plan->items[i].path, path)) return 1;
	}
	return 0;
}

static int planPush(GAPlan* plan, const char* path, int kind, int is_dir) {
	if (planHas(plan, path)) return 0;
	if (plan->count == plan->capacity) {
		int capacity = plan->capacity ? plan->capacity * 2 : 16;
		GAItem* items = realloc(plan->items, sizeof(GAItem) * (size_t)capacity);
		if (!items) return -1;
		plan->items = items;
		plan->capacity = capacity;
	}
	GAItem* item = &plan->items[plan->count++];
	fmt(item->path, sizeof(item->path), "%s", path);
	item->kind = kind;
	item->is_dir = is_dir;
	plan->counts[kind]++;
	return 0;
}

// Adds <dir>/<name> when it's a file or link (never a folder) whose folder
// is inside the card.
static int addSidecar(GAPlan* plan, const char* sd_real, const char* dir, const char* name, int kind) {
	char path[GA_PATH_LEN];
	int n = fmt(path, sizeof(path), "%s/%s", dir, name);
	if (n < 0 || (size_t)n >= sizeof(path)) return 0;
	struct stat st;
	if (lstat(path, &st) != 0 || !(S_ISREG(st.st_mode) || S_ISLNK(st.st_mode))) return 0;
	if (!parentInside(sd_real, path)) return 0;
	return planPush(plan, path, kind, 0);
}

// The folders in parent named <tag>-<something>, such as an emulator's
// states or settings folder, GB-gambatte.
static int tagFolders(const char* parent, const char* tag, Names* out) {
	DIR* dh = opendir(parent);
	if (!dh) return 0;
	size_t len = strlen(tag);
	struct dirent* dp;
	int result = 0;
	while (!result && (dp = readdir(dh)) != NULL) {
		if (strncmp(dp->d_name, tag, len) != 0 || dp->d_name[len] != '-' || !dp->d_name[len + 1]) continue;
		char path[GA_PATH_LEN];
		int n = fmt(path, sizeof(path), "%s/%s", parent, dp->d_name);
		if (n < 0 || (size_t)n >= sizeof(path) || !isRealDir(path)) continue;
		result = namesAdd(out, path);
	}
	closedir(dh);
	return result;
}

// Where stock keeps a game's saves, states and settings: each is in dir,
// named after the file minarch launched, whole (cut 0) or with its
// extension cut (cut 1), then suffix. Calls fn for each until one returns
// nonzero, and returns that, or -1 when memory ran out.
typedef int (*SidecarFn)(const char* dir, int cut, const char* suffix, int kind, void* ctx);

static int eachSidecar(const GACard* card, const char* tag, SidecarFn fn, void* ctx) {
	char saves[GA_PATH_LEN];
	char shared[GA_PATH_LEN];
	char slots[GA_PATH_LEN];
	char suffix[32];
	fmt(saves, sizeof(saves), "%s/Saves/%s", card->sdcard, tag);
	fmt(shared, sizeof(shared), "%s/.userdata/shared", card->sdcard);
	fmt(slots, sizeof(slots), "%s/.minui/%s", shared, tag);

	Names states = {0};
	Names configs = {0};
	int result = tagFolders(shared, tag, &states);
	if (!result && card->userdata) result = tagFolders(card->userdata, tag, &configs);

	// under the whole file name
	if (!result) result = fn(saves, 0, ".sav", GA_SAVE, ctx);
	if (!result) result = fn(saves, 0, ".rtc", GA_SAVE, ctx);
	for (int s = 0; !result && s < states.count; s++) {
		for (int slot = 0; !result && slot <= 9; slot++) {
			fmt(suffix, sizeof(suffix), ".st%i", slot);
			result = fn(states.items[s], 0, suffix, GA_STATE, ctx);
		}
	}
	if (!result) result = fn(slots, 0, ".txt", GA_SLOT, ctx);
	for (int slot = 0; !result && slot <= 9; slot++) {
		fmt(suffix, sizeof(suffix), ".%i.bmp", slot);
		result = fn(slots, 0, suffix, GA_SLOT, ctx);
		fmt(suffix, sizeof(suffix), ".%i.txt", slot);
		if (!result) result = fn(slots, 0, suffix, GA_SLOT, ctx);
	}
	for (int c = 0; !result && c < configs.count; c++) result = fn(configs.items[c], 0, ".cfg", GA_CONFIG, ctx);

	// under the name with its extension cut, for the .srm and .state formats
	if (!result) result = fn(saves, 1, ".sav", GA_SAVE, ctx);
	if (!result) result = fn(saves, 1, ".srm", GA_SAVE, ctx);
	for (int s = 0; !result && s < states.count; s++) {
		result = fn(states.items[s], 1, ".state", GA_STATE, ctx);
		if (!result) result = fn(states.items[s], 1, ".state.auto", GA_STATE, ctx);
		for (int slot = 0; !result && slot <= 8; slot++) {
			if (slot) {
				fmt(suffix, sizeof(suffix), ".state%i", slot);
				result = fn(states.items[s], 1, suffix, GA_STATE, ctx);
			}
			fmt(suffix, sizeof(suffix), ".state.%i", slot);
			if (!result) result = fn(states.items[s], 1, suffix, GA_STATE, ctx);
		}
	}

	namesFree(&states);
	namesFree(&configs);
	return result;
}

typedef struct DeleteSidecars {
	GAPlan* plan;
	const char* sd_real;
	const char* name;
	char cut[GA_PATH_LEN];
	int skip_full; // another game has the whole name
	int skip_cut;  // or cuts to the same name
} DeleteSidecars;

static int deleteSidecar(const char* dir, int cut, const char* suffix, int kind, void* ctx) {
	DeleteSidecars* d = ctx;
	if (cut ? d->skip_cut : d->skip_full) return 0;
	char file[GA_PATH_LEN];
	fmt(file, sizeof(file), "%s%s", cut ? d->cut : d->name, suffix);
	return addSidecar(d->plan, d->sd_real, dir, file, kind);
}

// Saves, states and settings stored under name, which minarch takes from
// the file it launched, unless another game has that name, or cuts to the
// same name for the formats that cut it.
static int addSaves(GAPlan* plan, const GACard* card, const char* sd_real, const char* tag,
		const char* name, const Names* other_full, const Names* other_cut) {
	if (strlen(name) > 255) return 0; // longer than a file name can be, so no sidecar has it
	DeleteSidecars d = { plan, sd_real, name, "", namesHas(other_full, name), 0 };
	stripped(name, d.cut, sizeof(d.cut));
	d.skip_cut = namesHas(other_cut, d.cut);
	return eachSidecar(card, tag, deleteSidecar, &d) ? -1 : 0;
}

// <dir>/.media/<art name>.png, unless another entry in dir (other than
// skip and the files being deleted) has the same art name.
static int addArt(GAPlan* plan, const char* sd_real, const char* dir, const char* name, const Names* going) {
	char art[GA_PATH_LEN];
	artName(name, art, sizeof(art));
	if (!art[0] || strlen(art) > 250) return 0;
	DIR* dh = opendir(dir);
	if (!dh) return 0;
	struct dirent* dp;
	int shared = 0;
	while (!shared && (dp = readdir(dh)) != NULL) {
		if (dp->d_name[0] == '.' || !strcmp(dp->d_name, name) || namesHas(going, dp->d_name)) continue;
		char other[GA_PATH_LEN];
		artName(dp->d_name, other, sizeof(other));
		if (!strcmp(other, art)) shared = 1;
	}
	closedir(dh);
	if (shared) return 0;
	char media[GA_PATH_LEN];
	char file[GA_PATH_LEN];
	fmt(media, sizeof(media), "%s/.media", dir);
	if (!isRealDir(media)) return 0;
	fmt(file, sizeof(file), "%s.png", art);
	return addSidecar(plan, sd_real, media, file, GA_ART);
}

int GA_planDelete(const GACard* card, const char* path, GAPlan* plan) {
	GA_planFree(plan);
	char target[GA_PATH_LEN];
	int is_dir = 0;
	if (!card || !card->sdcard || !GA_gameFor(card->sdcard, path, target, sizeof(target), &is_dir)) return -1;
	char sd_real[GA_PATH_LEN];
	if (!resolve(card->sdcard, sd_real)) return -1;
	if (planPush(plan, target, GA_GAME, is_dir) != 0) return -1;
	fmt(plan->name, sizeof(plan->name), "%s", baseName(target));

	char roms[GA_PATH_LEN];
	fmt(roms, sizeof(roms), "%s/Roms", card->sdcard);
	char folder[GA_PATH_LEN];
	const char* rest = target + strlen(roms) + 1;
	fmt(folder, sizeof(folder), "%.*s", (int)(strchr(rest, '/') - rest), rest);
	char tag[GA_PATH_LEN];
	GA_tagOf(folder, tag, sizeof(tag));

	char dir[GA_PATH_LEN];
	dirName(target, dir, sizeof(dir));

	Names going = {0};      // files deleted beside the game
	Names launched = {0};   // the names minarch saves the game under
	Names other_full = {0}; // file names other games in the system have
	Names other_cut = {0};  // and with the extension cut
	Names shared = {0};     // files other lists beside the game name
	int result = 0;

	if (is_dir) {
		char list[GA_PATH_LEN];
		if (namedAfter(target, ".m3u", list, sizeof(list))) result |= namesAdd(&launched, baseName(list));
		if (namedAfter(target, ".cue", list, sizeof(list))) result |= namesAdd(&launched, baseName(list));
	}
	else {
		result |= namesAdd(&launched, plan->name);
		if (endsWithCase(target, ".cue") || endsWithCase(target, ".m3u")) {
			Names listed = {0};
			result |= listedFiles(target, &listed);
			result |= othersListed(dir, target, &shared);
			for (int i = 0; !result && i < listed.count; i++) {
				if (namesHas(&shared, listed.items[i]) || !strcmp(listed.items[i], plan->name)) continue;
				int before = plan->count;
				result |= addSidecar(plan, sd_real, dir, listed.items[i], GA_DISC);
				if (plan->count > before) result |= namesAdd(&going, listed.items[i]);
			}
			namesFree(&listed);
		}
	}

	if (!result) {
		char parent[GA_PATH_LEN];
		if (is_dir) dirName(target, parent, sizeof(parent));
		else fmt(parent, sizeof(parent), "%s", dir);
		result |= addArt(plan, sd_real, parent, plan->name, &going);
	}

	if (!result && card->with_saves && tag[0]) {
		// the game and the discs going with it aren't other games
		Names skip = {0};
		for (int i = 0; !result && i < plan->count; i++) result |= namesAdd(&skip, plan->items[i].path);
		if (!result) result |= systemNames(roms, tag, &skip, &other_full, &other_cut);
		namesFree(&skip);
		for (int i = 0; !result && i < launched.count; i++) {
			result |= addSaves(plan, card, sd_real, tag, launched.items[i], &other_full, &other_cut);
		}
	}

	namesFree(&going);
	namesFree(&launched);
	namesFree(&other_full);
	namesFree(&other_cut);
	namesFree(&shared);
	if (result) {
		GA_planFree(plan);
		return -1;
	}
	return 0;
}

void GA_planSummary(const GAPlan* plan, char* out, size_t size) {
	char parts[6][128];
	int n = 0;
	int count = plan->counts[GA_DISC];
	if (count) fmt(parts[n++], sizeof(parts[0]), trPlural("%i disc file", "%i disc files", (unsigned long)count), count);
	count = plan->counts[GA_SAVE];
	if (count) fmt(parts[n++], sizeof(parts[0]), trPlural("%i save", "%i saves", (unsigned long)count), count);
	count = plan->counts[GA_STATE];
	if (count) fmt(parts[n++], sizeof(parts[0]), trPlural("%i state", "%i states", (unsigned long)count), count);
	else if (plan->counts[GA_SLOT]) fmt(parts[n++], sizeof(parts[0]), "%s", tr("its resume files"));
	if (plan->counts[GA_ART]) fmt(parts[n++], sizeof(parts[0]), "%s", tr("its art"));
	if (plan->counts[GA_CONFIG]) fmt(parts[n++], sizeof(parts[0]), "%s", tr("its settings"));
	if (size) out[0] = '\0';
	// "a, b and c": the parts but the last two joined by ", ", and those two by "%s and %s"
	for (int i = 0; i < n; i++) {
		size_t len = strlen(out);
		if (i == 0) fmt(out, size, "%s", parts[i]);
		else if (i < n - 1) fmt(out + len, size - len, ", %s", parts[i]);
		else {
			char head[512];
			fmt(head, sizeof(head), "%s", out);
			fmt(out, size, tr("%s and %s"), head, parts[i]);
		}
	}
}

// Empties and removes a real folder without following links: a link inside
// is removed, not what it points at.
static int removeTree(const char* dir, int depth) {
	if (depth > MAX_DEPTH) return -1;
	DIR* dh = opendir(dir);
	if (!dh) return -1;
	struct dirent* dp;
	int result = 0;
	while ((dp = readdir(dh)) != NULL) {
		if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..")) continue;
		char path[GA_PATH_LEN];
		int n = fmt(path, sizeof(path), "%s/%s", dir, dp->d_name);
		if (n < 0 || (size_t)n >= sizeof(path)) {
			result = -1;
			continue;
		}
		struct stat st;
		if (lstat(path, &st) != 0) {
			result = -1;
			continue;
		}
		if (S_ISDIR(st.st_mode)) {
			if (removeTree(path, depth + 1) != 0) result = -1;
		}
		else if (unlink(path) != 0) result = -1;
	}
	closedir(dh);
	if (rmdir(dir) != 0) result = -1;
	return result;
}

int GA_runDelete(const GACard* card, const GAPlan* plan, int* failed) {
	int removed = 0;
	int bad = 0;
	char sd_real[GA_PATH_LEN];
	char roms_real[GA_PATH_LEN];
	char roms[GA_PATH_LEN];
	fmt(roms, sizeof(roms), "%s/Roms", card && card->sdcard ? card->sdcard : "");
	if (!card || !card->sdcard || !resolve(card->sdcard, sd_real) || !resolve(roms, roms_real)) {
		if (failed) *failed = plan->count;
		return 0;
	}
	for (int i = 0; i < plan->count; i++) {
		const GAItem* item = &plan->items[i];
		struct stat st;
		if (!cleanPath(item->path) || lstat(item->path, &st) != 0) {
			bad++;
			continue;
		}
		// the game stays inside a system folder; sidecars inside the card
		int inside;
		if (item->kind == GA_GAME) {
			char dir[GA_PATH_LEN];
			char dir_real[PATH_MAX];
			inside = dirName(item->path, dir, sizeof(dir)) && realpath(dir, dir_real) && isUnder(roms_real, dir_real);
		}
		else inside = parentInside(sd_real, item->path);
		if (!inside) {
			bad++;
			continue;
		}
		int ok;
		if (item->is_dir) ok = S_ISDIR(st.st_mode) && item->kind == GA_GAME && removeTree(item->path, 0) == 0;
		else ok = !S_ISDIR(st.st_mode) && unlink(item->path) == 0;
		if (ok) removed++;
		else bad++;
	}
	if (failed) *failed = bad;
	return removed;
}

///////////////////////////////////////
// collections

int GA_collectionPath(const char* sdcard, const char* path, char* out, size_t size) {
	if (!sdcard || !path || !cleanPath(path) || !isUnder(sdcard, path)) return 0;
	size_t sd_len = strlen(sdcard);
	char full[GA_PATH_LEN];
	char roms[GA_PATH_LEN];
	fmt(roms, sizeof(roms), "%s/Roms", sdcard);
	struct stat st;
	if (lstat(path, &st) != 0) return 0;
	// a pak anywhere, or a game in a system folder
	if (S_ISDIR(st.st_mode) && endsWithCase(path, ".pak")) {
		fmt(out, size, "%s", path + sd_len);
		return 1;
	}
	const char* rest = isUnder(roms, path) ? path + strlen(roms) + 1 : NULL;
	const char* item = rest ? strchr(rest, '/') : NULL;
	if (!item || item == rest || rest[0] == '.' || strstr(rest, "/.") || !item[1]) return 0;
	if (S_ISDIR(st.st_mode)) {
		if (!namedAfter(path, ".m3u", full, sizeof(full)) && !namedAfter(path, ".cue", full, sizeof(full))) return 0;
	}
	else {
		char dir[GA_PATH_LEN];
		int in_system = strchr(item + 1, '/') == NULL;
		if (!in_system && !endsWithCase(path, ".m3u") && dirName(path, dir, sizeof(dir))
				&& namedAfter(dir, ".m3u", full, sizeof(full))) {
			// a disc: the game's .m3u
		}
		else fmt(full, sizeof(full), "%s", path);
	}
	fmt(out, size, "%s", full + sd_len);
	return 1;
}

static int compareNames(const void* a, const void* b) {
	return strcasecmp(*(char* const*)a, *(char* const*)b);
}

int GA_collections(const char* sdcard, char** names, int max) {
	char dir[GA_PATH_LEN];
	fmt(dir, sizeof(dir), "%s/Collections", sdcard);
	DIR* dh = opendir(dir);
	if (!dh) return 0;
	struct dirent* dp;
	int count = 0;
	while (count < max && (dp = readdir(dh)) != NULL) {
		if (dp->d_name[0] == '.' || !endsWithCase(dp->d_name, ".txt") || !strcasecmp(dp->d_name, "map.txt")) continue;
		char path[GA_PATH_LEN];
		int n = fmt(path, sizeof(path), "%s/%s", dir, dp->d_name);
		if (n < 0 || (size_t)n >= sizeof(path) || !isFile(path)) continue;
		size_t len = strlen(dp->d_name) - 4;
		char* name = malloc(len + 1);
		if (!name) break;
		memcpy(name, dp->d_name, len);
		name[len] = '\0';
		names[count++] = name;
	}
	closedir(dh);
	qsort(names, (size_t)count, sizeof(char*), compareNames);
	return count;
}

int GA_validCollectionName(const char* name) {
	if (!name || !name[0] || name[0] == '.' || strlen(name) >= GA_NAME_LEN) return 0;
	if (!strcasecmp(name, "map")) return 0;
	for (const char* p = name; *p; p++) {
		if (*p == '/' || *p == '\\' || *p == '\n' || *p == '\r' || *p == '\t') return 0;
	}
	return 1;
}

// The whole file, or an empty string when it's missing. NULL on an error.
static char* readAll(const char* file, size_t* length) {
	*length = 0;
	FILE* f = fopen(file, "rb");
	if (!f) return calloc(1, 1);
	char* data = malloc(MAX_FILE_BYTES + 1);
	if (!data) {
		fclose(f);
		return NULL;
	}
	size_t got = fread(data, 1, MAX_FILE_BYTES + 1, f);
	int error = ferror(f);
	fclose(f);
	if (error || got > MAX_FILE_BYTES) {
		free(data);
		return NULL;
	}
	data[got] = '\0';
	*length = got;
	return data;
}

// Writes data through a temporary file, so a power cut never leaves half
// a file.
static int writeAll(const char* file, const char* data, size_t length) {
	char tmp[GA_PATH_LEN + 8];
	fmt(tmp, sizeof(tmp), "%s.tmp", file);
	FILE* f = fopen(tmp, "wb");
	if (!f) return -1;
	int ok = fwrite(data, 1, length, f) == length;
	ok = fclose(f) == 0 && ok;
	if (!ok || rename(tmp, file) != 0) {
		unlink(tmp);
		return -1;
	}
	return 0;
}

// True when a line (up to its newline) names rel, or with is_dir is inside it.
static int lineMatches(const char* line, size_t len, const char* rel, int is_dir) {
	while (len && (line[len - 1] == '\r' || line[len - 1] == '\n')) len--;
	size_t rel_len = strlen(rel);
	if (len == rel_len && !strncmp(line, rel, len)) return 1;
	return is_dir && len > rel_len && !strncmp(line, rel, rel_len) && line[rel_len] == '/';
}

int GA_collectionAdd(const char* file, const char* rel) {
	if (!rel || rel[0] != '/' || strpbrk(rel, "\r\n")) return -1;
	size_t length;
	char* data = readAll(file, &length);
	if (!data) return -1;
	for (char* line = data; *line;) {
		char* end = strchr(line, '\n');
		size_t len = end ? (size_t)(end - line) : strlen(line);
		if (lineMatches(line, len, rel, 0)) {
			free(data);
			return 0;
		}
		line += len + (end ? 1 : 0);
	}
	size_t rel_len = strlen(rel);
	char* out = malloc(length + rel_len + 3);
	if (!out) {
		free(data);
		return -1;
	}
	memcpy(out, data, length);
	size_t at = length;
	if (at && out[at - 1] != '\n') out[at++] = '\n';
	memcpy(out + at, rel, rel_len);
	at += rel_len;
	out[at++] = '\n';
	int result = writeAll(file, out, at) == 0 ? 1 : -1;
	free(out);
	free(data);
	return result;
}

int GA_collectionRemove(const char* file, const char* rel, int is_dir) {
	if (!rel || rel[0] != '/') return -1;
	size_t length;
	char* data = readAll(file, &length);
	if (!data) return -1;
	char* out = malloc(length + 1);
	if (!out) {
		free(data);
		return -1;
	}
	size_t at = 0;
	int removed = 0;
	for (char* line = data; *line;) {
		char* end = strchr(line, '\n');
		size_t len = end ? (size_t)(end - line) + 1 : strlen(line);
		if (lineMatches(line, len, rel, is_dir)) removed++;
		else {
			memcpy(out + at, line, len);
			at += len;
		}
		line += len;
	}
	int result = removed;
	if (removed && writeAll(file, out, at) != 0) result = -1;
	free(out);
	free(data);
	return result;
}

///////////////////////////////////////
// name overrides

void GA_namesInit(GANames* names) {
	memset(names, 0, sizeof(*names));
}

void GA_namesFree(GANames* names) {
	for (int i = 0; i < names->count; i++) {
		free(names->paths[i]);
		free(names->names[i]);
	}
	free(names->paths);
	free(names->names);
	GA_namesInit(names);
}

static int namesFind(const GANames* names, const char* rel) {
	for (int i = 0; i < names->count; i++) {
		if (!strcmp(names->paths[i], rel)) return i;
	}
	return -1;
}

// name trimmed, with tabs and newlines as spaces.
static void cleanName(const char* name, char* out, size_t size) {
	while (*name && isspace((unsigned char)*name)) name++;
	fmt(out, size, "%s", name);
	for (char* p = out; *p; p++) {
		if (*p == '\t' || *p == '\n' || *p == '\r') *p = ' ';
	}
	size_t len = strlen(out);
	while (len && isspace((unsigned char)out[len - 1])) out[--len] = '\0';
}

static int namesAppend(GANames* names, const char* rel, const char* name) {
	if (names->count == names->capacity) {
		int capacity = names->capacity ? names->capacity * 2 : 16;
		char** paths = realloc(names->paths, sizeof(char*) * (size_t)capacity);
		if (!paths) return -1;
		names->paths = paths;
		char** shown = realloc(names->names, sizeof(char*) * (size_t)capacity);
		if (!shown) return -1;
		names->names = shown;
		names->capacity = capacity;
	}
	char* path_copy = strdup(rel);
	char* name_copy = strdup(name);
	if (!path_copy || !name_copy) {
		free(path_copy);
		free(name_copy);
		return -1;
	}
	names->paths[names->count] = path_copy;
	names->names[names->count] = name_copy;
	names->count++;
	return 0;
}

static void namesDrop(GANames* names, int i) {
	free(names->paths[i]);
	free(names->names[i]);
	memmove(&names->paths[i], &names->paths[i + 1], sizeof(char*) * (size_t)(names->count - i - 1));
	memmove(&names->names[i], &names->names[i + 1], sizeof(char*) * (size_t)(names->count - i - 1));
	names->count--;
}

int GA_namesLoad(GANames* names, const char* file) {
	GA_namesFree(names);
	size_t length;
	char* data = readAll(file, &length);
	if (!data) return -1;
	int result = 0;
	for (char* line = data; *line && !result;) {
		char* end = strchr(line, '\n');
		if (end) *end = '\0';
		char* next = end ? end + 1 : line + strlen(line);
		line[strcspn(line, "\r")] = '\0';
		char* tab = strchr(line, '\t');
		if (line[0] == '/' && tab) {
			*tab = '\0';
			char name[GA_NAME_LEN];
			cleanName(tab + 1, name, sizeof(name));
			if (name[0]) {
				int at = namesFind(names, line);
				if (at >= 0) namesDrop(names, at);
				result = namesAppend(names, line, name);
			}
		}
		line = next;
	}
	free(data);
	return result;
}

int GA_namesSave(const GANames* names, const char* file) {
	size_t length = 0;
	for (int i = 0; i < names->count; i++) length += strlen(names->paths[i]) + strlen(names->names[i]) + 2;
	char* out = malloc(length + 1);
	if (!out) return -1;
	size_t at = 0;
	for (int i = 0; i < names->count; i++) {
		at += (size_t)sprintf(out + at, "%s\t%s\n", names->paths[i], names->names[i]);
	}
	int result = writeAll(file, out, at);
	free(out);
	return result;
}

const char* GA_namesGet(const GANames* names, const char* rel) {
	if (!rel) return NULL;
	int at = namesFind(names, rel);
	return at >= 0 ? names->names[at] : NULL;
}

int GA_namesSet(GANames* names, const char* rel, const char* name) {
	if (!rel || rel[0] != '/' || strpbrk(rel, "\t\r\n")) return -1;
	char clean[GA_NAME_LEN];
	cleanName(name ? name : "", clean, sizeof(clean));
	int at = namesFind(names, rel);
	if (!clean[0]) {
		if (at < 0) return 0;
		namesDrop(names, at);
		return 1;
	}
	if (at >= 0) {
		if (!strcmp(names->names[at], clean)) return 0;
		char* copy = strdup(clean);
		if (!copy) return -1;
		free(names->names[at]);
		names->names[at] = copy;
		return 1;
	}
	return namesAppend(names, rel, clean) == 0 ? 1 : -1;
}

int GA_namesRemove(GANames* names, const char* rel, int is_dir) {
	int removed = 0;
	for (int i = names->count - 1; i >= 0; i--) {
		if (lineMatches(names->paths[i], strlen(names->paths[i]), rel, is_dir)) {
			namesDrop(names, i);
			removed++;
		}
	}
	return removed;
}

///////////////////////////////////////
// renaming a game's files

void GA_renameInit(GARename* r) {
	memset(r, 0, sizeof(*r));
}

void GA_renameFree(GARename* r) {
	free(r->moves);
	free(r->carries);
	GA_renameInit(r);
}

void GA_editableName(const char* path, int is_dir, char* out, size_t size) {
	const char* name = baseName(path);
	const char* dot = is_dir ? NULL : strrchr(name, '.');
	if (dot && dot != name) fmt(out, size, "%.*s", (int)(dot - name), name);
	else fmt(out, size, "%s", name);
}

int GA_validFileName(const char* name) {
	if (!name || !name[0] || name[0] == '.' || name[0] == ' ' || strlen(name) > GA_FILE_MAX) return 0;
	for (const unsigned char* p = (const unsigned char*)name; *p; p++) {
		if (*p == '/' || *p == '\\' || *p < 0x20 || *p == 0x7f) return 0;
	}
	return 1;
}

static int pushMove(GAMove** items, int* count, int* capacity, const char* from, const char* to, int kind, int copy) {
	if (*count == *capacity) {
		int more = *capacity ? *capacity * 2 : 16;
		GAMove* grown = realloc(*items, sizeof(GAMove) * (size_t)more);
		if (!grown) return -1;
		*items = grown;
		*capacity = more;
	}
	GAMove* m = &(*items)[(*count)++];
	if (fmt(m->from, sizeof(m->from), "%s", from) >= (int)sizeof(m->from) || fmt(m->to, sizeof(m->to), "%s", to) >= (int)sizeof(m->to)) {
		(*count)--;
		return -1;
	}
	m->kind = kind;
	m->copy = copy;
	return 0;
}

static int addMove(GARename* r, const char* from, const char* to, int kind, int copy) {
	for (int i = 0; i < r->count; i++) {
		if (!strcmp(r->moves[i].from, from)) return 0;
	}
	if (pushMove(&r->moves, &r->count, &r->capacity, from, to, kind, copy) != 0) return -1;
	r->counts[kind]++;
	return 0;
}

// The same file, as when only a name's case changes on a card that
// ignores case.
static int sameFile(const char* a, const char* b) {
	struct stat sa, sb;
	return lstat(a, &sa) == 0 && lstat(b, &sb) == 0 && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

typedef struct RenameSidecars {
	GARename* r;
	const char* sd_real;
	const char* old_name;
	const char* new_name;
	char old_cut[GA_PATH_LEN];
	char new_cut[GA_PATH_LEN];
	int shared_full; // another game has the old whole name
	int shared_cut;  // or cuts to the same name
	int taken_cut;   // another game cuts to the new one
} RenameSidecars;

// A sidecar that's there, as a file or a link in a folder inside the card,
// moves to the new name, or is copied when another game shares it. A
// shared link stays, as copying it would follow it.
static int sidecarStep(GARename* r, const char* sd_real, const char* dir, const char* old_file, const char* new_file, int kind, int shared) {
	char from[GA_PATH_LEN];
	char to[GA_PATH_LEN];
	int n = fmt(from, sizeof(from), "%s/%s", dir, old_file);
	int m = fmt(to, sizeof(to), "%s/%s", dir, new_file);
	if (n < 0 || (size_t)n >= sizeof(from) || m < 0 || (size_t)m >= sizeof(to)) return 0;
	struct stat st;
	if (lstat(from, &st) != 0 || !(S_ISREG(st.st_mode) || S_ISLNK(st.st_mode))) return 0;
	if (!parentInside(sd_real, from)) return 0;
	if (shared && !S_ISREG(st.st_mode)) return 0;
	return addMove(r, from, to, kind, shared);
}

static int renameSidecar(const char* dir, int cut, const char* suffix, int kind, void* ctx) {
	RenameSidecars* d = ctx;
	char old_file[GA_PATH_LEN];
	char new_file[GA_PATH_LEN];
	fmt(old_file, sizeof(old_file), "%s%s", cut ? d->old_cut : d->old_name, suffix);
	fmt(new_file, sizeof(new_file), "%s%s", cut ? d->new_cut : d->new_name, suffix);
	int before = d->r->count;
	int result = sidecarStep(d->r, d->sd_real, dir, old_file, new_file, kind, cut ? d->shared_cut : d->shared_full);
	// the new cut name another game's would share it from then on
	if (!result && cut && d->r->count > before && d->taken_cut) {
		d->r->error = GA_RENAME_TAKEN;
		fmt(d->r->clash, sizeof(d->r->clash), "%s", d->new_cut);
		return 1;
	}
	return result;
}

// Whether another entry in dir than the game and the files it lists has
// the same art name.
static int artShared(const char* dir, const char* game, const char* art, const Names* listed) {
	DIR* dh = opendir(dir);
	if (!dh) return 0;
	struct dirent* dp;
	int shared = 0;
	while (!shared && (dp = readdir(dh)) != NULL) {
		if (dp->d_name[0] == '.' || !strcmp(dp->d_name, game) || namesHas(listed, dp->d_name)) continue;
		char other[GA_PATH_LEN];
		artName(dp->d_name, other, sizeof(other));
		if (!strcmp(other, art)) shared = 1;
	}
	closedir(dh);
	return shared;
}

static int addCarry(GARename* r, const char* from) {
	if (r->carry_count >= GA_CARRY_MAX) return 0;
	char to[GA_PATH_LEN];
	if (!GA_renamedPath(r, from, to, sizeof(to))) return 0;
	for (int i = 0; i < r->carry_count; i++) {
		if (!strcmp(r->carries[i].from, from)) return 0;
	}
	return pushMove(&r->carries, &r->carry_count, &r->carry_capacity, from, to, GA_GAME, 0);
}

// The paths recents, sets and play history can know the game by: the
// game, and in a game folder its .m3u and .cue and each file beside them.
static int addCarries(GARename* r) {
	if (addCarry(r, r->from) != 0) return -1;
	if (!r->is_dir) return 0;
	for (int i = 0; i < r->list_count; i++) {
		char path[GA_PATH_LEN];
		fmt(path, sizeof(path), "%s/%s", r->from, r->lists[i][0]);
		if (addCarry(r, path) != 0) return -1;
	}
	DIR* dh = opendir(r->from);
	if (!dh) return 0;
	struct dirent* dp;
	int result = 0;
	while (!result && (dp = readdir(dh)) != NULL) {
		if (dp->d_name[0] == '.') continue;
		char path[GA_PATH_LEN];
		int n = fmt(path, sizeof(path), "%s/%s", r->from, dp->d_name);
		if (n < 0 || (size_t)n >= sizeof(path) || isRealDir(path)) continue;
		result = addCarry(r, path);
	}
	closedir(dh);
	return result;
}

static int refuse(GARename* r, int error, const char* clash) {
	int keep = r->error ? r->error : error;
	char saved[GA_PATH_LEN];
	fmt(saved, sizeof(saved), "%s", r->error ? r->clash : clash ? clash : "");
	GA_renameFree(r);
	r->error = keep;
	fmt(r->clash, sizeof(r->clash), "%s", saved);
	return -1;
}

int GA_planRename(const GACard* card, const char* path, const char* new_name, GARename* r) {
	GA_renameFree(r);
	char target[GA_PATH_LEN];
	int is_dir = 0;
	if (!card || !card->sdcard || !path || !GA_gameFor(card->sdcard, path, target, sizeof(target), &is_dir)) {
		return refuse(r, GA_RENAME_GAME, NULL);
	}
	char sd_real[GA_PATH_LEN];
	if (!resolve(card->sdcard, sd_real)) return refuse(r, GA_RENAME_GAME, NULL);
	if (!GA_validFileName(new_name)) return refuse(r, GA_RENAME_NAME, NULL);

	const char* old_file = baseName(target);
	const char* dot = is_dir ? NULL : strrchr(old_file, '.');
	if (dot == old_file) dot = NULL;
	char new_file[GA_PATH_LEN];
	fmt(new_file, sizeof(new_file), "%s%s", new_name, dot ? dot : "");
	if (strlen(new_file) > GA_FILE_MAX) return refuse(r, GA_RENAME_NAME, NULL);
	if (!strcmp(new_file, old_file)) return refuse(r, GA_RENAME_SAME, NULL);

	char dir[GA_PATH_LEN];
	dirName(target, dir, sizeof(dir));
	fmt(r->sdcard, sizeof(r->sdcard), "%s", card->sdcard);
	fmt(r->from, sizeof(r->from), "%s", target);
	if (fmt(r->to, sizeof(r->to), "%s/%s", dir, new_file) >= (int)sizeof(r->to)) return refuse(r, GA_RENAME_NAME, NULL);
	r->is_dir = is_dir;
	struct stat st;
	if (lstat(r->to, &st) == 0 && !sameFile(r->from, r->to)) return refuse(r, GA_RENAME_EXISTS, r->to);

	Names launched_old = {0}; // the names minarch saves the game under, before
	Names launched_new = {0}; // and after
	int result = 0;
	if (is_dir) {
		// the .m3u and .cue named after the folder, renamed inside it first
		static const char* exts[] = { ".m3u", ".cue" };
		for (int e = 0; !result && e < 2; e++) {
			char list[GA_PATH_LEN];
			if (!namedAfter(target, exts[e], list, sizeof(list))) continue;
			char renamed[GA_PATH_LEN];
			fmt(r->lists[r->list_count][0], sizeof(r->lists[0][0]), "%s", baseName(list));
			fmt(r->lists[r->list_count][1], sizeof(r->lists[0][1]), "%s%s", new_file, exts[e]);
			fmt(renamed, sizeof(renamed), "%s/%s", target, r->lists[r->list_count][1]);
			result |= addMove(r, list, renamed, GA_GAME, 0);
			result |= namesAdd(&launched_old, r->lists[r->list_count][0]);
			result |= namesAdd(&launched_new, r->lists[r->list_count][1]);
			r->list_count++;
		}
	}
	else {
		result |= namesAdd(&launched_old, old_file);
		result |= namesAdd(&launched_new, new_file);
	}
	result |= addMove(r, target, r->to, GA_GAME, 0);
	// a loose .cue or .m3u's own files aren't other games
	Names listed = {0};
	if (!result && !is_dir && (endsWithCase(target, ".cue") || endsWithCase(target, ".m3u"))) {
		result |= listedFiles(target, &listed);
	}

	// its art and details in .media, beside the game
	char old_art[GA_PATH_LEN];
	char new_art[GA_PATH_LEN];
	artName(old_file, old_art, sizeof(old_art));
	artName(new_file, new_art, sizeof(new_art));
	char media[GA_PATH_LEN];
	fmt(media, sizeof(media), "%s/.media", dir);
	if (!result && old_art[0] && new_art[0] && strcmp(old_art, new_art) != 0 && strlen(old_art) <= 250 && isRealDir(media)) {
		int shared = artShared(dir, old_file, old_art, &listed);
		static const char* kinds[] = { ".png", ".info" };
		for (int k = 0; !result && k < 2; k++) {
			char from_name[GA_PATH_LEN];
			char to_name[GA_PATH_LEN];
			fmt(from_name, sizeof(from_name), "%s%s", old_art, kinds[k]);
			fmt(to_name, sizeof(to_name), "%s%s", new_art, kinds[k]);
			result |= sidecarStep(r, sd_real, media, from_name, to_name, GA_ART, shared);
		}
	}

	// saves, states and settings, by the system's tag
	char roms[GA_PATH_LEN];
	fmt(roms, sizeof(roms), "%s/Roms", card->sdcard);
	const char* rest = target + strlen(roms) + 1;
	char folder[GA_PATH_LEN];
	fmt(folder, sizeof(folder), "%.*s", (int)(strchr(rest, '/') - rest), rest);
	char tag[GA_PATH_LEN];
	GA_tagOf(folder, tag, sizeof(tag));
	Names other_full = {0};
	Names other_cut = {0};
	if (!result && tag[0]) {
		Names skip = {0};
		result |= namesAdd(&skip, target);
		for (int i = 0; !result && i < listed.count; i++) {
			char path[GA_PATH_LEN];
			fmt(path, sizeof(path), "%s/%s", dir, listed.items[i]);
			result |= namesAdd(&skip, path);
		}
		if (!result) result |= systemNames(roms, tag, &skip, &other_full, &other_cut);
		namesFree(&skip);
	}
	for (int i = 0; !result && tag[0] && i < launched_old.count; i++) {
		const char* old_launch = launched_old.items[i];
		const char* new_launch = launched_new.items[i];
		if (namesHas(&other_full, new_launch)) {
			r->error = GA_RENAME_TAKEN;
			fmt(r->clash, sizeof(r->clash), "%s", new_launch);
			result = 1;
			break;
		}
		if (strlen(old_launch) > 255) continue;
		RenameSidecars d = { r, sd_real, old_launch, new_launch, "", "", namesHas(&other_full, old_launch), 0, 0 };
		stripped(old_launch, d.old_cut, sizeof(d.old_cut));
		stripped(new_launch, d.new_cut, sizeof(d.new_cut));
		d.shared_cut = namesHas(&other_cut, d.old_cut);
		d.taken_cut = namesHas(&other_cut, d.new_cut);
		result = eachSidecar(card, tag, renameSidecar, &d);
	}
	namesFree(&listed);
	namesFree(&launched_old);
	namesFree(&launched_new);
	namesFree(&other_full);
	namesFree(&other_cut);
	if (result) return refuse(r, GA_RENAME_MEMORY, NULL);

	// every target checked before anything changes: none may be there, or
	// be another step's too
	for (int i = 0; i < r->count; i++) {
		GAMove* m = &r->moves[i];
		int there = lstat(m->to, &st) == 0;
		if (there && sameFile(m->from, m->to)) {
			if (m->copy) { // the same file under both names: nothing to copy
				r->counts[m->kind]--;
				memmove(m, m + 1, sizeof(GAMove) * (size_t)(r->count - i - 1));
				r->count--;
				i--;
			}
			continue;
		}
		// a folder's .m3u checked where it'll be once the folder moves
		char final[GA_PATH_LEN];
		if (is_dir && isUnder(target, m->to) && GA_renamedPath(r, m->from, final, sizeof(final)) && lstat(final, &st) == 0) {
			return refuse(r, GA_RENAME_EXISTS, final);
		}
		if (there) return refuse(r, GA_RENAME_EXISTS, m->to);
		for (int j = 0; j < i; j++) {
			if (!strcmp(r->moves[j].to, m->to)) return refuse(r, GA_RENAME_EXISTS, m->to);
		}
	}
	if (addCarries(r) != 0) return refuse(r, GA_RENAME_MEMORY, NULL);
	return 0;
}

// Copies a regular file to a new one, never following a link or writing
// over anything.
static int copyFile(const char* from, const char* to) {
	int in = open(from, O_RDONLY | O_NOFOLLOW);
	if (in < 0) return -1;
	struct stat st;
	if (fstat(in, &st) != 0 || !S_ISREG(st.st_mode)) {
		close(in);
		return -1;
	}
	int out = open(to, O_WRONLY | O_CREAT | O_EXCL, 0644);
	if (out < 0) {
		close(in);
		return -1;
	}
	char buffer[16384];
	ssize_t got;
	int ok = 1;
	while (ok && (got = read(in, buffer, sizeof(buffer))) != 0) {
		if (got < 0) ok = 0;
		else ok = write(out, buffer, (size_t)got) == got;
	}
	ok = close(out) == 0 && ok;
	close(in);
	if (!ok) unlink(to);
	return ok ? 0 : -1;
}

int GA_undoRename(const GARename* r, int done, GARenameResult* out) {
	int stuck = 0;
	for (int i = done - 1; i >= 0; i--) {
		const GAMove* m = &r->moves[i];
		struct stat st;
		int ok = m->copy ? unlink(m->to) == 0
			: (lstat(m->from, &st) != 0 || sameFile(m->from, m->to)) && rename(m->to, m->from) == 0;
		if (ok) continue;
		if (!stuck++ && out) fmt(out->stuck_path, sizeof(out->stuck_path), "%s", m->to);
	}
	if (out) out->stuck += stuck;
	return stuck;
}

int GA_runRename(const GARename* r, GARenameResult* out) {
	if (out) memset(out, 0, sizeof(*out));
	char sd_real[GA_PATH_LEN];
	char roms_real[GA_PATH_LEN];
	char roms[GA_PATH_LEN];
	fmt(roms, sizeof(roms), "%s/Roms", r->sdcard);
	if (!r->count || !r->sdcard[0] || !resolve(r->sdcard, sd_real) || !resolve(roms, roms_real)) {
		if (out) fmt(out->failed, sizeof(out->failed), "%s", r->from);
		return -1;
	}
	for (int i = 0; i < r->count; i++) {
		const GAMove* m = &r->moves[i];
		struct stat st;
		int ok = cleanPath(m->from) && cleanPath(m->to) && lstat(m->from, &st) == 0;
		if (ok && m->kind == GA_GAME) ok = parentInside(roms_real, m->from) && parentInside(roms_real, m->to);
		else if (ok) ok = parentInside(sd_real, m->from) && parentInside(sd_real, m->to);
		if (ok) {
			struct stat there;
			// never over anything, apart from the same file under another case
			if (lstat(m->to, &there) == 0 && !(there.st_dev == st.st_dev && there.st_ino == st.st_ino && !m->copy)) ok = 0;
		}
		if (ok) ok = m->copy ? copyFile(m->from, m->to) == 0 : rename(m->from, m->to) == 0;
		if (!ok) {
			if (out) fmt(out->failed, sizeof(out->failed), "%s", m->from);
			GA_undoRename(r, i, out);
			return -1;
		}
	}
	return 0;
}

int GA_renamedPath(const GARename* r, const char* path, char* out, size_t size) {
	if (!path || !r->from[0]) return 0;
	if (!strcmp(path, r->from)) {
		fmt(out, size, "%s", r->to);
		return 1;
	}
	if (!r->is_dir || !isUnder(r->from, path)) return 0;
	const char* rest = path + strlen(r->from) + 1;
	for (int i = 0; i < r->list_count; i++) {
		if (!strcmp(rest, r->lists[i][0])) {
			fmt(out, size, "%s/%s", r->to, r->lists[i][1]);
			return 1;
		}
	}
	int n = fmt(out, size, "%s/%s", r->to, rest);
	return n > 0 && (size_t)n < size;
}

void GA_renameSummary(const GARename* r, char* out, size_t size) {
	GAPlan counts;
	GA_planInit(&counts);
	memcpy(counts.counts, r->counts, sizeof(counts.counts));
	GA_planSummary(&counts, out, size);
}

int GA_collectionRename(const char* file, const char* sdcard, const GARename* r) {
	if (!sdcard) return -1;
	size_t length;
	char* data = readAll(file, &length);
	if (!data) return -1;
	size_t sd_len = strlen(sdcard);
	size_t room = length + 1 + (size_t)GA_PATH_LEN * 4;
	char* out = malloc(room);
	if (!out) {
		free(data);
		return -1;
	}
	size_t used = 0;
	int changed = 0;
	int result = 0;
	for (char* line = data; *line && !result;) {
		char* end = strchr(line, '\n');
		size_t len = end ? (size_t)(end - line) + 1 : strlen(line);
		size_t text = len;
		while (text && (line[text - 1] == '\n' || line[text - 1] == '\r')) text--;
		char full[GA_PATH_LEN * 2];
		char moved[GA_PATH_LEN];
		const char* put = line;
		size_t put_len = len;
		char rewritten[GA_PATH_LEN + 2];
		if (line[0] == '/' && text < GA_PATH_LEN) {
			fmt(full, sizeof(full), "%s%.*s", sdcard, (int)text, line);
			if (GA_renamedPath(r, full, moved, sizeof(moved)) && isUnder(sdcard, moved)) {
				int n = fmt(rewritten, sizeof(rewritten), "%s%s", moved + sd_len, end ? "\n" : "");
				put = rewritten;
				put_len = (size_t)n;
				changed++;
				// there already: the old line just goes
				for (char* other = data; *other;) {
					char* other_end = strchr(other, '\n');
					size_t other_len = other_end ? (size_t)(other_end - other) : strlen(other);
					if (lineMatches(other, other_len, moved + sd_len, 0)) put_len = 0;
					other += other_len + (other_end ? 1 : 0);
				}
			}
		}
		if (used + put_len + 1 > room) {
			room = (used + put_len + 1) * 2;
			char* grown = realloc(out, room);
			if (!grown) result = -1;
			else out = grown;
		}
		if (!result) {
			memcpy(out + used, put, put_len);
			used += put_len;
		}
		line += len;
	}
	if (!result && changed && writeAll(file, out, used) != 0) result = -1;
	free(out);
	free(data);
	return result < 0 ? -1 : changed;
}
