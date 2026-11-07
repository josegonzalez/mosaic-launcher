#ifndef LISTSORT_H
#define LISTSORT_H

// Sorting a list with a chain of comparators, as plugins' sort_compare
// hooks are composed. Doesn't need SDL or the rest of the launcher.

#include "containers.h"
#include "nextui_plugin.h"

typedef int (*ListSortCompare)(const NUIEntry* a, const NUIEntry* b, const char* dir_path);

// Reorders items by the comparators: for each pair, the first comparator
// to return non-zero decides, and pairs none has an opinion on keep their
// current order.
void ListSort_chain(Array* items, const ListSortCompare* compares, int count, const char* dir_path);

#endif
