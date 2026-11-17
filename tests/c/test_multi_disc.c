// The multi-disc plugin's model: markers, which files make a game, its
// .m3u, the saves copied to its name and what moves with each disc.

#include <stdlib.h>
#include "check.h"
#include "../../plugins/multi-disc/disc_model.h"

static int marker(const char* name, int* number, int* total) {
	MDMarker m;
	if (!MD_marker(name, &m)) return 0;
	*number = m.number;
	*total = m.total;
	return 1;
}

static void testMarkers(void) {
	int n = 0, t = 0;
	CHECK(marker("Game (Disc 1).chd", &n, &t) && n == 1 && t == 0);
	CHECK(marker("Game (CD 2).cue", &n, &t) && n == 2 && t == 0);
	CHECK(marker("Game (Disk 3).adf", &n, &t) && n == 3 && t == 0);
	CHECK(marker("Game (disc 1 of 3).chd", &n, &t) && n == 1 && t == 3);
	CHECK(marker("Game (DISC 12 OF 12).chd", &n, &t) && n == 12 && t == 12);
	CHECK(marker("Game (Disc 01).chd", &n, &t) && n == 1);
	CHECK(marker("Game (USA) (Disc 2) (Rev 1).chd", &n, &t) && n == 2);
	// anything else stays separate
	CHECK(!marker("Game.chd", &n, &t));
	CHECK(!marker("Game (Disc A).chd", &n, &t));
	CHECK(!marker("Game (Disc 0).chd", &n, &t));
	CHECK(!marker("Game (Disc1).chd", &n, &t));
	CHECK(!marker("Game (Disc  1).chd", &n, &t));
	CHECK(!marker("Game [Disc 1].chd", &n, &t));
	CHECK(!marker("Game (Side 1).chd", &n, &t));
	CHECK(!marker("Game (Disc 1 of).chd", &n, &t));
	CHECK(!marker("Game (Disc 3 of 2).chd", &n, &t));
	CHECK(!marker("Game (Disc 1, 2).chd", &n, &t));
	CHECK(!marker("Game (Disc 1).(Disc 2).chd", &n, &t));
	CHECK(!marker("Game (Disc 1) (Disk 1).chd", &n, &t));
	CHECK(!marker("Game (Disc 12345).chd", &n, &t));
	CHECK(!marker("Game (Disc 1", &n, &t));
	CHECK(!marker(NULL, &n, &t));
}

static const char* base(const char* name) {
	static char out[MD_NAME_LEN];
	MDMarker m;
	if (!MD_marker(name, &m) || !MD_baseName(name, &m, out, sizeof(out))) return "";
	return out;
}

static void testBaseNames(void) {
	CHECK_STR(base("Game (Disc 1).chd"), "Game");
	CHECK_STR(base("Game (USA) (Disc 1).chd"), "Game (USA)");
	CHECK_STR(base("Game (Disc 1) (USA).chd"), "Game (USA)");
	CHECK_STR(base("(Disc 1) Game.chd"), "Game");
	CHECK_STR(base("Game(Disc 1).chd"), "Game");
	CHECK_STR(base("Game v1.1 (Disc 1)"), "Game v1.1");
	CHECK_STR(base("Game (Disc 1).tar.chd"), "Game.tar"); // only the last extension
	CHECK_STR(base("(Disc 1).chd"), "");
	CHECK_STR(base(".Game (Disc 1).chd"), "");
	CHECK(MD_extension("Game.chd") && !strcmp(MD_extension("Game.chd"), ".chd"));
	CHECK(MD_extension("Game.chdman") == NULL);
	CHECK(MD_extension("Game.c") == NULL);
	CHECK(MD_extension("Game") == NULL);
}

// The groups as "<m3u>:<disc>|<disc>;..."
static const char* grouped(const char* const* names, int count) {
	static char out[2048];
	out[0] = '\0';
	MDGroups groups;
	CHECK(MD_group(names, count, &groups) >= 0);
	for (int i = 0; i < groups.count; i++) {
		size_t used = strlen(out);
		snprintf(out + used, sizeof(out) - used, "%s%s:", i ? ";" : "", groups.items[i].m3u);
		for (int k = 0; k < groups.items[i].count; k++) {
			used = strlen(out);
			snprintf(out + used, sizeof(out) - used, "%s%s", k ? "|" : "", names[groups.items[i].discs[k]]);
		}
	}
	MD_groupsFree(&groups);
	return out;
}

#define COUNT(a) ((int)(sizeof(a) / sizeof(a[0])))

static void testGrouping(void) {
	const char* const simple[] = { "Zelda.gb", "Game (Disc 2).chd", "Game (Disc 1).chd", "Game (Disc 3).chd" };
	CHECK_STR(grouped(simple, COUNT(simple)), "Game.m3u:Game (Disc 1).chd|Game (Disc 2).chd|Game (Disc 3).chd");

	// numbers in order, not names: 10 after 9
	const char* const many[] = { "G (Disc 10).chd", "G (Disc 9).chd", "G (Disc 1).chd" };
	CHECK_STR(grouped(many, COUNT(many)), "G.m3u:G (Disc 1).chd|G (Disc 9).chd|G (Disc 10).chd");

	// of M, and a gap is fine
	const char* const of[] = { "FF (Disc 1 of 3).chd", "FF (Disc 3 of 3).chd" };
	CHECK_STR(grouped(of, COUNT(of)), "FF.m3u:FF (Disc 1 of 3).chd|FF (Disc 3 of 3).chd");

	// one disc alone, a disc twice, mixed words, totals or extensions stay separate
	const char* const alone[] = { "Solo (Disc 1).chd", "Solo.chd" };
	CHECK_STR(grouped(alone, COUNT(alone)), "");
	const char* const twice[] = { "Two (Disc 1).chd", "Two (Disc 01).chd", "Two (Disc 2).chd" };
	CHECK_STR(grouped(twice, COUNT(twice)), "");
	const char* const words[] = { "Mix (Disc 1).chd", "Mix (CD 2).chd" };
	CHECK_STR(grouped(words, COUNT(words)), "");
	const char* const totals[] = { "Tot (Disc 1 of 2).chd", "Tot (Disc 2).chd" };
	CHECK_STR(grouped(totals, COUNT(totals)), "");
	const char* const exts[] = { "Ext (Disc 1).chd", "Ext (Disc 2).pbp" };
	CHECK_STR(grouped(exts, COUNT(exts)), "");

	// a .cue's tracks belong to it, as do subchannel files; hidden names and .m3u are skipped
	const char* const cues[] = { "Cue (Disc 1).cue", "Cue (Disc 1).bin", "Cue (Disc 1) (Track 2).bin",
		"Cue (Disc 2).cue", "Cue (Disc 2).bin", "Cue (Disc 2).sbi", ".Cue (Disc 3).cue", "Cue (Disc 1).m3u" };
	CHECK_STR(grouped(cues, COUNT(cues)), "Cue.m3u:Cue (Disc 1).cue|Cue (Disc 2).cue");

	// a .bin without a sheet is a disc
	const char* const bins[] = { "Bin (Disc 1).bin", "Bin (Disc 2).bin" };
	CHECK_STR(grouped(bins, COUNT(bins)), "Bin.m3u:Bin (Disc 1).bin|Bin (Disc 2).bin");

	// two games that would share an .m3u stay separate, others still group
	const char* const clash[] = { "C (Disc 1).chd", "C (Disc 2).chd", "C (Disc 1).iso", "C (Disc 2).iso",
		"D (Disc 1).chd", "D (Disc 2).chd" };
	CHECK_STR(grouped(clash, COUNT(clash)), "D.m3u:D (Disc 1).chd|D (Disc 2).chd");

	// regions are part of the name, so two releases are two games
	const char* const regions[] = { "R (USA) (Disc 1).chd", "R (USA) (Disc 2).chd", "R (Europe) (Disc 1).chd",
		"R (Europe) (Disc 2).chd" };
	CHECK_STR(grouped(regions, COUNT(regions)),
		"R (Europe).m3u:R (Europe) (Disc 1).chd|R (Europe) (Disc 2).chd;R (USA).m3u:R (USA) (Disc 1).chd|R (USA) (Disc 2).chd");

	// floppy disks, any extension case
	const char* const disks[] = { "Amiga (Disk 1 of 2).ADF", "Amiga (Disk 2 of 2).adf" };
	CHECK_STR(grouped(disks, COUNT(disks)), "Amiga.m3u:Amiga (Disk 1 of 2).ADF|Amiga (Disk 2 of 2).adf");

	CHECK_STR(grouped(NULL, 0), "");

	MDGroups groups;
	CHECK(MD_group(simple, COUNT(simple), &groups) == 1);
	CHECK(MD_findGroup(&groups, "Game.m3u") == 0);
	CHECK(MD_findGroup(&groups, "game.m3u") == -1);
	CHECK(MD_findDisc(&groups, simple, "Game (Disc 3).chd") == 0);
	CHECK(MD_findDisc(&groups, simple, "Zelda.gb") == -1);
	char text[256];
	CHECK(MD_m3uText(&groups.items[0], simple, text, sizeof(text)) == (int)strlen(text));
	CHECK_STR(text, "Game (Disc 1).chd\nGame (Disc 2).chd\nGame (Disc 3).chd\n");
	CHECK(MD_m3uText(&groups.items[0], simple, text, 20) == -1);
	MD_groupsFree(&groups);
}

static void testStockFolders(void) {
	const char* const m3u[] = { "Game (Disc 1).chd", "Game (Disc 2).chd", "Game.m3u" };
	CHECK(MD_stockFolder("Game", m3u, 3));
	const char* const cue[] = { "Game.cue", "Game.bin" };
	CHECK(MD_stockFolder("Game", cue, 2));
	CHECK(!MD_stockFolder("Other", m3u, 3));
	CHECK(!MD_stockFolder("Game", m3u, 2));
}

static const MDCopy* findCopy(const MDCopy* copies, int count, int place, const char* from) {
	for (int i = 0; i < count; i++) {
		if (copies[i].place == place && !strcmp(copies[i].from, from)) return &copies[i];
	}
	return NULL;
}

static void testSaveCopies(void) {
	MDCopy copies[MD_COPIES_MAX];
	int count = MD_saveCopies("Game (Disc 1).chd", "Game.m3u", copies, MD_COPIES_MAX);
	// 4 saves, 10 + 2 + 8 + 9 states, 1 + 20 slot files
	CHECK(count == 4 + 29 + 21);
	const MDCopy* c;
	CHECK((c = findCopy(copies, count, MD_SAVES, "Game (Disc 1).chd.sav")) && !strcmp(c->to, "Game.m3u.sav"));
	CHECK((c = findCopy(copies, count, MD_SAVES, "Game (Disc 1).chd.rtc")) && !strcmp(c->to, "Game.m3u.rtc"));
	CHECK((c = findCopy(copies, count, MD_SAVES, "Game (Disc 1).sav")) && !strcmp(c->to, "Game.sav"));
	CHECK((c = findCopy(copies, count, MD_SAVES, "Game (Disc 1).srm")) && !strcmp(c->to, "Game.srm"));
	CHECK((c = findCopy(copies, count, MD_STATES, "Game (Disc 1).chd.st0")) && !strcmp(c->to, "Game.m3u.st0"));
	CHECK((c = findCopy(copies, count, MD_STATES, "Game (Disc 1).chd.st9")) && !strcmp(c->to, "Game.m3u.st9"));
	CHECK((c = findCopy(copies, count, MD_STATES, "Game (Disc 1).state")) && !strcmp(c->to, "Game.state"));
	CHECK((c = findCopy(copies, count, MD_STATES, "Game (Disc 1).state.auto")) && !strcmp(c->to, "Game.state.auto"));
	CHECK((c = findCopy(copies, count, MD_STATES, "Game (Disc 1).state8")) && !strcmp(c->to, "Game.state8"));
	CHECK(!findCopy(copies, count, MD_STATES, "Game (Disc 1).state0"));
	CHECK((c = findCopy(copies, count, MD_STATES, "Game (Disc 1).state.0")) && !strcmp(c->to, "Game.state.0"));
	CHECK((c = findCopy(copies, count, MD_SLOTS, "Game (Disc 1).chd.txt")) && !strcmp(c->to, "Game.m3u.txt"));
	CHECK((c = findCopy(copies, count, MD_SLOTS, "Game (Disc 1).chd.4.bmp")) && !strcmp(c->to, "Game.m3u.4.bmp"));
	CHECK((c = findCopy(copies, count, MD_SLOTS, "Game (Disc 1).chd.9.txt")) && !strcmp(c->to, "Game.m3u.9.txt"));

	// a disc with no extension minarch cuts has one name for both forms
	count = MD_saveCopies("Game (Disc 1)", "Game.m3u", copies, MD_COPIES_MAX);
	CHECK(count == 3 + 29 + 21);
	CHECK((c = findCopy(copies, count, MD_SAVES, "Game (Disc 1).sav")) && !strcmp(c->to, "Game.m3u.sav"));
	CHECK((c = findCopy(copies, count, MD_SAVES, "Game (Disc 1).srm")) && !strcmp(c->to, "Game.srm"));

	CHECK(MD_saveCopies("Game (Disc 1).chd", "Game.m3u", copies, 5) == 5);
	CHECK(MD_saveCopies(NULL, "Game.m3u", copies, MD_COPIES_MAX) == 0);
}

static void testCompanions(void) {
	CHECK(MD_companion("Cue (Disc 1).cue", "Cue (Disc 1).bin"));
	CHECK(MD_companion("Cue (Disc 1).cue", "Cue (Disc 1) (Track 2).bin"));
	CHECK(MD_companion("Cue (Disc 1).cue", "Cue (Disc 1).sbi"));
	CHECK(MD_companion("Chd (Disc 1).chd", "Chd (Disc 1).sbi"));
	CHECK(!MD_companion("Chd (Disc 1).chd", "Chd (Disc 1).bin"));
	CHECK(!MD_companion("Cue (Disc 1).cue", "Cue (Disc 10).bin"));
	CHECK(!MD_companion("Cue (Disc 1).cue", "Cue (Disc 1).cue"));
	CHECK(!MD_companion("Cue (Disc 1).cue", "Cue (Disc 1) (Alt).cue"));
	CHECK(!MD_companion("Cue (Disc 1).cue", "Cue (Disc 1).m3u"));
	CHECK(!MD_companion("Cue (Disc 1).cue", ".Cue (Disc 1).bin"));
}

static void testM3uMoved(void) {
	const char* const moved[] = { "G (Disc 1).chd", "G (Disc 2).chd" };
	char out[256];
	CHECK(MD_m3uMoved("G (Disc 1).chd\r\n\r\nG (Disc 2).chd", moved, 2, out, sizeof(out)) > 0);
	CHECK_STR(out, "G (Disc 1).chd\nG (Disc 2).chd\n");
	CHECK(MD_m3uMoved("G (Disc 2).chd\n", moved, 2, out, sizeof(out)) > 0);
	CHECK_STR(out, "G (Disc 2).chd\n");
	CHECK(MD_m3uMoved("G (Disc 1).chd\nOther/G (Disc 2).chd\n", moved, 2, out, sizeof(out)) == -1);
	CHECK(MD_m3uMoved("G (Disc 1).chd\nG (Disc 3).chd\n", moved, 2, out, sizeof(out)) == -1);
	CHECK(MD_m3uMoved("\n\n", moved, 2, out, sizeof(out)) == -1);
	CHECK(MD_m3uMoved("G (Disc 1).chd\n", moved, 2, out, 8) == -1);
}

int main(void) {
	testMarkers();
	testBaseNames();
	testGrouping();
	testStockFolders();
	testSaveCopies();
	testCompanions();
	testM3uMoved();
	CHECK_DONE();
}
