// The second screen's model: the platforms with a second display, the size
// setting, the frame's layout, fitting art, wrapping the name, the
// panel and the frame file's header.

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

// settings for SS_showFrom: key=value pairs separated by newlines
static const char* settings = "";
static const char* setting(const char* key, const char* fallback) {
	static char value[32];
	size_t klen = strlen(key);
	for (const char* p = settings; p && *p;) {
		const char* eol = strchr(p, '\n');
		size_t n = eol ? (size_t)(eol - p) : strlen(p);
		if (n > klen && !strncmp(p, key, klen) && p[klen] == '=') {
			snprintf(value, sizeof(value), "%.*s", (int)(n - klen - 1), p + klen + 1);
			return value;
		}
		p = eol ? eol + 1 : NULL;
	}
	return fallback;
}

static void showAll(SSShow* show) {
	show->details = show->caption = show->marks = show->music = 1;
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

	// the settings: every piece on unless it's off
	SSShow show;
	settings = "";
	SS_showFrom(setting, &show);
	CHECK(show.details && show.caption && show.marks && show.music);
	settings = "details=off\nmusic=off\ncaption=on";
	SS_showFrom(setting, &show);
	CHECK(!show.details && show.caption && show.marks && !show.music);
	settings = "caption=off\nmarks=off";
	SS_showFrom(setting, &show);
	CHECK(show.details && !show.caption && !show.marks && show.music);
	SS_showFrom(NULL, &show);
	CHECK(show.details && show.caption && show.marks && show.music);

	// labels for info.format's keys
	CHECK_STR(SS_infoLabel("year"), "Year");
	CHECK_STR(SS_infoLabel("players"), "Players");
	CHECK(SS_infoLabel("count") == NULL && SS_infoLabel("line") == NULL && SS_infoLabel("tier") == NULL);
	CHECK(SS_infoLabel(NULL) == NULL);

	// info.format's reply as lines, in its order, leaving out count, line
	// and keys without a label or a value
	char info[SS_INFO_MAX][SS_LINE_SIZE];
	CHECK(SS_infoLines("count=3\npublisher=Nintendo\nyear=1994\ngenre=Platformer\n"
		"line=Nintendo \xc2\xb7 1994 \xc2\xb7 Platformer", info, SS_INFO_MAX) == 3);
	CHECK_STR(info[0], "Publisher: Nintendo");
	CHECK_STR(info[1], "Year: 1994");
	CHECK_STR(info[2], "Genre: Platformer");
	CHECK(SS_infoLines("count=2\nyear=2001\ntier=3\ngenre=\ndeveloper=A=B\nline=x", info, SS_INFO_MAX) == 2);
	CHECK_STR(info[0], "Year: 2001");
	CHECK_STR(info[1], "Developer: A=B");
	CHECK(SS_infoLines("count=2\nyear=2001\ngenre=Racing", info, 1) == 1);
	CHECK(SS_infoLines("year=2001", info, SS_INFO_MAX) == 1);
	CHECK(SS_infoLines("count=0\nline=", info, SS_INFO_MAX) == 0);
	CHECK(SS_infoLines("", info, SS_INFO_MAX) == 0);
	CHECK(SS_infoLines(NULL, info, SS_INFO_MAX) == 0);
	// in the words it's given, then English again
	static const char* const words[] = { "Year", "A\xc3\xb1o", "Favorite", "Favorito", "Tags: %s", "Etiquetas: %s", NULL };
	fake_words = words;
	SSWords set = { fakeTr, fakePlural };
	SS_setWords(&set);
	CHECK(SS_infoLines("count=2\nyear=1994\nplayers=2", info, SS_INFO_MAX) == 2);
	CHECK_STR(info[0], "A\xc3\xb1o: 1994");
	CHECK_STR(info[1], "Players: 2");
	char marks[SS_LINE_SIZE];
	CHECK(SS_marks(1, "RPG", marks, sizeof(marks)));
	CHECK_STR(marks, "Favorito \xc2\xb7 Etiquetas: RPG");
	SS_setWords(NULL);
	CHECK(SS_infoLines("year=1994", info, SS_INFO_MAX) == 1);
	CHECK_STR(info[0], "Year: 1994");

	// tags from the sets an entry is in, joined
	char tags[32] = "";
	CHECK(SS_tagAdd(tags, sizeof(tags), "tags:RPG"));
	CHECK(!SS_tagAdd(tags, sizeof(tags), "favorites"));
	CHECK(!SS_tagAdd(tags, sizeof(tags), "smart:New"));
	CHECK(!SS_tagAdd(tags, sizeof(tags), "tags:"));
	CHECK(SS_tagAdd(tags, sizeof(tags), "tags:Co-op"));
	CHECK_STR(tags, "RPG, Co-op");
	// one that doesn't fit is left out whole
	CHECK(!SS_tagAdd(tags, sizeof(tags), "tags:A very long tag name"));
	CHECK_STR(tags, "RPG, Co-op");
	CHECK(!SS_tagAdd(NULL, 4, "tags:RPG") && !SS_tagAdd(tags, sizeof(tags), NULL));

	// the marks line
	CHECK(SS_marks(1, "", marks, sizeof(marks)));
	CHECK_STR(marks, "Favorite");
	CHECK(SS_marks(0, "RPG, Co-op", marks, sizeof(marks)));
	CHECK_STR(marks, "Tags: RPG, Co-op");
	CHECK(SS_marks(1, "RPG", marks, sizeof(marks)));
	CHECK_STR(marks, "Favorite \xc2\xb7 Tags: RPG");
	CHECK(!SS_marks(0, "", marks, sizeof(marks)));
	CHECK_STR(marks, "");
	CHECK(!SS_marks(0, NULL, marks, sizeof(marks)));

	// the panel: kind, marks, fields and caption under the name, and what
	// music plays for the bottom
	SSPanel panel;
	SSPieces pieces = { 0, MOSAIC_ENTRY_ROM, "count=2\nyear=1994\npublisher=Nintendo\nline=1994 \xc2\xb7 Nintendo",
		"2h 5m \xc2\xb7 yesterday", 1, "RPG", "Song - Artist" };
	showAll(&show);
	SS_panel(&show, &pieces, &panel);
	CHECK(panel.count == 5);
	CHECK(panel.rows[0].style == SS_ROW_KIND);
	CHECK_STR(panel.rows[0].text, "Game");
	CHECK(panel.rows[1].style == SS_ROW_MARKS);
	CHECK_STR(panel.rows[1].text, "Favorite \xc2\xb7 Tags: RPG");
	CHECK(panel.rows[2].style == SS_ROW_INFO && panel.rows[3].style == SS_ROW_INFO);
	CHECK_STR(panel.rows[2].text, "Year: 1994");
	CHECK_STR(panel.rows[3].text, "Publisher: Nintendo");
	CHECK(panel.rows[4].style == SS_ROW_CAPTION);
	CHECK_STR(panel.rows[4].text, "2h 5m \xc2\xb7 yesterday");
	CHECK_STR(panel.music, "Song - Artist");
	// each setting off leaves its piece out
	show.details = 0;
	SS_panel(&show, &pieces, &panel);
	CHECK(panel.count == 3 && panel.rows[2].style == SS_ROW_CAPTION);
	show.details = 1;
	show.marks = 0;
	SS_panel(&show, &pieces, &panel);
	CHECK(panel.count == 4 && panel.rows[1].style == SS_ROW_INFO);
	show.marks = 1;
	show.caption = 0;
	SS_panel(&show, &pieces, &panel);
	CHECK(panel.count == 4 && panel.rows[3].style == SS_ROW_INFO);
	show.caption = 1;
	show.music = 0;
	SS_panel(&show, &pieces, &panel);
	CHECK(panel.count == 5 && !panel.music[0]);
	// an owner that's off answers nothing, so its piece doesn't show
	showAll(&show);
	SSPieces bare = { 0, MOSAIC_ENTRY_ROM, NULL, NULL, 0, NULL, NULL };
	SS_panel(&show, &bare, &panel);
	CHECK(panel.count == 1 && panel.rows[0].style == SS_ROW_KIND && !panel.music[0]);
	bare.info = "count=0\nline=";
	bare.caption = "";
	bare.tags = "";
	bare.music = "";
	SS_panel(&show, &bare, &panel);
	CHECK(panel.count == 1 && !panel.music[0]);
	// an empty folder has no rows, but music still shows
	SSPieces empty = pieces;
	empty.empty = 1;
	SS_panel(&show, &empty, &panel);
	CHECK(panel.count == 0);
	CHECK_STR(panel.music, "Song - Artist");
	// an entry of another kind has no kind row
	bare.type = MOSAIC_ENTRY_TOGGLE;
	SS_panel(&show, &bare, &panel);
	CHECK(panel.count == 0);
	SS_panel(NULL, &pieces, &panel);
	CHECK(panel.count == 0 && !panel.music[0]);

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
