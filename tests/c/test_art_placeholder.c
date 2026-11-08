// Placeholder art: its model, then the plugin against a fake host that
// records what it draws.

#include <stdlib.h>
#include "check.h"
#include "../../plugins/art-placeholder/art_placeholder.c"

static void testStyle(void) {
	CHECK(PA_style("title") == PA_STYLE_TITLE);
	CHECK(PA_style("pattern") == PA_STYLE_PATTERN);
	CHECK(PA_style("Pattern") == PA_STYLE_TITLE);
	CHECK(PA_style("") == PA_STYLE_TITLE);
	CHECK(PA_style(NULL) == PA_STYLE_TITLE);
}

static void testSize(void) {
	int w = 0, h = 0;
	// desktop: the built-in list's thumbnail is 460 by 460 at most
	CHECK(PA_size(MOSAIC_ART_THUMBNAIL, 1024, 768, &w, &h));
	CHECK(w == 460 && h == 460);
	CHECK(PA_size(MOSAIC_ART_BOX, 1024, 768, &w, &h));
	CHECK(w == 345 && h == 460);
	// tg5040: the height decides
	CHECK(PA_size(MOSAIC_ART_THUMBNAIL, 1280, 720, &w, &h));
	CHECK(w == 432 && h == 432);
	// h700's 640x480: the width does
	CHECK(PA_size(MOSAIC_ART_THUMBNAIL, 640, 480, &w, &h));
	CHECK(w == 288 && h == 288);
	CHECK(PA_size(MOSAIC_ART_BOX, 640, 480, &w, &h));
	CHECK(w == 216 && h == 288);
	CHECK(!PA_size(MOSAIC_ART_BOX_3D, 1024, 768, &w, &h));
	CHECK(!PA_size(MOSAIC_ART_LOGO, 1024, 768, &w, &h));
	CHECK(!PA_size(MOSAIC_ART_FOLDER_BG, 1024, 768, &w, &h));
	CHECK(!PA_size(MOSAIC_ART_THUMBNAIL, 20, 20, &w, &h));
}

static void testHash(void) {
	// FNV-1a's published values, so it can't drift between builds
	CHECK(PA_hash("") == 2166136261u);
	CHECK(PA_hash("a") == 0xE40C292Cu);
	CHECK(PA_hash("foobar") == 0xBF9CF968u);
	CHECK(PA_hash(NULL) == PA_hash(""));
	CHECK(PA_hash("/r/GB/Tetris.gb") == PA_hash("/r/GB/Tetris.gb"));
	CHECK(PA_hash("/r/GB/Tetris.gb") != PA_hash("/r/GB/Tetris.gbc"));

	uint32_t a = 1, b = 1;
	CHECK(PA_next(&a) == 270369u);
	CHECK(PA_next(&b) == 270369u);
	CHECK(PA_next(&a) == PA_next(&b));
	uint32_t zero = 0;
	CHECK(PA_next(&zero) != 0 && zero != 0);
}

static void testColours(void) {
	CHECK(PA_hsv(0, 100, 100) == 0xFF0000);
	CHECK(PA_hsv(120, 100, 100) == 0x00FF00);
	CHECK(PA_hsv(240, 100, 100) == 0x0000FF);
	CHECK(PA_hsv(60, 100, 100) == 0xFFFF00);
	CHECK(PA_hsv(360, 100, 100) == 0xFF0000);
	CHECK(PA_hsv(-120, 100, 100) == 0x0000FF);
	CHECK(PA_hsv(200, 0, 100) == 0xFFFFFF);
	CHECK(PA_hsv(200, 100, 0) == 0x000000);
	CHECK(PA_hsv(30, 100, 100) == 0xFF7F00);

	CHECK(PA_luma(0x000000) == 0);
	CHECK(PA_luma(0xFFFFFF) == 255);
	CHECK(PA_luma(0x00FF00) > PA_luma(0xFF0000));

	// stock's default accent takes white text, and a pale one black
	CHECK(PA_contrast(0x9B2257, 0xFFFFFF, 0x000000) == 0xFFFFFF);
	CHECK(PA_contrast(0xF0E080, 0xFFFFFF, 0x000000) == 0x000000);
	CHECK(PA_contrast(0x808080, 0x707070, 0x909090) == 0x707070);

	CHECK(PA_themeOr(-1, 0x123456) == 0x123456);
	CHECK(PA_themeOr(0x9B2257, 0x123456) == 0x9B2257);
	CHECK(PA_themeOr(0, 0x123456) == 0);
}

// 10 pixels a byte
static int fakeWidth(const char* text, void* data) {
	(void)data;
	return (int)strlen(text) * 10;
}

static int wrap(const char* name, int max_width, char lines[PA_LINES][PA_LINE_LEN]) {
	return PA_wrap(name, max_width, fakeWidth, NULL, lines);
}

static void testWrap(void) {
	char lines[PA_LINES][PA_LINE_LEN];
	CHECK(wrap("Tetris", 100, lines) == 1);
	CHECK_STR(lines[0], "Tetris");
	CHECK_STR(lines[1], "");

	CHECK(wrap("Super Mario Land", 120, lines) == 2);
	CHECK_STR(lines[0], "Super Mario");
	CHECK_STR(lines[1], "Land");

	// spaces anywhere are dropped
	CHECK(wrap("  Super   Mario  Land ", 120, lines) == 2);
	CHECK_STR(lines[0], "Super Mario");
	CHECK_STR(lines[1], "Land");

	// the last line takes the rest, to be truncated
	CHECK(wrap("The Legend of Zelda Link's Awakening DX", 100, lines) == 3);
	CHECK_STR(lines[0], "The Legend");
	CHECK_STR(lines[1], "of Zelda");
	CHECK_STR(lines[2], "Link's Awakening DX");

	// a word too wide for a line has it to itself
	CHECK(wrap("Ultraextralongname 2", 100, lines) == 2);
	CHECK_STR(lines[0], "Ultraextralongname");
	CHECK_STR(lines[1], "2");
	CHECK(wrap("Go Ultraextralongname 2", 100, lines) == 3);
	CHECK_STR(lines[0], "Go");
	CHECK_STR(lines[1], "Ultraextralongname");
	CHECK_STR(lines[2], "2");
	CHECK(wrap("Ultraextralongname", 100, lines) == 1);
	CHECK_STR(lines[0], "Ultraextralongname");

	CHECK(wrap("", 100, lines) == 0);
	CHECK(wrap("   ", 100, lines) == 0);
	CHECK(wrap(NULL, 100, lines) == 0);
	CHECK_STR(lines[0], "");

	// a line longer than the buffer is cut, not overrun
	char huge[1000];
	memset(huge, 'x', sizeof(huge) - 1);
	huge[sizeof(huge) - 1] = '\0';
	CHECK(wrap(huge, 100, lines) == 1);
	CHECK(strlen(lines[0]) == PA_LINE_LEN - 1);
}

static void testTitleLayout(void) {
	PATitle t;
	PA_titleLayout(460, 460, 3, &t);
	CHECK(t.margin == 28 && t.radius == 28 && t.padding == 28);
	CHECK(t.text_w == 460 - 4 * 28);
	PA_titleLayout(345, 460, 3, &t);
	CHECK(t.margin == 21);
	CHECK(t.text_w == 345 - 4 * 21);
	// never less than two units of stock's layout
	PA_titleLayout(40, 40, 3, &t);
	CHECK(t.margin == 6);
	PA_titleLayout(10, 10, 3, &t);
	CHECK(t.text_w == 1);

	CHECK(PA_textTop(460, 1, 40) == 210);
	CHECK(PA_textTop(460, 3, 40) == 170);
}

static int samePattern(const PAPattern* a, const PAPattern* b) {
	return !memcmp(a, b, sizeof(*a));
}

static void testPattern(void) {
	PAPattern a, b, c;
	PA_pattern("/r/GB/Tetris.gb", 460, 460, &a);
	PA_pattern("/r/GB/Tetris.gb", 460, 460, &b);
	CHECK(samePattern(&a, &b));
	CHECK(a.rows == 6);
	PA_pattern("/r/GB/Tetris.gb", 345, 460, &b);
	CHECK(b.rows == 8);
	CHECK(b.bg == a.bg && b.colors[0] == a.colors[0] && b.colors[1] == a.colors[1]);

	// mirrored, never blank, and only tiles 0 to 2
	const char* games[] = { "/r/GB/Tetris.gb", "/r/GB/Tetris.gbc", "/r/NES/Contra (USA).nes", "/r/GBA/Metroid Fusion.gba",
		"/r/MD/Sonic.md", "", "/r/SFC/Super Metroid.sfc" };
	int n = (int)(sizeof(games) / sizeof(games[0]));
	for (int g = 0; g < n; g++) {
		PA_pattern(games[g], 460, 460, &c);
		int any = 0;
		for (int row = 0; row < c.rows; row++) {
			for (int col = 0; col < PA_COLS; col++) {
				CHECK(c.tiles[row][col] <= 2);
				CHECK(c.tiles[row][col] == c.tiles[row][PA_COLS - 1 - col]);
				any |= c.tiles[row][col];
			}
		}
		CHECK(any);
		CHECK(c.colors[0] != c.colors[1]);
		// different games differ
		for (int o = 0; o < g; o++) {
			PA_pattern(games[o], 460, 460, &b);
			CHECK(!samePattern(&b, &c));
		}
	}

	// a different hue for most of a few hundred games
	int hues[360] = { 0 };
	int distinct = 0;
	for (int i = 0; i < 300; i++) {
		char path[64];
		snprintf(path, sizeof(path), "/r/GB/Game %i.gb", i);
		PA_pattern(path, 460, 460, &c);
		int hue = (int)(PA_hash(path) % 360);
		if (!hues[hue]++) distinct++;
	}
	CHECK(distinct > 150);

	// rows stay in range
	PA_pattern("x", 100, 1000, &c);
	CHECK(c.rows == PA_MAX_ROWS);
	PA_pattern("x", 1000, 10, &c);
	CHECK(c.rows == 1);
	PA_pattern("x", 0, 10, &c);
	CHECK(c.rows == 1);
}

static void testTiles(void) {
	int x, y, size;
	CHECK(PA_tile(460, 460, 6, 0, 0, &x, &y, &size));
	// cells of 76, centred with 2 to spare, a gap of 7
	CHECK(x == 2 + 7 && y == 2 + 7 && size == 62);
	CHECK(PA_tile(460, 460, 6, 5, 5, &x, &y, &size));
	CHECK(x == 2 + 5 * 76 + 7 && y == 2 + 5 * 76 + 7);
	CHECK(x + size <= 460 && y + size <= 460);
	CHECK(PA_tile(345, 460, 8, 5, 7, &x, &y, &size));
	CHECK(x + size <= 345 && y + size <= 460);
	CHECK(!PA_tile(460, 460, 6, 6, 0, &x, &y, &size));
	CHECK(!PA_tile(460, 460, 6, 0, 6, &x, &y, &size));
	CHECK(!PA_tile(460, 460, 6, -1, 0, &x, &y, &size));
	CHECK(!PA_tile(460, 460, 0, 0, 0, &x, &y, &size));
	CHECK(!PA_tile(5, 5, 1, 0, 0, &x, &y, &size));
}

///////////////////////////////////////
// the plugin, against a fake host

struct MosaicEntry {
	const char* path;
	const char* name;
	int type;
};

struct MosaicSurface {
	int w, h;
	int freed;
};

static MosaicSurface surfaces[8];
static int surface_count = 0;
static int fills = 0, round_fills = 0, texts = 0, truncates = 0;
static char drawn[PA_LINES][PA_LINE_LEN];
static unsigned int first_fill = 0, text_color = 0;
static const char* real_art = NULL; // a path art_find has a file for
static int finds = 0;
static int themed = 1;
static const char* style_value = "title";

static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	(void)id;
	return !strcmp(key, "style") ? style_value : fallback;
}
static void fakeLog(const char* id, const char* message) { (void)id; (void)message; }
static const char* fakeEntryPath(const MosaicEntry* e) { return e->path; }
static const char* fakeEntryName(const MosaicEntry* e) { return e->name; }
static int fakeEntryType(const MosaicEntry* e) { return e->type; }
static int fakeScreenWidth(void) { return 1024; }
static int fakeScreenHeight(void) { return 768; }
static int fakeScreenScale(void) { return 3; }
static int fakeTheme(int index) {
	if (!themed) return -1;
	switch (index) {
		case MOSAIC_COLOR_MAIN: return 0xFFFFFF;
		case MOSAIC_COLOR_ACCENT: return 0xE0E0A0;
		case MOSAIC_COLOR_ACCENT2: return 0x102030;
		case MOSAIC_COLOR_LIST_TEXT_SELECTED: return 0x000000;
		default: return -1;
	}
}
static MosaicSurface* fakeSurfaceNew(const char* id, int w, int h) {
	(void)id;
	if (surface_count >= 8) return NULL;
	MosaicSurface* s = &surfaces[surface_count++];
	s->w = w;
	s->h = h;
	s->freed = 0;
	return s;
}
static int fakeFillRect(MosaicSurface* s, const MosaicRect* rect, MosaicColor color) {
	(void)s;
	if (!rect && !fills) first_fill = (unsigned)color.r << 16 | (unsigned)color.g << 8 | color.b;
	fills++;
	return 0;
}
static int fakeFillRoundRect(MosaicSurface* s, const MosaicRect* rect, int radius, MosaicColor color) {
	(void)color;
	CHECK(rect && rect->x >= 0 && rect->y >= 0 && rect->x + rect->w <= s->w && rect->y + rect->h <= s->h);
	CHECK(radius >= 0);
	round_fills++;
	return 0;
}
static int fakeTextWidth(int font, const char* text) {
	(void)font;
	return (int)strlen(text) * 20;
}
static int fakeTextHeight(int font) {
	(void)font;
	return 40;
}
static int fakeTruncate(int font, const char* text, int max_width, char* out, size_t size) {
	(void)font;
	truncates++;
	size_t fit = (size_t)max_width / 20;
	if (fit >= size) fit = size - 1;
	snprintf(out, size, "%.*s", (int)fit, text);
	return (int)strlen(out) * 20;
}
static int fakeDrawText(MosaicSurface* s, int font, const char* text, int x, int y, MosaicColor color, int max_width) {
	(void)font;
	(void)max_width;
	CHECK(x >= 0 && y >= 0 && x + (int)strlen(text) * 20 <= s->w && y + 40 <= s->h);
	if (texts < PA_LINES) snprintf(drawn[texts], PA_LINE_LEN, "%s", text);
	text_color = (unsigned)color.r << 16 | (unsigned)color.g << 8 | color.b;
	texts++;
	return 0;
}
static int fakeArtFind(const MosaicEntry* e, int kind, char* out, size_t size) {
	(void)kind;
	finds++;
	int found = real_art && !strcmp(e->path, real_art);
	snprintf(out, size, "%s", found ? "/r/GB/.media/x.png" : "");
	return found;
}
static int fakeArtGet(const MosaicEntry* e, int kind, const MosaicSurface** out) {
	(void)e;
	(void)kind;
	*out = NULL;
	return MOSAIC_ART_MISSING;
}

static MosaicHost fakeHost(void) {
	MosaicHost h;
	memset(&h, 0, sizeof(h));
	h.size = sizeof(h);
	h.api_version = MOSAIC_API_VERSION;
	h.log = fakeLog;
	h.cfg_get = fakeCfgGet;
	h.entry_path = fakeEntryPath;
	h.entry_name = fakeEntryName;
	h.entry_type = fakeEntryType;
	h.theme_color = fakeTheme;
	h.screen_width = fakeScreenWidth;
	h.screen_height = fakeScreenHeight;
	h.screen_scale = fakeScreenScale;
	h.surface_new = fakeSurfaceNew;
	h.fill_rect = fakeFillRect;
	h.fill_round_rect = fakeFillRoundRect;
	h.text_width = fakeTextWidth;
	h.text_height = fakeTextHeight;
	h.text_truncate = fakeTruncate;
	h.draw_text = fakeDrawText;
	h.art_find = fakeArtFind;
	h.art_get = fakeArtGet;
	h.plugin_art = 1;
	return h;
}

static void reset(void) {
	surface_count = fills = round_fills = texts = truncates = finds = 0;
	first_fill = text_color = 0;
	memset(drawn, 0, sizeof(drawn));
}

static void testPlugin(void) {
	MosaicHost h = fakeHost();
	const MosaicPlugin* p = mosaic_plugin_get(&h);
	CHECK(p->art_surface == artSurface);
	CHECK(p->init(&h) == 0);
	MosaicEntry tetris = { "/r/GB/Tetris.gb", "Tetris", MOSAIC_ENTRY_ROM };
	MosaicEntry zelda = { "/r/GB/Zelda.gb", "The Legend of Zelda Link's Awakening", MOSAIC_ENTRY_ROM };
	MosaicEntry folder = { "/r/GB", "GB", MOSAIC_ENTRY_DIR };
	MosaicEntry pak = { "/r/Tools/Clock.pak", "Clock", MOSAIC_ENTRY_PAK };

	// a title card in the theme's colours, with black text on a pale accent
	reset();
	MosaicSurface* s = p->art_surface(&tetris, MOSAIC_ART_THUMBNAIL);
	CHECK(s && s->w == 460 && s->h == 460);
	CHECK(first_fill == 0x102030);
	CHECK(round_fills == 1);
	CHECK(texts == 1);
	CHECK_STR(drawn[0], "Tetris");
	CHECK(text_color == 0x000000);
	CHECK(finds == 4);

	// a box, its name wrapped and the rest truncated
	reset();
	s = p->art_surface(&zelda, MOSAIC_ART_BOX);
	CHECK(s && s->w == 345 && s->h == 460);
	// 345 - 4 * 21 = 261 wide: 13 characters a line
	CHECK(texts == 3 && truncates == 3);
	CHECK_STR(drawn[0], "The Legend of");
	CHECK_STR(drawn[1], "Zelda Link's");
	CHECK_STR(drawn[2], "Awakening");

	// stock's defaults without theme colours: white on its accent
	themed = 0;
	reset();
	CHECK(p->art_surface(&tetris, MOSAIC_ART_THUMBNAIL) != NULL);
	CHECK(first_fill == 0x1E2329);
	CHECK(text_color == 0xFFFFFF);
	themed = 1;

	// no card for other kinds, folders, paks or a game with real art
	reset();
	CHECK(p->art_surface(&tetris, MOSAIC_ART_BOX_3D) == NULL);
	CHECK(p->art_surface(&tetris, MOSAIC_ART_LOGO) == NULL);
	CHECK(p->art_surface(&folder, MOSAIC_ART_THUMBNAIL) == NULL);
	CHECK(p->art_surface(&pak, MOSAIC_ART_THUMBNAIL) == NULL);
	CHECK(surface_count == 0 && finds == 0);
	real_art = tetris.path;
	CHECK(p->art_surface(&tetris, MOSAIC_ART_BOX) == NULL);
	CHECK(surface_count == 0);
	real_art = NULL;

	// patterns: tiles in the pattern's colours, the same each time
	style_value = "pattern";
	CHECK(p->init(&h) == 0);
	reset();
	s = p->art_surface(&tetris, MOSAIC_ART_THUMBNAIL);
	CHECK(s && s->w == 460);
	CHECK(texts == 0);
	PAPattern pattern;
	PA_pattern(tetris.path, 460, 460, &pattern);
	CHECK(first_fill == pattern.bg);
	int tiles = 0;
	for (int row = 0; row < pattern.rows; row++) {
		for (int col = 0; col < PA_COLS; col++) tiles += pattern.tiles[row][col] != 0;
	}
	CHECK(round_fills == tiles && tiles > 0);
	style_value = "title";

	// a launcher without generated art declines it
	MosaicHost old = fakeHost();
	old.plugin_art = 0;
	CHECK(p->init(&old) == 1);
	old.size = offsetof(MosaicHost, plugin_art);
	old.plugin_art = 1;
	CHECK(p->init(&old) == 1);
	CHECK(p->init(&h) == 0);
}

int main(void) {
	testStyle();
	testSize();
	testHash();
	testColours();
	testWrap();
	testTitleLayout();
	testPattern();
	testTiles();
	testPlugin();
	CHECK_DONE();
}
