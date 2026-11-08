#include <stdio.h>
#include <string.h>
#include "selmodel.h"

void SelKey_reset(SelKey* key) {
	key->top = NULL;
	key->entries = NULL;
	key->selected = -1;
	key->seen = 0;
}

int SelKey_changed(SelKey* key, const void* top, const void* entries, int selected) {
	if (key->seen && top == key->top && entries == key->entries && selected == key->selected) return 0;
	key->top = top;
	key->entries = entries;
	key->selected = selected;
	key->seen = 1;
	return 1;
}

void SelSettle_init(SelSettle* s) {
	s->moving = 0;
	s->moved_at = 0;
	s->sent = 0;
	s->folder[0] = '\0';
	s->entry[0] = '\0';
	s->now_folder[0] = '\0';
	s->now_entry[0] = '\0';
}

int SelSettle_step(SelSettle* s, unsigned long now, int moved, int held, const char* folder, const char* entry) {
	if (!folder) folder = "";
	if (!entry) entry = "";
	if (strcmp(s->now_folder, folder) || strcmp(s->now_entry, entry)) {
		snprintf(s->now_folder, sizeof(s->now_folder), "%s", folder);
		snprintf(s->now_entry, sizeof(s->now_entry), "%s", entry);
		moved = 1;
	}
	if (moved) {
		s->moving = 1;
		s->moved_at = now;
	}
	if (!s->moving || held || now - s->moved_at < SEL_SETTLE_MS) return 0;
	s->moving = 0;
	if (s->sent && !strcmp(s->folder, folder) && !strcmp(s->entry, entry)) return 0;
	snprintf(s->folder, sizeof(s->folder), "%s", folder);
	snprintf(s->entry, sizeof(s->entry), "%s", entry);
	s->sent = 1;
	return 1;
}
