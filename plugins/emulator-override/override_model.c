#include <dirent.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "override_model.h"

#define MAX_FILE_BYTES (1024 * 1024) // the most of the choices file read
#define MAX_SCAN 512                 // paks looked at before sorting

///////////////////////////////////////
// paths

// snprintf, through a va_list so the compiler doesn't warn about paths
// that might not fit. Returns 1 when it fit.
static int fmt(char* out, size_t size, const char* format, ...) {
	va_list args;
	va_start(args, format);
	int n = vsnprintf(out, size, format, args);
	va_end(args);
	return n >= 0 && (size_t)n < size;
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

// True when path is root itself or inside it.
static int inside(const char* path, const char* root) {
	size_t len = strlen(root);
	return !strncmp(path, root, len) && (path[len] == '/' || path[len] == '\0');
}

static int isFile(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

// <dir>/<dir name><ext>, as stock looks for a game folder's .cue or .m3u.
static int namedAfter(const char* dir, const char* ext, char* out, size_t size) {
	return fmt(out, size, "%s/%s%s", dir, baseName(dir), ext) && isFile(out);
}

///////////////////////////////////////
// games

static void tagOf(const char* folder, char* out, size_t size) {
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

int EO_gameKey(const char* sdcard, const char* path, int is_dir, char* key, size_t key_size, char* tag, size_t tag_size) {
	if (!sdcard || !path || !key || !tag || !key_size || !tag_size) return 0;
	char roms[EO_PATH_LEN];
	if (!fmt(roms, sizeof(roms), "%s/Roms", sdcard)) return 0;
	size_t roms_len = strlen(roms);
	if (strncmp(path, roms, roms_len) != 0 || path[roms_len] != '/' || !cleanPath(path)) return 0;
	if (strlen(path) >= EO_PATH_LEN) return 0;

	// the system folder, which has to have something in it
	const char* system = path + roms_len + 1;
	const char* slash = strchr(system, '/');
	if (!slash) return 0;
	char folder[EO_PATH_LEN];
	memcpy(folder, system, (size_t)(slash - system));
	folder[slash - system] = '\0';
	tagOf(folder, tag, tag_size);
	if (!tag[0]) return 0;

	char game[EO_PATH_LEN];
	if (is_dir) {
		if (!namedAfter(path, ".m3u", game, sizeof(game)) && !namedAfter(path, ".cue", game, sizeof(game))) return 0;
	}
	else {
		char dir[EO_PATH_LEN];
		if (!dirName(path, dir, sizeof(dir)) || !namedAfter(dir, ".m3u", game, sizeof(game))) strcpy(game, path);
	}
	return fmt(key, key_size, "%s", game + strlen(sdcard));
}

///////////////////////////////////////
// emulator paks

int EO_validName(const char* name) {
	if (!name || !name[0] || name[0] == '.' || strlen(name) >= EO_NAME_LEN) return 0;
	for (const unsigned char* p = (const unsigned char*)name; *p; p++) {
		if (*p < 0x20 || *p == 0x7f || *p == '/' || *p == '\\') return 0;
	}
	return 1;
}

// The two folders stock looks in for an emulator pak, in its order.
static int pakRoot(const char* sdcard, const char* platform, int which, char* out, size_t size) {
	if (which == 0) return fmt(out, size, "%s/Emus/%s", sdcard, platform);
	return fmt(out, size, "%s/.system/%s/paks/Emus", sdcard, platform);
}

int EO_pakLaunch(const char* sdcard, const char* platform, const char* name, char* out, size_t size) {
	if (!sdcard || !platform || !platform[0] || strchr(platform, '/') || !EO_validName(name) || !out || !size) return 0;
	for (int which = 0; which < 2; which++) {
		char root[EO_PATH_LEN];
		char launch[EO_PATH_LEN];
		if (!pakRoot(sdcard, platform, which, root, sizeof(root))) return 0;
		if (!fmt(launch, sizeof(launch), "%s/%s.pak/launch.sh", root, name)) return 0;
		struct stat st;
		if (stat(launch, &st) != 0) continue; // stock tries the next folder
		// stock would launch this one, so it has to be right, not the next
		if (!S_ISREG(st.st_mode)) return 0;

		char card_real[PATH_MAX];
		char root_real[PATH_MAX];
		char real[PATH_MAX];
		char want[PATH_MAX];
		if (!realpath(sdcard, card_real) || !realpath(root, root_real) || !realpath(launch, real)) return 0;
		if (!inside(root_real, card_real)) return 0;
		if (!fmt(want, sizeof(want), "%s/%s.pak/launch.sh", root_real, name) || strcmp(real, want) != 0) return 0;
		return fmt(out, size, "%s", real);
	}
	return 0;
}

static const char* const groups[][5] = {
	{ "GB", "GBC", "SGB", "MGBA", NULL },
	{ "GBC", "GB", "SGB", "MGBA", NULL },
	{ "SGB", "GB", "GBC", "MGBA", NULL },
	{ "GBA", "MGBA", NULL },
	{ "MGBA", "GBA", NULL },
	{ "SFC", "SUPA", NULL },
	{ "SUPA", "SFC", NULL },
};

int EO_compatible(const char* tag, const char* pak) {
	if (!tag || !pak) return 0;
	for (size_t i = 0; i < sizeof(groups) / sizeof(groups[0]); i++) {
		if (strcasecmp(groups[i][0], tag) != 0) continue;
		for (int j = 1; groups[i][j]; j++) {
			if (!strcasecmp(groups[i][j], pak)) return 1;
		}
	}
	return 0;
}

static int byName(const void* a, const void* b) {
	const char* x = *(const char* const*)a;
	const char* y = *(const char* const*)b;
	int c = strcasecmp(x, y);
	return c ? c : strcmp(x, y);
}

int EO_paks(const char* sdcard, const char* platform, const char* tag, int all, char** names, int max) {
	if (!sdcard || !platform || !tag || !names || max <= 0) return 0;
	char* found[MAX_SCAN];
	int count = 0;
	for (int which = 0; which < 2; which++) {
		char root[EO_PATH_LEN];
		if (!pakRoot(sdcard, platform, which, root, sizeof(root))) continue;
		DIR* dir = opendir(root);
		if (!dir) continue;
		struct dirent* d;
		while ((d = readdir(dir)) && count < MAX_SCAN) {
			size_t len = strlen(d->d_name);
			if (len <= 4 || strcmp(d->d_name + len - 4, ".pak") != 0 || len - 4 >= EO_NAME_LEN) continue;
			char name[EO_NAME_LEN];
			memcpy(name, d->d_name, len - 4);
			name[len - 4] = '\0';
			if (!EO_validName(name) || !strcasecmp(name, tag)) continue;
			if (!all && !EO_compatible(tag, name)) continue;
			int seen = 0;
			for (int i = 0; i < count && !seen; i++) seen = !strcasecmp(found[i], name);
			if (seen) continue;
			char launch[EO_PATH_LEN];
			if (!EO_pakLaunch(sdcard, platform, name, launch, sizeof(launch))) continue;
			char* copy = strdup(name);
			if (copy) found[count++] = copy;
		}
		closedir(dir);
	}
	qsort(found, (size_t)count, sizeof(found[0]), byName);
	int kept = count < max ? count : max;
	for (int i = 0; i < count; i++) {
		if (i < kept) names[i] = found[i];
		else free(found[i]);
	}
	return kept;
}

///////////////////////////////////////
// the choices file

void EO_mapInit(EOMap* map) {
	memset(map, 0, sizeof(*map));
}

void EO_mapFree(EOMap* map) {
	for (int i = 0; i < map->count; i++) {
		free(map->keys[i]);
		free(map->paks[i]);
	}
	free(map->keys);
	free(map->paks);
	EO_mapInit(map);
}

static int find(const EOMap* map, const char* key) {
	for (int i = 0; i < map->count; i++) {
		if (!strcmp(map->keys[i], key)) return i;
	}
	return -1;
}

const char* EO_mapGet(const EOMap* map, const char* key) {
	if (!map || !key) return NULL;
	int i = find(map, key);
	return i < 0 ? NULL : map->paks[i];
}

int EO_mapSet(EOMap* map, const char* key, const char* pak) {
	if (!map || !key || !key[0] || strpbrk(key, "\t\r\n")) return -1;
	int i = find(map, key);
	if (!pak || !pak[0]) {
		if (i < 0) return 0;
		free(map->keys[i]);
		free(map->paks[i]);
		memmove(map->keys + i, map->keys + i + 1, sizeof(char*) * (size_t)(map->count - i - 1));
		memmove(map->paks + i, map->paks + i + 1, sizeof(char*) * (size_t)(map->count - i - 1));
		map->count--;
		return 1;
	}
	if (!EO_validName(pak)) return -1;
	if (i >= 0) {
		if (!strcmp(map->paks[i], pak)) return 0;
		char* copy = strdup(pak);
		if (!copy) return -1;
		free(map->paks[i]);
		map->paks[i] = copy;
		return 1;
	}
	if (map->count == map->capacity) {
		int capacity = map->capacity ? map->capacity * 2 : 16;
		char** keys = realloc(map->keys, sizeof(char*) * (size_t)capacity);
		if (!keys) return -1;
		map->keys = keys;
		char** paks = realloc(map->paks, sizeof(char*) * (size_t)capacity);
		if (!paks) return -1;
		map->paks = paks;
		map->capacity = capacity;
	}
	char* key_copy = strdup(key);
	char* pak_copy = strdup(pak);
	if (!key_copy || !pak_copy) {
		free(key_copy);
		free(pak_copy);
		return -1;
	}
	map->keys[map->count] = key_copy;
	map->paks[map->count] = pak_copy;
	map->count++;
	return 1;
}

int EO_mapLoad(EOMap* map, const char* file) {
	EO_mapFree(map);
	FILE* f = fopen(file, "r");
	if (!f) return 0; // nothing chosen yet
	char* text = malloc(MAX_FILE_BYTES + 1);
	if (!text) {
		fclose(f);
		return -1;
	}
	size_t got = fread(text, 1, MAX_FILE_BYTES, f);
	int failed = ferror(f);
	fclose(f);
	text[got] = '\0';
	char* line = text;
	while (!failed && line && *line) {
		char* next = strchr(line, '\n');
		if (next) *next++ = '\0';
		size_t len = strlen(line);
		if (len && line[len - 1] == '\r') line[len - 1] = '\0';
		char* tab = strchr(line, '\t');
		if (tab) {
			*tab = '\0';
			if (line[0] && EO_validName(tab + 1) && EO_mapSet(map, line, tab + 1) < 0) failed = 1;
		}
		line = next;
	}
	free(text);
	return failed ? -1 : 0;
}

int EO_mapSave(const EOMap* map, const char* file) {
	char tmp[EO_PATH_LEN + 8];
	if (!fmt(tmp, sizeof(tmp), "%s.tmp", file)) return -1;
	FILE* f = fopen(tmp, "w");
	if (!f) return -1;
	int failed = 0;
	for (int i = 0; i < map->count && !failed; i++) {
		failed = fprintf(f, "%s\t%s\n", map->keys[i], map->paks[i]) < 0;
	}
	if (fclose(f) != 0) failed = 1;
	if (failed || rename(tmp, file) != 0) {
		remove(tmp);
		return -1;
	}
	return 0;
}
