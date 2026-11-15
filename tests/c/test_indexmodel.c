// The card index: building it from a fixture card, asking it, the file it's
// kept in (a corrupt or stale one too), reusing the systems that didn't
// change, and cardindex.c's worker handing it over.

#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include "check.h"
#include "cardindex.h"
#include "indexmodel.h"
#include "mosaic_plugin.h"

#define CARD TEST_DIR "/card"
#define ROMS CARD "/Roms"
#define GB ROMS "/Game Boy (GB)"
#define GBC ROMS "/Game Boy Color (GBC)"
#define PS ROMS "/PlayStation (PS)"
#define GG ROMS "/Game Gear (GG)"
#define FILE_PATH TEST_DIR "/card-index.txt"

static const IndexCard card = { CARD, "test" };

static void touch(const char* path) {
	FILE* f = fopen(path, "w");
	if (f) fclose(f);
}

static void writeText(const char* path, const char* text) {
	FILE* f = fopen(path, "w");
	if (!f) return;
	fputs(text, f);
	fclose(f);
}

static char* readText(const char* path) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	char* text = malloc((size_t)size + 1);
	if (text) text[fread(text, 1, (size_t)size, f)] = '\0';
	fclose(f);
	return text;
}

// Sets a folder's modification time, so a change shows (or doesn't)
// whatever the file system's clock resolution.
static void age(const char* path, long seconds) {
	struct timeval times[2] = { { seconds, 0 }, { seconds, 0 } };
	utimes(path, times);
}

// Ages everything on the card, so nothing counts as changed a moment ago.
static void ageAll(void) {
	CHECK(system("find '" CARD "' -exec touch -t 200101010000 {} +") == 0);
}

static void makeCard(void) {
	CHECK(system("rm -rf '" TEST_DIR "' && mkdir -p '" GB "/Hacks/1/2/3/4/5/6/7/8' '" GB "/.media' '" GBC "' '" GG "'"
		" '" PS "/Final Fantasy VII (USA)' '" PS "/Crash (USA)' '" PS "/Loose' '" GB "/Tool.pak' '" ROMS "/Secret (SC)'"
		" '" CARD "/.system/test/paks/Emus/GB.pak' '" CARD "/Emus/test/GBC.pak' '" CARD "/Emus/test/PS.pak'") == 0);
	touch(CARD "/.system/test/paks/Emus/GB.pak/launch.sh");
	touch(CARD "/Emus/test/GBC.pak/launch.sh");
	touch(CARD "/Emus/test/PS.pak/launch.sh");

	touch(GB "/Tetris (World) (Rev 1).gb");
	touch(GB "/Kirby.gb");
	touch(GB "/.Secret.gb");
	touch(GB "/Old.gb.disabled");
	touch(GB "/Away.gb");
	writeText(GB "/map.txt", "Kirby.gb\tKirby's Dream Land\nAway.gb\t.Away\nHacks\tROM Hacks\n");
	writeText(GB "/.media/Tetris (World) (Rev 1).info", "year=1989\npublisher=Nintendo\ngenre=Puzzle\nplayers=1-2\n");
	writeText(GB "/.media/Kirby.info", "genre=Platform\n");
	touch(GB "/Hacks/Mario Hack.gb");
	touch(GB "/Hacks/1/2/3/4/5/6/Seven.gb");
	touch(GB "/Hacks/1/2/3/4/5/6/7/Eight.gb"); // more than 7 folders in
	touch(GBC "/Tetris DX.gbc");
	touch(PS "/Final Fantasy VII (USA)/Final Fantasy VII (USA).m3u");
	touch(PS "/Final Fantasy VII (USA)/Final Fantasy VII (USA) (Disc 1).cue");
	touch(PS "/Crash (USA)/Crash (USA).cue");
	touch(PS "/Crash (USA)/Crash (USA).m3u");
	touch(PS "/Loose/Spyro.cue");
	touch(GG "/Sonic.gg"); // no emulator
	touch(ROMS "/Secret (SC)/Hidden.sc");
	touch(ROMS "/notes.txt"); // not a folder
	writeText(ROMS "/map.txt", "Secret (SC)\t.Secret\nGame Gear (GG)\tSega Game Gear\n");
	age(ROMS "/map.txt", 1000000000);
}

// The entry at path, or NULL.
static const IndexEntry* entryAt(const CardIndex* index, const char* path, const IndexSystem** sys) {
	const IndexEntry* e = NULL;
	return Index_find(index, path, &e, sys) >= 0 ? e : NULL;
}

static void testNames(void) {
	char out[256];
	Index_cleanName("Tetris (World) (Rev 1).gb", out, sizeof(out));
	CHECK_STR(out, "Tetris");
	Index_cleanName("Pokemon [!].gbc", out, sizeof(out));
	CHECK_STR(out, "Pokemon");
	Index_cleanName("(Prototype).gb", out, sizeof(out));
	CHECK_STR(out, "(Prototype)");
	Index_cleanName("Game.tar.gz", out, sizeof(out));
	CHECK_STR(out, "Game");
	Index_cleanName("Doom.wad1234", out, sizeof(out));
	CHECK_STR(out, "Doom.wad1234");
	Index_emuTag("Game Boy (GB)", out, sizeof(out));
	CHECK_STR(out, "GB");
	Index_emuTag("Arcade (FBN) (MAME)", out, sizeof(out));
	CHECK_STR(out, "MAME");
	Index_emuTag("Ports", out, sizeof(out));
	CHECK_STR(out, "Ports");
}

static void testBuild(void) {
	makeCard();
	CardIndex index;
	IndexBuildStats stats;
	CHECK(Index_build(&index, &card, NULL, NULL, &stats) == 0);
	CHECK(stats.systems_read == 4 && stats.systems_kept == 0 && stats.changed == 1);

	// every system folder but the hidden one, by name
	CHECK(index.system_count == 4);
	CHECK_STR(index.systems[0].folder, "Game Boy (GB)");
	CHECK_STR(index.systems[1].folder, "Game Boy Color (GBC)");
	CHECK_STR(index.systems[2].folder, "Game Gear (GG)");
	CHECK_STR(index.systems[3].folder, "PlayStation (PS)");
	CHECK(Index_count(&index, "Secret (SC)") == 0);
	CHECK(Index_count(&index, "Game Boy Color (GBC)") == 2);
	CHECK(Index_count(&index, NULL) == index.total);

	// a system folder comes first in its own
	const IndexSystem* sys = NULL;
	const IndexEntry* e = Index_at(&index, "Game Gear (GG)", 0, &sys);
	CHECK(e && e->kind == INDEX_KIND_SYSTEM && e->depth == -1 && e->type == MOSAIC_ENTRY_DIR);
	if (e) {
		CHECK_STR(Index_str(sys, e->path), GG);
		CHECK_STR(Index_str(sys, e->name), "Sega Game Gear"); // Roms/map.txt
		CHECK(sys->emulator == 0);
		CHECK_STR(sys->tag, "GG");
	}
	e = Index_at(&index, "Game Gear (GG)", 1, &sys);
	CHECK(e && e->kind == INDEX_KIND_GAME && !strcmp(Index_str(sys, e->path), GG "/Sonic.gg"));
	CHECK(Index_at(&index, "Game Gear (GG)", 2, &sys) == NULL);

	// games, with their names and .info fields
	e = entryAt(&index, GB "/Tetris (World) (Rev 1).gb", &sys);
	CHECK(e && e->kind == INDEX_KIND_GAME && e->type == MOSAIC_ENTRY_ROM && e->depth == 0);
	if (e) {
		CHECK_STR(Index_str(sys, e->name), "Tetris");
		CHECK_STR(Index_str(sys, e->launch), GB "/Tetris (World) (Rev 1).gb");
		CHECK_STR(Index_str(sys, e->year), "1989");
		CHECK_STR(Index_str(sys, e->publisher), "Nintendo");
		CHECK_STR(Index_str(sys, e->genre), "Puzzle");
		CHECK_STR(Index_str(sys, e->players), "1-2");
		CHECK(sys->emulator == 1);
		CHECK_STR(sys->tag, "GB");
	}
	e = entryAt(&index, GB "/Kirby.gb", &sys);
	CHECK(e && !strcmp(Index_str(sys, e->name), "Kirby's Dream Land") && !strcmp(Index_str(sys, e->genre), "Platform")
		&& !strcmp(Index_str(sys, e->year), ""));
	// what stock never lists
	CHECK(!entryAt(&index, GB "/.Secret.gb", NULL));
	CHECK(!entryAt(&index, GB "/Old.gb.disabled", NULL));
	CHECK(!entryAt(&index, GB "/Away.gb", NULL));
	CHECK(!entryAt(&index, GB "/map.txt", NULL));
	CHECK(!entryAt(&index, GB "/.media", NULL));
	// a plain folder, named by map.txt, and what's in it, as deep as 7 folders in
	e = entryAt(&index, GB "/Hacks", &sys);
	CHECK(e && e->kind == INDEX_KIND_FOLDER && e->type == MOSAIC_ENTRY_DIR && e->depth == 0
		&& !strcmp(Index_str(sys, e->name), "ROM Hacks"));
	e = entryAt(&index, GB "/Hacks/Mario Hack.gb", &sys);
	CHECK(e && e->kind == INDEX_KIND_GAME && e->depth == 1);
	e = entryAt(&index, GB "/Hacks/1/2/3/4/5/6/Seven.gb", &sys);
	CHECK(e && e->depth == 7);
	CHECK(entryAt(&index, GB "/Hacks/1/2/3/4/5/6/7", NULL) != NULL);
	CHECK(!entryAt(&index, GB "/Hacks/1/2/3/4/5/6/7/Eight.gb", NULL));
	// a pak
	e = entryAt(&index, GB "/Tool.pak", &sys);
	CHECK(e && e->kind == INDEX_KIND_GAME && e->type == MOSAIC_ENTRY_PAK && !strcmp(Index_str(sys, e->name), "Tool"));
	// game folders launch their .cue, else their .m3u, and aren't read inside
	e = entryAt(&index, PS "/Crash (USA)", &sys);
	CHECK(e && e->kind == INDEX_KIND_GAME && e->type == MOSAIC_ENTRY_DIR);
	if (e) CHECK_STR(Index_str(sys, e->launch), PS "/Crash (USA)/Crash (USA).cue");
	e = entryAt(&index, PS "/Final Fantasy VII (USA)", &sys);
	CHECK(e && e->kind == INDEX_KIND_GAME && !strcmp(Index_str(sys, e->name), "Final Fantasy VII"));
	if (e) CHECK_STR(Index_str(sys, e->launch), PS "/Final Fantasy VII (USA)/Final Fantasy VII (USA).m3u");
	CHECK(!entryAt(&index, PS "/Crash (USA)/Crash (USA).cue", NULL));
	e = entryAt(&index, PS "/Loose", &sys);
	CHECK(e && e->kind == INDEX_KIND_FOLDER);
	CHECK(entryAt(&index, PS "/Loose/Spyro.cue", NULL) != NULL);

	// in order: every system by name, each by path
	const char* last = "";
	const IndexSystem* last_sys = NULL;
	for (int i = 0; i < index.total; i++) {
		e = Index_at(&index, NULL, i, &sys);
		CHECK(e != NULL);
		if (!e) break;
		const char* path = Index_str(sys, e->path);
		if (sys == last_sys) CHECK(strcmp(last, path) < 0);
		CHECK(Index_find(&index, path, NULL, NULL) == i);
		last = path;
		last_sys = sys;
	}
	CHECK(Index_at(&index, NULL, index.total, &sys) == NULL);
	CHECK(Index_at(&index, NULL, -1, &sys) == NULL);
	CHECK(Index_find(&index, ROMS, NULL, NULL) == -1);
	CHECK(Index_find(&index, ROMS "/Nope (NO)/a.gb", NULL, NULL) == -1);
	CHECK(Index_find(&index, CARD "/Elsewhere/a.gb", NULL, NULL) == -1);
	CHECK(Index_find(&index, GB "/Nope.gb", NULL, NULL) == -1);
	CHECK(Index_bytes(&index) > 0);
	Index_free(&index);
	CHECK(index.total == 0 && index.system_count == 0);

	// no Roms at all is an empty index, not a failure
	IndexCard nowhere = { TEST_DIR "/nowhere", "test" };
	CHECK(Index_build(&index, &nowhere, NULL, NULL, &stats) == 0 && index.total == 0);
	Index_free(&index);

	// a cancelled build leaves nothing
	volatile int stop = 1;
	CHECK(Index_build(&index, &card, NULL, &stop, &stats) == -1 && index.total == 0);
}

// The same entries, field for field.
static int sameIndex(const CardIndex* a, const CardIndex* b) {
	if (a->total != b->total || a->system_count != b->system_count) return 0;
	for (int i = 0; i < a->total; i++) {
		const IndexSystem *sa, *sb;
		const IndexEntry* x = Index_at(a, NULL, i, &sa);
		const IndexEntry* y = Index_at(b, NULL, i, &sb);
		if (!x || !y || x->kind != y->kind || x->type != y->type || x->depth != y->depth || sa->emulator != sb->emulator
				|| strcmp(sa->folder, sb->folder) != 0) return 0;
		const uint32_t xs[] = { x->path, x->launch, x->name, x->year, x->publisher, x->genre, x->players };
		const uint32_t ys[] = { y->path, y->launch, y->name, y->year, y->publisher, y->genre, y->players };
		for (int k = 0; k < 7; k++) {
			if (strcmp(Index_str(sa, xs[k]), Index_str(sb, ys[k])) != 0) return 0;
		}
	}
	return 1;
}

static void testFile(void) {
	makeCard();
	// a name with a tab, a newline and a backslash in it survives
	touch(GBC "/Odd\tName\\with\nlines.gbc");
	CardIndex built, loaded;
	CHECK(Index_build(&built, &card, NULL, NULL, NULL) == 0);
	CHECK(Index_save(&built, FILE_PATH) == 0);
	CHECK(access(FILE_PATH ".tmp", F_OK) != 0);
	CHECK(Index_load(&loaded, FILE_PATH, ROMS) == 0);
	CHECK(sameIndex(&built, &loaded));
	CHECK(entryAt(&loaded, GBC "/Odd\tName\\with\nlines.gbc", NULL) != NULL);
	CHECK(loaded.roms_mtime == built.roms_mtime && loaded.map_mtime == built.map_mtime);
	Index_free(&loaded);

	// another card's, or none
	CHECK(Index_load(&loaded, FILE_PATH, TEST_DIR "/other/Roms") == -1 && loaded.total == 0);
	CHECK(Index_load(&loaded, TEST_DIR "/missing.txt", ROMS) == -1);
	CHECK(Index_save(&built, TEST_DIR "/no/such/folder/index.txt") == -1);

	// corrupt files are refused whole
	char* good = readText(FILE_PATH);
	CHECK(good != NULL);
	if (good) {
		size_t len = strlen(good);
		char* bad = malloc(len + 64);
		struct {
			const char* what;
			const char* from;
			const char* to;
		} edits[] = {
			{ "another version", "mosaic-card-index\t2", "mosaic-card-index\t1" },
			{ "not a number", "system\tGame Boy (GB)\t1\t", "system\tGame Boy (GB)\tx\t" },
			{ "a bad escape", "Odd\\tName", "Odd\\qName" },
			{ "an unknown kind", "entry\t2\t", "entry\t7\t" },
			{ "a path outside its system", GBC "/Tetris DX.gbc", GB "/Tetris DX.gbc" },
			{ "a missing field", "\tPuzzle\t1-2\n", "\tPuzzle\n" },
			{ "the wrong system count", "end\t4", "end\t5" },
		};
		for (size_t i = 0; i < sizeof(edits) / sizeof(edits[0]); i++) {
			const char* at = strstr(good, edits[i].from);
			CHECK(at != NULL);
			if (!at) continue;
			size_t head = (size_t)(at - good);
			snprintf(bad, len + 64, "%.*s%s%s", (int)head, good, edits[i].to, at + strlen(edits[i].from));
			writeText(TEST_DIR "/bad.txt", bad);
			if (Index_load(&loaded, TEST_DIR "/bad.txt", ROMS) != -1 || loaded.total != 0) {
				fprintf(stderr, "a file with %s was read\n", edits[i].what);
				check_failures++;
			}
		}
		// cut short, anywhere
		for (size_t cut = 0; cut < len; cut += len / 23 + 1) {
			snprintf(bad, len + 64, "%.*s", (int)cut, good);
			writeText(TEST_DIR "/bad.txt", bad);
			CHECK(Index_load(&loaded, TEST_DIR "/bad.txt", ROMS) == -1);
		}
		// with something after its end
		snprintf(bad, len + 64, "%sentry\n", good);
		writeText(TEST_DIR "/bad.txt", bad);
		CHECK(Index_load(&loaded, TEST_DIR "/bad.txt", ROMS) == -1);
		// entries out of order
		const char* kirby = strstr(good, "entry\t2\t2\t0\t" GB "/Kirby.gb");
		const char* tetris = strstr(good, "entry\t2\t2\t0\t" GB "/Tetris");
		CHECK(kirby && tetris && kirby < tetris);
		if (kirby && tetris) {
			const char* kirby_end = strchr(kirby, '\n') + 1;
			const char* tetris_end = strchr(tetris, '\n') + 1;
			snprintf(bad, len + 64, "%.*s%.*s%.*s%.*s%s", (int)(kirby - good), good, (int)(tetris_end - tetris), tetris,
				(int)(tetris - kirby_end), kirby_end, (int)(kirby_end - kirby), kirby, tetris_end);
			writeText(TEST_DIR "/bad.txt", bad);
			CHECK(Index_load(&loaded, TEST_DIR "/bad.txt", ROMS) == -1);
		}
		free(bad);
		free(good);
	}
	Index_free(&built);
}

static void testStale(void) {
	makeCard();
	ageAll();
	CardIndex first, next;
	IndexBuildStats stats;
	CHECK(Index_build(&first, &card, NULL, NULL, &stats) == 0);
	CHECK(Index_save(&first, FILE_PATH) == 0);
	CardIndex cached;
	CHECK(Index_load(&cached, FILE_PATH, ROMS) == 0);

	// nothing changed: every system is kept
	CHECK(Index_build(&next, &card, &cached, NULL, &stats) == 0);
	CHECK(stats.systems_read == 0 && stats.systems_kept == 4 && stats.changed == 0 && stats.restamped == 0);
	CHECK(sameIndex(&first, &next));
	Index_free(&next);

	// a folder touched is read again, but nothing changed
	age(GBC, 1000000050);
	CHECK(Index_build(&next, &card, &cached, NULL, &stats) == 0);
	CHECK(stats.systems_read == 1 && stats.changed == 0 && stats.restamped == 1);
	Index_free(&next);

	// a game added to one system reads only that one again
	touch(GBC "/Unseen.gbc");
	age(GBC, 1000000100);
	CHECK(Index_build(&next, &card, &cached, NULL, &stats) == 0);
	CHECK(stats.systems_read == 1 && stats.systems_kept == 3 && stats.changed == 1);
	CHECK(entryAt(&next, GBC "/Unseen.gbc", NULL) != NULL);
	CHECK(next.total == first.total + 1);
	Index_free(&next);

	// a folder changed a moment ago is read again next time too, as a
	// second change in the same tick of a coarse clock wouldn't show
	touch(GBC "/New.gbc");
	CardIndex fresh;
	CHECK(Index_build(&fresh, &card, &cached, NULL, &stats) == 0 && stats.systems_read == 1);
	CHECK(Index_build(&next, &card, &fresh, NULL, &stats) == 0 && stats.systems_read == 1 && stats.changed == 0);
	Index_free(&next);
	ageAll();
	CHECK(Index_build(&next, &card, &fresh, NULL, &stats) == 0 && stats.systems_read == 1);
	Index_free(&fresh);
	fresh = next;
	CHECK(Index_build(&next, &card, &fresh, NULL, &stats) == 0 && stats.systems_read == 0 && stats.systems_kept == 4);
	Index_free(&next);
	Index_free(&fresh);

	// a .info added later, which changes its .media folder
	Index_free(&cached);
	CHECK(Index_build(&cached, &card, NULL, NULL, NULL) == 0);
	mkdir(PS "/Loose/.media", 0755);
	writeText(PS "/Loose/.media/Spyro.info", "year=1998\n");
	ageAll();
	Index_free(&cached);
	CHECK(Index_build(&cached, &card, NULL, NULL, NULL) == 0);
	const IndexSystem* sys;
	const IndexEntry* e = entryAt(&cached, PS "/Loose/Spyro.cue", &sys);
	CHECK(e && !strcmp(Index_str(sys, e->year), "1998"));
	writeText(PS "/Loose/.media/Spyro.info", "year=1999\n");
	writeText(PS "/Loose/.media/Other.info", "year=2000\n");
	age(PS "/Loose/.media", 1000000300);
	CHECK(Index_build(&next, &card, &cached, NULL, &stats) == 0 && stats.systems_read == 1 && stats.changed == 1);
	e = entryAt(&next, PS "/Loose/Spyro.cue", &sys);
	CHECK(e && !strcmp(Index_str(sys, e->year), "1999"));
	Index_free(&next);

	// a folder gone, or a whole system
	CHECK(system("rm -rf '" PS "/Loose' '" GG "'") == 0);
	CHECK(Index_build(&next, &card, &cached, NULL, &stats) == 0);
	CHECK(stats.changed == 1 && next.system_count == 3 && !entryAt(&next, PS "/Loose/Spyro.cue", NULL));
	Index_free(&next);

	// an emulator installed changes only the flag
	CHECK(system("mkdir -p '" CARD "/Emus/test/GG.pak' '" GG "'") == 0);
	ageAll();
	Index_free(&cached);
	CHECK(Index_build(&cached, &card, NULL, NULL, NULL) == 0);
	touch(CARD "/Emus/test/GG.pak/launch.sh");
	CHECK(Index_build(&next, &card, &cached, NULL, &stats) == 0);
	CHECK(stats.systems_read == 0 && stats.changed == 1);
	CHECK(Index_at(&next, "Game Gear (GG)", 0, &sys) && sys->emulator == 1);
	Index_free(&next);

	// Roms/map.txt changing reads every system again
	writeText(ROMS "/map.txt", "Game Gear (GG)\tGame Gear\n");
	age(ROMS "/map.txt", 1000000700);
	CHECK(Index_build(&next, &card, &cached, NULL, &stats) == 0);
	CHECK(stats.systems_kept == 0 && stats.changed == 1);
	CHECK(Index_count(&next, "Secret (SC)") == 2); // no longer hidden
	e = Index_at(&next, "Game Gear (GG)", 0, &sys);
	CHECK(e && !strcmp(Index_str(sys, e->name), "Game Gear"));
	Index_free(&next);
	Index_free(&cached);
	Index_free(&first);
}

///////////////////////////////////////
// cardindex.c

static void quietLog(const char* message) {
	(void)message;
}

// Polls until the worker's index is in place, for up to 5 seconds.
static int waitReady(void) {
	for (int i = 0; i < 500; i++) {
		if (CardIndex_poll()) return 1;
		usleep(10000);
	}
	return 0;
}

static void testWorker(void) {
	makeCard();
	unlink(FILE_PATH);
	CardIndex_setup(CARD, "test", FILE_PATH, quietLog);
	// nothing on the card: not ready until it's built
	CardIndex_poll();
	CHECK(CardIndex_get() == NULL);
	CHECK(waitReady());
	const CardIndex* index = CardIndex_get();
	CHECK(index && Index_count(index, "Game Boy Color (GBC)") == 2);
	CHECK(access(FILE_PATH, F_OK) == 0); // and kept
	// a refresh with nothing changed puts nothing new in place
	CardIndex_refresh();
	for (int i = 0; i < 50; i++) {
		CHECK(CardIndex_poll() == 0);
		usleep(2000);
	}
	// one after a game is added does
	touch(GBC "/Added.gbc");
	age(GBC, 1000000500);
	CardIndex_refresh();
	CardIndex_refresh(); // while it runs: once more after
	CHECK(waitReady());
	index = CardIndex_get();
	CHECK(index && Index_find(index, GBC "/Added.gbc", NULL, NULL) >= 0);
	CardIndex_quit();
	CardIndex_quit();

	// the next start answers from the file at once, then checks it
	CardIndex_setup(CARD, "test", FILE_PATH, quietLog);
	index = CardIndex_get();
	CHECK(index && Index_find(index, GBC "/Added.gbc", NULL, NULL) >= 0);
	unlink(GBC "/Added.gbc");
	age(GBC, 1000000600);
	CHECK(waitReady());
	index = CardIndex_get();
	CHECK(index && Index_find(index, GBC "/Added.gbc", NULL, NULL) < 0);
	CardIndex_quit();

	// a corrupt file is read from the card instead
	writeText(FILE_PATH, "mosaic-card-index\t2\nroms\tnope\n");
	CardIndex_setup(CARD, "test", FILE_PATH, quietLog);
	CHECK(CardIndex_get() == NULL);
	CHECK(waitReady());
	CHECK(CardIndex_get() && Index_count(CardIndex_get(), NULL) > 0);
	// quitting while it builds
	CardIndex_refresh();
	CardIndex_quit();
	// a refresh before anything asks does nothing
	CardIndex_setup(CARD, "test", FILE_PATH, quietLog);
	CardIndex_refresh();
	CHECK(CardIndex_poll() == 0);
	CardIndex_quit();
}

int main(void) {
	testNames();
	testBuild();
	testFile();
	testStale();
	testWorker();
	CHECK(system("rm -rf '" TEST_DIR "'") == 0);
	CHECK_DONE();
}
