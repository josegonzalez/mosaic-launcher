// The first run wizard's model; see wizard_model.h.

#include <stdio.h>
#include <string.h>
#include "wizard_model.h"

// English the plugin translates where it draws it, for make pot
#define TR_MARK(text) (text)

void FR_init(FrWizard* w, int rows, int pin, int plugins) {
	memset(w, 0, sizeof(*w));
	w->rows = rows < 0 ? 0 : rows > FR_TOGGLES_MAX ? FR_TOGGLES_MAX : rows;
	w->has[FR_WELCOME] = 1;
	w->has[FR_TOGGLES] = w->rows > 0;
	w->has[FR_PIN] = pin != 0;
	w->has[FR_PLUGINS] = plugins != 0;
	w->has[FR_DONE] = 1;
	w->page = FR_WELCOME;
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

int FR_input(FrWizard* w, unsigned int pressed) {
	if (pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_A)) {
		int next = FR_nextPage(w, w->page);
		if (next < 0) return FR_FINISH;
		w->page = next;
		w->row = 0;
		return FR_REDRAW;
	}
	if (pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B)) {
		int prev = FR_prevPage(w, w->page);
		if (prev < 0) return FR_NOTHING;
		w->page = prev;
		w->row = 0;
		return FR_REDRAW;
	}
	if (pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_Y)) {
		return FR_nextPage(w, w->page) < 0 ? FR_NOTHING : FR_SKIP;
	}
	if (w->page != FR_TOGGLES || w->rows <= 0) return FR_NOTHING;
	if (pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_UP)) {
		w->row = (w->row + w->rows - 1) % w->rows;
		return FR_REDRAW;
	}
	if (pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_DOWN)) {
		w->row = (w->row + 1) % w->rows;
		return FR_REDRAW;
	}
	if (pressed & (MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_LEFT) | MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_RIGHT))) return FR_FLIP;
	return FR_NOTHING;
}

int FR_shouldOpen(int seen, int opened, const char* current, const char* home) {
	if (seen || opened || !current) return 0;
	if (!strcmp(current, "gamelist")) return 1;
	return home && home[0] && !strcmp(current, home);
}

void FR_hints(const FrWizard* w, MosaicHint left[2], int* left_count, MosaicHint right[1], int* right_count) {
	*left_count = 0;
	*right_count = 0;
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

const char* FR_describe(const char* id) {
	static const struct {
		const char* id;
		const char* what;
	} known[] = {
		{ "home-dashboard", TR_MARK("a home screen with your last game") },
		{ "switcher-plus", TR_MARK("more game switcher styles") },
		{ "favorites", TR_MARK("a Favorites folder, Y adds a game") },
		{ "search", TR_MARK("search every game by name") },
	};
	if (!id) return NULL;
	for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
		if (!strcmp(known[i].id, id)) return known[i].what;
	}
	return NULL;
}

int FR_pluginOn(const char* plugins_txt, const char* id) {
	if (!plugins_txt || !id || !id[0]) return 0;
	size_t len = strlen(id);
	int on = 0;
	const char* line = plugins_txt;
	while (*line) {
		const char* end = strchr(line, '\n');
		if (!end) end = line + strlen(line);
		const char* a = line;
		const char* b = end;
		while (a < b && isSpace(*a)) a++;
		while (b > a && isSpace(b[-1])) b--;
		// the last line for the plugin wins, as the loader reads it
		if ((size_t)(b - a) > len && !strncmp(a, id, len) && a[len] == '=') {
			on = (size_t)(b - a) == len + 3 && !strncmp(a + len + 1, "on", 2);
		}
		line = *end ? end + 1 : end;
	}
	return on;
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
