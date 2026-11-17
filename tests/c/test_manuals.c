// The manuals plugin's model: names, where a manual is looked for and
// found on a fixture card (the manual set's question), pages in order,
// text tidied and wrapped, a zoomed page's pan and the last page read.

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"
#include "manual_model.h"

#define CARD TEST_SDCARD_PATH
#define ROMS CARD "/Roms"
#define GB ROMS "/Game Boy (GB)"
#define PS ROMS "/Sony PlayStation (PS)"
#define MAME ROMS "/Arcade (MAME)"

static void put(const char* path, const char* body) {
	FILE* f = fopen(path, "w");
	CHECK(f != NULL);
	if (!f) return;
	fputs(body, f);
	fclose(f);
}

static void makeCard(void) {
	CHECK(system("rm -rf '" CARD "'") == 0);
	CHECK(system("mkdir -p '" GB "/.manuals/Tetris' '" GB "/.manuals/Empty' '" GB "/.manuals/Both/sub' '" GB "/Sub'"
		" '" PS "/.manuals/Final Fantasy' '" PS "/Chrono/' '" PS "/.manuals/Chrono' '" MAME "/.manuals/Street Fighter II'"
		" '" CARD "/Outside'") == 0);
	put(GB "/.manuals/Tetris/02.png", "");
	put(GB "/.manuals/Tetris/1.png", "");
	put(GB "/.manuals/Tetris/10.PNG", "");
	put(GB "/.manuals/Tetris/._1.png", "");
	put(GB "/.manuals/Tetris/notes.txt", "");
	put(GB "/.manuals/Zelda.txt", "Zelda");
	put(GB "/.manuals/Empty/readme.md", "");
	put(GB "/.manuals/Both.txt", "");
	put(GB "/.manuals/Both/cover.jpg", "");
	put(GB "/.manuals/Mario.TXT", "");
	put(GB "/.manuals/.hidden.txt", "");
	put(CARD "/Outside/01.png", "");
	CHECK(symlink(CARD "/Outside", GB "/.manuals/Linked") == 0);
	CHECK(symlink(CARD "/Outside/01.png", GB "/.manuals/Tetris/05.png") == 0);
	put(GB "/Tetris.gb", "");
	put(GB "/Sub/Mario.gb", "");
	put(PS "/.manuals/Final Fantasy/01.jpeg", "");
	put(PS "/Final Fantasy (Disc 1).chd", "");
	put(PS "/Chrono/Chrono.m3u", "");
	put(PS "/.manuals/Chrono/01.png", "");
	put(MAME "/sf2.zip", "");
	put(MAME "/.manuals/Street Fighter II/01.png", "");
}

///////////////////////////////////////
// names

static void testNames(void) {
	CHECK(MN_isPage("01.png"));
	CHECK(MN_isPage("Page 2.JPG"));
	CHECK(MN_isPage("cover.jpeg"));
	CHECK(!MN_isPage("._01.png"));
	CHECK(!MN_isPage(".png"));
	CHECK(!MN_isPage("notes.txt"));
	CHECK(!MN_isPage("01.png.bak"));
	CHECK(!MN_isPage("a/01.png"));
	CHECK(!MN_isPage(NULL));

	// numbers by value, then letters ignoring case
	CHECK(MN_nameCompare("2.png", "10.png") < 0);
	CHECK(MN_nameCompare("02.png", "10.png") < 0);
	CHECK(MN_nameCompare("page 9.png", "Page 10.png") < 0);
	CHECK(MN_nameCompare("b.png", "A.png") > 0);
	CHECK(MN_nameCompare("01.png", "1.png") != 0); // a tie is broken bytewise
	CHECK(MN_nameCompare("1.png", "1.png") == 0);
	CHECK(MN_nameCompare("1.png", "1a.png") < 0);

	char out[MN_NAME_MAX];
	CHECK(MN_stripDisc("Final Fantasy (Disc 1)", out, sizeof(out)) == 1);
	CHECK_STR(out, "Final Fantasy");
	CHECK(MN_stripDisc("Game (USA) (cd 2) (Rev 1)", out, sizeof(out)) == 1);
	CHECK_STR(out, "Game (USA) (Rev 1)");
	CHECK(MN_stripDisc("Game (Disk 3 of 4)", out, sizeof(out)) == 1);
	CHECK_STR(out, "Game");
	CHECK(MN_stripDisc("Game (Side A)", out, sizeof(out)) == 0);
	CHECK_STR(out, "Game (Side A)");
	CHECK(MN_stripDisc("Game (Disc)", out, sizeof(out)) == 0);
	CHECK(MN_stripDisc(" (Disc 1)", out, sizeof(out)) == 0); // nothing would be left
}

static void testWhere(void) {
	MNWhere w;
	CHECK(MN_where(ROMS, GB "/Tetris (World).gb", 0, "Tetris", &w) == 2);
	CHECK(w.dir_count == 1);
	CHECK_STR(w.dirs[0], GB);
	CHECK_STR(w.names[0], "Tetris (World)");
	CHECK_STR(w.names[1], "Tetris");

	// a disc, and its game as multi-disc shows it, by the .m3u
	CHECK(MN_where(ROMS, PS "/Final Fantasy (Disc 1).chd", 0, "", &w) == 2);
	CHECK_STR(w.names[0], "Final Fantasy (Disc 1)");
	CHECK_STR(w.names[1], "Final Fantasy");
	CHECK(MN_where(ROMS, PS "/Final Fantasy.m3u", 0, NULL, &w) == 1);
	CHECK_STR(w.names[0], "Final Fantasy");

	// a game in a folder of its own: by the folder, beside it
	CHECK(MN_where(ROMS, PS "/Chrono/Chrono.m3u", 0, NULL, &w) == 1);
	CHECK_STR(w.dirs[0], PS);
	CHECK_STR(w.names[0], "Chrono");
	CHECK(MN_where(ROMS, PS "/Chrono", 1, NULL, &w) == 1);
	CHECK_STR(w.dirs[0], PS);
	CHECK_STR(w.names[0], "Chrono");
	CHECK(MN_where(ROMS, PS "/Chrono/", 1, NULL, &w) == 1);
	CHECK_STR(w.names[0], "Chrono");
	// an .m3u that isn't the folder's own is a game in it
	CHECK(MN_where(ROMS, PS "/Chrono/Other.m3u", 0, NULL, &w) == 1);
	CHECK_STR(w.dirs[0], PS "/Chrono");

	// deeper than its system folder: that folder too
	CHECK(MN_where(ROMS, GB "/Sub/Mario.gb", 0, NULL, &w) == 1);
	CHECK(w.dir_count == 2);
	CHECK_STR(w.dirs[0], GB "/Sub");
	CHECK_STR(w.dirs[1], GB);

	// arcade-names' title through the index's name; a name with a slash isn't one
	CHECK(MN_where(ROMS, MAME "/sf2.zip", 0, "Street Fighter II", &w) == 2);
	CHECK_STR(w.names[1], "Street Fighter II");
	CHECK(MN_where(ROMS, MAME "/sf2.zip", 0, "AC/DC", &w) == 1);

	// a file with no extension, a dot file's name, and nothing
	CHECK(MN_where(ROMS, GB "/README", 0, NULL, &w) == 1);
	CHECK_STR(w.names[0], "README");
	CHECK(MN_where(ROMS, GB "/.hidden", 0, NULL, &w) == 1);
	CHECK_STR(w.names[0], ".hidden");
	CHECK(MN_where(ROMS, "noslash", 0, NULL, &w) == 0);
	CHECK(MN_where(ROMS, NULL, 0, NULL, &w) == 0);
}

///////////////////////////////////////
// the manual set

static int found(MNShelf* shelf, const char* path, int is_dir, const char* shown, char* out) {
	MNWhere w;
	if (!MN_where(ROMS, path, is_dir, shown, &w)) return MN_NONE;
	return MN_find(shelf, &w, out, MN_PATH_MAX);
}

static void testFind(void) {
	MNShelf shelf;
	MN_shelfInit(&shelf);
	char path[MN_PATH_MAX];
	CHECK(found(&shelf, GB "/Tetris.gb", 0, NULL, path) == MN_PAGES);
	CHECK_STR(path, GB "/.manuals/Tetris");
	// the card's FAT ignores case, and so does finding it
	CHECK(found(&shelf, GB "/tetris.gb", 0, NULL, path) == MN_PAGES);
	CHECK_STR(path, GB "/.manuals/Tetris");
	// by the name the list shows, a stock-tidied one without its tags
	CHECK(found(&shelf, GB "/Tetris (World) (Rev 1).gb", 0, "Tetris", path) == MN_PAGES);
	CHECK(found(&shelf, GB "/Zelda.gb", 0, NULL, path) == MN_TEXT);
	CHECK_STR(path, GB "/.manuals/Zelda.txt");
	// pages win over a .txt
	CHECK(found(&shelf, GB "/Both.gb", 0, NULL, path) == MN_PAGES);
	CHECK_STR(path, GB "/.manuals/Both");
	// a folder with no pages, a link to one with pages and a dot file
	CHECK(found(&shelf, GB "/Empty.gb", 0, NULL, path) == MN_NONE);
	CHECK_STR(path, "");
	CHECK(found(&shelf, GB "/Linked.gb", 0, NULL, path) == MN_NONE);
	CHECK(found(&shelf, GB "/.hidden.gb", 0, NULL, path) == MN_NONE);
	CHECK(found(&shelf, GB "/Kirby.gb", 0, NULL, path) == MN_NONE);
	// in its system folder's .manuals, a .TXT too
	CHECK(found(&shelf, GB "/Sub/Mario.gb", 0, NULL, path) == MN_TEXT);
	CHECK_STR(path, GB "/.manuals/Mario.TXT");
	// multi-disc's game and a disc, a game folder, and an arcade set by its title
	CHECK(found(&shelf, PS "/Final Fantasy.m3u", 0, NULL, path) == MN_PAGES);
	CHECK(found(&shelf, PS "/Final Fantasy (Disc 2).chd", 0, NULL, path) == MN_PAGES);
	CHECK_STR(path, PS "/.manuals/Final Fantasy");
	CHECK(found(&shelf, PS "/Chrono", 1, NULL, path) == MN_PAGES);
	CHECK(found(&shelf, PS "/Chrono/Chrono.m3u", 0, NULL, path) == MN_PAGES);
	CHECK(found(&shelf, MAME "/sf2.zip", 0, NULL, path) == MN_NONE);
	CHECK(found(&shelf, MAME "/sf2.zip", 0, "Street Fighter II", path) == MN_PAGES);

	// each folder is read once, until it's cleared
	CHECK(shelf.count == 4);
	put(GB "/.manuals/Kirby.txt", "");
	CHECK(found(&shelf, GB "/Kirby.gb", 0, NULL, path) == MN_NONE);
	MN_shelfClear(&shelf);
	CHECK(found(&shelf, GB "/Kirby.gb", 0, NULL, path) == MN_TEXT);
	CHECK(MN_shelfFind(&shelf, GB, "") == NULL);
	CHECK(MN_shelfFind(&shelf, NULL, "Kirby") == NULL);
	MN_shelfClear(&shelf);

	// past MN_SHELF_MAX folders, the least used goes
	for (int i = 0; i < MN_SHELF_MAX + 4; i++) {
		char dir[MN_PATH_MAX];
		snprintf(dir, sizeof(dir), CARD "/nowhere/%i", i);
		MN_shelfFind(&shelf, dir, "x");
		MN_shelfFind(&shelf, GB, "Tetris");
	}
	CHECK(shelf.count == MN_SHELF_MAX);
	CHECK(MN_shelfFind(&shelf, GB, "Tetris") != NULL);
	MN_shelfClear(&shelf);
}

///////////////////////////////////////
// pages

static void testPages(void) {
	MNPages pages = { 0 };
	CHECK(MN_pagesScan(GB "/.manuals/Tetris", &pages) == 3);
	if (pages.count == 3) {
		CHECK_STR(pages.files[0], "1.png");
		CHECK_STR(pages.files[1], "02.png");
		CHECK_STR(pages.files[2], "10.PNG");
	}
	// a link to a folder isn't scanned, and a missing one is -1
	CHECK(MN_pagesScan(GB "/.manuals/Linked", &pages) == -1);
	CHECK(pages.count == 0);
	CHECK(MN_pagesScan(GB "/.manuals/Missing", &pages) == -1);
	CHECK(MN_pagesScan(GB "/.manuals/Empty", &pages) == 0);
	MN_pagesFree(&pages);
}

///////////////////////////////////////
// text

static int oneEach(const char* s, int len, void* ctx) {
	(void)s;
	(void)len;
	(void)ctx;
	return 1;
}

// "w" is 3 wide, everything else 1
static int wide(const char* s, int len, void* ctx) {
	(void)ctx;
	return len == 1 && s[0] == 'w' ? 3 : 1;
}

static void lineIs(const MNText* t, int i, const char* want) {
	CHECK(i < t->line_count);
	if (i >= t->line_count) return;
	char got[256];
	snprintf(got, sizeof(got), "%.*s", t->lines[i].len, t->text + t->lines[i].start);
	CHECK_STR(got, want);
}

static void testText(void) {
	MNText t = { 0 };
	// a BOM, CRLF and a lone CR, a tab, a bell, a stray byte, a C1 control and é
	const char raw[] = "\xEF\xBB\xBF" "a\r\nb\rc\td\a\xFFx\xC2\x85y\xC3\xA9";
	CHECK(MN_textSet(&t, raw, sizeof(raw) - 1) == 0);
	CHECK_STR(t.text, "a\nb\nc    d?xy\xC3\xA9");
	CHECK(t.size == (int)strlen(t.text));
	// a cut character, an overlong one and a surrogate
	CHECK(MN_textSet(&t, "\xE2\x82", 2) == 0);
	CHECK_STR(t.text, "??");
	CHECK(MN_textSet(&t, "\xC0\xAF\xED\xA0\x80", 5) == 0);
	CHECK_STR(t.text, "?????");

	CHECK(MN_textSet(&t, "hello world foo\n\nabcdefghijklmnop\n  two  spaces\n", 48) == 0);
	CHECK(MN_wrap(&t, 10, oneEach, NULL) == 7);
	lineIs(&t, 0, "hello");
	lineIs(&t, 1, "world foo");
	lineIs(&t, 2, "");
	lineIs(&t, 3, "abcdefghij");
	lineIs(&t, 4, "klmnop");
	lineIs(&t, 5, "  two ");
	lineIs(&t, 6, "spaces");
	// wider characters break sooner, and a line always takes one
	CHECK(MN_textSet(&t, "wwwwa b", 7) == 0);
	CHECK(MN_wrap(&t, 7, wide, NULL) == 3);
	lineIs(&t, 0, "ww");
	lineIs(&t, 1, "wwa");
	lineIs(&t, 2, "b");
	CHECK(MN_wrap(&t, 1, wide, NULL) == 6);
	// UTF-8 isn't split
	CHECK(MN_textSet(&t, "\xC3\xA9\xC3\xA9\xC3\xA9", 6) == 0);
	CHECK(MN_wrap(&t, 2, oneEach, NULL) == 2);
	lineIs(&t, 0, "\xC3\xA9\xC3\xA9");
	// nothing is one empty line
	CHECK(MN_textSet(&t, "", 0) == 0);
	CHECK(MN_wrap(&t, 10, oneEach, NULL) == 1);
	CHECK(MN_wrap(&t, 0, oneEach, NULL) == -1);

	CHECK(MN_lastTop(100, 12) == 88);
	CHECK(MN_lastTop(5, 12) == 0);
	CHECK(MN_scroll(0, -1, 100, 12) == 0);
	CHECK(MN_scroll(80, 11, 100, 12) == 88);
	CHECK(MN_scroll(10, 11, 100, 12) == 21);
	CHECK(MN_scroll(3, 1, 5, 12) == 0);

	// a file: read, cut short past MN_TEXT_MAX, and never through a link
	put(CARD "/Outside/guide.txt", "line one\nline two\n");
	CHECK(MN_textLoad(CARD "/Outside/guide.txt", &t) == 0);
	CHECK_STR(t.text, "line one\nline two\n");
	CHECK(!t.truncated);
	FILE* f = fopen(CARD "/Outside/long.txt", "w");
	CHECK(f != NULL);
	for (int i = 0; f && i < MN_TEXT_MAX / 8 + 10; i++) fputs("abcdefg\n", f);
	if (f) fclose(f);
	CHECK(MN_textLoad(CARD "/Outside/long.txt", &t) == 0);
	CHECK(t.truncated);
	CHECK(t.size == MN_TEXT_MAX);
	CHECK(symlink(CARD "/Outside/guide.txt", CARD "/Outside/link.txt") == 0);
	CHECK(MN_textLoad(CARD "/Outside/link.txt", &t) == -1);
	CHECK(MN_textLoad(CARD "/Outside", &t) == -1);
	CHECK(MN_textLoad(CARD "/Outside/missing.txt", &t) == -1);
	MN_textFree(&t);
}

///////////////////////////////////////
// a page on screen

static int rectIs(MNRect r, int x, int y, int w, int h) {
	return r.x == x && r.y == y && r.w == w && r.h == h;
}

static void testPlace(void) {
	int w = 0, h = 0;
	// a scan's page to a 640x480 screen, and a small one enlarged
	CHECK(MN_fit(2550, 3300, 640, 480, &w, &h) == 0);
	CHECK(w == 371 && h == 480);
	CHECK(MN_fit(160, 120, 640, 480, &w, &h) == 0);
	CHECK(w == 640 && h == 480);
	CHECK(MN_fit(0, 120, 640, 480, &w, &h) == -1);

	MNRect view = { 0, 0, 640, 480 }, src, dst;
	// fitted: centred, all of it
	MN_place(&view, 371, 480, 0, 0, &src, &dst);
	CHECK(rectIs(src, 0, 0, 371, 480));
	CHECK(rectIs(dst, 134, 0, 371, 480));
	// zoomed: the view's size from the pan, still centred where it's narrower
	MN_place(&view, 600, 960, 50, 300, &src, &dst);
	CHECK(rectIs(src, 0, 300, 600, 480));
	CHECK(rectIs(dst, 20, 0, 600, 480));
	MN_place(&view, 742, 960, 500, 900, &src, &dst);
	CHECK(rectIs(src, 102, 480, 640, 480));
	CHECK(rectIs(dst, 0, 0, 640, 480));

	int x = -5, y = 9999;
	MN_panClamp(&view, 742, 960, &x, &y);
	CHECK(x == 0 && y == 480);
	x = 50;
	y = 50;
	MN_panClamp(&view, 300, 400, &x, &y);
	CHECK(x == 0 && y == 0);

	// around the status pill and header widgets, in the top right
	MNRect status = { 500, 20, 120, 60 }, header = { 400, 20, 90, 60 }, none = { 0, 0, 0, 0 }, parts[4];
	MNRect hole = MN_hole(&status, &header);
	CHECK(rectIs(hole, 400, 20, 220, 60));
	hole = MN_hole(&none, &status);
	CHECK(rectIs(hole, 500, 20, 120, 60));
	hole = MN_hole(&header, &none);
	CHECK(rectIs(hole, 400, 20, 90, 60));
	hole = MN_hole(&none, &none);
	CHECK(hole.w == 0);
	hole = MN_hole(&status, &header);
	CHECK(MN_avoid(&view, &hole, parts) == 4);
	CHECK(rectIs(parts[0], 0, 0, 640, 20));
	CHECK(rectIs(parts[1], 0, 20, 400, 60));
	CHECK(rectIs(parts[2], 620, 20, 20, 60));
	CHECK(rectIs(parts[3], 0, 80, 640, 400));
	// a page that doesn't reach it, and one only partly beside it
	MNRect page = { 134, 0, 371, 480 };
	CHECK(MN_avoid(&page, &hole, parts) == 3);
	CHECK(rectIs(parts[0], 134, 0, 371, 20));
	CHECK(rectIs(parts[1], 134, 20, 266, 60));
	CHECK(rectIs(parts[2], 134, 80, 371, 400));
	MNRect left = { 0, 100, 300, 300 };
	CHECK(MN_avoid(&left, &hole, parts) == 1);
	CHECK(rectIs(parts[0], 0, 100, 300, 300));
	CHECK(MN_avoid(&none, &hole, parts) == 0);
	CHECK(MN_avoid(&view, &none, parts) == 1);

	// zooming in keeps the middle in the middle, and out goes back to fitted
	MN_panZoom(&view, 371, 480, 0, 0, 742, 960, &x, &y);
	CHECK(x == 50 && y == 240);
	MN_panZoom(&view, 742, 960, 102, 0, 371, 480, &x, &y);
	CHECK(x == 0 && y == 0);
}

///////////////////////////////////////
// the last page read

static void testMemory(void) {
	MNMemory m;
	MN_memoryInit(&m);
	char key[MN_PATH_MAX];
	MN_memoryKey(CARD, GB "/Tetris.gb", key, sizeof(key));
	CHECK_STR(key, "/Roms/Game Boy (GB)/Tetris.gb");
	MN_memoryKey(CARD, "/elsewhere/Tetris.gb", key, sizeof(key));
	CHECK_STR(key, "/elsewhere/Tetris.gb");
	MN_memoryKey(CARD, CARD "x/Tetris.gb", key, sizeof(key));
	CHECK_STR(key, CARD "x/Tetris.gb");

	CHECK(MN_memoryGet(&m, "/Roms/a.gb") == 0);
	CHECK(MN_memorySet(&m, "/Roms/a.gb", 3) == 1);
	CHECK(MN_memorySet(&m, "/Roms/b.gb", 7) == 1);
	CHECK(MN_memorySet(&m, "/Roms/b.gb", 7) == 0);
	CHECK(MN_memoryGet(&m, "/Roms/a.gb") == 3);
	// a game read again goes first
	CHECK(MN_memorySet(&m, "/Roms/a.gb", 4) == 1);
	CHECK_STR(m.items[0].key, "/Roms/a.gb");
	CHECK(m.count == 2);
	CHECK(MN_memorySet(&m, "/Roms/tab\there.gb", 1) == -1);
	CHECK(MN_memorySet(&m, "", 1) == -1);
	CHECK(MN_memorySet(&m, "/Roms/a.gb", -1) == -1);

	const char* file = CARD "/manuals.txt";
	CHECK(MN_memorySave(&m, file) == 0);
	MNMemory back;
	MN_memoryInit(&back);
	CHECK(MN_memoryLoad(&back, file) == 2);
	CHECK(MN_memoryGet(&back, "/Roms/a.gb") == 4);
	CHECK(MN_memoryGet(&back, "/Roms/b.gb") == 7);
	CHECK(access(CARD "/manuals.txt.tmp", F_OK) != 0);

	// lines it can't read are skipped, and a key given twice keeps its first
	put(file, "2\t/Roms/c.gb\nx\t/Roms/d.gb\n\t/Roms/e.gb\n5\n123456\t/Roms/f.gb\n9\t/Roms/c.gb\n1\t/Roms/g.gb\r\n");
	CHECK(MN_memoryLoad(&back, file) == 2);
	CHECK(MN_memoryGet(&back, "/Roms/c.gb") == 2);
	CHECK(MN_memoryGet(&back, "/Roms/g.gb") == 1);
	CHECK(MN_memoryGet(&back, "/Roms/d.gb") == 0);
	CHECK(MN_memoryLoad(&back, CARD "/missing.txt") == 0);

	// past MN_MEMORY_MAX the oldest goes
	for (int i = 0; i < MN_MEMORY_MAX + 5; i++) {
		char k[64];
		snprintf(k, sizeof(k), "/Roms/%i.gb", i);
		MN_memorySet(&m, k, i % 7);
	}
	CHECK(m.count == MN_MEMORY_MAX);
	CHECK_STR(m.items[0].key, "/Roms/504.gb");
	CHECK(MN_memoryGet(&m, "/Roms/0.gb") == 0);
	CHECK(MN_memoryGet(&m, "/Roms/5.gb") == 5);
	CHECK(MN_memoryGet(&m, "/Roms/a.gb") == 0);
	MN_memoryFree(&m);
	MN_memoryFree(&back);
}

int main(void) {
	makeCard();
	testNames();
	testWhere();
	testFind();
	testPages();
	testText();
	testPlace();
	testMemory();
	CHECK_DONE();
}
