#ifndef LISTSORT_H
#define LISTSORT_H

// Sorting a list with a chain of comparators, as plugins' sort_compare
// hooks are composed. Doesn't need SDL or the rest of the launcher.

#include "containers.h"
#include "mosaic_plugin.h"

typedef int (*ListSortCompare)(const MosaicEntry* a, const MosaicEntry* b, const char* dir_path);

// Reorders items by the comparators: for each pair, the first comparator
// to return non-zero decides, and pairs none has an opinion on keep their
// current order.
void ListSort_chain(Array* items, const ListSortCompare* compares, int count, const char* dir_path);

typedef int (*ListHideName)(const MosaicEntry* entry, const char* dir_path);

// Which items' names the hooks hide, as plugins' hide_name hooks are
// chained: one byte per item, 1 when one of them returned 1 for it, asking
// them in order until one does. Returns a new array the caller frees, or
// NULL when none is hidden (or there are no hooks or items).
unsigned char* ListHide_chain(const Array* items, const ListHideName* hides, int count, const char* dir_path);

// A plugin's section_key answer, rc what it returned and key what it
// wrote: taken when rc is 1 and key isn't empty once cut at its first
// newline and trimmed of spaces. Writes it to out, cut between UTF-8
// characters to fit size, and returns 1, or writes "" and returns 0.
int ListSection_take(int rc, const char* key, char* out, size_t size);

#endif
