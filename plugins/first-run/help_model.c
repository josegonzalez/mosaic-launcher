#include "help_model.h"

#include <stdio.h>
#include <string.h>

// the order lines are in
static const int order[] = {
	MOSAIC_BUTTON_A,
	MOSAIC_BUTTON_B,
	MOSAIC_BUTTON_X,
	MOSAIC_BUTTON_Y,
	MOSAIC_BUTTON_L1,
	MOSAIC_BUTTON_R1,
	MOSAIC_BUTTON_L2,
	MOSAIC_BUTTON_R2,
	MOSAIC_BUTTON_START,
	MOSAIC_BUTTON_SELECT,
	MOSAIC_BUTTON_UP,
	MOSAIC_BUTTON_DOWN,
	MOSAIC_BUTTON_LEFT,
	MOSAIC_BUTTON_RIGHT,
	MOSAIC_GLYPH_UP_DOWN,
	MOSAIC_GLYPH_LEFT_RIGHT,
	MOSAIC_GLYPH_DPAD,
	MOSAIC_BUTTON_MENU,
	MOSAIC_BUTTON_POWER,
	MOSAIC_BUTTON_FN1,
	MOSAIC_BUTTON_FN2,
	MOSAIC_BUTTON_FN3,
};
#define ORDER_COUNT ((int)(sizeof(order) / sizeof(order[0])))

#define ITEMS_MAX 16

// What one button does, as it's gathered.
typedef struct Items {
	const char* text[ITEMS_MAX];
	int count;
} Items;

static void add(Items* items, const char* text) {
	if (!text || !text[0] || items->count >= ITEMS_MAX) return;
	for (int i = 0; i < items->count; i++) {
		if (!strcmp(items->text[i], text)) return;
	}
	items->text[items->count++] = text;
}

static int isFn(int button) {
	return button >= MOSAIC_BUTTON_FN1 && button <= MOSAIC_BUTTON_FN3;
}

static int fnThere(const FrHelpSource* s, int button) {
	return isFn(button) && s->fn_there && s->fn_there[button - MOSAIC_BUTTON_FN1];
}

// Whether a binding works on the screen showing: the game list's on the
// game list, and an FN button's anywhere but a dialog, on a device with it.
static int applies(const FrHelpSource* s, const MosaicActionBinding* b) {
	const char* screen = s->screen ? s->screen : "";
	if (b->screen < 0) return fnThere(s, b->button) && strcmp(screen, "dialog") != 0;
	return b->screen == MOSAIC_SCREEN_GAMELIST && !strcmp(screen, "gamelist");
}

static const char* wording(const FrHelpSource* s, const MosaicActionInfo* a) {
	const char* label = a->label[0] ? a->label : a->id;
	const char* worded = s->label ? s->label(a->id, label) : NULL;
	return worded && worded[0] ? worded : label;
}

static int bindings(const MosaicActionInfo* a) {
	return a->binding_count < MOSAIC_ACTION_BINDINGS ? a->binding_count : MOSAIC_ACTION_BINDINGS;
}

// Appends len bytes of text to out, cut to fit.
static void append(char* out, size_t size, const char* text, size_t len) {
	size_t used = strlen(out);
	if (used + 1 >= size) return;
	if (len > size - used - 1) len = size - used - 1;
	memcpy(out + used, text, len);
	out[used + len] = '\0';
}

// Appends text to out, after ", " when out has something.
static void join(char* out, size_t size, const char* text) {
	size_t used = strlen(out);
	if (used + 1 >= size) return;
	snprintf(out + used, size - used, "%s%s", used ? ", " : "", text);
}

static void gather(const FrHelpSource* s, int button, char* out, size_t size) {
	Items first = { { 0 }, 0 };
	Items shared = { { 0 }, 0 };
	for (int i = 0; i < s->own_count; i++) {
		if (s->own[i].button == button) add(&first, s->own[i].label);
	}
	// the screen's own and overrides first, noting the triggers they take
	unsigned int taken = 0;
	for (int i = 0; i < s->count; i++) {
		const MosaicActionInfo* a = &s->actions[i];
		if (!s->available || !s->available[i]) continue;
		for (int j = 0; j < bindings(a); j++) {
			const MosaicActionBinding* b = &a->bindings[j];
			if (b->button != button || b->by == MOSAIC_BIND_BUTTON || !applies(s, b)) continue;
			add(&first, wording(s, a));
			taken |= 1u << (b->trigger & 31);
		}
	}
	if (fnThere(s, button) && s->fn_pak) add(&first, s->fn_pak[button - MOSAIC_BUTTON_FN1]);
	for (int i = 0; i < s->count; i++) {
		const MosaicActionInfo* a = &s->actions[i];
		if (!s->available || !s->available[i]) continue;
		for (int j = 0; j < bindings(a); j++) {
			const MosaicActionBinding* b = &a->bindings[j];
			if (b->button != button || b->by != MOSAIC_BIND_BUTTON || !applies(s, b)) continue;
			if (taken & (1u << (b->trigger & 31))) continue;
			add(&shared, wording(s, a));
		}
	}
	out[0] = '\0';
	for (int i = 0; i < first.count; i++) join(out, size, first.text[i]);
	if (shared.count == 1) join(out, size, shared.text[0]);
	else if (shared.count > 1) {
		char list[FR_HELP_TEXT] = "";
		for (int i = 0; i < shared.count; i++) join(list, sizeof(list), shared.text[i]);
		// the format is the plugin's own text, with one %s
		const char* format = s->menu && strstr(s->menu, "%s") ? s->menu : "%s";
		const char* at = strstr(format, "%s");
		char whole[FR_HELP_TEXT] = "";
		append(whole, sizeof(whole), format, (size_t)(at - format));
		append(whole, sizeof(whole), list, strlen(list));
		append(whole, sizeof(whole), at + 2, strlen(at + 2));
		join(out, size, whole);
	}
}

int FR_helpLines(const FrHelpSource* source, FrHelpLine* out, int max) {
	int n = 0;
	if (!source || (source->screen && !strcmp(source->screen, "dialog"))) return 0;
	for (int i = 0; i < ORDER_COUNT && n < max; i++) {
		char text[FR_HELP_TEXT];
		gather(source, order[i], text, sizeof(text));
		if (!text[0]) continue;
		out[n].button = order[i];
		snprintf(out[n].text, sizeof(out[n].text), "%s", text);
		n++;
	}
	return n;
}

const char* FR_helpButtonName(int button) {
	static const char* const names[] = { "A", "B", "X", "Y", "L1", "R1", "L2", "R2", "START", "SELECT", "UP", "DOWN", "LEFT",
		"RIGHT", "MENU", "POWER" };
	if (button >= 0 && button < (int)(sizeof(names) / sizeof(names[0]))) return names[button];
	if (button == MOSAIC_GLYPH_UP_DOWN) return "UP/DOWN";
	if (button == MOSAIC_GLYPH_LEFT_RIGHT) return "LEFT/RIGHT";
	if (button == MOSAIC_GLYPH_DPAD) return "D-PAD";
	return NULL;
}

///////////////////////////////////////
// input

void FR_helpInit(FrHelp* h, int hold) {
	memset(h, 0, sizeof(*h));
	h->hold = hold;
}

void FR_helpOpen(FrHelp* h) {
	h->open = 1;
	h->drawn = 0;
	h->swallow |= h->held;
}

void FR_helpClose(FrHelp* h) {
	h->open = 0;
	h->drawn = 0;
	h->swallow |= h->held;
}

void FR_helpDrawn(FrHelp* h) {
	if (h->open) h->drawn = 1;
}

unsigned int FR_helpFilter(FrHelp* h, const MosaicInput* input, int* closed) {
	unsigned int power = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_POWER);
	unsigned int consumed = 0;
	h->held = input->held;
	if (h->swallow) {
		consumed = (input->repeated | input->released) & h->swallow;
		h->swallow &= input->held & ~input->released;
	}
	if (h->open) {
		if (h->drawn) consumed |= (input->pressed | input->repeated | input->released) & ~power;
		if (input->pressed) {
			int drawn = h->drawn;
			h->open = 0;
			h->drawn = 0;
			// what's down goes on being taken, but not a press that wasn't seen over
			h->swallow = drawn ? input->held & ~power : 0;
			if (closed) *closed = 1;
		}
		h->holding = 0;
		return consumed;
	}
	if (h->hold >= 0) {
		unsigned int bit = MOSAIC_BUTTON_BIT(h->hold);
		if ((input->pressed & bit) && input->held == bit) {
			h->holding = 1;
			h->fired = 0;
			h->since = h->now;
		}
		else if (input->held != bit) h->holding = 0;
	}
	return consumed;
}

int FR_helpTick(FrHelp* h, unsigned long now) {
	h->now = now;
	if (h->open || !h->holding || h->fired || now - h->since < FR_HELP_HOLD_MS) return 0;
	h->fired = 1;
	return 1;
}
