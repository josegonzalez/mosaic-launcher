#include "tags_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

void TG_init(TGStore* store) {
	memset(store, 0, sizeof(*store));
}

static void tagFree(TGTag* tag) {
	for (int i = 0; i < tag->count; i++) free(tag->games[i]);
	free(tag->games);
	tag->games = NULL;
	tag->count = tag->capacity = 0;
}

void TG_free(TGStore* store) {
	for (int i = 0; i < store->count; i++) tagFree(&store->tags[i]);
	free(store->tags);
	TG_init(store);
}

///////////////////////////////////////
// names and paths

int TG_cleanName(const char* name, char* out, size_t size) {
	if (!out || !size) return 0;
	out[0] = '\0';
	if (!name) return 0;
	while (*name == ' ' || (unsigned char)*name < 0x20 || *name == 0x7f) {
		if (!*name) return 0;
		name++;
	}
	size_t limit = size < TG_NAME_LEN ? size - 1 : TG_NAME_LEN - 1;
	size_t len = 0;
	for (; name[len] && len < limit; len++) {
		unsigned char c = (unsigned char)name[len];
		out[len] = c < 0x20 || c == 0x7f ? ' ' : (char)c;
	}
	// cut between characters: drop a UTF-8 sequence the limit split
	if (name[len]) {
		size_t start = len;
		while (start > 0 && ((unsigned char)out[start - 1] & 0xC0) == 0x80) start--;
		if (start > 0 && ((unsigned char)out[start - 1] & 0x80)) {
			unsigned char lead = (unsigned char)out[start - 1];
			size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
			if (len - (start - 1) < need) len = start - 1;
		}
	}
	while (len > 0 && out[len - 1] == ' ') len--;
	out[len] = '\0';
	return len > 0 && !strchr(out, '/') && out[0] != '.' && out[0] != '#';
}

int TG_validPath(const char* rel) {
	if (!rel || rel[0] != '/' || strlen(rel) >= TG_PATH_LEN || strpbrk(rel, "\t\r\n")) return 0;
	for (const char* part = rel + 1; *part;) {
		const char* end = strchr(part, '/');
		size_t len = end ? (size_t)(end - part) : strlen(part);
		if ((len == 1 && part[0] == '.') || (len == 2 && part[0] == '.' && part[1] == '.')) return 0;
		if (!end) break;
		part = end + 1;
	}
	return rel[1] != '\0';
}

///////////////////////////////////////
// the store

int TG_find(const TGStore* store, const char* name) {
	if (!name) return -1;
	for (int i = 0; i < store->count; i++) {
		if (!strcasecmp(store->tags[i].name, name)) return i;
	}
	return -1;
}

static int nameOrder(const char* a, const char* b) {
	int c = strcasecmp(a, b);
	return c ? c : strcmp(a, b);
}

int TG_add(TGStore* store, const char* name, int* added) {
	if (added) *added = 0;
	char clean[TG_NAME_LEN];
	if (!TG_cleanName(name, clean, sizeof(clean))) return -1;
	int at = TG_find(store, clean);
	if (at >= 0) return at;
	if (store->count >= TG_TAGS_MAX) return -1;
	if (store->count == store->capacity) {
		int capacity = store->capacity ? store->capacity * 2 : 16;
		TGTag* tags = realloc(store->tags, (size_t)capacity * sizeof(TGTag));
		if (!tags) return -1;
		store->tags = tags;
		store->capacity = capacity;
	}
	at = 0;
	while (at < store->count && nameOrder(store->tags[at].name, clean) < 0) at++;
	memmove(&store->tags[at + 1], &store->tags[at], (size_t)(store->count - at) * sizeof(TGTag));
	memset(&store->tags[at], 0, sizeof(TGTag));
	snprintf(store->tags[at].name, TG_NAME_LEN, "%s", clean);
	store->count++;
	if (added) *added = 1;
	return at;
}

int TG_remove(TGStore* store, int index) {
	if (index < 0 || index >= store->count) return -1;
	tagFree(&store->tags[index]);
	memmove(&store->tags[index], &store->tags[index + 1], (size_t)(store->count - index - 1) * sizeof(TGTag));
	store->count--;
	return 0;
}

static int gameAt(const TGTag* tag, const char* rel) {
	for (int i = 0; i < tag->count; i++) {
		if (!strcmp(tag->games[i], rel)) return i;
	}
	return -1;
}

int TG_has(const TGStore* store, int tag, const char* rel) {
	if (tag < 0 || tag >= store->count || !rel) return 0;
	return gameAt(&store->tags[tag], rel) >= 0;
}

int TG_set(TGStore* store, int index, const char* rel, int on) {
	if (index < 0 || index >= store->count || !TG_validPath(rel)) return -1;
	TGTag* tag = &store->tags[index];
	int at = gameAt(tag, rel);
	if (!on) {
		if (at < 0) return 0;
		free(tag->games[at]);
		memmove(&tag->games[at], &tag->games[at + 1], (size_t)(tag->count - at - 1) * sizeof(char*));
		tag->count--;
		return 1;
	}
	if (at >= 0) return 0;
	if (tag->count >= TG_GAMES_MAX) return -1;
	if (tag->count == tag->capacity) {
		int capacity = tag->capacity ? tag->capacity * 2 : 8;
		char** games = realloc(tag->games, (size_t)capacity * sizeof(char*));
		if (!games) return -1;
		tag->games = games;
		tag->capacity = capacity;
	}
	char* copy = strdup(rel);
	if (!copy) return -1;
	tag->games[tag->count++] = copy;
	return 1;
}

///////////////////////////////////////
// the file

int TG_parse(TGStore* store, const char* text) {
	TG_free(store);
	if (!text) return 0;
	const char* line = text;
	while (*line) {
		const char* end = strchr(line, '\n');
		size_t len = end ? (size_t)(end - line) : strlen(line);
		while (len && line[len - 1] == '\r') len--;
		if (len && line[0] != '#' && len < TG_NAME_LEN + TG_PATH_LEN + 2) {
			char buffer[TG_NAME_LEN + TG_PATH_LEN + 2];
			memcpy(buffer, line, len);
			buffer[len] = '\0';
			char* tab = strchr(buffer, '\t');
			if (tab) *tab = '\0';
			char name[TG_NAME_LEN];
			// a name too long to keep whole is skipped rather than merged into a shorter one
			if (strlen(buffer) < TG_NAME_LEN && TG_cleanName(buffer, name, sizeof(name))
					&& (!tab || TG_validPath(tab + 1))) {
				int index = TG_find(store, name);
				if (index < 0) {
					if (store->count >= TG_TAGS_MAX) goto next;
					index = TG_add(store, name, NULL);
					if (index < 0) return -1;
				}
				if (tab && TG_set(store, index, tab + 1, 1) < 0 && store->tags[index].count < TG_GAMES_MAX) return -1;
			}
		}
	next:
		if (!end) break;
		line = end + 1;
	}
	return 0;
}

char* TG_format(const TGStore* store) {
	size_t length = 1;
	for (int i = 0; i < store->count; i++) {
		const TGTag* tag = &store->tags[i];
		size_t name = strlen(tag->name);
		length += name + 1;
		for (int k = 0; k < tag->count; k++) length += name + strlen(tag->games[k]) + 2;
	}
	char* out = malloc(length);
	if (!out) return NULL;
	size_t at = 0;
	for (int i = 0; i < store->count; i++) {
		const TGTag* tag = &store->tags[i];
		at += (size_t)sprintf(out + at, "%s\n", tag->name);
		for (int k = 0; k < tag->count; k++) at += (size_t)sprintf(out + at, "%s\t%s\n", tag->name, tag->games[k]);
	}
	out[at] = '\0';
	return out;
}

int TG_load(TGStore* store, const char* file) {
	FILE* f = fopen(file, "rb");
	if (!f) {
		TG_free(store);
		return access(file, F_OK) == 0 ? -1 : 0;
	}
	char* text = NULL;
	size_t length = 0, capacity = 0;
	int ok = 1;
	for (;;) {
		if (length + 4096 + 1 > capacity) {
			capacity = capacity ? capacity * 2 : 8192;
			char* grown = realloc(text, capacity);
			if (!grown) {
				ok = 0;
				break;
			}
			text = grown;
		}
		size_t got = fread(text + length, 1, 4096, f);
		length += got;
		if (got < 4096) {
			if (ferror(f)) ok = 0;
			break;
		}
	}
	fclose(f);
	if (!ok) {
		free(text);
		return -1;
	}
	text[length] = '\0';
	int result = TG_parse(store, text);
	free(text);
	return result;
}

int TG_save(const TGStore* store, const char* file) {
	char* text = TG_format(store);
	if (!text) return -1;
	char tmp[TG_PATH_LEN + 8];
	if ((size_t)snprintf(tmp, sizeof(tmp), "%s.tmp", file) >= sizeof(tmp)) {
		free(text);
		return -1;
	}
	FILE* f = fopen(tmp, "wb");
	if (!f) {
		free(text);
		return -1;
	}
	size_t length = strlen(text);
	int ok = fwrite(text, 1, length, f) == length;
	free(text);
	ok = fflush(f) == 0 && ok;
	ok = fsync(fileno(f)) == 0 && ok;
	ok = fclose(f) == 0 && ok;
	if (!ok || rename(tmp, file) != 0) {
		unlink(tmp);
		return -1;
	}
	return 0;
}

///////////////////////////////////////
// what an entry records

static int exists(const char* path) {
	struct stat st;
	return stat(path, &st) == 0;
}

static int endsWith(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcasecmp(s + a - b, suffix);
}

// <dir>/<dir name><ext>, when it exists
static int namedAfter(const char* dir, const char* ext, char* out, size_t size) {
	const char* name = strrchr(dir, '/');
	if (!name || !name[1]) return 0;
	if ((size_t)snprintf(out, size, "%s%s%s", dir, name, ext) >= size) return 0;
	return exists(out);
}

int TG_gamePath(const char* sdcard, const char* path, int type, char* out, size_t size) {
	if (!out || !size) return 0;
	out[0] = '\0';
	if (!sdcard || !path) return 0;
	size_t card = strlen(sdcard);
	if (strncmp(path, sdcard, card) != 0 || path[card] != '/') return 0;

	char full[TG_PATH_LEN];
	switch (type) {
		case MOSAIC_ENTRY_PAK:
			if ((size_t)snprintf(full, sizeof(full), "%s", path) >= sizeof(full)) return 0;
			break;
		case MOSAIC_ENTRY_DIR:
			// a folder that launches its game when opened
			if (!namedAfter(path, ".m3u", full, sizeof(full)) && !namedAfter(path, ".cue", full, sizeof(full))) return 0;
			break;
		case MOSAIC_ENTRY_ROM: {
			// a disc of a multi-disc game is the game's .m3u
			char dir[TG_PATH_LEN];
			if ((size_t)snprintf(dir, sizeof(dir), "%s", path) >= sizeof(dir)) return 0;
			char* slash = strrchr(dir, '/');
			if (slash && slash > dir + card) {
				*slash = '\0';
				if (!endsWith(path, ".m3u") && namedAfter(dir, ".m3u", full, sizeof(full))) break;
			}
			if ((size_t)snprintf(full, sizeof(full), "%s", path) >= sizeof(full)) return 0;
			break;
		}
		default:
			return 0;
	}
	const char* rel = full + card;
	if (!TG_validPath(rel)) return 0;
	snprintf(out, size, "%s", rel);
	return strlen(rel) < size;
}

int TG_folderTag(const TGStore* store, const char* tags_folder, const char* path) {
	if (!tags_folder || !path) return -1;
	size_t len = strlen(tags_folder);
	if (strncmp(path, tags_folder, len) != 0 || path[len] != '/' || strchr(path + len + 1, '/')) return -1;
	for (int i = 0; i < store->count; i++) {
		if (!strcmp(store->tags[i].name, path + len + 1)) return i;
	}
	return -1;
}

///////////////////////////////////////
// the checklist

int TG_move(int selected, int count, int step, int wrap) {
	if (count <= 0) return -1;
	if (selected < 0) selected = 0;
	if (selected >= count) selected = count - 1;
	int next = selected + step;
	if (next < 0) return wrap ? count - 1 : 0;
	if (next >= count) return wrap ? 0 : count - 1;
	return next;
}

int TG_window(int selected, int count, int fit, int first) {
	if (fit <= 0 || count <= fit) return 0;
	if (first > count - fit) first = count - fit;
	if (first < 0) first = 0;
	if (selected < first) first = selected;
	if (selected >= first + fit) first = selected - fit + 1;
	if (first < 0) first = 0;
	return first;
}
