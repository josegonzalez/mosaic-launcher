#ifndef STUBS_H
#define STUBS_H

#include <stdbool.h>

extern bool stub_show_recents;
extern bool stub_show_collections;
extern bool stub_show_tools;

#include "listsort.h"

extern Array* stub_root_replace;
extern int stub_root_with_entries;
extern void (*stub_root_entries)(Array* root);
extern Array* (*stub_quick_items)(void);
extern ListSortCompare stub_sort_compare;
extern int (*stub_list_ready)(Array* entries, const char* dir_path);

#endif
