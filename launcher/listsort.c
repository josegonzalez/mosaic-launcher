#include <stdlib.h>
#include "listsort.h"

typedef struct Ranked {
	void* item;
	int index; // where it was before sorting, so ties keep their order
} Ranked;

// qsort has no context argument; the launcher sorts on one thread.
static const ListSortCompare* chain;
static int chain_count;
static const char* chain_dir;

static int compareRanked(const void* x, const void* y) {
	const Ranked* a = x;
	const Ranked* b = y;
	for (int i = 0; i < chain_count; i++) {
		int order = chain[i]((const MosaicEntry*)a->item, (const MosaicEntry*)b->item, chain_dir);
		if (order) return order < 0 ? -1 : 1;
	}
	return a->index - b->index;
}

void ListSort_chain(Array* items, const ListSortCompare* compares, int count, const char* dir_path) {
	if (!items || items->count < 2 || count <= 0) return;
	Ranked* ranked = malloc(sizeof(Ranked) * items->count);
	if (!ranked) return;
	for (int i = 0; i < items->count; i++) {
		ranked[i].item = items->items[i];
		ranked[i].index = i;
	}
	chain = compares;
	chain_count = count;
	chain_dir = dir_path;
	qsort(ranked, items->count, sizeof(Ranked), compareRanked);
	for (int i = 0; i < items->count; i++) items->items[i] = ranked[i].item;
	free(ranked);
}

unsigned char* ListHide_chain(const Array* items, const ListHideName* hides, int count, const char* dir_path) {
	if (!items || items->count <= 0 || count <= 0) return NULL;
	unsigned char* hidden = NULL;
	for (int i = 0; i < items->count; i++) {
		int hide = 0;
		for (int h = 0; h < count && !hide; h++) hide = hides[h]((const MosaicEntry*)items->items[i], dir_path) != 0;
		if (!hide) continue;
		if (!hidden) hidden = calloc(items->count, 1);
		if (!hidden) return NULL;
		hidden[i] = 1;
	}
	return hidden;
}
