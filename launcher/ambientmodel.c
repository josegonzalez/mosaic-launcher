#include <stdio.h>
#include <string.h>
#include "ambientmodel.h"

// The ids a setting names, in order, without repeats or blanks, at most max.
static int parse(const char* setting, char ids[][AMBIENT_ID_MAX], int max) {
	if (!setting || !setting[0]) setting = AMBIENT_ORDER_DEFAULT;
	int n = 0;
	const char* p = setting;
	while (*p && n < max) {
		const char* end = strchr(p, ',');
		size_t len = end ? (size_t)(end - p) : strlen(p);
		while (len && (*p == ' ' || *p == '\t')) {
			p++;
			len--;
		}
		while (len && (p[len - 1] == ' ' || p[len - 1] == '\t')) len--;
		if (len && len < AMBIENT_ID_MAX) {
			char id[AMBIENT_ID_MAX];
			memcpy(id, p, len);
			id[len] = '\0';
			int seen = 0;
			for (int i = 0; i < n && !seen; i++) seen = !strcmp(ids[i], id);
			if (!seen) snprintf(ids[n++], AMBIENT_ID_MAX, "%s", id);
		}
		if (!end) break;
		p = end + 1;
	}
	return n;
}

#define NAMED_MAX 64

int Ambient_order(const char* setting, const char* const* ids, int count, int* out) {
	char named[NAMED_MAX][AMBIENT_ID_MAX];
	int n = parse(setting, named, NAMED_MAX);
	int used[AMBIENT_MAX * 4] = { 0 };
	if (count > (int)(sizeof(used) / sizeof(used[0]))) count = (int)(sizeof(used) / sizeof(used[0]));
	int k = 0;
	for (int j = 0; j < n; j++) {
		for (int i = 0; i < count; i++) {
			if (!used[i] && ids[i] && !strcmp(ids[i], named[j])) {
				used[i] = 1;
				out[k++] = i;
				break;
			}
		}
	}
	for (int i = 0; i < count; i++) {
		if (!used[i]) out[k++] = i;
	}
	return count;
}

int Ambient_move(const char* setting, const char* const* shown, int count, int index, int step, char* out, size_t size) {
	if (index < 0 || index >= count || (step != -1 && step != 1)) return -1;
	int to = index + step;
	if (to < 0 || to >= count) return -1;
	char all[NAMED_MAX][AMBIENT_ID_MAX];
	int n = parse(setting, all, NAMED_MAX);
	// the shown ones the setting doesn't name yet go after it, in their order
	for (int i = 0; i < count && n < NAMED_MAX; i++) {
		int named = 0;
		for (int j = 0; j < n && !named; j++) named = !strcmp(all[j], shown[i]);
		if (!named) snprintf(all[n++], AMBIENT_ID_MAX, "%s", shown[i]);
	}
	int a = -1, b = -1;
	for (int j = 0; j < n; j++) {
		if (!strcmp(all[j], shown[index])) a = j;
		if (!strcmp(all[j], shown[to])) b = j;
	}
	if (a < 0 || b < 0) return -1;
	char swap[AMBIENT_ID_MAX];
	memcpy(swap, all[a], AMBIENT_ID_MAX);
	memcpy(all[a], all[b], AMBIENT_ID_MAX);
	memcpy(all[b], swap, AMBIENT_ID_MAX);
	size_t used = 0;
	if (size) out[0] = '\0';
	for (int j = 0; j < n; j++) {
		int w = snprintf(out + used, size - used, "%s%s", j ? "," : "", all[j]);
		if (w < 0 || (size_t)w >= size - used) return -1;
		used += (size_t)w;
	}
	return to;
}

void AmbientSet_clear(AmbientSet* set) {
	memset(set, 0, sizeof(*set));
}

int AmbientSet_add(AmbientSet* set, const char* id, int owner, int (*wants)(unsigned long idle_ms, void* data), void* data) {
	if (!id || !id[0] || strlen(id) >= AMBIENT_ID_MAX || !wants) return AMBIENT_BAD;
	if (AmbientSet_find(set, id) >= 0) return AMBIENT_TAKEN;
	if (set->count >= AMBIENT_MAX) return AMBIENT_FULL;
	Ambient* a = &set->items[set->count];
	memset(a, 0, sizeof(*a));
	snprintf(a->id, sizeof(a->id), "%s", id);
	a->owner = owner;
	a->wants = wants;
	a->data = data;
	set->order[set->count] = set->count;
	set->count++;
	return AMBIENT_ADDED;
}

int AmbientSet_find(const AmbientSet* set, const char* id) {
	if (!id) return -1;
	for (int i = 0; i < set->count; i++) {
		if (!strcmp(set->items[i].id, id)) return i;
	}
	return -1;
}

int AmbientSet_dropOwner(AmbientSet* set, int owner) {
	int kept = 0, dropped = 0;
	for (int i = 0; i < set->count; i++) {
		if (set->items[i].owner == owner) {
			dropped++;
			continue;
		}
		if (kept != i) set->items[kept] = set->items[i];
		kept++;
	}
	set->count = kept;
	for (int i = 0; i < kept; i++) set->order[i] = i;
	return dropped;
}

void AmbientSet_sort(AmbientSet* set, const char* setting) {
	const char* ids[AMBIENT_MAX];
	for (int i = 0; i < set->count; i++) ids[i] = set->items[i].id;
	Ambient_order(setting, ids, set->count, set->order);
}
