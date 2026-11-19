#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "media_model.h"

void SM_listInit(SMList* list) {
	memset(list, 0, sizeof(*list));
}

void SM_listFree(SMList* list) {
	free(list->items);
	SM_listInit(list);
}

// appends path, shown from label on; -1 when the list is full or out of memory
static int add(SMList* list, const char* path, int label, int kind) {
	if (list->count >= SM_MAX_ITEMS) return -1;
	if (list->count == list->cap) {
		int cap = list->cap ? list->cap * 2 : 16;
		if (cap > SM_MAX_ITEMS) cap = SM_MAX_ITEMS;
		SMItem* grown = realloc(list->items, (size_t)cap * sizeof(SMItem));
		if (!grown) return -1;
		list->items = grown;
		list->cap = cap;
	}
	SMItem* item = &list->items[list->count++];
	snprintf(item->path, sizeof(item->path), "%s", path);
	item->label = label;
	item->kind = kind;
	return 0;
}

// the file name in path, or NULL without a folder or a name
static const char* fileOf(const char* path) {
	const char* slash = path ? strrchr(path, '/') : NULL;
	if (!slash || slash == path || !slash[1]) return NULL;
	return slash + 1;
}

// path's file name less its last extension (all of it with keep), as out
static int stem(const char* path, int keep, char* out, size_t size) {
	if (size) out[0] = '\0';
	const char* file = fileOf(path);
	if (!file || !size) return 0;
	size_t n = strlen(file);
	const char* dot = keep ? NULL : strrchr(file, '.');
	if (dot && dot != file) n = (size_t)(dot - file);
	if (n == 0 || n >= size) return 0;
	memcpy(out, file, n);
	out[n] = '\0';
	return 1;
}

int SM_artName(const char* path, int is_dir, char* out, size_t size) {
	return stem(path, is_dir, out, size);
}

int SM_listName(const char* path, int is_dir, char* out, size_t size) {
	if (!SM_artName(path, is_dir, out, size)) return 0;
	size_t n = strlen(out);
	for (;;) {
		while (n > 0 && out[n - 1] == ' ') n--;
		if (n == 0 || (out[n - 1] != ')' && out[n - 1] != ']')) break;
		char open = out[n - 1] == ')' ? '(' : '[';
		size_t at = n - 1;
		while (at > 0 && out[at] != open) at--;
		if (out[at] != open || at == 0) break; // keep a name that's all tags
		n = at;
	}
	if (n == 0) return 1;
	out[n] = '\0';
	return 1;
}

// 1 for a real folder at path, not a link
static int isFolder(const char* path) {
	struct stat st;
	return lstat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// 1 for a regular file at path, not a link
static int isFile(const char* path) {
	struct stat st;
	return lstat(path, &st) == 0 && S_ISREG(st.st_mode);
}

typedef struct Finder {
	SMList* list; // NULL to only look
	int found;
	int stop;     // 1 to stop at the first
} Finder;

static int done(const Finder* f) {
	return f->stop && f->found;
}

// takes path when it's a regular file, shown from label on
static void take(Finder* f, const char* path, int label, int kind) {
	if (done(f) || !isFile(path)) return;
	if (f->list && add(f->list, path, label, kind) != 0) return;
	f->found++;
}

// <dir>/<folder>/<name><ext> for each ext, when folder is a real folder
static void lookIn(Finder* f, const char* dir, const char* folder, const char* name, const char* const* exts, int kind) {
	char path[SV_PATH_MAX];
	if (done(f) || snprintf(path, sizeof(path), "%s/%s", dir, folder) >= (int)sizeof(path) || !isFolder(path)) return;
	for (int i = 0; exts[i] && !done(f); i++) {
		if (snprintf(path, sizeof(path), "%s/%s/%s%s", dir, folder, name, exts[i]) >= (int)sizeof(path)) continue;
		take(f, path, (int)strlen(dir) + 1, kind);
	}
}

// the folder rom is in, as dir
static int dirOf(const char* rom, char* dir, size_t size) {
	const char* file = fileOf(rom);
	if (!file) return 0;
	size_t n = (size_t)(file - 1 - rom);
	if (n >= size) return 0;
	memcpy(dir, rom, n);
	dir[n] = '\0';
	return 1;
}

static void findImages(Finder* f, const char* rom, int is_dir) {
	static const char* const png[] = { ".png", NULL };
	static const char* const images[] = { ".png", ".jpg", ".jpeg", NULL };
	static const char* const kinds[] = { "boxart", "boxart2d", "mix", "screenshot", "wheel" };
	char dir[SV_PATH_MAX], name[SV_NAME_MAX], media[SV_PATH_MAX];
	if (!dirOf(rom, dir, sizeof(dir)) || !SM_artName(rom, is_dir, name, sizeof(name))) return;
	lookIn(f, dir, ".media", name, png, SM_IMAGE);
	// .media must be a real folder for the ones in it too
	if (snprintf(media, sizeof(media), "%s/.media", dir) < (int)sizeof(media) && isFolder(media)) {
		for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
			char folder[32];
			snprintf(folder, sizeof(folder), ".media/%s", kinds[i]);
			lookIn(f, dir, folder, name, images, SM_IMAGE);
		}
	}
	lookIn(f, dir, "Imgs", name, png, SM_IMAGE);
}

static void findClip(Finder* f, const char* rom) {
	static const char* const mp4[] = { ".mp4", NULL };
	char dir[SV_PATH_MAX], name[SV_NAME_MAX];
	// as video-previews names it: the file name less its last extension
	if (dirOf(rom, dir, sizeof(dir)) && stem(rom, 0, name, sizeof(name))) lookIn(f, dir, ".videos", name, mp4, SM_CLIP);
}

static void findShots(Finder* f, const char* game, const char* shots) {
	if (done(f) || !game || !game[0] || !shots) return;
	const char* folder = fileOf(shots);
	if (!folder) return;
	SVList all;
	SV_listInit(&all);
	SV_scan(shots, &all);
	for (int i = 0; i < all.count && !done(f); i++) {
		if (strcmp(all.shots[i].game, game) != 0) continue;
		char path[SV_PATH_MAX];
		if (snprintf(path, sizeof(path), "%s/%s", shots, all.shots[i].file) >= (int)sizeof(path)) continue;
		take(f, path, (int)(folder - shots), SM_IMAGE);
	}
	SV_listFree(&all);
}

static int find(Finder* f, const char* rom, int is_dir, const char* game, const char* shots, int clips) {
	int has_rom = rom && rom[0];
	if (has_rom) findImages(f, rom, is_dir);
	findShots(f, game, shots);
	if (has_rom && clips) findClip(f, rom);
	return f->found;
}

int SM_find(const char* rom, int is_dir, const char* game, const char* shots, int clips, SMList* list) {
	list->count = 0;
	Finder f = { list, 0, 0 };
	return find(&f, rom, is_dir, game, shots, clips);
}

int SM_any(const char* rom, int is_dir, const char* game, const char* shots, int clips) {
	Finder f = { NULL, 0, 1 };
	return find(&f, rom, is_dir, game, shots, clips) > 0;
}

const char* SM_label(const SMItem* item) {
	int n = (int)strlen(item->path);
	return item->path + (item->label >= 0 && item->label <= n ? item->label : 0);
}

void SM_title(const SMList* list, int index, char* out, size_t size) {
	if (!size) return;
	out[0] = '\0';
	if (index < 0 || index >= list->count) return;
	snprintf(out, size, "%i/%i \xc2\xb7 %s", index + 1, list->count, SM_label(&list->items[index]));
}

void SM_hints(const SMList* list, int index, MosaicHint left[1], int* left_count, MosaicHint right[1],
		int* right_count) {
	left[0] = (MosaicHint){ MOSAIC_BUTTON_B, "BACK" };
	*left_count = 1;
	*right_count = 0;
	if (index >= 0 && index < list->count && list->items[index].kind == SM_CLIP) {
		right[0] = (MosaicHint){ MOSAIC_BUTTON_A, "PLAY" };
		*right_count = 1;
	}
}
