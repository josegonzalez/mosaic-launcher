// The second screen's model: the platforms with a second display, the size
// setting, the frame's layout, fitting art, wrapping the name, the .info
// lines and the frame file's header.

#include "check.h"
#include "words.h"
#include "../../plugins/second-screen/ss_model.h"

// ten pixels a byte, so widths are easy to work out
static int measure(const char* text, void* data) {
	(void)data;
	return (int)strlen(text) * 10;
}

static int rectIs(MosaicRect r, int x, int y, int w, int h) {
	if (r.x == x && r.y == y && r.w == w && r.h == h) return 1;
	fprintf(stderr, "got %i,%i %ix%i, want %i,%i %ix%i\n", r.x, r.y, r.w, r.h, x, y, w, h);
	return 0;
}

// a fake .info: key=value pairs separated by newlines
static int infoGet(const char* key, char* out, size_t size, void* data) {
	const char* p = (const char*)data;
	size_t klen = strlen(key);
	while (p && *p) {
		const char* eol = strchr(p, '\n');
		size_t n = eol ? (size_t)(eol - p) : strlen(p);
		if (n > klen && !strncmp(p, key, klen) && p[klen] == '=') {
			size_t v = n - klen - 1;
			if (v >= size) v = size - 1;
			memcpy(out, p + klen + 1, v);
			out[v] = '\0';
			return 1;
		}
		p = eol ? eol + 1 : NULL;
	}
	out[0] = '\0';
	return 0;
}

int main(void) {
	// only the desktop build has a second display
	CHECK(SS_hasSecondDisplay("desktop"));
	CHECK(!SS_hasSecondDisplay("tg5040"));
	CHECK(!SS_hasSecondDisplay("tg5050"));
	CHECK(!SS_hasSecondDisplay("h700"));
	CHECK(!SS_hasSecondDisplay(""));
	CHECK(!SS_hasSecondDisplay(NULL));

	// the size setting
	int w = 1, h = 1;
	CHECK(SS_parseSize("640x480", &w, &h) && w == 640 && h == 480);
	CHECK(SS_parseSize("480x800", &w, &h) && w == 480 && h == 800);
	CHECK(SS_parseSize("160x1920", &w, &h) && w == 160 && h == 1920);
	w = h = 7;
	CHECK(!SS_parseSize("159x480", &w, &h));
	CHECK(!SS_parseSize("640x1921", &w, &h));
	CHECK(!SS_parseSize("640", &w, &h));
	CHECK(!SS_parseSize("640x", &w, &h));
	CHECK(!SS_parseSize("x480", &w, &h));
	CHECK(!SS_parseSize(" 640x480", &w, &h));
	CHECK(!SS_parseSize("640x480 ", &w, &h));
	CHECK(!SS_parseSize("640X480", &w, &h));
	CHECK(!SS_parseSize("-640x480", &w, &h));
	CHECK(!SS_parseSize("", &w, &h));
	CHECK(!SS_parseSize(NULL, &w, &h));
	CHECK(w == 7 && h == 7);

	// 4:3 and wider: a square of art on the left, the text on its right
	SSLayout l;
	SS_layout(640, 480, &l);
	CHECK(l.beside && l.margin == 24);
	// the square is the height less two margins, or half the width less
	// three, whichever is smaller, centred down the frame
	CHECK(rectIs(l.art, 24, 98, 284, 284));
	CHECK(rectIs(l.text, 332, 24, 284, 432));
	SS_layout(800, 480, &l);
	CHECK(l.beside);
	CHECK(rectIs(l.art, 24, 58, 364, 364));
	CHECK(rectIs(l.text, 412, 24, 364, 432));
	SS_layout(1600, 480, &l);
	CHECK(rectIs(l.art, 24, 24, 432, 432));
	CHECK(l.text.x >= l.art.x + l.art.w + l.margin);
	CHECK(rectIs(l.text, 480, 24, 1096, 432));
	// narrower: the art on top, the text under it
	SS_layout(480, 640, &l);
	CHECK(!l.beside && l.margin == 24);
	CHECK(rectIs(l.art, 24, 24, 432, 284));
	CHECK(rectIs(l.text, 24, 332, 432, 284));
	SS_layout(400, 320, &l);
	CHECK(!l.beside);
	CHECK(l.art.y + l.art.h + l.margin == l.text.y);
	CHECK(l.text.y + l.text.h == 320 - l.margin);
	// a tiny frame keeps a margin of 4 and no negative sizes
	SS_layout(20, 10, &l);
	CHECK(l.margin == 4 && l.art.w >= 0 && l.art.h >= 0 && l.text.w >= 0 && l.text.h >= 0);
	SS_layout(0, 480, &l);
	CHECK(l.art.w == 0 && l.text.w == 0);

	// fitting art keeps its shape, centred
	MosaicRect box = { 10, 20, 200, 100 };
	CHECK(rectIs(SS_fit(8, 8, box), 60, 20, 100, 100));
	CHECK(rectIs(SS_fit(400, 100, box), 10, 45, 200, 50));
	CHECK(rectIs(SS_fit(200, 100, box), 10, 20, 200, 100));
	CHECK(rectIs(SS_fit(4, 2, (MosaicRect){ 0, 0, 432, 432 }), 0, 108, 432, 216));
	// never thinner than a pixel
	CHECK(rectIs(SS_fit(1, 1000, box), 109, 20, 1, 100));
	CHECK(rectIs(SS_fit(1000, 1, box), 10, 69, 200, 1));
	CHECK(rectIs(SS_fit(0, 8, box), 0, 0, 0, 0));
	CHECK(rectIs(SS_fit(8, 8, (MosaicRect){ 0, 0, 0, 10 }), 0, 0, 0, 0));

	// wrapping the name at spaces
	char lines[SS_TITLE_LINES][SS_LINE_SIZE];
	CHECK(SS_wrap("Super Mario World", 200, 3, measure, NULL, lines) == 1);
	CHECK_STR(lines[0], "Super Mario World");
	CHECK(SS_wrap("Super Mario World", 110, 3, measure, NULL, lines) == 2);
	CHECK_STR(lines[0], "Super Mario");
	CHECK_STR(lines[1], "World");
	CHECK(SS_wrap("The Legend of Zelda A Link to the Past", 100, 3, measure, NULL, lines) == 3);
	CHECK_STR(lines[0], "The Legend");
	CHECK_STR(lines[1], "of Zelda A");
	CHECK_STR(lines[2], "Link to the Past");
	// a word wider than the line gets a line of its own
	CHECK(SS_wrap("A Supercalifragilistic B", 100, 3, measure, NULL, lines) == 3);
	CHECK_STR(lines[0], "A");
	CHECK_STR(lines[1], "Supercalifragilistic");
	CHECK_STR(lines[2], "B");
	// extra spaces go
	CHECK(SS_wrap("  Two   Words  ", 50, 3, measure, NULL, lines) == 2);
	CHECK_STR(lines[0], "Two");
	CHECK_STR(lines[1], "Words");
	CHECK(SS_wrap("One line only", 1000, 1, measure, NULL, lines) == 1);
	CHECK_STR(lines[0], "One line only");
	CHECK(SS_wrap("", 100, 3, measure, NULL, lines) == 0);
	CHECK(SS_wrap("   ", 100, 3, measure, NULL, lines) == 0);
	CHECK(SS_wrap(NULL, 100, 3, measure, NULL, lines) == 0);
	CHECK(SS_wrap("x", 100, 0, measure, NULL, lines) == 0);
	// a word longer than a line's buffer is cut to fit it
	char huge[600];
	memset(huge, 'a', sizeof(huge) - 1);
	huge[sizeof(huge) - 1] = '\0';
	CHECK(SS_wrap(huge, 100, 2, measure, NULL, lines) == 2);
	CHECK(strlen(lines[0]) == SS_LINE_SIZE - 1);
	CHECK(strlen(lines[1]) == SS_LINE_SIZE - 1);

	// .info keys and labels
	CHECK_STR(SS_infoKey(SS_YEAR), "year");
	CHECK_STR(SS_infoKey(SS_PLAYERS), "players");
	CHECK_STR(SS_infoLabel(SS_GENRE), "Genre");
	CHECK(SS_infoKey(-1) == NULL && SS_infoKey(SS_INFO_COUNT) == NULL && SS_infoLabel(SS_INFO_COUNT) == NULL);

	// tidying values
	char v[16];
	CHECK(SS_clean(SS_YEAR, "1994-11-21", v, sizeof(v)));
	CHECK_STR(v, "1994");
	CHECK(SS_clean(SS_YEAR, " 1994 ", v, sizeof(v)));
	CHECK_STR(v, "1994");
	CHECK(SS_clean(SS_YEAR, "199x-01", v, sizeof(v)));
	CHECK_STR(v, "199x-01");
	CHECK(SS_clean(SS_GENRE, "1994-11-21", v, sizeof(v)));
	CHECK_STR(v, "1994-11-21");
	CHECK(SS_clean(SS_GENRE, "\t Puzzle \r", v, sizeof(v)));
	CHECK_STR(v, "Puzzle");
	CHECK(SS_clean(SS_PUBLISHER, "A Very Long Publisher Name", v, sizeof(v)));
	CHECK_STR(v, "A Very Long Pub");
	CHECK(!SS_clean(SS_GENRE, "   ", v, sizeof(v)));
	CHECK_STR(v, "");
	CHECK(!SS_clean(SS_GENRE, NULL, v, sizeof(v)));

	// the lines, in the frame's order, leaving out empty and missing keys
	char info[SS_INFO_COUNT][SS_LINE_SIZE];
	CHECK(SS_infoLines(infoGet, "publisher=Nintendo\nyear=1994-11-21\ngenre= \ntier=3\nplayers=2", info, SS_INFO_COUNT) == 3);
	CHECK_STR(info[0], "Year: 1994");
	CHECK_STR(info[1], "Publisher: Nintendo");
	CHECK_STR(info[2], "Players: 2");
	CHECK(SS_infoLines(infoGet, "year=2001\ngenre=Racing\npublisher=P\ndeveloper=D\nplayers=4", info, 2) == 2);
	CHECK_STR(info[1], "Genre: Racing");
	CHECK(SS_infoLines(infoGet, "tier=3", info, SS_INFO_COUNT) == 0);
	CHECK(SS_infoLines(infoGet, "", info, SS_INFO_COUNT) == 0);
	CHECK(SS_infoLines(NULL, "", info, SS_INFO_COUNT) == 0);
	// in the words it's given, then English again
	static const char* const words[] = { "Year", "A\xc3\xb1o", NULL };
	fake_words = words;
	SSWords set = { fakeTr, fakePlural };
	SS_setWords(&set);
	CHECK(SS_infoLines(infoGet, "year=1994\nplayers=2", info, SS_INFO_COUNT) == 2);
	CHECK_STR(info[0], "A\xc3\xb1o: 1994");
	CHECK_STR(info[1], "Players: 2");
	SS_setWords(NULL);
	CHECK(SS_infoLines(infoGet, "year=1994", info, SS_INFO_COUNT) == 1);
	CHECK_STR(info[0], "Year: 1994");

	// what an entry is
	CHECK_STR(SS_kindLabel(MOSAIC_ENTRY_ROM), "Game");
	CHECK_STR(SS_kindLabel(MOSAIC_ENTRY_DIR), "Folder");
	CHECK_STR(SS_kindLabel(MOSAIC_ENTRY_PAK), "Pak");
	CHECK_STR(SS_kindLabel(MOSAIC_ENTRY_TOGGLE), "");
	CHECK_STR(SS_kindLabel(99), "");

	// the frame file's header
	unsigned char frame[SS_FRAME_HEADER + 2 * 3 * 4];
	memset(frame, 0, sizeof(frame));
	SS_frameHeader(frame, 0x01020304u, 2, 3);
	CHECK(!memcmp(frame, "MSS1", 4));
	CHECK(frame[4] == 4 && frame[5] == 3 && frame[6] == 2 && frame[7] == 1);
	CHECK(frame[8] == 2 && frame[12] == 3);
	unsigned int number = 0;
	w = h = 0;
	CHECK(SS_frameParse(frame, sizeof(frame), &number, &w, &h));
	CHECK(number == 0x01020304u && w == 2 && h == 3);
	// short of pixels, a bad magic or a bad size
	CHECK(!SS_frameParse(frame, sizeof(frame) - 1, &number, &w, &h));
	CHECK(!SS_frameParse(frame, SS_FRAME_HEADER - 1, &number, &w, &h));
	SS_frameHeader(frame, 1, 0, 3);
	CHECK(!SS_frameParse(frame, sizeof(frame), &number, &w, &h));
	SS_frameHeader(frame, 1, 4097, 1);
	CHECK(!SS_frameParse(frame, sizeof(frame), &number, &w, &h));
	SS_frameHeader(frame, 1, 2, 3);
	frame[0] = 'X';
	CHECK(!SS_frameParse(frame, sizeof(frame), &number, &w, &h));
	CHECK(!SS_frameParse(NULL, sizeof(frame), &number, &w, &h));
	// a big size can't overflow the length check
	SS_frameHeader(frame, 1, 4096, 4096);
	CHECK(!SS_frameParse(frame, sizeof(frame), &number, &w, &h));

	// the display program's environment: what SDL needs, never a preload
	CHECK(SS_keepEnv("DISPLAY=:99"));
	CHECK(SS_keepEnv("XAUTHORITY=/tmp/xvfb-run.x/Xauthority"));
	CHECK(SS_keepEnv("WAYLAND_DISPLAY=wayland-0"));
	CHECK(SS_keepEnv("XDG_RUNTIME_DIR=/run/user/1000"));
	CHECK(SS_keepEnv("HOME=/home/a"));
	CHECK(SS_keepEnv("PATH=/usr/bin:/bin"));
	CHECK(SS_keepEnv("PATH="));
	CHECK(SS_keepEnv("SDL_VIDEODRIVER=x11"));
	CHECK(SS_keepEnv("LC_ALL=C"));
	CHECK(SS_keepEnv("LD_LIBRARY_PATH=/var/tmp/nextui/lib"));
	CHECK(SS_keepEnv("MOSAIC_SECOND_SCREEN_SHOT=/tmp/shots"));
	CHECK(!SS_keepEnv("LD_PRELOAD=/tmp/presser.so"));
	CHECK(!SS_keepEnv("DYLD_INSERT_LIBRARIES=/tmp/presser.so"));
	CHECK(!SS_keepEnv("LD_AUDIT=/tmp/a.so"));
	CHECK(!SS_keepEnv("MOSAIC_LAUNCHER_PLUGIN_PATH=/tmp/plugins"));
	CHECK(!SS_keepEnv("PRESSER_KEYS=wait"));
	// a name has to match whole, and a prefix needs more after it
	CHECK(!SS_keepEnv("DISPLAYS=:99"));
	CHECK(!SS_keepEnv("PATHX=/bin"));
	CHECK(!SS_keepEnv("XDG_=x"));
	CHECK(!SS_keepEnv("HOME"));
	CHECK(!SS_keepEnv("=HOME"));
	CHECK(!SS_keepEnv(""));
	CHECK(!SS_keepEnv(NULL));

	CHECK_DONE();
}
