#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "remap_model.h"
#include "mosaic_plugin.h"

static const char* const keys[REMAP_BUTTONS] = {
	[MOSAIC_BUTTON_A] = "a",
	[MOSAIC_BUTTON_B] = "b",
	[MOSAIC_BUTTON_X] = "x",
	[MOSAIC_BUTTON_Y] = "y",
	[MOSAIC_BUTTON_L1] = "l1",
	[MOSAIC_BUTTON_R1] = "r1",
	[MOSAIC_BUTTON_L2] = "l2",
	[MOSAIC_BUTTON_R2] = "r2",
	[MOSAIC_BUTTON_START] = "start",
	[MOSAIC_BUTTON_SELECT] = "select",
};

const char* Remap_key(int button) {
	if (button < 0 || button >= REMAP_BUTTONS) return NULL;
	return keys[button];
}

int Remap_stockOwned(int button) {
	return button == MOSAIC_BUTTON_A || button == MOSAIC_BUTTON_B || button == MOSAIC_BUTTON_X
		|| button == MOSAIC_BUTTON_L1 || button == MOSAIC_BUTTON_R1 || button == MOSAIC_BUTTON_SELECT;
}

static int isIdChar(int c) {
	return isalnum(c) || c == '.' || c == '_' || c == '-';
}

// Copies the id in [start, end) with spaces trimmed into out, which holds
// size. Returns 0, or -1 for an empty, too long or badly spelled one.
static int copyId(const char* start, const char* end, char* out, size_t size) {
	while (start < end && isspace((unsigned char)*start)) start++;
	while (end > start && isspace((unsigned char)end[-1])) end--;
	size_t len = (size_t)(end - start);
	if (len == 0 || len >= size) return -1;
	for (const char* p = start; p < end; p++) {
		if (!isIdChar((unsigned char)*p)) return -1;
	}
	memcpy(out, start, len);
	out[len] = '\0';
	return 0;
}

int Remap_parse(const char* value, RemapButton* out) {
	memset(out, 0, sizeof(*out));
	if (!value) return REMAP_UNSET;
	while (isspace((unsigned char)*value)) value++;
	if (!*value) return REMAP_UNSET;

	RemapButton b;
	memset(&b, 0, sizeof(b));
	b.set = 1;
	b.trigger = MOSAIC_TRIGGER_PRESS;
	const char* colon = strchr(value, ':');
	if (colon) {
		char word[16];
		if (copyId(value, colon, word, sizeof(word)) != 0) return REMAP_BAD;
		if (!strcasecmp(word, "press")) b.trigger = MOSAIC_TRIGGER_PRESS;
		else if (!strcasecmp(word, "release")) b.trigger = MOSAIC_TRIGGER_RELEASE;
		else if (!strcasecmp(word, "repeat")) b.trigger = MOSAIC_TRIGGER_REPEAT;
		else return REMAP_BAD;
		value = colon + 1;
	}

	const char* p = value;
	for (;;) {
		const char* comma = strchr(p, ',');
		const char* end = comma ? comma : p + strlen(p);
		if (b.count >= REMAP_ACTIONS_MAX) return REMAP_BAD;
		if (copyId(p, end, b.actions[b.count], sizeof(b.actions[0])) != 0) return REMAP_BAD;
		for (int i = 0; i < b.count; i++) {
			if (!strcmp(b.actions[i], b.actions[b.count])) return REMAP_BAD;
		}
		b.count++;
		if (!comma) break;
		p = comma + 1;
	}
	if (b.count == 1 && !strcasecmp(b.actions[0], "none")) b.count = 0;
	else {
		for (int i = 0; i < b.count; i++) {
			if (!strcasecmp(b.actions[i], "none")) return REMAP_BAD; // none with something else
		}
	}
	*out = b;
	return REMAP_SET;
}

static void setButton(RemapTable* table, int button, int trigger, const char* first, const char* second) {
	RemapButton* b = &table->buttons[button];
	memset(b, 0, sizeof(*b));
	b->set = 1;
	b->trigger = trigger;
	snprintf(b->actions[b->count++], REMAP_ID_LEN, "%s", first);
	if (second) snprintf(b->actions[b->count++], REMAP_ID_LEN, "%s", second);
}

void Remap_presets(RemapTable* table, int resume_with_a, int resume_on_press) {
	memset(table, 0, sizeof(*table));
	// each button keeps its stock timing: A on press, X on release
	if (resume_with_a) {
		setButton(table, MOSAIC_BUTTON_A, MOSAIC_TRIGGER_PRESS, "gamelist.resume", "gamelist.open");
		setButton(table, MOSAIC_BUTTON_X, MOSAIC_TRIGGER_RELEASE, "gamelist.open", NULL);
	}
	if (resume_on_press) {
		RemapButton* x = &table->buttons[MOSAIC_BUTTON_X];
		if (!x->set) setButton(table, MOSAIC_BUTTON_X, MOSAIC_TRIGGER_PRESS, "gamelist.resume", NULL);
		x->trigger = MOSAIC_TRIGGER_PRESS;
	}
}

int Remap_calls(const RemapTable* table, RemapCall* out) {
	int n = 0;
	for (int button = 0; button < REMAP_BUTTONS; button++) {
		const RemapButton* b = &table->buttons[button];
		if (b->set && b->count == 0) out[n++] = (RemapCall){ 0, button, b->trigger, NULL };
	}
	for (int rank = 0; rank < REMAP_ACTIONS_MAX; rank++) {
		for (int button = 0; button < REMAP_BUTTONS; button++) {
			const RemapButton* b = &table->buttons[button];
			if (!b->set || rank >= b->count) continue;
			// bind_button doesn't take repeat, so a free button that repeats is overridden
			int bind = !Remap_stockOwned(button) && b->trigger != MOSAIC_TRIGGER_REPEAT;
			out[n++] = (RemapCall){ bind, button, b->trigger, b->actions[rank] };
		}
	}
	return n;
}
