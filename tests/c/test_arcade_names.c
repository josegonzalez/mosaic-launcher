// The arcade names plugin's model: reading Logiqx XML and ClrMamePro text
// DATs (and untrusted ones), catver.ini, the cache, clean titles and the
// names and hiding of a folder. Every DAT here is made up for the test.

#include <stdlib.h>
#include <sys/stat.h>
#include "check.h"
#include "../../plugins/arcade-names/dat_model.h"

#define CACHE TEST_DIR "/arcade-names.cache"

static const char* xml =
	"\xEF\xBB\xBF<?xml version=\"1.0\"?>\n"
	"<!DOCTYPE datafile PUBLIC \"-//Logiqx//DTD ROM Management Datafile//EN\" \"http://www.logiqx.com/Dats/datafile.dtd\">\n"
	"<datafile>\n"
	"\t<header><name>Made up</name><description>Made up (1.0)</description></header>\n"
	"\t<!-- a comment with <game name=\"commented\"> in it -->\n"
	"\t<game name=\"zfight\" sourcefile=\"z.cpp\">\n"
	"\t\t<description>Zed Fighter: The &amp; Warrior (World 910522)</description>\n"
	"\t\t<year>1991</year>\n"
	"\t\t<rom name=\"z.bin\" size=\"1\" crc=\"00000000\"/>\n"
	"\t</game>\n"
	"\t<game name=\"zfightu\" cloneof=\"zfight\" romof=\"zfight\">\n"
	"\t\t<description>Zed Fighter: The &amp; Warrior (US 910522)</description>\n"
	"\t</game>\n"
	"\t<game name=\"ZFIGHTJ\" cloneof=\"ZFight\">\n"
	"\t\t<description><![CDATA[Zed Fighter: The & Warrior (Japan 910214)]]></description>\n"
	"\t</game>\n"
	"\t<game name='zbios' isbios='yes'><description>Zed BIOS</description></game>\n"
	"\t<machine name=\"zdevice\" isdevice=\"yes\" runnable=\"no\"><description>Zed sound device</description></machine>\n"
	"\t<machine name=\"zold\" runnable=\"no\"/>\n"
	"\t<machine name=\"zbroken\">\n"
	"\t\t<description>Broken  Game\n (Japan)</description>\n"
	"\t\t<driver status=\"preliminary\" emulation=\"preliminary\"/>\n"
	"\t</machine>\n"
	"\t<machine name=\"zimperfect\"><description>Imperfect &#233;t&#xE9; &#0; &bogus;</description><driver status=\"imperfect\"/></machine>\n"
	"</datafile>\n";

static const char* cmp =
	"clrmamepro (\n"
	"\tname \"Made up\"\n"
	"\tdescription \"Made up (text)\"\n"
	")\n"
	"\n"
	"resource (\n"
	"\tname cbios\n"
	"\tdescription \"Text BIOS\"\n"
	"\trom ( name bios.bin size 1 crc 00000000 )\n"
	")\n"
	"\n"
	"game (\n"
	"\tname cgame\n"
	"\tdescription \"Text Game (set 1)\"\n"
	"\tyear 1990\n"
	"\tromof cbios\n"
	"\trom ( name a.bin size 1 crc 00000000 )\n"
	")\n"
	"\n"
	"game (\n"
	"\tname cgame2\n"
	"\tcloneof cgame\n"
	"\tdescription \"Text Game (set 2)\"\n"
	"\tdriver ( status preliminary color good )\n"
	")\n"
	"machine ( name cmach description \"Text Machine\" )\n"
	// a second zfight: the first DAT's is kept
	"game ( name zfight description \"Not Kept\" )\n";

static ANTable table;

static void fresh(void) {
	AN_free(&table);
	AN_init(&table);
}

static ANSet* set(const char* name) {
	int i = AN_find(&table, name);
	return i >= 0 ? &table.sets[i] : NULL;
}

static const char* desc(const char* name) {
	const ANSet* s = set(name);
	return s ? AN_str(&table, s->desc) : "(missing)";
}

static const char* parent(const char* name) {
	const ANSet* s = set(name);
	return s && s->parent >= 0 ? AN_str(&table, table.sets[s->parent].name) : "";
}

static void readBoth(void) {
	fresh();
	CHECK(AN_readDatText(&table, xml, strlen(xml)) == 8);
	CHECK(AN_readDatText(&table, cmp, strlen(cmp)) == 5);
	CHECK(AN_finish(&table) == 0);
}

static void testXml(void) {
	readBoth();
	CHECK(table.count == 12);
	for (int i = 1; i < table.count; i++) CHECK(strcmp(AN_str(&table, table.sets[i - 1].name), AN_str(&table, table.sets[i].name)) < 0);
	CHECK_STR(desc("zfight"), "Zed Fighter: The & Warrior (World 910522)");
	CHECK_STR(desc("ZFightJ"), "Zed Fighter: The & Warrior (Japan 910214)");
	CHECK_STR(parent("zfightu"), "zfight");
	CHECK_STR(parent("zfightj"), "zfight");
	CHECK_STR(parent("zfight"), "");
	CHECK(set("zbios") && set("zbios")->flags == AN_BIOS);
	CHECK(set("zdevice") && set("zdevice")->flags == AN_DEVICE);
	CHECK(set("zold") && set("zold")->flags == AN_DEVICE);
	CHECK_STR(desc("zold"), "");
	CHECK(set("zbroken") && set("zbroken")->flags == AN_NOT_WORKING);
	CHECK_STR(desc("zbroken"), "Broken Game (Japan)");
	CHECK(set("zimperfect") && set("zimperfect")->flags == 0);
	CHECK_STR(desc("zimperfect"), "Imperfect \xC3\xA9t\xC3\xA9 &#0; &bogus;");
	CHECK(!set("commented"));
	CHECK(AN_find(&table, "nothing") < 0);
	CHECK(AN_find(&table, NULL) < 0);
}

static void testText(void) {
	readBoth();
	CHECK(set("cbios") && set("cbios")->flags == AN_BIOS);
	CHECK_STR(desc("cgame"), "Text Game (set 1)");
	CHECK_STR(parent("cgame"), ""); // romof isn't a parent
	CHECK_STR(parent("cgame2"), "cgame");
	CHECK(set("cgame2") && set("cgame2")->flags == AN_NOT_WORKING);
	CHECK_STR(desc("cmach"), "Text Machine");
	CHECK_STR(desc("zfight"), "Zed Fighter: The & Warrior (World 910522)");
	CHECK(!set("clrmamepro") && !set("made"));
}

static char* append(char* p, const char* s) {
	size_t n = strlen(s);
	memcpy(p, s, n);
	return p + n;
}

// DATs are untrusted: entities aren't expanded, and sizes are bounded.
static void testUntrusted(void) {
	fresh();
	char* big = malloc(AN_TAG_MAX * 2 + AN_DESC_LEN * 8 + 4096);
	if (!big) return;
	char* p = big;
	p = append(p, "<?xml version=\"1.0\"?>\n<!DOCTYPE x [\n"
		"  <!ENTITY xxe SYSTEM \"file:///etc/passwd\">\n"
		"  <!ENTITY boom \"&boom;&boom;\">\n"
		"  <!-- a ] and a > and a quote ' in a comment -->\n"
		"  <!ATTLIST game name CDATA #IMPLIED>\n"
		"]>\n<datafile>\n");
	p = append(p, "<game name=\"xxe\"><description>&xxe; &boom;</description></game>\n");
	// a tag too long to keep is skipped, with its set
	p = append(p, "<game name=\"longtag\" junk=\"");
	memset(p, 'a', AN_TAG_MAX);
	p += AN_TAG_MAX;
	p = append(p, "\"><description>Long Tag</description></game>\n");
	// a name too long for a set
	p = append(p, "<game name=\"");
	memset(p, 'n', AN_NAME_LEN);
	p += AN_NAME_LEN;
	p = append(p, "\"><description>Long Name</description></game>\n");
	// a description cut to fit, between characters
	p = append(p, "<game name=\"longdesc\"><description>");
	for (int i = 0; i < AN_DESC_LEN; i++) p = append(p, "\xC3\xA9");
	p = append(p, "</description></game>\n");
	// no name, an unclosed quote, a set left open at the end
	p = append(p, "<game><description>No Name</description></game>\n<game name=\"open\"><description>Open");
	CHECK(AN_readDatText(&table, big, (size_t)(p - big)) == 3);
	free(big);
	CHECK(AN_finish(&table) == 0);
	CHECK_STR(desc("xxe"), "&xxe; &boom;");
	CHECK(!set("longtag"));
	CHECK(table.count == 3);
	const char* cut = desc("longdesc");
	CHECK(strlen(cut) == AN_DESC_LEN - 2 && (unsigned char)cut[strlen(cut) - 1] == 0xA9);
	CHECK_STR(desc("open"), "Open");

	// nothing at all, and garbage
	fresh();
	CHECK(AN_readDatText(&table, "", 0) == 0);
	CHECK(AN_readDatText(&table, "<<<>>>&&& (((", 13) == 0);
	CHECK(AN_readDatText(&table, ") ) game ( name", 15) == 0);
	CHECK(AN_readDat(&table, TEST_DIR "/missing.dat") == -1);
	CHECK(AN_finish(&table) == 0 && table.count == 0);
	// added after finishing: refused
	CHECK(AN_readDatText(&table, cmp, strlen(cmp)) == 0);
}

static void testCatver(void) {
	readBoth();
	const char* ini =
		";; catver.ini, made up\r\n"
		"[FOLDER_SETTINGS]\r\n"
		"RootFolderIcon mame\r\n"
		"[Category]\r\n"
		"cgame=Tabletop / Mahjong * Mature *\r\n"
		"cgame2 = Casino / Cards\r\n"
		"cmach=Quiz / Japanese\r\n"
		"zfight=Fighter / Versus\r\n"
		"nothere=Quiz / English\r\n"
		"; zbroken=Quiz\r\n"
		"[VerAdded]\r\n"
		"zimperfect=Quiz\r\n";
	CHECK(AN_readCatverText(&table, ini, strlen(ini)) == 3);
	CHECK(set("cgame")->flags & AN_CATEGORY);
	CHECK(set("cgame2")->flags & AN_CATEGORY);
	CHECK(set("cmach")->flags & AN_CATEGORY);
	CHECK(!(set("zfight")->flags & AN_CATEGORY));
	CHECK(!(set("zbroken")->flags & AN_CATEGORY));
	CHECK(!(set("zimperfect")->flags & AN_CATEGORY));
	CHECK(AN_readCatver(&table, TEST_DIR "/missing.ini") == -1);
}

static void testNames(void) {
	char out[AN_DESC_LEN];
	AN_cleanTitle("Zed Fighter: The Warrior (World 910522)", out, sizeof(out));
	CHECK_STR(out, "Zed Fighter: The Warrior");
	AN_cleanTitle("Game [bootleg] (set 2)", out, sizeof(out));
	CHECK_STR(out, "Game");
	AN_cleanTitle("(Unknown) Game", out, sizeof(out));
	CHECK_STR(out, "(Unknown) Game");
	AN_cleanTitle("Plain", out, sizeof(out));
	CHECK_STR(out, "Plain");
	AN_region("Zed (World 910522)", out, sizeof(out));
	CHECK_STR(out, "World");
	AN_region("Zed (US, rev A)", out, sizeof(out));
	CHECK_STR(out, "US");
	AN_region("Zed (set 2) (Japan)", out, sizeof(out));
	CHECK_STR(out, "set 2");
	AN_region("Zed (1998-04-12)", out, sizeof(out));
	CHECK_STR(out, "");
	AN_region("Zed", out, sizeof(out));
	CHECK_STR(out, "");

	AN_stockName("zfight.zip", out, sizeof(out));
	CHECK_STR(out, "zfight");
	AN_stockName("Game (USA) [!].7z", out, sizeof(out));
	CHECK_STR(out, "Game");
	AN_stockName("(Prototype).zip", out, sizeof(out));
	CHECK_STR(out, "(Prototype)");
	CHECK(AN_setOf("ZFight.ZIP", out, sizeof(out)) && !strcmp(out, "zfight"));
	CHECK(AN_setOf("zfight", out, sizeof(out)) && !strcmp(out, "zfight"));
	CHECK(!AN_setOf(".hidden.zip", out, sizeof(out)));
	CHECK(!AN_setOf(".zip", out, sizeof(out)));

	CHECK(AN_hasTag("MAME, FBN,fbneo", "FBN"));
	CHECK(AN_hasTag("MAME, FBN,fbneo", "FBNEO"));
	CHECK(AN_hasTag("MAME, FBN,fbneo", "mame"));
	CHECK(!AN_hasTag("MAME, FBN,fbneo", "FB"));
	CHECK(!AN_hasTag("MAME", ""));
	CHECK(!AN_hasTag("", "MAME"));
}

// A folder's names and hiding, as the plugin asks for them.
static ANFolder folder;

static void list(const ANOptions* options, const char* const* files, int count) {
	AN_folderFree(&folder);
	CHECK(AN_folder(&table, options, files, count, &folder) == 0);
}

static const char* shown(const char* file) {
	int i = AN_folderFind(&folder, file);
	if (i < 0) return "(not a set)";
	if (folder.hidden[i]) return "(hidden)";
	return folder.names[i] ? folder.names[i] : "(as it is)";
}

static void testFolder(void) {
	readBoth();
	ANOptions plain = { 0 };
	static const char* const one[] = { "zfight.zip", "readme.txt", "zbios.zip", "zdevice.zip", "cbios.7z" };
	list(&plain, one, 5);
	CHECK(folder.count == 4);
	CHECK_STR(shown("zfight.zip"), "Zed Fighter: The & Warrior");
	CHECK_STR(shown("readme.txt"), "(not a set)");
	CHECK_STR(shown("zbios.zip"), "(hidden)");
	CHECK_STR(shown("zdevice.zip"), "(hidden)");
	CHECK_STR(shown("cbios.7z"), "(hidden)");

	// clones sharing the parent's clean title get their regions back
	static const char* const clones[] = { "zfight.zip", "zfightu.zip", "ZFIGHTJ.zip", "cgame.zip", "cgame2.zip" };
	list(&plain, clones, 5);
	CHECK_STR(shown("zfight.zip"), "Zed Fighter: The & Warrior (World)");
	CHECK_STR(shown("zfightu.zip"), "Zed Fighter: The & Warrior (US)");
	CHECK_STR(shown("ZFIGHTJ.zip"), "Zed Fighter: The & Warrior (Japan)");
	// "set 1" and "set 2" are their regions
	CHECK_STR(shown("cgame.zip"), "Text Game (set 1)");
	CHECK_STR(shown("cgame2.zip"), "Text Game (set 2)");
	// the same set twice isn't a collision
	static const char* const twice[] = { "zfight.zip", "zfight.7z" };
	list(&plain, twice, 2);
	CHECK_STR(shown("zfight.zip"), "Zed Fighter: The & Warrior");
	CHECK_STR(shown("zfight.7z"), "Zed Fighter: The & Warrior");

	// full names
	ANOptions full = { .full = 1 };
	list(&full, clones, 5);
	CHECK_STR(shown("zfight.zip"), "Zed Fighter: The & Warrior (World 910522)");
	CHECK_STR(shown("zfightu.zip"), "Zed Fighter: The & Warrior (US 910522)");
	CHECK_STR(shown("cgame.zip"), "Text Game (set 1)");

	// clones hidden only when their parent is there
	ANOptions clones_off = { .clones = 1 };
	list(&clones_off, clones, 5);
	CHECK_STR(shown("zfight.zip"), "Zed Fighter: The & Warrior");
	CHECK_STR(shown("zfightu.zip"), "(hidden)");
	CHECK_STR(shown("ZFIGHTJ.zip"), "(hidden)");
	CHECK_STR(shown("cgame2.zip"), "(hidden)");
	static const char* const orphans[] = { "zfightu.zip", "ZFIGHTJ.zip" };
	list(&clones_off, orphans, 2);
	CHECK_STR(shown("zfightu.zip"), "Zed Fighter: The & Warrior (US)");
	CHECK_STR(shown("ZFIGHTJ.zip"), "Zed Fighter: The & Warrior (Japan)");

	// not working
	ANOptions working = { .not_working = 1 };
	static const char* const broken[] = { "zbroken.zip", "zimperfect.zip", "cgame2.zip" };
	list(&working, broken, 3);
	CHECK_STR(shown("zbroken.zip"), "(hidden)");
	CHECK_STR(shown("zimperfect.zip"), "Imperfect \xC3\xA9t\xC3\xA9 &#0; &bogus;");
	CHECK_STR(shown("cgame2.zip"), "(hidden)");
	list(&plain, broken, 3);
	CHECK_STR(shown("zbroken.zip"), "Broken Game");

	// a parent hidden for its category leaves its clone showing
	static const char* const pair[] = { "cgame.zip", "cgame2.zip" };
	ANOptions clones_only = { .clones = 1 };
	set("cgame")->flags |= AN_CATEGORY;
	ANOptions categories = { .clones = 1, .categories = 1 };
	list(&categories, pair, 2);
	CHECK_STR(shown("cgame.zip"), "(hidden)");
	CHECK_STR(shown("cgame2.zip"), "Text Game");
	list(&clones_only, pair, 2);
	CHECK_STR(shown("cgame.zip"), "Text Game");
	CHECK_STR(shown("cgame2.zip"), "(hidden)");

	// a set with no description keeps its name
	static const char* const bare[] = { "zold.zip", "cmach.zip" };
	set("zold")->flags = 0;
	list(&plain, bare, 2);
	CHECK_STR(shown("zold.zip"), "(as it is)");
	CHECK_STR(shown("cmach.zip"), "Text Machine");

	list(&plain, NULL, 0);
	CHECK(folder.count == 0);
	AN_folderFree(&folder);
}

// Titles the same down to their parentheses: the whole of them, then the
// full description.
static void testCollisions(void) {
	fresh();
	const char* dat =
		"<datafile>"
		"<game name=\"a\"><description>Same (US 920101, rev A)</description></game>"
		"<game name=\"b\"><description>Same (US 920101, rev B)</description></game>"
		"<game name=\"c\"><description>Same (Japan)</description></game>"
		"<game name=\"d\"><description>Same (Europe) [x]</description></game>"
		"<game name=\"e\"><description>Same (Europe) [y]</description></game>"
		"<game name=\"f\"><description>Other</description></game>"
		"</datafile>";
	CHECK(AN_readDatText(&table, dat, strlen(dat)) == 6);
	CHECK(AN_finish(&table) == 0);
	static const char* const files[] = { "a.zip", "b.zip", "c.zip", "d.zip", "e.zip", "f.zip" };
	ANOptions plain = { 0 };
	list(&plain, files, 6);
	CHECK_STR(shown("a.zip"), "Same (US, rev A)");
	CHECK_STR(shown("b.zip"), "Same (US, rev B)");
	CHECK_STR(shown("c.zip"), "Same (Japan)");
	CHECK_STR(shown("d.zip"), "Same (Europe) [x]");
	CHECK_STR(shown("e.zip"), "Same (Europe) [y]");
	CHECK_STR(shown("f.zip"), "Other");
	AN_folderFree(&folder);
}

static void writeBytes(const char* path, const void* data, size_t len) {
	FILE* f = fopen(path, "wb");
	if (!f) return;
	fwrite(data, 1, len, f);
	fclose(f);
}

static long fileSize(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

// The cache: read back while the signature matches, refused when it's
// stale or damaged.
static void testCache(void) {
	CHECK(system("mkdir -p '" TEST_DIR "'") == 0);
	readBoth();
	const char* catver = "[Category]\ncgame=Quiz\n";
	CHECK(AN_readCatverText(&table, catver, strlen(catver)) == 1);
	const char* signature = "made-up.dat\t100\t200\ncatver.ini\t10\t20\n";
	CHECK(AN_save(&table, CACHE, signature) == 0);
	ANTable loaded;
	CHECK(AN_load(&loaded, CACHE, signature) == 0);
	CHECK(loaded.count == table.count && loaded.strings_len == table.strings_len);
	ANTable saved = table;
	table = loaded;
	CHECK_STR(desc("zfight"), "Zed Fighter: The & Warrior (World 910522)");
	CHECK_STR(parent("zfightj"), "zfight");
	CHECK(set("zbios")->flags == AN_BIOS && (set("cgame")->flags & AN_CATEGORY));
	table = saved;
	AN_free(&loaded);

	// a DAT changed
	CHECK(AN_load(&loaded, CACHE, "made-up.dat\t101\t200\ncatver.ini\t10\t20\n") == -1 && loaded.count == 0);
	CHECK(AN_load(&loaded, TEST_DIR "/missing.cache", signature) == -1);

	// damaged: cut short, a byte flipped in a set, garbage after it
	FILE* f = fopen(CACHE, "rb");
	long size = fileSize(CACHE);
	char* bytes = size > 0 ? malloc((size_t)size + 1) : NULL;
	if (!f || !bytes || fread(bytes, 1, (size_t)size, f) != (size_t)size) {
		CHECK(0);
		if (f) fclose(f);
		free(bytes);
		return;
	}
	fclose(f);
	writeBytes(CACHE, bytes, (size_t)size - 1);
	CHECK(AN_load(&loaded, CACHE, signature) == -1);
	bytes[size] = 'x';
	writeBytes(CACHE, bytes, (size_t)size + 1);
	CHECK(AN_load(&loaded, CACHE, signature) == -1);
	size_t header = 24 + strlen(signature);
	char* damaged = malloc((size_t)size);
	if (damaged) {
		memcpy(damaged, bytes, (size_t)size);
		damaged[header + 3] = 0x7f; // the first set's name, far past the strings
		writeBytes(CACHE, damaged, (size_t)size);
		CHECK(AN_load(&loaded, CACHE, signature) == -1);
		memcpy(damaged, bytes, (size_t)size);
		damaged[header + 8] = 0x7f; // its parent, past the sets
		writeBytes(CACHE, damaged, (size_t)size);
		CHECK(AN_load(&loaded, CACHE, signature) == -1);
		memcpy(damaged, bytes, (size_t)size);
		damaged[size - 1] = 'x'; // the strings unterminated
		writeBytes(CACHE, damaged, (size_t)size);
		CHECK(AN_load(&loaded, CACHE, signature) == -1);
		free(damaged);
	}
	free(bytes);
	CHECK(loaded.count == 0 && loaded.sets == NULL);
	remove(CACHE);
}

// A DAT on the card, read as the plugin reads it.
static void testFile(void) {
	CHECK(system("mkdir -p '" TEST_DIR "'") == 0);
	writeBytes(TEST_DIR "/made-up.dat", xml, strlen(xml));
	fresh();
	CHECK(AN_readDat(&table, TEST_DIR "/made-up.dat") == 8);
	CHECK(AN_finish(&table) == 0 && table.count == 8);
	CHECK_STR(desc("zfightu"), "Zed Fighter: The & Warrior (US 910522)");
	const char* catver = "[Category]\nzfight=Casino / Reels\n";
	writeBytes(TEST_DIR "/catver.ini", catver, strlen(catver));
	CHECK(AN_readCatver(&table, TEST_DIR "/catver.ini") == 1);
	CHECK(AN_bytes(&table) > 0);
}

int main(void) {
	AN_init(&table);
	testXml();
	testText();
	testUntrusted();
	testCatver();
	testNames();
	testFolder();
	testCollisions();
	testCache();
	testFile();
	AN_free(&table);
	CHECK(system("rm -rf '" TEST_DIR "'") == 0);
	CHECK_DONE();
}
