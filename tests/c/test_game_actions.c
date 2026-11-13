// The game actions plugin: its model on a fixture card, above all the list
// of paths a delete removes, then the plugin against a fake host.

#include <stdlib.h>
#include <unistd.h>
#include "check.h"
#include "words.h"
#include "../../plugins/game-actions/game_actions.c"

struct MosaicEntry {
	char path[1024];
	char name[128];
	int type;
};

#define CARD TEST_SDCARD_PATH
#define OUTSIDE TEST_SDCARD_PATH "-outside"
#define USERDATA CARD "/.userdata/desktop"
#define GB CARD "/Roms/Game Boy (GB)"
#define PS CARD "/Roms/Sony PlayStation (PS)"

static void writeText(const char* path, const char* text) {
	char dir[1024];
	snprintf(dir, sizeof(dir), "%s", path);
	char* slash = strrchr(dir, '/');
	*slash = '\0';
	char cmd[1200];
	snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", dir);
	(void)system(cmd);
	FILE* f = fopen(path, "w");
	if (!f) {
		fprintf(stderr, "can't write %s\n", path);
		exit(1);
	}
	fputs(text, f);
	fclose(f);
}

static void touch(const char* path) {
	writeText(path, "x");
}

static int there(const char* path) {
	struct stat st;
	return lstat(path, &st) == 0;
}

static char* readText(const char* path) {
	static char text[4096];
	text[0] = '\0';
	FILE* f = fopen(path, "r");
	if (!f) return text;
	size_t got = fread(text, 1, sizeof(text) - 1, f);
	text[got] = '\0';
	fclose(f);
	return text;
}

static void makeCard(void) {
	(void)system("rm -rf '" CARD "' '" OUTSIDE "'");

	// a plain game, with every kind of sidecar
	touch(GB "/Tetris.gb");
	touch(GB "/.media/Tetris.png");
	touch(CARD "/Saves/GB/Tetris.gb.sav");
	touch(CARD "/Saves/GB/Tetris.gb.rtc");
	touch(CARD "/Saves/GB/Tetris.srm");
	touch(CARD "/.userdata/shared/GB-gambatte/Tetris.gb.st0");
	touch(CARD "/.userdata/shared/GB-gambatte/Tetris.gb.st9");
	touch(CARD "/.userdata/shared/GB-mgba/Tetris.state");
	touch(CARD "/.userdata/shared/GB-mgba/Tetris.state.auto");
	touch(CARD "/.userdata/shared/GB-mgba/Tetris.state3");
	touch(CARD "/.userdata/shared/.minui/GB/Tetris.gb.txt");
	touch(CARD "/.userdata/shared/.minui/GB/Tetris.gb.0.bmp");
	touch(CARD "/.userdata/shared/.minui/GB/Tetris.gb.0.txt");
	touch(USERDATA "/GB-gambatte/Tetris.gb.cfg");
	// what it must leave: other games, other systems, folders that aren't
	// a core's, and a sidecar name that's a folder
	touch(GB "/Tetris 2.gb");
	touch(CARD "/Saves/GB/Tetris 2.gb.sav");
	touch(CARD "/Saves/GB/Tetris.gbc.sav");
	touch(CARD "/Saves/GBC/Tetris.gb.sav");
	touch(CARD "/.userdata/shared/GBC-gambatte/Tetris.gb.st0");
	touch(CARD "/.userdata/shared/GB/Tetris.gb.st1");
	touch(CARD "/.userdata/shared/GB-/Tetris.gb.st2");
	touch(CARD "/.userdata/shared/.minui/GB/Tetris 2.gb.txt");
	touch(CARD "/Saves/GB/Tetris.gb.sav.bak");
	(void)system("mkdir -p '" CARD "/Saves/GB/Tetris.sav'");
	touch(CARD "/Saves/GB/Tetris.sav/inside");

	// names another game shares: Dr. Mario in two folders of the system,
	// Zelda twice in one folder
	touch(GB "/Dr. Mario.gb");
	touch(GB "/Hacks/Dr. Mario.gb");
	touch(GB "/.media/Dr. Mario.png");
	touch(CARD "/Saves/GB/Dr. Mario.gb.sav");
	touch(CARD "/Saves/GB/Dr. Mario.srm");
	touch(GB "/Zelda.gb");
	touch(GB "/Zelda.zip");
	touch(GB "/.media/Zelda.png");
	touch(CARD "/Saves/GB/Zelda.gb.sav");
	touch(CARD "/Saves/GB/Zelda.srm");
	// a collated folder with the same tag counts as the same system
	touch(CARD "/Roms/Game Boy Hacks (GB)/Pokemon.gb");
	touch(GB "/Pokemon.gb");
	touch(CARD "/Saves/GB/Pokemon.gb.sav");

	// a multi-disc game in its own folder, with a link out of the card in it
	touch(PS "/FF7/FF7.m3u");
	writeText(PS "/FF7/FF7.m3u", "FF7 (Disc 1).cue\nFF7 (Disc 2).cue\n");
	writeText(PS "/FF7/FF7 (Disc 1).cue", "FILE \"FF7 (Disc 1).bin\" BINARY\n");
	touch(PS "/FF7/FF7 (Disc 1).bin");
	touch(PS "/FF7/FF7 (Disc 2).cue");
	touch(PS "/FF7/.media/FF7.png");
	touch(PS "/.media/FF7.png");
	touch(CARD "/Saves/PS/FF7.m3u.sav");
	touch(CARD "/Saves/PS/FF7.srm");
	touch(CARD "/.userdata/shared/PS-pcsx/FF7.m3u.st0");
	touch(OUTSIDE "/precious.bin");
	(void)symlink(OUTSIDE "/precious.bin", PS "/FF7/linked.bin");
	(void)system("mkdir -p '" OUTSIDE "/dir'");
	touch(OUTSIDE "/dir/keep");
	(void)symlink(OUTSIDE "/dir", PS "/FF7/linked-dir");

	// a loose cue sheet: its own tracks go, a track another sheet lists stays
	writeText(PS "/Crash.cue", "FILE \"Crash.bin\" BINARY\nFILE \"Crash (Track 2).bin\" BINARY\nFILE Shared.bin BINARY\nFILE \"../../escape.bin\" BINARY\n");
	touch(PS "/Crash.bin");
	touch(PS "/Crash (Track 2).bin");
	touch(PS "/Shared.bin");
	writeText(PS "/Other.cue", "FILE \"Shared.bin\" BINARY\n");
	touch(PS "/.media/Crash.png");
	touch(CARD "/Saves/PS/Crash.srm");
	touch(CARD "/Saves/PS/Crash.cue.sav");
	touch(CARD "/Roms/escape.bin");

	// a cue-only game folder
	writeText(PS "/Spyro/Spyro.cue", "FILE \"Spyro.bin\" BINARY\n");
	touch(PS "/Spyro/Spyro.bin");
	touch(CARD "/Saves/PS/Spyro.cue.sav");

	// things delete must refuse
	touch(GB "/Hacks/readme.txt");
	(void)system("mkdir -p '" CARD "/Tools/desktop/Thing.pak' '" GB "/Emu.pak' '" OUTSIDE "/Linked (LN)'");
	touch(OUTSIDE "/Linked (LN)/Game.ln");
	(void)symlink(OUTSIDE "/Linked (LN)", CARD "/Roms/Linked (LN)");
	touch(GB "/Linked.gb");
	(void)symlink(OUTSIDE "/precious.bin", GB "/Linked.gb.link");
	// a save folder that's a link out of the card
	touch(CARD "/Roms/NES (FC)/Mario.nes");
	touch(OUTSIDE "/saves/Mario.nes.sav");
	(void)symlink(OUTSIDE "/saves", CARD "/Saves/FC");

	// collections
	writeText(CARD "/Collections/Favorites.txt", "/Roms/Game Boy (GB)/Tetris.gb\n/Roms/Sony PlayStation (PS)/FF7/FF7.m3u\n");
	writeText(CARD "/Collections/arcade.txt", "/Roms/Game Boy (GB)/Zelda.gb");
	writeText(CARD "/Collections/map.txt", "arcade.txt\tArcade\n");
	touch(CARD "/Collections/.hidden.txt");
	(void)system("mkdir -p '" CARD "/Collections/Favorites/.media'");
}

// The plan as sorted "kind path" lines, card-relative.
static const char* kinds[] = { "game", "disc", "save", "state", "slot", "art", "config" };
static int compareLines(const void* a, const void* b) {
	return strcmp(*(char* const*)a, *(char* const*)b);
}
static char* planText(const GAPlan* plan) {
	static char text[8192];
	char* lines[64];
	int n = 0;
	for (int i = 0; i < plan->count && n < 64; i++) {
		lines[n] = malloc(1200);
		const char* path = plan->items[i].path;
		if (!strncmp(path, CARD, strlen(CARD))) path += strlen(CARD);
		snprintf(lines[n], 1200, "%s%s %s", kinds[plan->items[i].kind], plan->items[i].is_dir ? "/" : "", path);
		n++;
	}
	qsort(lines, (size_t)n, sizeof(char*), compareLines);
	text[0] = '\0';
	for (int i = 0; i < n; i++) {
		strcat(text, lines[i]);
		strcat(text, "\n");
		free(lines[i]);
	}
	return text;
}

static void testTag(void) {
	char tag[64];
	GA_tagOf("Game Boy (GB)", tag, sizeof(tag));
	CHECK_STR(tag, "GB");
	GA_tagOf("Super (Japan) (SFC)", tag, sizeof(tag));
	CHECK_STR(tag, "SFC");
	GA_tagOf("Arcade", tag, sizeof(tag));
	CHECK_STR(tag, "Arcade");
}

static void testGameFor(void) {
	char out[1024];
	int is_dir = -1;
	CHECK(GA_gameFor(CARD, GB "/Tetris.gb", out, sizeof(out), &is_dir) == 1);
	CHECK_STR(out, GB "/Tetris.gb");
	CHECK(is_dir == 0);
	// anything in a game folder is the folder
	CHECK(GA_gameFor(CARD, PS "/FF7/FF7 (Disc 1).cue", out, sizeof(out), &is_dir) == 1);
	CHECK_STR(out, PS "/FF7");
	CHECK(is_dir == 1);
	CHECK(GA_gameFor(CARD, PS "/FF7/FF7.m3u", out, sizeof(out), &is_dir) == 1);
	CHECK_STR(out, PS "/FF7");
	CHECK(GA_gameFor(CARD, PS "/FF7", out, sizeof(out), &is_dir) == 1);
	CHECK_STR(out, PS "/FF7");
	CHECK(GA_gameFor(CARD, PS "/Spyro", out, sizeof(out), &is_dir) == 1);
	// a game in a plain subfolder is just the game
	CHECK(GA_gameFor(CARD, GB "/Hacks/Dr. Mario.gb", out, sizeof(out), &is_dir) == 1);
	CHECK_STR(out, GB "/Hacks/Dr. Mario.gb");
	CHECK(is_dir == 0);
	// a link is itself, not what it points at
	CHECK(GA_gameFor(CARD, GB "/Linked.gb.link", out, sizeof(out), &is_dir) == 1);

	// refused
	CHECK(GA_gameFor(CARD, GB, out, sizeof(out), &is_dir) == 0);
	CHECK(GA_gameFor(CARD, CARD "/Roms", out, sizeof(out), &is_dir) == 0);
	CHECK(GA_gameFor(CARD, GB "/Hacks", out, sizeof(out), &is_dir) == 0);
	CHECK(GA_gameFor(CARD, GB "/Emu.pak", out, sizeof(out), &is_dir) == 0);
	CHECK(GA_gameFor(CARD, CARD "/Tools/desktop/Thing.pak", out, sizeof(out), &is_dir) == 0);
	CHECK(GA_gameFor(CARD, CARD "/Collections/Favorites.txt", out, sizeof(out), &is_dir) == 0);
	CHECK(GA_gameFor(CARD, CARD "/Roms/escape.bin", out, sizeof(out), &is_dir) == 0);
	CHECK(GA_gameFor(CARD, CARD "/Roms/Linked (LN)/Game.ln", out, sizeof(out), &is_dir) == 0);
	CHECK(GA_gameFor(CARD, GB "/../Game Boy (GB)/Tetris.gb", out, sizeof(out), &is_dir) == 0);
	CHECK(GA_gameFor(CARD, GB "/./Tetris.gb", out, sizeof(out), &is_dir) == 0);
	CHECK(GA_gameFor(CARD, GB "//Tetris.gb", out, sizeof(out), &is_dir) == 0);
	CHECK(GA_gameFor(CARD, GB "/Tetris.gb/", out, sizeof(out), &is_dir) == 0);
	CHECK(GA_gameFor(CARD, GB "/Missing.gb", out, sizeof(out), &is_dir) == 0);
	CHECK(GA_gameFor(CARD, GB "/.media/Tetris.png", out, sizeof(out), &is_dir) == 0);
	CHECK(GA_gameFor(CARD, PS "/FF7/.media/FF7.png", out, sizeof(out), &is_dir) == 0);
	CHECK(GA_gameFor(CARD, "relative/Roms/GB/Tetris.gb", out, sizeof(out), &is_dir) == 0);
	CHECK(GA_gameFor(CARD, CARD "/.userdata/shared/GB-gambatte/Tetris.gb.st0", out, sizeof(out), &is_dir) == 0);
}

static GACard testCard(int with_saves) {
	GACard c = { CARD, USERDATA, with_saves };
	return c;
}

// The paths each delete would remove, checked before any is deleted.
static void testPlans(void) {
	GACard c = testCard(1);
	GAPlan plan;
	GA_planInit(&plan);

	CHECK(GA_planDelete(&c, GB "/Tetris.gb", &plan) == 0);
	CHECK_STR(planText(&plan),
		"art /Roms/Game Boy (GB)/.media/Tetris.png\n"
		"config /.userdata/desktop/GB-gambatte/Tetris.gb.cfg\n"
		"game /Roms/Game Boy (GB)/Tetris.gb\n"
		"save /Saves/GB/Tetris.gb.rtc\n"
		"save /Saves/GB/Tetris.gb.sav\n"
		"save /Saves/GB/Tetris.srm\n"
		"slot /.userdata/shared/.minui/GB/Tetris.gb.0.bmp\n"
		"slot /.userdata/shared/.minui/GB/Tetris.gb.0.txt\n"
		"slot /.userdata/shared/.minui/GB/Tetris.gb.txt\n"
		"state /.userdata/shared/GB-gambatte/Tetris.gb.st0\n"
		"state /.userdata/shared/GB-gambatte/Tetris.gb.st9\n"
		"state /.userdata/shared/GB-mgba/Tetris.state\n"
		"state /.userdata/shared/GB-mgba/Tetris.state.auto\n"
		"state /.userdata/shared/GB-mgba/Tetris.state3\n");
	CHECK_STR(plan.name, "Tetris.gb");
	CHECK_STR(plan.items[0].path, GB "/Tetris.gb");
	char summary[160];
	GA_planSummary(&plan, summary, sizeof(summary));
	CHECK_STR(summary, "3 saves, 5 states, its art and its settings");

	// with the saves setting off, only the game and its art
	GACard keep = testCard(0);
	CHECK(GA_planDelete(&keep, GB "/Tetris.gb", &plan) == 0);
	CHECK_STR(planText(&plan),
		"art /Roms/Game Boy (GB)/.media/Tetris.png\n"
		"game /Roms/Game Boy (GB)/Tetris.gb\n");
	GA_planSummary(&plan, summary, sizeof(summary));
	CHECK_STR(summary, "its art");

	// another Dr. Mario.gb in the system shares its saves; its art is its own
	CHECK(GA_planDelete(&c, GB "/Dr. Mario.gb", &plan) == 0);
	CHECK_STR(planText(&plan),
		"art /Roms/Game Boy (GB)/.media/Dr. Mario.png\n"
		"game /Roms/Game Boy (GB)/Dr. Mario.gb\n");
	// the one in Hacks has no art of its own
	CHECK(GA_planDelete(&c, GB "/Hacks/Dr. Mario.gb", &plan) == 0);
	CHECK_STR(planText(&plan), "game /Roms/Game Boy (GB)/Hacks/Dr. Mario.gb\n");

	// Zelda.zip shares the art and the cut name's save, not Zelda.gb.sav
	CHECK(GA_planDelete(&c, GB "/Zelda.gb", &plan) == 0);
	CHECK_STR(planText(&plan),
		"game /Roms/Game Boy (GB)/Zelda.gb\n"
		"save /Saves/GB/Zelda.gb.sav\n");

	// a folder with the same tag is the same system
	CHECK(GA_planDelete(&c, GB "/Pokemon.gb", &plan) == 0);
	CHECK_STR(planText(&plan), "game /Roms/Game Boy (GB)/Pokemon.gb\n");

	// a game folder: the folder, its art beside it, and saves under the m3u's name
	CHECK(GA_planDelete(&c, PS "/FF7/FF7 (Disc 1).cue", &plan) == 0);
	CHECK_STR(planText(&plan),
		"art /Roms/Sony PlayStation (PS)/.media/FF7.png\n"
		"game/ /Roms/Sony PlayStation (PS)/FF7\n"
		"save /Saves/PS/FF7.m3u.sav\n"
		"save /Saves/PS/FF7.srm\n"
		"state /.userdata/shared/PS-pcsx/FF7.m3u.st0\n");
	CHECK_STR(plan.name, "FF7");
	GA_planSummary(&plan, summary, sizeof(summary));
	CHECK_STR(summary, "2 saves, 1 state and its art");

	// a cue-only folder saves under the cue's name
	CHECK(GA_planDelete(&c, PS "/Spyro", &plan) == 0);
	CHECK_STR(planText(&plan),
		"game/ /Roms/Sony PlayStation (PS)/Spyro\n"
		"save /Saves/PS/Spyro.cue.sav\n");

	// a loose cue sheet takes the tracks only it lists, never one outside
	// its folder; Crash.bin going too doesn't make the art or Crash.srm shared
	CHECK(GA_planDelete(&c, PS "/Crash.cue", &plan) == 0);
	CHECK_STR(planText(&plan),
		"art /Roms/Sony PlayStation (PS)/.media/Crash.png\n"
		"disc /Roms/Sony PlayStation (PS)/Crash (Track 2).bin\n"
		"disc /Roms/Sony PlayStation (PS)/Crash.bin\n"
		"game /Roms/Sony PlayStation (PS)/Crash.cue\n"
		"save /Saves/PS/Crash.cue.sav\n"
		"save /Saves/PS/Crash.srm\n");
	GA_planSummary(&plan, summary, sizeof(summary));
	CHECK_STR(summary, "2 disc files, 2 saves and its art");

	// a save folder that links off the card is left alone
	CHECK(GA_planDelete(&c, CARD "/Roms/NES (FC)/Mario.nes", &plan) == 0);
	CHECK_STR(planText(&plan), "game /Roms/NES (FC)/Mario.nes\n");

	CHECK(GA_planDelete(&c, GB "/Tetris 2.gb", &plan) == 0);
	CHECK_STR(planText(&plan),
		"game /Roms/Game Boy (GB)/Tetris 2.gb\n"
		"save /Saves/GB/Tetris 2.gb.sav\n"
		"slot /.userdata/shared/.minui/GB/Tetris 2.gb.txt\n");
	GA_planSummary(&plan, summary, sizeof(summary));
	CHECK_STR(summary, "1 save and its resume files");
	// in the words it's given, then English again
	static const char* const words[] = { "%s and %s", "%s y %s", "its resume files", "sus archivos de reanudar", NULL };
	static const char* const plurals[] = { "%i save", "%i partida guardada", "%i partidas guardadas", NULL };
	fake_words = words;
	fake_plurals = plurals;
	GAWords set = { fakeTr, fakePlural };
	GA_setWords(&set);
	GA_planSummary(&plan, summary, sizeof(summary));
	CHECK_STR(summary, "1 partida guardada y sus archivos de reanudar");
	GA_setWords(NULL);
	GA_planSummary(&plan, summary, sizeof(summary));
	CHECK_STR(summary, "1 save and its resume files");

	// refusals leave an empty plan
	CHECK(GA_planDelete(&c, GB "/Hacks", &plan) == -1);
	CHECK(plan.count == 0);
	CHECK(GA_planDelete(&c, CARD "/Roms/Linked (LN)/Game.ln", &plan) == -1);
	CHECK(GA_planDelete(&c, CARD "/Tools/desktop/Thing.pak", &plan) == -1);
	CHECK(plan.count == 0);
	GA_planFree(&plan);
}

static void testDelete(void) {
	GACard c = testCard(1);
	GAPlan plan;
	GA_planInit(&plan);
	int failed = -1;

	CHECK(GA_planDelete(&c, GB "/Tetris.gb", &plan) == 0);
	int count = plan.count;
	CHECK(GA_runDelete(&c, &plan, &failed) == count);
	CHECK(failed == 0);
	CHECK(!there(GB "/Tetris.gb"));
	CHECK(!there(GB "/.media/Tetris.png"));
	CHECK(!there(CARD "/Saves/GB/Tetris.gb.sav"));
	CHECK(!there(CARD "/.userdata/shared/GB-mgba/Tetris.state.auto"));
	CHECK(!there(USERDATA "/GB-gambatte/Tetris.gb.cfg"));
	CHECK(there(GB "/Tetris 2.gb"));
	CHECK(there(CARD "/Saves/GB/Tetris.gbc.sav"));
	CHECK(there(CARD "/Saves/GB/Tetris.gb.sav.bak"));
	CHECK(there(CARD "/Saves/GB/Tetris.sav/inside"));
	CHECK(there(CARD "/.userdata/shared/GBC-gambatte/Tetris.gb.st0"));
	CHECK(there(CARD "/.userdata/shared/GB/Tetris.gb.st1"));
	CHECK(there(CARD "/.userdata/shared/GB-/Tetris.gb.st2"));
	CHECK(there(CARD "/.userdata/shared/GB-gambatte"));
	// running it again finds nothing left to remove
	CHECK(GA_runDelete(&c, &plan, &failed) == 0);
	CHECK(failed == count);

	// a game folder goes with everything in it; links in it go, not what
	// they point at
	CHECK(GA_planDelete(&c, PS "/FF7", &plan) == 0);
	CHECK(GA_runDelete(&c, &plan, &failed) == plan.count);
	CHECK(failed == 0);
	CHECK(!there(PS "/FF7"));
	CHECK(!there(PS "/.media/FF7.png"));
	CHECK(there(OUTSIDE "/precious.bin"));
	CHECK(there(OUTSIDE "/dir/keep"));
	CHECK(there(PS "/Crash.cue"));

	CHECK(GA_planDelete(&c, PS "/Crash.cue", &plan) == 0);
	CHECK(GA_runDelete(&c, &plan, &failed) == plan.count);
	CHECK(!there(PS "/Crash.bin"));
	CHECK(there(PS "/Shared.bin"));
	CHECK(there(PS "/Other.cue"));
	CHECK(there(CARD "/Roms/escape.bin"));

	// a link as the game is removed, not what it points at
	CHECK(GA_planDelete(&c, GB "/Linked.gb.link", &plan) == 0);
	CHECK(GA_runDelete(&c, &plan, &failed) == 1);
	CHECK(!there(GB "/Linked.gb.link"));
	CHECK(there(OUTSIDE "/precious.bin"));

	// a plan pointed outside the card by hand is refused at run time
	GA_planFree(&plan);
	GA_planInit(&plan);
	plan.items = calloc(2, sizeof(GAItem));
	plan.capacity = 2;
	plan.count = 2;
	snprintf(plan.items[0].path, sizeof(plan.items[0].path), "%s", OUTSIDE "/precious.bin");
	plan.items[0].kind = GA_GAME;
	snprintf(plan.items[1].path, sizeof(plan.items[1].path), "%s", OUTSIDE "/dir/keep");
	plan.items[1].kind = GA_SAVE;
	CHECK(GA_runDelete(&c, &plan, &failed) == 0);
	CHECK(failed == 2);
	CHECK(there(OUTSIDE "/precious.bin"));
	CHECK(there(OUTSIDE "/dir/keep"));
	// a sidecar marked as a folder isn't removed as one
	snprintf(plan.items[0].path, sizeof(plan.items[0].path), "%s", CARD "/Saves/GB/Tetris.sav");
	plan.items[0].kind = GA_SAVE;
	plan.items[0].is_dir = 1;
	plan.count = 1;
	CHECK(GA_runDelete(&c, &plan, &failed) == 0);
	CHECK(there(CARD "/Saves/GB/Tetris.sav/inside"));
	GA_planFree(&plan);
}

static void testCollections(void) {
	char rel[1024];
	CHECK(GA_collectionPath(CARD, GB "/Zelda.gb", rel, sizeof(rel)) == 1);
	CHECK_STR(rel, "/Roms/Game Boy (GB)/Zelda.gb");
	CHECK(GA_collectionPath(CARD, PS "/FF7/FF7 (Disc 1).cue", rel, sizeof(rel)) == 1);
	CHECK_STR(rel, "/Roms/Sony PlayStation (PS)/FF7/FF7.m3u");
	CHECK(GA_collectionPath(CARD, PS "/FF7", rel, sizeof(rel)) == 1);
	CHECK_STR(rel, "/Roms/Sony PlayStation (PS)/FF7/FF7.m3u");
	CHECK(GA_collectionPath(CARD, PS "/Spyro", rel, sizeof(rel)) == 1);
	CHECK_STR(rel, "/Roms/Sony PlayStation (PS)/Spyro/Spyro.cue");
	CHECK(GA_collectionPath(CARD, CARD "/Tools/desktop/Thing.pak", rel, sizeof(rel)) == 1);
	CHECK_STR(rel, "/Tools/desktop/Thing.pak");
	CHECK(GA_collectionPath(CARD, GB "/Hacks", rel, sizeof(rel)) == 0);
	CHECK(GA_collectionPath(CARD, GB, rel, sizeof(rel)) == 0);
	CHECK(GA_collectionPath(CARD, GB "/.media/Zelda.png", rel, sizeof(rel)) == 0);
	CHECK(GA_collectionPath(CARD, CARD "/Collections/arcade.txt", rel, sizeof(rel)) == 0);
	CHECK(GA_collectionPath(CARD, "/elsewhere/Roms/GB/x.gb", rel, sizeof(rel)) == 0);

	char* names[8];
	int count = GA_collections(CARD, names, 8);
	CHECK(count == 2);
	if (count == 2) {
		CHECK_STR(names[0], "arcade");
		CHECK_STR(names[1], "Favorites");
	}
	for (int i = 0; i < count; i++) free(names[i]);
	CHECK(GA_collections(CARD, names, 1) == 1);
	free(names[0]);

	CHECK(GA_validCollectionName("Arcade"));
	CHECK(GA_validCollectionName("Best (2026)"));
	CHECK(!GA_validCollectionName(""));
	CHECK(!GA_validCollectionName(".hidden"));
	CHECK(!GA_validCollectionName("a/b"));
	CHECK(!GA_validCollectionName("map"));

	// adding keeps the file as it was, ending a last line without a newline
	const char* arcade = CARD "/Collections/arcade.txt";
	CHECK(GA_collectionAdd(arcade, "/Roms/Game Boy (GB)/Zelda.gb") == 0);
	CHECK(GA_collectionAdd(arcade, "/Roms/Game Boy (GB)/Pokemon.gb") == 1);
	CHECK_STR(readText(arcade), "/Roms/Game Boy (GB)/Zelda.gb\n/Roms/Game Boy (GB)/Pokemon.gb\n");
	CHECK(GA_collectionAdd(arcade, "/Roms/Game Boy (GB)/Pokemon.gb") == 0);
	CHECK(GA_collectionAdd(arcade, "no slash") == -1);
	CHECK(GA_collectionAdd(arcade, "/two\nlines") == -1);
	const char* fresh = CARD "/Collections/New.txt";
	CHECK(GA_collectionAdd(fresh, "/Roms/Game Boy (GB)/Zelda.gb") == 1);
	CHECK_STR(readText(fresh), "/Roms/Game Boy (GB)/Zelda.gb\n");

	// removing a game, or a folder with everything in it
	writeText(fresh, "/Roms/A/a.gb\r\n/Roms/PS/FF7/FF7.m3u\n/Roms/PS/FF7 Remake.m3u\n/Roms/PS/FF7\n/Roms/A/b.gb");
	CHECK(GA_collectionRemove(fresh, "/Roms/A/a.gb", 0) == 1);
	CHECK_STR(readText(fresh), "/Roms/PS/FF7/FF7.m3u\n/Roms/PS/FF7 Remake.m3u\n/Roms/PS/FF7\n/Roms/A/b.gb");
	CHECK(GA_collectionRemove(fresh, "/Roms/PS/FF7", 1) == 2);
	CHECK_STR(readText(fresh), "/Roms/PS/FF7 Remake.m3u\n/Roms/A/b.gb");
	CHECK(GA_collectionRemove(fresh, "/Roms/A/b.gb", 0) == 1);
	CHECK(GA_collectionRemove(fresh, "/Roms/missing", 0) == 0);
	CHECK(GA_collectionRemove(CARD "/Collections/none.txt", "/Roms/x", 0) == 0);
	CHECK(!there(CARD "/Collections/none.txt"));
	unlink(fresh);
}

static void testNames(void) {
	GANames n;
	GA_namesInit(&n);
	CHECK(GA_namesGet(&n, "/Roms/GB/a.gb") == NULL);
	CHECK(GA_namesSet(&n, "/Roms/GB/a.gb", "  Alpha\tOne  ") == 1);
	CHECK_STR(GA_namesGet(&n, "/Roms/GB/a.gb"), "Alpha One");
	CHECK(GA_namesSet(&n, "/Roms/GB/a.gb", "Alpha One") == 0);
	CHECK(GA_namesSet(&n, "/Roms/PS/FF7", "Final Fantasy VII") == 1);
	CHECK(GA_namesSet(&n, "/Roms/PS/FF7/FF7 (Disc 1).cue", "First disc") == 1);
	CHECK(GA_namesSet(&n, "relative", "x") == -1);
	CHECK(GA_namesSet(&n, "/tab\there", "x") == -1);

	const char* file = CARD "/names.txt";
	CHECK(GA_namesSave(&n, file) == 0);
	CHECK_STR(readText(file), "/Roms/GB/a.gb\tAlpha One\n/Roms/PS/FF7\tFinal Fantasy VII\n/Roms/PS/FF7/FF7 (Disc 1).cue\tFirst disc\n");
	GANames loaded;
	GA_namesInit(&loaded);
	writeText(file, "/Roms/GB/a.gb\tAlpha\r\n# a comment\nno tab\n/Roms/GB/b.gb\t   \n/Roms/GB/a.gb\tAlpha again\n");
	CHECK(GA_namesLoad(&loaded, file) == 0);
	CHECK(loaded.count == 1);
	CHECK_STR(GA_namesGet(&loaded, "/Roms/GB/a.gb"), "Alpha again");
	CHECK(GA_namesLoad(&loaded, CARD "/missing.txt") == 0);
	CHECK(loaded.count == 0);
	GA_namesFree(&loaded);

	// an empty name puts the usual one back
	CHECK(GA_namesSet(&n, "/Roms/GB/a.gb", "") == 1);
	CHECK(GA_namesGet(&n, "/Roms/GB/a.gb") == NULL);
	CHECK(GA_namesSet(&n, "/Roms/GB/a.gb", NULL) == 0);
	CHECK(GA_namesRemove(&n, "/Roms/PS/FF7", 1) == 2);
	CHECK(n.count == 0);
	GA_namesFree(&n);
	unlink(file);
}

///////////////////////////////////////
// the plugin against a fake host

#define MAX_FAKE_ACTIONS 8
static struct {
	char id[64];
	char label[32];
	int (*available)(const char*, void*);
	void (*run)(const char*, void*);
	int button;
} actions[MAX_FAKE_ACTIONS];
static int action_count = 0;
static MosaicEntry selected_entry;
static int has_selection = 0;
static char toasted[160];
static char asked[256];
static void (*answer_yes)(void*) = NULL;
static char kb_title[64], kb_initial[160];
static void (*kb_done)(const char*, void*) = NULL;
static char chosen_title[64];
static char chosen[8][160];
static int chosen_count = -1;
static void (*on_pick)(int, void*) = NULL;
static char recents[4][1024];
static int recent_count = 0;
static int reloads = 0;
static char cfg_saves[8] = "";

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; }
static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (!strcmp(key, "saves") && cfg_saves[0]) return cfg_saves;
	return fallback;
}
static void fakeCfgSet(const char* key, const char* value) { (void)key; (void)value; }
static const char* fakeEntryPath(const MosaicEntry* e) { return e->path; }
static const char* fakeEntryName(const MosaicEntry* e) { return e->name; }
static int fakeEntryType(const MosaicEntry* e) { return e->type; }
static const MosaicEntry* fakeSelected(void) { return has_selection ? &selected_entry : NULL; }
static void fakeRedraw(void) {}
static int fakeRegister(const char* id, const char* label, int flags, int (*available)(const char*, void*), void (*run)(const char*, void*), void* data) {
	(void)flags;
	(void)data;
	if (action_count >= MAX_FAKE_ACTIONS) return -1;
	snprintf(actions[action_count].id, sizeof(actions[0].id), "%s", id);
	snprintf(actions[action_count].label, sizeof(actions[0].label), "%s", label);
	actions[action_count].available = available;
	actions[action_count].run = run;
	actions[action_count].button = -1;
	action_count++;
	return 0;
}
static int fakeBind(int screen, int button, int trigger, const char* id) {
	if (screen != MOSAIC_SCREEN_GAMELIST || trigger != MOSAIC_TRIGGER_PRESS) return -1;
	for (int i = 0; i < action_count; i++) {
		if (!strcmp(actions[i].id, id)) {
			actions[i].button = button;
			return 0;
		}
	}
	return -1;
}
static void fakeToast(const char* m) { snprintf(toasted, sizeof(toasted), "%s", m); }
static void fakeConfirm(const char* message, const char* yes, void (*yes_cb)(void*), void* data) {
	(void)yes; (void)data;
	snprintf(asked, sizeof(asked), "%s", message);
	answer_yes = yes_cb;
}
static void fakeKeyboard(const char* title, const char* initial, void (*done)(const char*, void*), void* data) {
	(void)data;
	snprintf(kb_title, sizeof(kb_title), "%s", title);
	snprintf(kb_initial, sizeof(kb_initial), "%s", initial);
	kb_done = done;
}
static int fakeChoose(const char* title, const char* const* list, int count, int sel, void (*pick)(int, void*), void* data) {
	(void)sel; (void)data;
	if (count < 1 || count > 64) return -1;
	snprintf(chosen_title, sizeof(chosen_title), "%s", title);
	chosen_count = count;
	for (int i = 0; i < count && i < 8; i++) snprintf(chosen[i], sizeof(chosen[0]), "%s", list[i]);
	on_pick = pick;
	return 0;
}
static int fakeRecentsCount(void) { return recent_count; }
static const char* fakeRecentPath(int i) { return i >= 0 && i < recent_count ? recents[i] : NULL; }
static int fakeRecentRemove(int i) {
	if (i < 0 || i >= recent_count) return -1;
	memmove(recents[i], recents[i + 1], sizeof(recents[0]) * (size_t)(recent_count - i - 1));
	recent_count--;
	return 0;
}
static void fakeReload(void) { reloads++; }

static MosaicHost fake = {
	.size = sizeof(MosaicHost),
	.api_version = MOSAIC_API_VERSION,
	.platform = "desktop",
	.sdcard_path = CARD,
	.userdata_path = USERDATA,
	.log = fakeLog,
	.cfg_get = fakeCfgGet,
	.cfg_set = fakeCfgSet,
	.entry_path = fakeEntryPath,
	.entry_name = fakeEntryName,
	.entry_type = fakeEntryType,
	.selected_entry = fakeSelected,
	.request_redraw = fakeRedraw,
	.register_action = fakeRegister,
	.bind_button = fakeBind,
	.toast = fakeToast,
	.confirm = fakeConfirm,
	.keyboard = fakeKeyboard,
	.recents_count = fakeRecentsCount,
	.recent_path = fakeRecentPath,
	.recent_remove = fakeRecentRemove,
	.reload_lists = fakeReload,
	.choose = fakeChoose,
};

static int find(const char* id) {
	for (int i = 0; i < action_count; i++) {
		if (!strcmp(actions[i].id, id)) return i;
	}
	return -1;
}

static void select_(const char* path, int type, const char* name) {
	snprintf(selected_entry.path, sizeof(selected_entry.path), "%s", path);
	snprintf(selected_entry.name, sizeof(selected_entry.name), "%s", name);
	selected_entry.type = type;
	has_selection = 1;
}

static const MosaicPlugin* start(void) {
	action_count = 0;
	const MosaicPlugin* p = mosaic_plugin_get(&fake);
	CHECK(p->init(&fake) == 0);
	return p;
}

static void testPlugin(void) {
	makeCard();
	const MosaicPlugin* p = start();
	CHECK_STR(p->id, "game-actions");
	CHECK(action_count == 3);
	int rename_at = find("game-actions.rename");
	int collect_at = find("game-actions.collect");
	int delete_at = find("game-actions.delete");
	CHECK(rename_at >= 0 && collect_at >= 0 && delete_at >= 0);
	if (rename_at < 0 || collect_at < 0 || delete_at < 0) return;
	// all on Y, so they share it with favorites and hidden
	CHECK(actions[rename_at].button == MOSAIC_BUTTON_Y);
	CHECK(actions[collect_at].button == MOSAIC_BUTTON_Y);
	CHECK(actions[delete_at].button == MOSAIC_BUTTON_Y);
	CHECK_STR(actions[rename_at].label, "RENAME");
	CHECK_STR(actions[delete_at].label, "DELETE");

	// what's available where
	has_selection = 0;
	CHECK(!actions[rename_at].available(NULL, NULL));
	CHECK(!actions[delete_at].available(NULL, NULL));
	select_(GB "/Zelda.gb", MOSAIC_ENTRY_ROM, "Zelda");
	CHECK(actions[rename_at].available(NULL, NULL));
	CHECK(actions[collect_at].available(NULL, NULL));
	CHECK(actions[delete_at].available(NULL, NULL));
	select_(GB "/Hacks", MOSAIC_ENTRY_DIR, "Hacks");
	CHECK(actions[rename_at].available(NULL, NULL));
	CHECK(!actions[collect_at].available(NULL, NULL));
	CHECK(!actions[delete_at].available(NULL, NULL));
	select_(CARD "/Tools/desktop/Thing.pak", MOSAIC_ENTRY_PAK, "Thing");
	CHECK(actions[collect_at].available(NULL, NULL));
	CHECK(!actions[delete_at].available(NULL, NULL));

	// rename: the keyboard starts on the shown name, and the name sticks
	MosaicEntry zelda = { GB "/Zelda.gb", "Zelda", MOSAIC_ENTRY_ROM };
	char out[256];
	CHECK(p->display_name(&zelda, out, sizeof(out)) == 0);
	select_(GB "/Zelda.gb", MOSAIC_ENTRY_ROM, "Zelda");
	reloads = 0;
	actions[rename_at].run(NULL, NULL);
	CHECK_STR(kb_title, "Rename");
	CHECK_STR(kb_initial, "Zelda");
	kb_done("Link's Awakening", NULL);
	CHECK_STR(toasted, "Renamed");
	CHECK(reloads == 1);
	CHECK(p->display_name(&zelda, out, sizeof(out)) == 1);
	CHECK_STR(out, "Link's Awakening");
	CHECK_STR(readText(USERDATA "/launcher/game-actions-names.txt"), "/Roms/Game Boy (GB)/Zelda.gb\tLink's Awakening\n");
	// renaming never touches the game
	CHECK(there(GB "/Zelda.gb"));
	// cancelling changes nothing; an empty name restores it
	actions[rename_at].run(NULL, NULL);
	CHECK_STR(kb_initial, "Link's Awakening");
	kb_done(NULL, NULL);
	CHECK(p->display_name(&zelda, out, sizeof(out)) == 1);
	actions[rename_at].run(NULL, NULL);
	kb_done("", NULL);
	CHECK_STR(toasted, "Name restored");
	CHECK(p->display_name(&zelda, out, sizeof(out)) == 0);

	// add to collection: existing ones, then a new one
	actions[collect_at].run(NULL, NULL);
	CHECK_STR(chosen_title, "Add to collection");
	CHECK(chosen_count == 3);
	CHECK_STR(chosen[0], "arcade");
	CHECK_STR(chosen[1], "Favorites");
	CHECK_STR(chosen[2], "New collection");
	on_pick(1, NULL);
	CHECK_STR(toasted, "Added to Favorites");
	CHECK_STR(readText(CARD "/Collections/Favorites.txt"),
		"/Roms/Game Boy (GB)/Tetris.gb\n/Roms/Sony PlayStation (PS)/FF7/FF7.m3u\n/Roms/Game Boy (GB)/Zelda.gb\n");
	actions[collect_at].run(NULL, NULL);
	on_pick(0, NULL);
	CHECK_STR(toasted, "Already in arcade");
	actions[collect_at].run(NULL, NULL);
	on_pick(-1, NULL);
	actions[collect_at].run(NULL, NULL);
	on_pick(2, NULL);
	CHECK_STR(kb_title, "New collection");
	kb_done("a/b", NULL);
	CHECK_STR(toasted, "That name can't be a collection");
	CHECK(!there(CARD "/Collections/a"));
	actions[collect_at].run(NULL, NULL);
	on_pick(2, NULL);
	kb_done("Zelda games", NULL);
	CHECK_STR(toasted, "Added to Zelda games");
	CHECK_STR(readText(CARD "/Collections/Zelda games.txt"), "/Roms/Game Boy (GB)/Zelda.gb\n");

	// delete: a game folder, named in the question with what goes with it
	select_(PS "/FF7/FF7 (Disc 1).cue", MOSAIC_ENTRY_ROM, "Disc 1");
	actions[rename_at].run(NULL, NULL);
	kb_done("Final Fantasy", NULL);
	select_(PS "/FF7", MOSAIC_ENTRY_DIR, "FF7");
	actions[rename_at].run(NULL, NULL);
	kb_done("Final Fantasy VII", NULL);
	snprintf(recents[0], sizeof(recents[0]), "%s", GB "/Zelda.gb");
	snprintf(recents[1], sizeof(recents[1]), "%s", PS "/FF7/FF7.m3u");
	snprintf(recents[2], sizeof(recents[2]), "%s", PS "/FF7 Remake.m3u");
	recent_count = 3;
	select_(PS "/FF7", MOSAIC_ENTRY_DIR, "Final Fantasy VII");
	answer_yes = NULL;
	actions[delete_at].run(NULL, NULL);
	CHECK_STR(asked, "Delete Final Fantasy VII?\nAlso deletes its folder, 2 saves, 1 state and its art.\nThis can't be undone.");
	CHECK(there(PS "/FF7")); // nothing goes before the answer
	CHECK(answer_yes != NULL);
	reloads = 0;
	if (answer_yes) answer_yes(NULL);
	CHECK_STR(toasted, "Deleted");
	CHECK(reloads == 1);
	CHECK(!there(PS "/FF7"));
	CHECK(!there(CARD "/Saves/PS/FF7.srm"));
	CHECK(there(OUTSIDE "/precious.bin"));
	// gone from recents, collections and the name overrides
	CHECK(recent_count == 2);
	CHECK_STR(recents[0], GB "/Zelda.gb");
	CHECK_STR(recents[1], PS "/FF7 Remake.m3u");
	CHECK_STR(readText(CARD "/Collections/Favorites.txt"), "/Roms/Game Boy (GB)/Tetris.gb\n/Roms/Game Boy (GB)/Zelda.gb\n");
	CHECK_STR(readText(USERDATA "/launcher/game-actions-names.txt"), "");
	CHECK(!actions[delete_at].available(NULL, NULL));

	// a plain game, from Recently Played
	select_(GB "/Zelda.gb", MOSAIC_ENTRY_ROM, "Zelda");
	CHECK(actions[delete_at].available(NULL, NULL));
	actions[delete_at].run(NULL, NULL);
	CHECK_STR(asked, "Delete Zelda?\nAlso deletes 1 save.\nThis can't be undone.");
	answer_yes(NULL);
	CHECK(!there(GB "/Zelda.gb"));
	CHECK(there(GB "/Zelda.zip"));
	CHECK(there(GB "/.media/Zelda.png"));
	CHECK(there(CARD "/Saves/GB/Zelda.srm"));
	CHECK(recent_count == 1);
	CHECK_STR(readText(CARD "/Collections/arcade.txt"), "");
	CHECK(!there(CARD "/Collections/Zelda games.txt") || !strcmp(readText(CARD "/Collections/Zelda games.txt"), ""));

	// with the saves setting off, a game's saves stay
	snprintf(cfg_saves, sizeof(cfg_saves), "off");
	p->quit();
	p = start();
	delete_at = find("game-actions.delete");
	select_(GB "/Tetris.gb", MOSAIC_ENTRY_ROM, "Tetris");
	actions[delete_at].run(NULL, NULL);
	CHECK_STR(asked, "Delete Tetris?\nAlso deletes its art.\nThis can't be undone.");
	answer_yes(NULL);
	CHECK(!there(GB "/Tetris.gb"));
	CHECK(there(CARD "/Saves/GB/Tetris.gb.sav"));
	cfg_saves[0] = '\0';

	// a long name is cut to fit
	select_(GB "/Dr. Mario.gb", MOSAIC_ENTRY_ROM, "An extremely long name for a game that goes on and on");
	answer_yes = NULL;
	actions[delete_at].run(NULL, NULL);
	CHECK_STR(asked, "Delete An extremely long name for a game that g...?\nAlso deletes its art.\nThis can't be undone.");
	answer_yes = NULL; // not answered: nothing goes
	CHECK(there(GB "/Dr. Mario.gb"));
	p->quit();

	// an older launcher without the dialogs gets no actions
	fake.size = offsetof(MosaicHost, confirm);
	p = start();
	CHECK(action_count == 0);
	p->quit();
	fake.size = sizeof(MosaicHost);
}

int main(void) {
	makeCard();
	testTag();
	testGameFor();
	testPlans();
	testDelete();
	makeCard();
	testCollections();
	testNames();
	testPlugin();
	(void)system("rm -rf '" CARD "' '" OUTSIDE "'");
	CHECK_DONE();
}
