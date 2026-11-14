// The first run wizard's model; see wizard_model.h.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wizard_model.h"

static void clear(FrWizard* w) {
	memset(w, 0, sizeof(*w));
	w->page = FR_WELCOME;
}

void FR_init(FrWizard* w, int toggles, int setups, int pin, int plugins, int lines) {
	clear(w);
	w->rows[FR_TOGGLES] = toggles < 0 ? 0 : toggles > FR_TOGGLES_MAX ? FR_TOGGLES_MAX : toggles;
	w->rows[FR_SETUP] = setups < 0 ? 0 : setups;
	w->rows[FR_PLUGINS] = plugins < 0 ? 0 : plugins;
	w->rows[FR_BUTTONS] = lines < 0 ? 0 : lines;
	w->has[FR_WELCOME] = 1;
	w->has[FR_TOGGLES] = w->rows[FR_TOGGLES] > 0;
	w->has[FR_SETUP] = w->rows[FR_SETUP] > 0;
	w->has[FR_PIN] = pin != 0;
	w->has[FR_PLUGINS] = w->rows[FR_PLUGINS] > 0;
	w->has[FR_BUTTONS] = w->rows[FR_BUTTONS] > 0;
	w->has[FR_DONE] = 1;
}

void FR_initButtons(FrWizard* w, int lines) {
	clear(w);
	w->rows[FR_BUTTONS] = lines < 0 ? 0 : lines;
	w->has[FR_BUTTONS] = 1;
	w->page = FR_BUTTONS;
	w->alone = 1;
}

int FR_nextPage(const FrWizard* w, int page) {
	for (int p = page + 1; p < FR_PAGES; p++) {
		if (w->has[p]) return p;
	}
	return -1;
}

int FR_prevPage(const FrWizard* w, int page) {
	for (int p = page - 1; p >= 0; p--) {
		if (w->has[p]) return p;
	}
	return -1;
}

int FR_pageNumber(const FrWizard* w, int page) {
	int n = 0;
	for (int p = 0; p <= page && p < FR_PAGES; p++) n += w->has[p];
	return n;
}

int FR_pageCount(const FrWizard* w) {
	return FR_pageNumber(w, FR_PAGES - 1);
}

static int maxScroll(const FrWizard* w) {
	int most = w->rows[FR_BUTTONS] - (w->visible > 0 ? w->visible : 1);
	return most < 0 ? 0 : most;
}

int FR_input(FrWizard* w, unsigned int pressed) {
	unsigned int a = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_A), b = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B);
	if (w->alone) {
		if (pressed & (a | b)) return FR_CLOSE;
	}
	else if (pressed & a) {
		int next = FR_nextPage(w, w->page);
		if (next < 0) return FR_FINISH;
		w->page = next;
		w->row = 0;
		return FR_REDRAW;
	}
	else if (pressed & b) {
		int prev = FR_prevPage(w, w->page);
		if (prev < 0) return FR_NOTHING;
		w->page = prev;
		w->row = 0;
		return FR_REDRAW;
	}
	else if (pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_Y)) {
		return FR_nextPage(w, w->page) < 0 ? FR_NOTHING : FR_SKIP;
	}
	int up = (pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_UP)) != 0;
	int down = (pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_DOWN)) != 0;
	int left = (pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_LEFT)) != 0;
	int right = (pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_RIGHT)) != 0;
	int rows = w->rows[w->page];
	if (w->page == FR_BUTTONS) {
		if (up && w->row > 0) {
			w->row--;
			return FR_REDRAW;
		}
		if (down && !up && w->row < maxScroll(w)) {
			w->row++;
			return FR_REDRAW;
		}
		return FR_NOTHING;
	}
	if ((w->page != FR_TOGGLES && w->page != FR_SETUP && w->page != FR_PLUGINS) || rows <= 0) return FR_NOTHING;
	if (up) {
		w->row = (w->row + rows - 1) % rows;
		return FR_REDRAW;
	}
	if (down) {
		w->row = (w->row + 1) % rows;
		return FR_REDRAW;
	}
	if ((left || right) && w->page == FR_TOGGLES) return FR_FLIP;
	if ((left || right) && w->page == FR_SETUP) {
		w->dir = left ? -1 : 1;
		return FR_CHANGE;
	}
	return FR_NOTHING;
}

void FR_setVisible(FrWizard* w, int visible) {
	w->visible = visible < 1 ? 1 : visible;
	if (w->row > maxScroll(w)) w->row = maxScroll(w);
	if (w->row < 0) w->row = 0;
}

int FR_shouldOpen(int seen, int opened, const char* current, const char* home) {
	if (seen || opened || !current) return 0;
	if (!strcmp(current, "gamelist")) return 1;
	return home && home[0] && !strcmp(current, home);
}

void FR_hints(const FrWizard* w, MosaicHint left[2], int* left_count, MosaicHint right[1], int* right_count) {
	*left_count = 0;
	*right_count = 0;
	if (w->alone) {
		left[(*left_count)++] = (MosaicHint){ MOSAIC_BUTTON_B, "BACK" };
		return;
	}
	int last = FR_nextPage(w, w->page) < 0;
	if (FR_prevPage(w, w->page) >= 0) left[(*left_count)++] = (MosaicHint){ MOSAIC_BUTTON_B, "BACK" };
	if (!last) left[(*left_count)++] = (MosaicHint){ MOSAIC_BUTTON_Y, "SKIP" };
	right[(*right_count)++] = (MosaicHint){ MOSAIC_BUTTON_A, last ? "DONE" : "NEXT" };
}

static int isSpace(char c) {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

int FR_parseIds(const char* text, char ids[][FR_ID_MAX], int max) {
	int count = 0;
	const char* p = text ? text : "";
	while (*p && count < max) {
		const char* end = strchr(p, ',');
		if (!end) end = p + strlen(p);
		const char* a = p;
		const char* b = end;
		while (a < b && isSpace(*a)) a++;
		while (b > a && isSpace(b[-1])) b--;
		size_t len = (size_t)(b - a);
		if (len > 0 && len < FR_ID_MAX) {
			char id[FR_ID_MAX];
			memcpy(id, a, len);
			id[len] = '\0';
			int repeat = 0;
			for (int i = 0; i < count && !repeat; i++) repeat = !strcmp(ids[i], id);
			if (!repeat) memcpy(ids[count++], id, len + 1);
		}
		p = *end ? end + 1 : end;
	}
	return count;
}

int FR_pluginLoaded(const MosaicHost* host, const char* id) {
	return host && id && id[0] && MOSAIC_HOST_HAS(host, plugin_loaded) && host->plugin_loaded(id);
}

int FR_value(const char* text, const char* key, char* out, size_t size) {
	if (out && size) out[0] = '\0';
	if (!text || !key || !key[0]) return -1;
	size_t want = strlen(key);
	const char* line = text;
	while (*line) {
		const char* end = strchr(line, '\n');
		if (!end) end = line + strlen(line);
		if ((size_t)(end - line) > want && !strncmp(line, key, want) && line[want] == '=') {
			const char* a = line + want + 1;
			const char* b = end;
			while (a < b && (*a == ' ' || *a == '\t')) a++;
			while (b > a && isSpace(b[-1])) b--;
			size_t len = (size_t)(b - a);
			if (out && size) {
				size_t copy = len < size - 1 ? len : size - 1;
				memcpy(out, a, copy);
				out[copy] = '\0';
			}
			return (int)len;
		}
		if (!*end) break;
		line = end + 1;
	}
	return -1;
}

int FR_manifest(const char* manifest, const char* id, char* name, size_t name_size, char* text, size_t text_size) {
	char found[FR_ID_MAX];
	if (!id || !id[0] || FR_value(manifest, "id", found, sizeof(found)) <= 0 || strcmp(found, id) != 0) return 0;
	if (FR_value(manifest, "name", name, name_size) <= 0) snprintf(name, name_size, "%s", id);
	if (FR_value(manifest, "description", text, text_size) <= 0) FR_value(manifest, "summary", text, text_size);
	return 1;
}

int FR_parseSetup(const char* reply, const char* key, FrSetup* out) {
	memset(out, 0, sizeof(*out));
	if (FR_value(reply, key, out->value, sizeof(out->value)) < 0) return -1;
	char k[96], n[16];
	snprintf(k, sizeof(k), "%s.count", key);
	int count = FR_value(reply, k, n, sizeof(n)) > 0 ? atoi(n) : 0;
	if (count > FR_CHOICES_MAX) count = FR_CHOICES_MAX;
	for (int i = 0; i < count; i++) {
		snprintf(k, sizeof(k), "%s.%i", key, i);
		if (FR_value(reply, k, out->values[out->count], sizeof(out->values[0])) < 0) continue;
		snprintf(k, sizeof(k), "%s.label.%i", key, i);
		if (FR_value(reply, k, out->labels[out->count], sizeof(out->labels[0])) <= 0) {
			memcpy(out->labels[out->count], out->values[out->count], sizeof(out->labels[0]));
		}
		out->count++;
	}
	out->restart = FR_value(reply, "restart", n, sizeof(n)) > 0 && !strcmp(n, "1");
	return out->count > 0 ? 0 : -1;
}

int FR_setupIndex(const FrSetup* s) {
	for (int i = 0; i < s->count; i++) {
		if (!strcmp(s->values[i], s->value)) return i;
	}
	return -1;
}

const char* FR_setupLabel(const FrSetup* s) {
	int i = FR_setupIndex(s);
	return i < 0 ? s->value : s->labels[i];
}

int FR_setupStep(const FrSetup* s, int dir) {
	if (s->count <= 0) return -1;
	int i = FR_setupIndex(s);
	if (i < 0) return dir < 0 ? s->count - 1 : 0;
	return (i + (dir < 0 ? s->count - 1 : 1)) % s->count;
}

const char* FR_buttonName(int button) {
	static const char* const names[] = { "A", "B", "X", "Y", "L1", "R1", "L2", "R2", "START", "SELECT" };
	if (button < 0 || button >= (int)(sizeof(names) / sizeof(names[0]))) return NULL;
	return names[button];
}

static int addLine(FrLine* out, int* n, int max, int heading, int button, int screen, const char* label) {
	if (*n >= max) return 0;
	FrLine* l = &out[(*n)++];
	l->heading = heading;
	l->button = button;
	l->screen = screen;
	snprintf(l->label, sizeof(l->label), "%s", label ? label : "");
	return 1;
}

static const char* actionLabel(const MosaicActionInfo* a) {
	return a->label[0] ? a->label : a->id;
}

// n, less a heading left last with none of its items
static int finished(const FrLine* out, int n) {
	return n > 0 && out[n - 1].heading ? n - 1 : n;
}

int FR_buttonLines(const MosaicActionInfo* actions, int count, const MosaicFnBinding fn[3], const int fn_there[3], FrLine* out,
	int max) {
	int n = 0;
	static const int order[] = { MOSAIC_BUTTON_A, MOSAIC_BUTTON_B, MOSAIC_BUTTON_X, MOSAIC_BUTTON_Y, MOSAIC_BUTTON_L1, MOSAIC_BUTTON_R1,
		MOSAIC_BUTTON_L2, MOSAIC_BUTTON_R2, MOSAIC_BUTTON_START, MOSAIC_BUTTON_SELECT, MOSAIC_BUTTON_FN1, MOSAIC_BUTTON_FN2,
		MOSAIC_BUTTON_FN3 };
	for (size_t o = 0; o < sizeof(order) / sizeof(order[0]); o++) {
		int button = order[o];
		int fn_index = button - MOSAIC_BUTTON_FN1;
		int is_fn = fn_index >= 0;
		if (is_fn && !(fn_there && fn_there[fn_index])) continue;
		int heading = n;
		if (!addLine(out, &n, max, 1, button, -1, NULL)) return finished(out, n);
		for (int i = 0; actions && i < count; i++) {
			const MosaicActionInfo* a = &actions[i];
			int bound = a->binding_count < MOSAIC_ACTION_BINDINGS ? a->binding_count : MOSAIC_ACTION_BINDINGS;
			for (int j = 0; j < bound; j++) {
				const MosaicActionBinding* b = &a->bindings[j];
				if (b->button != button || (is_fn != (b->screen < 0))) continue;
				if (!addLine(out, &n, max, 0, button, b->screen, actionLabel(a))) return finished(out, n);
			}
		}
		if (is_fn && fn && fn[fn_index].by != MOSAIC_FN_NONE && !strncmp(fn[fn_index].action, "pak:", 4)) {
			// a Tools pak, by its folder's name without .pak
			char pak[128];
			const char* folder = fn[fn_index].action + 4;
			size_t len = strlen(folder);
			if (len > 4 && !strcmp(folder + len - 4, ".pak")) len -= 4;
			snprintf(pak, sizeof(pak), "%.*s", (int)len, folder);
			if (!addLine(out, &n, max, 0, button, -1, pak)) return finished(out, n);
		}
		if (n == heading + 1) n = heading; // nothing on it
	}
	if (!addLine(out, &n, max, 1, -1, -1, NULL)) return finished(out, n);
	for (int i = 0; actions && i < count; i++) {
		if (actions[i].binding_count > 0) continue;
		if (!addLine(out, &n, max, 0, -1, -1, actionLabel(&actions[i]))) return finished(out, n);
	}
	return finished(out, n);
}

// Cuts line until line + "..." fits max_w, then adds the dots.
static void ellipsize(char* line, int max_w, int (*width)(const char*, void*), void* data) {
	char tried[FR_LINE_MAX];
	size_t len = strlen(line);
	for (;;) {
		while (len > 0 && line[len - 1] == ' ') len--;
		snprintf(tried, sizeof(tried), "%.*s...", (int)len, line);
		if (len == 0 || width(tried, data) <= max_w) break;
		len--;
	}
	snprintf(line, FR_LINE_MAX, "%s", tried);
}

int FR_wrap(const char* text, int max_w, int (*width)(const char* line, void* data), void* data, char (*lines)[FR_LINE_MAX],
	int max) {
	if (!text || !width || max <= 0 || max_w <= 0) return 0;
	int count = 0;
	const char* p = text;
	while (*p == ' ') p++;
	while (*p) {
		if (count == max) {
			ellipsize(lines[max - 1], max_w, width, data);
			return count;
		}
		char* line = lines[count];
		line[0] = '\0';
		size_t used = 0;
		while (*p) {
			const char* word_end = p;
			while (*word_end && *word_end != ' ') word_end++;
			size_t word = (size_t)(word_end - p);
			char tried[FR_LINE_MAX];
			int n = snprintf(tried, sizeof(tried), "%s%s%.*s", line, used ? " " : "", (int)word, p);
			int fits = n > 0 && (size_t)n < sizeof(tried) && width(tried, data) <= max_w;
			if (!fits && used) break; // the word starts the next line
			snprintf(line, FR_LINE_MAX, "%s", tried);
			used = strlen(line);
			p = word_end;
			while (*p == ' ') p++;
			if (!fits) break; // too wide alone: a line of its own
		}
		count++;
	}
	return count;
}
