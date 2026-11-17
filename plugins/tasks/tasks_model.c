// The tasks plugin's SDL-free half: see tasks_model.h.

#include "tasks_model.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

int TK_isTask(const char* file_name) {
	if (!file_name || file_name[0] == '.') return 0;
	size_t len = strlen(file_name);
	return len > 3 && !strcmp(file_name + len - 3, ".sh");
}

static int isFile(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int isDir(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int byName(const void* a, const void* b) {
	const TKTask* x = a;
	const TKTask* y = b;
	int c = strcasecmp(x->name, y->name);
	if (!c) c = strcmp(x->name, y->name);
	if (!c) c = strcasecmp(x->tag, y->tag);
	return c;
}

static int scanInto(const char* dir, const char* tag, TKList* list) {
	DIR* d = opendir(dir);
	if (!d) return 0;
	int added = 0;
	struct dirent* e;
	while ((e = readdir(d)) != NULL && list->count < TK_MAX) {
		if (!TK_isTask(e->d_name)) continue;
		TKTask* t = &list->items[list->count];
		int n = snprintf(t->path, sizeof(t->path), "%s/%s", dir, e->d_name);
		if (n < 0 || (size_t)n >= sizeof(t->path) || !isFile(t->path)) continue;
		size_t len = strlen(e->d_name) - 3;
		if (len >= sizeof(t->name)) continue;
		memcpy(t->name, e->d_name, len);
		t->name[len] = '\0';
		snprintf(t->tag, sizeof(t->tag), "%s", tag ? tag : "");
		list->count++;
		added++;
	}
	closedir(d);
	return added;
}

int TK_scanDir(const char* dir, const char* tag, TKList* list) {
	if (!dir || !list) return 0;
	int start = list->count;
	int added = scanInto(dir, tag, list);
	qsort(list->items + start, (size_t)added, sizeof(TKTask), byName);
	return added;
}

void TK_scan(const char* tasks_dir, TKList* global, TKList* game) {
	if (global) global->count = 0;
	if (game) game->count = 0;
	if (!tasks_dir) return;
	if (global) TK_scanDir(tasks_dir, "", global);
	if (!game) return;
	char game_dir[TK_PATH_MAX];
	int n = snprintf(game_dir, sizeof(game_dir), "%s/Game", tasks_dir);
	if (n < 0 || (size_t)n >= sizeof(game_dir)) return;
	scanInto(game_dir, "", game);
	DIR* d = opendir(game_dir);
	if (d) {
		struct dirent* e;
		while ((e = readdir(d)) != NULL) {
			if (e->d_name[0] == '.' || strlen(e->d_name) >= TK_TAG_MAX) continue;
			char sub[TK_PATH_MAX];
			n = snprintf(sub, sizeof(sub), "%s/%s", game_dir, e->d_name);
			if (n < 0 || (size_t)n >= sizeof(sub) || !isDir(sub)) continue;
			scanInto(sub, e->d_name, game);
		}
		closedir(d);
	}
	qsort(game->items, (size_t)game->count, sizeof(TKTask), byName);
}

// The system folder's part of path, after <sdcard>/Roms/, and its length;
// NULL unless something comes after it.
static const char* systemFolder(const char* sdcard, const char* path, size_t* len) {
	if (!sdcard || !path) return NULL;
	size_t sd = strlen(sdcard);
	if (strncmp(path, sdcard, sd) != 0 || strncmp(path + sd, "/Roms/", 6) != 0) return NULL;
	const char* folder = path + sd + 6;
	const char* slash = strchr(folder, '/');
	if (!slash || slash == folder || !slash[1]) return NULL;
	*len = (size_t)(slash - folder);
	return folder;
}

int TK_systemTag(const char* sdcard, const char* path, char* out, size_t size) {
	if (out && size) out[0] = '\0';
	size_t len;
	const char* folder = systemFolder(sdcard, path, &len);
	if (!folder) return 0;
	// the tag is the last "(...)" the folder's name ends with
	if (len < 3 || folder[len - 1] != ')') return 1;
	size_t open = len - 1;
	while (open > 0 && folder[open - 1] != '(') open--;
	if (open == 0) return 1;
	size_t tag_len = len - 1 - open;
	if (out && size && tag_len < size) {
		memcpy(out, folder + open, tag_len);
		out[tag_len] = '\0';
	}
	return 1;
}

int TK_isGame(const char* sdcard, const char* path) {
	size_t len;
	if (!systemFolder(sdcard, path, &len)) return 0;
	if (isFile(path)) return 1;
	if (!isDir(path)) return 0;
	const char* name = strrchr(path, '/') + 1;
	if (!name[0]) return 0;
	static const char* kinds[] = { "m3u", "cue" };
	for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
		char sheet[TK_PATH_MAX * 2];
		snprintf(sheet, sizeof(sheet), "%s/%s.%s", path, name, kinds[i]);
		if (isFile(sheet)) return 1;
	}
	return 0;
}

int TK_appliesTo(const TKTask* task, const char* tag) {
	if (!task) return 0;
	if (!task->tag[0]) return 1;
	return tag && !strcasecmp(task->tag, tag);
}

static int isSpace(char c) {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

void TK_lastLine(const char* text, size_t len, char* out, size_t size) {
	if (!out || !size) return;
	out[0] = '\0';
	if (!text) return;
	size_t end = len;
	while (end > 0) {
		// the line ending at end
		size_t start = end;
		while (start > 0 && text[start - 1] != '\n') start--;
		size_t a = start, b = end;
		while (a < b && isSpace(text[a])) a++;
		while (b > a && isSpace(text[b - 1])) b--;
		if (a < b) {
			size_t n = b - a;
			if (n >= size) {
				n = size - 1;
				// back off to a UTF-8 character's first byte
				while (n > 0 && ((unsigned char)text[a + n] & 0xC0) == 0x80) n--;
			}
			memcpy(out, text + a, n);
			out[n] = '\0';
			return;
		}
		if (start == 0) break;
		end = start - 1;
	}
}

void TK_logName(const char* tasks_dir, const char* script, char* out, size_t size) {
	if (!out || !size) return;
	out[0] = '\0';
	if (!tasks_dir || !script) return;
	size_t dir_len = strlen(tasks_dir);
	if (strncmp(script, tasks_dir, dir_len) != 0 || script[dir_len] != '/' || !script[dir_len + 1]) return;
	const char* rel = script + dir_len + 1;
	size_t len = strlen(rel);
	if (len > 3 && !strcmp(rel + len - 3, ".sh")) len -= 3;
	if (len >= size) return;
	for (size_t i = 0; i < len; i++) out[i] = rel[i] == '/' ? '-' : rel[i];
	out[len] = '\0';
}

// 1 when entry ("NAME=value") is for the variable name ("NAME=")
static int named(const char* entry, const char* name, size_t name_len) {
	return !strncmp(entry, name, name_len);
}

int TK_envBuild(char* const* from, const char* const* set, int set_count, char** out, int max) {
	if (!out || max <= 0) return 0;
	int n = 0;
	for (int i = 0; from && from[i] && n < max - 1; i++) {
		const char* entry = from[i];
		if (named(entry, "LD_PRELOAD=", 11) || named(entry, "DYLD_INSERT_LIBRARIES=", 22)) continue;
		int replaced = 0;
		for (int k = 0; k < set_count && !replaced; k++) {
			const char* eq = set[k] ? strchr(set[k], '=') : NULL;
			if (eq && named(entry, set[k], (size_t)(eq - set[k]) + 1)) replaced = 1;
		}
		if (!replaced) out[n++] = (char*)entry;
	}
	for (int k = 0; k < set_count && n < max - 1; k++) {
		if (set[k] && strchr(set[k], '=')) out[n++] = (char*)set[k];
	}
	out[n] = NULL;
	return n;
}
