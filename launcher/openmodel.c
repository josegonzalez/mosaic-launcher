#include "openmodel.h"

#include "mosaic_plugin.h"

int OpenModel_asks(int type) {
	return type == MOSAIC_ENTRY_ROM || type == MOSAIC_ENTRY_PAK;
}

int OpenModel_first(int count, int (*ask)(int index, void* data), void* data) {
	if (!ask) return -1;
	for (int i = 0; i < count; i++) {
		if (ask(i, data) == 1) return i;
	}
	return -1;
}
