#include "genre_model.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int endsWith(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcmp(s + a - b, suffix);
}

///////////////////////////////////////
// names

void GV_cleanName(const char* file_name, char* out, size_t size) {
	if (!size) return;
	snprintf(out, size, "%s", file_name);

	char* dot;
	while ((dot = strrchr(out, '.')) != NULL) {
		size_t len = strlen(dot);
		if (len < 3 || len > 5) break;
		*dot = '\0';
	}

	char bare[GV_NAME_LEN];
	snprintf(bare, sizeof(bare), "%s", out);
	for (;;) {
		char* open = strrchr(out, '(');
		if (!open) open = strrchr(out, '[');
		if (!open || open == out) break;
		*open = '\0';
	}
	if (!out[0]) snprintf(out, size, "%s", bare);

	size_t len = strlen(out);
	while (len > 1 && isspace((unsigned char)out[len - 1])) out[--len] = '\0';
}

void GV_key(const char* name, char* out, size_t size) {
	if (!size) return;
	size_t n = 0;
	int space = 0;
	for (const unsigned char* p = (const unsigned char*)name; *p && n + 1 < size; p++) {
		if (*p >= 0x80 || isalnum(*p)) {
			if (space && n > 0 && n + 2 < size) out[n++] = ' ';
			space = 0;
			out[n++] = (char)tolower(*p);
		}
		else space = 1;
	}
	out[n] = '\0';
	if (n) return;
	// nothing but punctuation: match it as it is
	for (; name[n] && n + 1 < size; n++) out[n] = (char)tolower((unsigned char)name[n]);
	out[n] = '\0';
}

int GV_listHas(const char* list, const char* name) {
	if (!list || !name) return 0;
	char want[GV_NAME_LEN];
	GV_key(name, want, sizeof(want));
	while (*list) {
		const char* comma = strchr(list, ',');
		size_t len = comma ? (size_t)(comma - list) : strlen(list);
		char item[GV_NAME_LEN];
		if (len >= sizeof(item)) len = sizeof(item) - 1;
		memcpy(item, list, len);
		item[len] = '\0';
		char key[GV_NAME_LEN];
		GV_key(item, key, sizeof(key));
		if (key[0] && !strcmp(key, want)) return 1;
		if (!comma) break;
		list = comma + 1;
	}
	return 0;
}

int GV_hidden(const char* name) {
	return name[0] == '.' || endsWith(name, ".disabled") || !strcmp(name, "map.txt");
}

///////////////////////////////////////
// map.txt

static int compareMapItems(const void* a, const void* b) {
	const GVMapItem* x = a;
	const GVMapItem* y = b;
	int c = strcmp(x->key, y->key);
	return c ? c : x->line - y->line;
}

int GV_mapParse(GVMap* map, char* text) {
	memset(map, 0, sizeof(*map));
	map->text = text;
	if (!text) return 0;

	int lines = 1;
	for (const char* p = text; *p; p++) {
		if (*p == '\n') lines++;
	}
	map->items = malloc(sizeof(GVMapItem) * (size_t)lines);
	if (!map->items) return -1;

	int line = 0;
	for (char* p = text; p; line++) {
		char* end = strchr(p, '\n');
		if (end) *end = '\0';
		size_t len = strlen(p);
		while (len > 0 && p[len - 1] == '\r') p[--len] = '\0';
		char* tab = strchr(p, '\t');
		if (tab && tab[1]) {
			*tab = '\0';
			map->items[map->count++] = (GVMapItem){ p, tab + 1, line };
		}
		p = end ? end + 1 : NULL;
	}

	// the later of two lines for a file name wins: keep only the last
	qsort(map->items, (size_t)map->count, sizeof(GVMapItem), compareMapItems);
	int kept = 0;
	for (int i = 0; i < map->count; i++) {
		if (i + 1 < map->count && !strcmp(map->items[i].key, map->items[i + 1].key)) continue;
		map->items[kept++] = map->items[i];
	}
	map->count = kept;
	return 0;
}

const char* GV_mapGet(const GVMap* map, const char* file_name) {
	if (!map || !map->count) return NULL;
	int lo = 0, hi = map->count - 1;
	while (lo <= hi) {
		int mid = lo + (hi - lo) / 2;
		int c = strcmp(file_name, map->items[mid].key);
		if (!c) return map->items[mid].value;
		if (c < 0) hi = mid - 1;
		else lo = mid + 1;
	}
	return NULL;
}

void GV_mapFree(GVMap* map) {
	free(map->items);
	free(map->text);
	memset(map, 0, sizeof(*map));
}

int GV_shownName(const GVMap* map, const char* file_name, char* out, size_t size) {
	if (GV_hidden(file_name)) return 0;
	const char* alias = GV_mapGet(map, file_name);
	if (alias) snprintf(out, size, "%s", alias);
	else GV_cleanName(file_name, out, size);
	return !GV_hidden(out); // map.txt can hide an entry
}

///////////////////////////////////////
// systems and paths

void GV_emuTag(const char* system, char* out, size_t size) {
	if (!size) return;
	const char* open = strrchr(system, '(');
	if (!open) {
		snprintf(out, size, "%s", system);
		return;
	}
	const char* close = strchr(open + 1, ')');
	size_t len = close ? (size_t)(close - open - 1) : strlen(open + 1);
	if (len >= size) len = size - 1;
	memcpy(out, open + 1, len);
	out[len] = '\0';
}

void GV_systemName(const char* system, char* out, size_t size) {
	if (!size) return;
	snprintf(out, size, "%s", system);
	char* open = strrchr(out, '(');
	if (open && open != out) *open = '\0';
	size_t len = strlen(out);
	while (len > 1 && isspace((unsigned char)out[len - 1])) out[--len] = '\0';
}

int GV_systemOf(const char* roms, const char* path, char* out, size_t size) {
	size_t len = strlen(roms);
	if (!path || strncmp(path, roms, len) != 0 || path[len] != '/') return 0;
	const char* system = path + len + 1;
	const char* slash = strchr(system, '/');
	if (!slash || slash == system || !slash[1]) return 0;
	if (size) {
		size_t n = (size_t)(slash - system);
		if (n >= size) n = size - 1;
		memcpy(out, system, n);
		out[n] = '\0';
	}
	return 1;
}

int GV_pathNames(const char* folder, const char* path, char names[][GV_NAME_LEN], int max) {
	size_t len = strlen(folder);
	if (!path || strncmp(path, folder, len) != 0) return -1;
	const char* p = path + len;
	if (!*p) return 0;
	if (*p != '/') return -1;

	int count = 0;
	while (*p == '/') {
		p++;
		const char* slash = strchr(p, '/');
		size_t n = slash ? (size_t)(slash - p) : strlen(p);
		if (!n || n >= GV_NAME_LEN || count >= max) return -1;
		memcpy(names[count], p, n);
		names[count][n] = '\0';
		count++;
		p += n;
	}
	return count;
}

///////////////////////////////////////
// merged folders

static int grow(void** items, int* cap, int count, size_t item_size) {
	if (count < *cap) return 0;
	int next = *cap ? *cap * 2 : 16;
	void* bigger = realloc(*items, item_size * (size_t)next);
	if (!bigger) return -1;
	*items = bigger;
	*cap = next;
	return 0;
}

void GV_groupsInit(GVGroups* groups) {
	memset(groups, 0, sizeof(*groups));
}

static void groupFree(GVGroup* group) {
	for (int k = 0; k < group->count; k++) {
		free(group->sources[k].path);
		free(group->sources[k].system);
	}
	free(group->sources);
	free(group->name);
	free(group->key);
}

void GV_groupsFree(GVGroups* groups) {
	for (int i = 0; i < groups->count; i++) groupFree(&groups->items[i]);
	free(groups->items);
	GV_groupsInit(groups);
}

int GV_groupsFind(const GVGroups* groups, const char* name) {
	char key[GV_NAME_LEN];
	GV_key(name, key, sizeof(key));
	for (int i = 0; i < groups->count; i++) {
		if (!strcmp(groups->items[i].key, key)) return i;
	}
	return -1;
}

int GV_groupsAdd(GVGroups* groups, const char* name, const char* path, const char* system) {
	if (!name[0] || strchr(name, '/')) return -1;
	int i = GV_groupsFind(groups, name);
	if (i < 0) {
		if (grow((void**)&groups->items, &groups->cap, groups->count, sizeof(GVGroup)) != 0) return -1;
		GVGroup* group = &groups->items[groups->count];
		memset(group, 0, sizeof(*group));
		char key[GV_NAME_LEN];
		GV_key(name, key, sizeof(key));
		group->name = strdup(name);
		group->key = strdup(key);
		if (!group->name || !group->key) {
			groupFree(group);
			return -1;
		}
		i = groups->count++;
	}

	GVGroup* group = &groups->items[i];
	if (grow((void**)&group->sources, &group->cap, group->count, sizeof(GVSource)) != 0) return -1;
	GVSource* source = &group->sources[group->count];
	source->path = strdup(path);
	source->system = strdup(system);
	if (!source->path || !source->system) {
		free(source->path);
		free(source->system);
		return -1;
	}
	group->count++;
	return i;
}

static int compareGroups(const void* a, const void* b) {
	const GVGroup* x = a;
	const GVGroup* y = b;
	int c = strcasecmp(x->name, y->name);
	return c ? c : strcmp(x->name, y->name);
}

void GV_groupsSort(GVGroups* groups) {
	if (groups->count > 1) qsort(groups->items, (size_t)groups->count, sizeof(GVGroup), compareGroups);
}

int GV_groupSystems(const GVGroup* group) {
	int systems = 0;
	for (int k = 0; k < group->count; k++) {
		char name[GV_NAME_LEN];
		GV_systemName(group->sources[k].system, name, sizeof(name));
		int seen = 0;
		for (int j = 0; j < k && !seen; j++) {
			char other[GV_NAME_LEN];
			GV_systemName(group->sources[j].system, other, sizeof(other));
			seen = !strcmp(name, other);
		}
		if (!seen) systems++;
	}
	return systems;
}

int GV_groupsKeep(GVGroups* groups, int min, const char* skip) {
	int kept = 0;
	for (int i = 0; i < groups->count; i++) {
		GVGroup* group = &groups->items[i];
		if (GV_groupSystems(group) < min || GV_listHas(skip, group->name)) {
			groupFree(group);
			continue;
		}
		groups->items[kept++] = *group;
	}
	groups->count = kept;
	return kept;
}

///////////////////////////////////////
// a merged folder's list

void GV_itemsInit(GVItems* items) {
	memset(items, 0, sizeof(*items));
}

void GV_itemsFree(GVItems* items) {
	for (int i = 0; i < items->count; i++) {
		free(items->items[i].path);
		free(items->items[i].name);
	}
	free(items->items);
	GV_itemsInit(items);
}

int GV_itemsAdd(GVItems* items, const char* path, const char* name, int type) {
	if (grow((void**)&items->items, &items->cap, items->count, sizeof(GVItem)) != 0) return -1;
	GVItem* item = &items->items[items->count];
	item->path = strdup(path);
	item->name = strdup(name);
	item->type = type;
	if (!item->path || !item->name) {
		free(item->path);
		free(item->name);
		return -1;
	}
	items->count++;
	return 0;
}

static int compareItems(const void* a, const void* b) {
	const GVItem* x = a;
	const GVItem* y = b;
	int c = strcasecmp(x->name, y->name);
	return c ? c : strcmp(x->path, y->path);
}

void GV_itemsSort(GVItems* items) {
	if (items->count > 1) qsort(items->items, (size_t)items->count, sizeof(GVItem), compareItems);
}

///////////////////////////////////////
// telling games apart

int GV_tagsMode(const char* value) {
	if (value && !strcmp(value, "off")) return GV_TAGS_OFF;
	if (value && !strcmp(value, "always")) return GV_TAGS_ALWAYS;
	return GV_TAGS_SHARED;
}

typedef struct Named {
	const char* name;
	const char* system;
	int index;
} Named;

static int compareNamed(const void* a, const void* b) {
	const Named* x = a;
	const Named* y = b;
	int c = strcmp(x->name, y->name);
	return c ? c : strcmp(x->system, y->system);
}

int GV_tags(const char* const* names, const char* const* systems, int count, int mode, int* tagged) {
	for (int i = 0; i < count; i++) tagged[i] = 0;
	if (mode == GV_TAGS_OFF || count < 1) return 0;

	int marked = 0;
	if (mode == GV_TAGS_ALWAYS) {
		for (int i = 0; i < count; i++) {
			tagged[i] = systems[i] != NULL;
			marked += tagged[i];
		}
		return marked;
	}

	Named* sorted = malloc(sizeof(Named) * (size_t)count);
	if (!sorted) return -1;
	int n = 0;
	for (int i = 0; i < count; i++) {
		if (systems[i]) sorted[n++] = (Named){ names[i], systems[i], i };
	}
	qsort(sorted, (size_t)n, sizeof(Named), compareNamed);
	for (int start = 0; start < n; ) {
		int end = start + 1;
		while (end < n && !strcmp(sorted[end].name, sorted[start].name)) end++;
		// sorted by system within a name, so the ends differ when any do
		if (strcmp(sorted[start].system, sorted[end - 1].system) != 0) {
			for (int k = start; k < end; k++) tagged[sorted[k].index] = 1;
			marked += end - start;
		}
		start = end;
	}
	free(sorted);
	return marked;
}
