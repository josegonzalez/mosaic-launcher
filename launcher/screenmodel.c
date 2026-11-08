#include <stdio.h>
#include <string.h>
#include "screenmodel.h"

void ScreenSet_clear(ScreenSet* set) {
	memset(set, 0, sizeof(*set));
}

static const char* const builtins[] = { "gamelist", "switcher", "quickmenu", "dialog" };

int Screen_builtinName(const char* id) {
	for (size_t i = 0; id && i < sizeof(builtins) / sizeof(builtins[0]); i++) {
		if (!strcmp(id, builtins[i])) return 1;
	}
	return 0;
}

int ScreenSet_find(const ScreenSet* set, const char* id) {
	for (int i = 0; id && i < set->count; i++) {
		if (!strcmp(set->screens[i].id, id)) return i;
	}
	return -1;
}

int ScreenSet_add(ScreenSet* set, const char* id, int owner, const MosaicScreenHooks* hooks) {
	if (!id || !id[0] || strlen(id) >= SCREEN_ID_MAX || Screen_builtinName(id)) return SCREEN_BAD_ID;
	size_t need = offsetof(MosaicScreenHooks, render) + sizeof(hooks->render);
	if (!hooks || hooks->size < need || !hooks->render) return SCREEN_BAD_HOOKS;
	if (ScreenSet_find(set, id) >= 0) return SCREEN_TAKEN;
	if (set->count >= SCREENS_MAX) return SCREEN_FULL;
	PluginScreen* s = &set->screens[set->count++];
	memset(s, 0, sizeof(*s));
	snprintf(s->id, sizeof(s->id), "%s", id);
	s->owner = owner;
	// only what the plugin's header has; the rest stays zero
	memcpy(&s->hooks, hooks, hooks->size < sizeof(s->hooks) ? hooks->size : sizeof(s->hooks));
	s->hooks.size = sizeof(s->hooks);
	return SCREEN_ADDED;
}

int ScreenSet_dropOwner(ScreenSet* set, int owner) {
	int kept = 0, dropped = 0;
	for (int i = 0; i < set->count; i++) {
		if (set->screens[i].owner == owner) {
			dropped++;
			continue;
		}
		if (kept != i) set->screens[kept] = set->screens[i];
		kept++;
	}
	set->count = kept;
	return dropped;
}

int ScreenSet_position(const ScreenSet* set, int index) {
	for (int i = 0; i < set->depth; i++) {
		if (set->stack[i] == index) return i;
	}
	return -1;
}

int ScreenSet_canOpen(const ScreenSet* set, int index) {
	if (index < 0 || index >= set->count) return SCREEN_UNKNOWN;
	if (set->screens[index].off) return SCREEN_IS_OFF;
	if (ScreenSet_position(set, index) >= 0) return SCREEN_IS_OPEN;
	if (set->depth >= SCREEN_STACK_MAX) return SCREEN_STACK_FULL;
	return SCREEN_OPENS;
}

int ScreenSet_push(ScreenSet* set, int index, int base) {
	int why = ScreenSet_canOpen(set, index);
	if (why != SCREEN_OPENS) return why;
	if (set->depth == 0) set->base = base;
	set->stack[set->depth++] = index;
	return SCREEN_OPENS;
}

int ScreenSet_top(const ScreenSet* set) {
	return set->depth > 0 ? set->stack[set->depth - 1] : -1;
}

int ScreenSet_pop(ScreenSet* set) {
	if (set->depth == 0) return -1;
	return set->stack[--set->depth];
}

int ScreenKind_slides(int from, int to) {
	return from != SCREENKIND_SWITCHER && from != SCREENKIND_DIALOG && to != SCREENKIND_SWITCHER && to != SCREENKIND_DIALOG;
}

int ScreenKind_overlays(int kind, int flags) {
	if (kind == SCREENKIND_DIALOG) return 0;
	if (kind == SCREENKIND_PLUGIN && (flags & MOSAIC_SCREEN_FULL)) return 0;
	return 1;
}
