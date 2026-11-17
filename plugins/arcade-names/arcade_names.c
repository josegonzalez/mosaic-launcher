// Arcade names: real titles for arcade sets, such as "Street Fighter II:
// The World Warrior" for sf2.zip, from DATs the user puts in DATs/ on the
// card, with BIOS and device sets hidden.
//
// dat_model.h reads the DATs (Logiqx XML or ClrMamePro text) and a
// catver.ini beside them, once: what it read is kept as a compact cache in
// .userdata/<platform>/launcher/arcade-names.cache, used while the files'
// names, sizes and times are the same, and read again when one changes.
// Only system folders whose tag is in the tags setting are named, by
// display_name for the game list and index_name for the card index, so
// search and the other plugins reading it see the same names. A name
// the list already has from map.txt, or from a plugin before this one, is
// left as it is: only a name that's still the file's own is changed.
// filter hides BIOS and device sets, and, by the settings, clones whose
// parent is in the folder, sets marked not working and catver.ini's
// mahjong, casino and quiz sets. With no DAT it does nothing.

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include "dat_model.h"
#include "mosaic_plugin.h"

#define ID "arcade-names"
#define PATH_LEN 1024
#define FOLDERS 16      // folders kept read, so a list across many, such as the index's, reads each once
#define SOURCES_MAX 32  // DATs read, and catver.ini
#define DEFAULT_TAGS "MAME,FBN,FBNEO,FBA,ARCADE,CPS1,CPS2,CPS3,NEOGEO,HBMAME,MAME2003,MAME2003PLUS,MAME2010"

static const MosaicHost* host;
static char roms_path[PATH_LEN];
static char tags[512];
static ANOptions options;
static ANTable table;

__attribute__((format(printf, 1, 2)))
static void sayf(const char* fmt, ...) {
	char message[PATH_LEN * 2];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static double nowMs(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static int endsWith(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcasecmp(s + a - b, suffix);
}

// The folder a path is in, into out; 0 when it has none.
static int parentOf(const char* path, char* out, size_t size) {
	const char* slash = strrchr(path, '/');
	if (!slash || slash == path || (size_t)(slash - path) >= size) return 0;
	snprintf(out, size, "%.*s", (int)(slash - path), path);
	return 1;
}

// 1 for a folder in a system folder, or the system folder itself, whose
// tag is one of the tags setting's.
static int arcadeFolder(const char* dir) {
	size_t n = strlen(roms_path);
	if (!dir || strncmp(dir, roms_path, n) != 0 || dir[n] != '/' || !dir[n + 1]) return 0;
	const char* system = dir + n + 1;
	size_t len = strcspn(system, "/");
	char name[256], tag[128];
	if (len >= sizeof(name)) return 0;
	snprintf(name, sizeof(name), "%.*s", (int)len, system);
	char* open = strrchr(name, '(');
	char* close = open ? strchr(open, ')') : NULL;
	int wrote = open && close ? snprintf(tag, sizeof(tag), "%.*s", (int)(close - open - 1), open + 1) : snprintf(tag, sizeof(tag), "%s", name);
	if (wrote < 0 || wrote >= (int)sizeof(tag)) return 0;
	return AN_hasTag(tags, tag);
}

///////////////////////////////////////
// the DATs, read once

typedef struct Source {
	char name[256];
	long long size, mtime;
	int catver;
} Source;

static int compareSources(const void* a, const void* b) {
	return strcmp(((const Source*)a)->name, ((const Source*)b)->name);
}

// The DATs (*.dat and *.xml) and catver.ini in dir, by name.
static int listSources(const char* dir, Source* out, int max) {
	DIR* dh = opendir(dir);
	if (!dh) return 0;
	int n = 0;
	struct dirent* dp;
	while (n < max && (dp = readdir(dh)) != NULL) {
		const char* name = dp->d_name;
		int catver = !strcasecmp(name, "catver.ini");
		if (name[0] == '.' || (!catver && !endsWith(name, ".dat") && !endsWith(name, ".xml")) || strlen(name) >= sizeof(out[n].name)) continue;
		char path[PATH_LEN];
		struct stat st;
		if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path) || stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
		snprintf(out[n].name, sizeof(out[n].name), "%s", name);
		out[n].size = (long long)st.st_size;
		out[n].mtime = (long long)st.st_mtime;
		out[n].catver = catver;
		n++;
	}
	closedir(dh);
	qsort(out, (size_t)n, sizeof(Source), compareSources);
	return n;
}

static void loadDats(void) {
	char dir[PATH_LEN], cache[PATH_LEN];
	snprintf(dir, sizeof(dir), "%s/DATs", host->sdcard_path);
	snprintf(cache, sizeof(cache), "%s/launcher/" ID ".cache", host->userdata_path);
	static Source sources[SOURCES_MAX];
	int count = listSources(dir, sources, SOURCES_MAX);
	int dats = 0;
	for (int i = 0; i < count; i++) dats += !sources[i].catver;
	if (!dats) {
		sayf("no DAT in %s, naming nothing", dir);
		remove(cache);
		return;
	}

	// what the cache was read from
	char signature[SOURCES_MAX * 300];
	size_t used = 0;
	for (int i = 0; i < count && used < sizeof(signature); i++) {
		used += (size_t)snprintf(signature + used, sizeof(signature) - used, "%s\t%lld\t%lld\n", sources[i].name, sources[i].size,
			sources[i].mtime);
	}
	double began = nowMs();
	if (AN_load(&table, cache, signature) == 0) {
		sayf("%i sets from the cache in %.1f ms", table.count, nowMs() - began);
		return;
	}

	int read = 0;
	for (int i = 0; i < count; i++) {
		if (sources[i].catver) continue;
		char path[PATH_LEN];
		if (snprintf(path, sizeof(path), "%s/%s", dir, sources[i].name) >= (int)sizeof(path)) continue;
		int sets = AN_readDat(&table, path);
		if (sets < 0) sayf("couldn't read %s", sources[i].name);
		else {
			sayf("%i sets in %s", sets, sources[i].name);
			read++;
		}
	}
	if (AN_finish(&table) != 0) {
		sayf("out of memory reading the DATs");
		return;
	}
	for (int i = 0; i < count; i++) {
		if (!sources[i].catver) continue;
		char path[PATH_LEN];
		if (snprintf(path, sizeof(path), "%s/%s", dir, sources[i].name) >= (int)sizeof(path)) continue;
		int marked = AN_readCatver(&table, path);
		if (marked < 0) sayf("couldn't read %s", sources[i].name);
		else sayf("%i mahjong, casino and quiz sets in %s", marked, sources[i].name);
	}
	if (table.full) sayf("the DATs have more than it keeps; some sets were left out");
	sayf("%i sets from %i DATs in %.1f ms, %zu KB", table.count, read, nowMs() - began, AN_bytes(&table) / 1024);
	char launcher_dir[PATH_LEN];
	snprintf(launcher_dir, sizeof(launcher_dir), "%s/launcher", host->userdata_path);
	mkdir(launcher_dir, 0755);
	if (AN_save(&table, cache, signature) != 0) sayf("couldn't save the cache");
}

///////////////////////////////////////
// folders, read once while they don't change

typedef struct Folder {
	char path[PATH_LEN];
	time_t mtime;
	unsigned long used; // when it was last asked for, to drop the oldest
	ANFolder sets;
} Folder;

static Folder folders[FOLDERS];
static unsigned long uses = 0;

static void forget(Folder* f) {
	AN_folderFree(&f->sets);
	memset(f, 0, sizeof(*f));
}

static int readFolder(Folder* f) {
	DIR* dh = opendir(f->path);
	if (!dh) return 0;
	char** names = NULL;
	int count = 0, cap = 0;
	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL) {
		if (dp->d_name[0] == '.' || dp->d_type == DT_DIR) continue;
		if (count == cap) {
			int next = cap ? cap * 2 : 256;
			char** bigger = realloc(names, sizeof(char*) * (size_t)next);
			if (!bigger) break;
			names = bigger;
			cap = next;
		}
		if ((names[count] = strdup(dp->d_name)) != NULL) count++;
	}
	closedir(dh);
	if (AN_folder(&table, &options, (const char* const*)names, count, &f->sets) != 0) sayf("out of memory naming a folder");
	for (int i = 0; i < count; i++) free(names[i]);
	free(names);
	return 1;
}

// An arcade folder with its sets' names, or NULL. One already read is used
// while its time is the same, once that time is more than 3 seconds old,
// as FAT's 2 second clock wouldn't show a second change.
static Folder* folderAt(const char* dir) {
	if (!table.count || !arcadeFolder(dir) || strlen(dir) >= PATH_LEN) return NULL;
	// the same folder again within a second, as for each entry of a list, without asking the card
	static Folder* last = NULL;
	static time_t last_at = 0;
	time_t now = time(NULL);
	if (last && last->path[0] && now == last_at && !strcmp(last->path, dir)) return last;
	last = NULL;
	struct stat st;
	if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) return NULL;
	Folder* oldest = &folders[0];
	for (int i = 0; i < FOLDERS; i++) {
		Folder* f = &folders[i];
		if (f->path[0] && !strcmp(f->path, dir)) {
			if (f->mtime == st.st_mtime && now - st.st_mtime > 3) {
				f->used = ++uses;
				last = f;
				last_at = now;
				return f;
			}
			oldest = f;
			break;
		}
		if (!f->path[0] || f->used < oldest->used) oldest = f;
	}
	Folder* f = oldest;
	forget(f);
	snprintf(f->path, sizeof(f->path), "%s", dir);
	f->mtime = st.st_mtime;
	f->used = ++uses;
	if (!readFolder(f)) {
		forget(f);
		return NULL;
	}
	last = f;
	last_at = now;
	return f;
}

// The folder an entry's set is in and its index there, or -1.
static int setAt(const MosaicEntry* entry, Folder** out) {
	if (!table.count || host->entry_type(entry) != MOSAIC_ENTRY_ROM) return -1;
	const char* path = host->entry_path(entry);
	char dir[PATH_LEN];
	if (!path || !parentOf(path, dir, sizeof(dir))) return -1;
	Folder* f = folderAt(dir);
	if (!f) return -1;
	*out = f;
	return AN_folderFind(&f->sets, strrchr(path, '/') + 1);
}

///////////////////////////////////////
// the list

// 1 for a list that shows what the user put there themselves: Recently
// Played, collections and favorites' Favorites folder.
static int ownList(const char* dir_path) {
	char recents[PATH_LEN], collections[PATH_LEN], favorites[PATH_LEN];
	snprintf(recents, sizeof(recents), "%s/Recently Played", host->sdcard_path);
	snprintf(collections, sizeof(collections), "%s/Collections", host->sdcard_path);
	snprintf(favorites, sizeof(favorites), "%s/Favorites", host->sdcard_path);
	size_t n = strlen(collections);
	return !strcmp(dir_path, recents) || !strcmp(dir_path, favorites)
		|| (!strncmp(dir_path, collections, n) && (!dir_path[n] || dir_path[n] == '/'));
}

// BIOS and device sets, and what the settings hide: in arcade folders and
// other plugins' lists, such as search's, but not in Recently Played,
// collections or Favorites.
static int filter(const MosaicEntry* entry, const char* dir_path) {
	if (!table.count || !dir_path || ownList(dir_path)) return 1;
	Folder* f;
	int i = setAt(entry, &f);
	return i < 0 || !f->sets.hidden[i];
}

// The set's name, unless the entry's name isn't its file's own any more,
// as when map.txt or another plugin named it.
static int name(const MosaicEntry* entry, char* out, size_t size) {
	Folder* f;
	int i = setAt(entry, &f);
	if (i < 0 || !f->sets.names[i]) return 0;
	char own[512];
	AN_stockName(f->sets.files[i], own, sizeof(own));
	const char* current = host->entry_name(entry);
	if (!current || strcmp(current, own) != 0) return 0;
	snprintf(out, size, "%s", f->sets.names[i]);
	return 1;
}

///////////////////////////////////////

static int init(const MosaicHost* h) {
	host = h;
	snprintf(roms_path, sizeof(roms_path), "%s/Roms", host->sdcard_path);
	snprintf(tags, sizeof(tags), "%s", host->cfg_get("tags", DEFAULT_TAGS));
	memset(&options, 0, sizeof(options));
	options.full = !strcmp(host->cfg_get("names", "Clean"), "Full");
	options.clones = !strcmp(host->cfg_get("clones", "off"), "on");
	options.not_working = !strcmp(host->cfg_get("not_working", "off"), "on");
	options.categories = !strcmp(host->cfg_get("categories", "off"), "on");
	memset(folders, 0, sizeof(folders));
	AN_init(&table);
	loadDats();
	return 0;
}

static void quit(void) {
	for (int i = 0; i < FOLDERS; i++) forget(&folders[i]);
	AN_free(&table);
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.filter = filter,
	.display_name = name,
	.index_name = name,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
