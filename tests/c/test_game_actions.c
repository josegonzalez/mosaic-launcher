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
	touch(GB "/.media/Tetris.info");
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
	touch(GB "/Linker.gb");
	(void)symlink(OUTSIDE "/precious.bin", CARD "/Saves/GB/Linker.gb.sav");
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

// A rename plan as "kind from -> to" lines in order, card-relative, with
// "copy" for a step that copies.
static char* renameText(const GARename* r) {
	static char text[16384];
	text[0] = '\0';
	size_t len = strlen(CARD);
	for (int i = 0; i < r->count; i++) {
		const GAMove* m = &r->moves[i];
		const char* from = strncmp(m->from, CARD, len) ? m->from : m->from + len;
		const char* to = strncmp(m->to, CARD, len) ? m->to : m->to + len;
		size_t at = strlen(text);
		snprintf(text + at, sizeof(text) - at, "%s%s %s -> %s\n", kinds[m->kind], m->copy ? " copy" : "", from, to);
	}
	return text;
}

static char* carryText(const GARename* r) {
	static char text[8192];
	text[0] = '\0';
	size_t len = strlen(CARD);
	for (int i = 0; i < r->carry_count; i++) {
		const GAMove* m = &r->carries[i];
		size_t at = strlen(text);
		snprintf(text + at, sizeof(text) - at, "%s -> %s\n", m->from + len, m->to + len);
	}
	return text;
}

// Renaming: every old to new path pair, the checks before anything
// changes, and the names it refuses.
static void testRenamePlans(void) {
	GACard c = testCard(1);
	GARename r;
	GA_renameInit(&r);
	char name[256];

	GA_editableName(GB "/Tetris.gb", 0, name, sizeof(name));
	CHECK_STR(name, "Tetris");
	GA_editableName(GB "/Dr. Mario.gb", 0, name, sizeof(name));
	CHECK_STR(name, "Dr. Mario");
	GA_editableName(PS "/FF7 v1.1", 1, name, sizeof(name));
	CHECK_STR(name, "FF7 v1.1");
	CHECK(GA_validFileName("Tetris DX"));
	CHECK(!GA_validFileName(""));
	CHECK(!GA_validFileName(".hidden"));
	CHECK(!GA_validFileName(" lead"));
	CHECK(!GA_validFileName("a/b"));
	CHECK(!GA_validFileName("a\\b"));
	CHECK(!GA_validFileName("a\tb"));
	CHECK(!GA_validFileName(NULL));

	// a plain game: the rom, its art, and every save, state, slot and setting
	CHECK(GA_planRename(&c, GB "/Tetris.gb", "Tetris DX", &r) == 0);
	CHECK(r.error == GA_RENAME_OK);
	CHECK_STR(renameText(&r),
		"game /Roms/Game Boy (GB)/Tetris.gb -> /Roms/Game Boy (GB)/Tetris DX.gb\n"
		"art /Roms/Game Boy (GB)/.media/Tetris.png -> /Roms/Game Boy (GB)/.media/Tetris DX.png\n"
		"art /Roms/Game Boy (GB)/.media/Tetris.info -> /Roms/Game Boy (GB)/.media/Tetris DX.info\n"
		"save /Saves/GB/Tetris.gb.sav -> /Saves/GB/Tetris DX.gb.sav\n"
		"save /Saves/GB/Tetris.gb.rtc -> /Saves/GB/Tetris DX.gb.rtc\n"
		"state /.userdata/shared/GB-gambatte/Tetris.gb.st0 -> /.userdata/shared/GB-gambatte/Tetris DX.gb.st0\n"
		"state /.userdata/shared/GB-gambatte/Tetris.gb.st9 -> /.userdata/shared/GB-gambatte/Tetris DX.gb.st9\n"
		"slot /.userdata/shared/.minui/GB/Tetris.gb.txt -> /.userdata/shared/.minui/GB/Tetris DX.gb.txt\n"
		"slot /.userdata/shared/.minui/GB/Tetris.gb.0.bmp -> /.userdata/shared/.minui/GB/Tetris DX.gb.0.bmp\n"
		"slot /.userdata/shared/.minui/GB/Tetris.gb.0.txt -> /.userdata/shared/.minui/GB/Tetris DX.gb.0.txt\n"
		"config /.userdata/desktop/GB-gambatte/Tetris.gb.cfg -> /.userdata/desktop/GB-gambatte/Tetris DX.gb.cfg\n"
		"save /Saves/GB/Tetris.srm -> /Saves/GB/Tetris DX.srm\n"
		"state /.userdata/shared/GB-mgba/Tetris.state -> /.userdata/shared/GB-mgba/Tetris DX.state\n"
		"state /.userdata/shared/GB-mgba/Tetris.state.auto -> /.userdata/shared/GB-mgba/Tetris DX.state.auto\n"
		"state /.userdata/shared/GB-mgba/Tetris.state3 -> /.userdata/shared/GB-mgba/Tetris DX.state3\n");
	CHECK_STR(carryText(&r), "/Roms/Game Boy (GB)/Tetris.gb -> /Roms/Game Boy (GB)/Tetris DX.gb\n");
	char summary[256];
	GA_renameSummary(&r, summary, sizeof(summary));
	CHECK_STR(summary, "3 saves, 5 states, its art and its settings");
	char out[1024];
	CHECK(GA_renamedPath(&r, GB "/Tetris.gb", out, sizeof(out)) == 1);
	CHECK_STR(out, GB "/Tetris DX.gb");
	CHECK(GA_renamedPath(&r, GB "/Tetris 2.gb", out, sizeof(out)) == 0);
	CHECK(GA_renamedPath(&r, GB "/Tetris.gb/x", out, sizeof(out)) == 0);

	// a game folder: its .m3u inside first, then the folder, its art beside
	// it, and the saves under the .m3u's name; the links in it move with it
	CHECK(GA_planRename(&c, PS "/FF7", "Final Fantasy VII", &r) == 0);
	CHECK_STR(renameText(&r),
		"game /Roms/Sony PlayStation (PS)/FF7/FF7.m3u -> /Roms/Sony PlayStation (PS)/FF7/Final Fantasy VII.m3u\n"
		"game /Roms/Sony PlayStation (PS)/FF7 -> /Roms/Sony PlayStation (PS)/Final Fantasy VII\n"
		"art /Roms/Sony PlayStation (PS)/.media/FF7.png -> /Roms/Sony PlayStation (PS)/.media/Final Fantasy VII.png\n"
		"save /Saves/PS/FF7.m3u.sav -> /Saves/PS/Final Fantasy VII.m3u.sav\n"
		"state /.userdata/shared/PS-pcsx/FF7.m3u.st0 -> /.userdata/shared/PS-pcsx/Final Fantasy VII.m3u.st0\n"
		"save /Saves/PS/FF7.srm -> /Saves/PS/Final Fantasy VII.srm\n");
	CHECK(GA_renamedPath(&r, PS "/FF7/FF7.m3u", out, sizeof(out)) == 1);
	CHECK_STR(out, PS "/Final Fantasy VII/Final Fantasy VII.m3u");
	CHECK(GA_renamedPath(&r, PS "/FF7/FF7 (Disc 1).cue", out, sizeof(out)) == 1);
	CHECK_STR(out, PS "/Final Fantasy VII/FF7 (Disc 1).cue");
	CHECK(GA_renamedPath(&r, PS "/FF7 Remake.m3u", out, sizeof(out)) == 0);
	CHECK(strstr(carryText(&r), "/Roms/Sony PlayStation (PS)/FF7 -> /Roms/Sony PlayStation (PS)/Final Fantasy VII\n"
		"/Roms/Sony PlayStation (PS)/FF7/FF7.m3u -> /Roms/Sony PlayStation (PS)/Final Fantasy VII/Final Fantasy VII.m3u\n") != NULL);
	CHECK(strstr(carryText(&r), "/FF7/FF7 (Disc 2).cue -> /Roms/Sony PlayStation (PS)/Final Fantasy VII/FF7 (Disc 2).cue\n") != NULL);
	CHECK(strstr(carryText(&r), "/FF7/.media") == NULL);

	// a cue sheet folder: its .cue named after it, the tracks stay as they are
	CHECK(GA_planRename(&c, PS "/Spyro/Spyro.bin", "Spyro the Dragon", &r) == 0);
	CHECK_STR(renameText(&r),
		"game /Roms/Sony PlayStation (PS)/Spyro/Spyro.cue -> /Roms/Sony PlayStation (PS)/Spyro/Spyro the Dragon.cue\n"
		"game /Roms/Sony PlayStation (PS)/Spyro -> /Roms/Sony PlayStation (PS)/Spyro the Dragon\n"
		"save /Saves/PS/Spyro.cue.sav -> /Saves/PS/Spyro the Dragon.cue.sav\n");

	// a loose cue sheet: only the sheet, as it names its tracks
	CHECK(GA_planRename(&c, PS "/Crash.cue", "Crash Bandicoot", &r) == 0);
	CHECK_STR(renameText(&r),
		"game /Roms/Sony PlayStation (PS)/Crash.cue -> /Roms/Sony PlayStation (PS)/Crash Bandicoot.cue\n"
		"art /Roms/Sony PlayStation (PS)/.media/Crash.png -> /Roms/Sony PlayStation (PS)/.media/Crash Bandicoot.png\n"
		"save /Saves/PS/Crash.cue.sav -> /Saves/PS/Crash Bandicoot.cue.sav\n"
		"save /Saves/PS/Crash.srm -> /Saves/PS/Crash Bandicoot.srm\n");

	// shared names are copied: Zelda.zip's art and its cut save name, and
	// the other Dr. Mario's whole name
	CHECK(GA_planRename(&c, GB "/Zelda.gb", "Link", &r) == 0);
	CHECK_STR(renameText(&r),
		"game /Roms/Game Boy (GB)/Zelda.gb -> /Roms/Game Boy (GB)/Link.gb\n"
		"art copy /Roms/Game Boy (GB)/.media/Zelda.png -> /Roms/Game Boy (GB)/.media/Link.png\n"
		"save /Saves/GB/Zelda.gb.sav -> /Saves/GB/Link.gb.sav\n"
		"save copy /Saves/GB/Zelda.srm -> /Saves/GB/Link.srm\n");
	CHECK(GA_planRename(&c, GB "/Hacks/Dr. Mario.gb", "Dr. Luigi", &r) == 0);
	CHECK_STR(renameText(&r),
		"game /Roms/Game Boy (GB)/Hacks/Dr. Mario.gb -> /Roms/Game Boy (GB)/Hacks/Dr. Luigi.gb\n"
		"save copy /Saves/GB/Dr. Mario.gb.sav -> /Saves/GB/Dr. Luigi.gb.sav\n"
		"save copy /Saves/GB/Dr. Mario.srm -> /Saves/GB/Dr. Luigi.srm\n");

	// a sidecar that's a link moves as a link; a save folder that's a link
	// out of the card is left
	CHECK(GA_planRename(&c, GB "/Linker.gb", "Linked Up", &r) == 0);
	CHECK_STR(renameText(&r),
		"game /Roms/Game Boy (GB)/Linker.gb -> /Roms/Game Boy (GB)/Linked Up.gb\n"
		"save /Saves/GB/Linker.gb.sav -> /Saves/GB/Linked Up.gb.sav\n");
	CHECK(GA_planRename(&c, CARD "/Roms/NES (FC)/Mario.nes", "Luigi", &r) == 0);
	CHECK_STR(renameText(&r), "game /Roms/NES (FC)/Mario.nes -> /Roms/NES (FC)/Luigi.nes\n");

	// only the case changes: the same file isn't a clash
	CHECK(GA_planRename(&c, GB "/Tetris 2.gb", "TETRIS 2", &r) == 0);
	CHECK(r.count == 3);

	// refused, naming the target that's there
	CHECK(GA_planRename(&c, GB "/Tetris.gb", "Tetris 2", &r) == -1);
	CHECK(r.error == GA_RENAME_EXISTS);
	CHECK_STR(r.clash, GB "/Tetris 2.gb");
	CHECK(r.count == 0);
	touch(CARD "/.userdata/shared/GB-mgba/Tetris Clash.state3");
	CHECK(GA_planRename(&c, GB "/Tetris.gb", "Tetris Clash", &r) == -1);
	CHECK(r.error == GA_RENAME_EXISTS);
	CHECK_STR(r.clash, CARD "/.userdata/shared/GB-mgba/Tetris Clash.state3");
	unlink(CARD "/.userdata/shared/GB-mgba/Tetris Clash.state3");
	touch(PS "/FF7/Final.m3u");
	CHECK(GA_planRename(&c, PS "/FF7", "Final", &r) == -1);
	CHECK(r.error == GA_RENAME_EXISTS);
	CHECK_STR(r.clash, PS "/FF7/Final.m3u");
	unlink(PS "/FF7/Final.m3u");
	// another game in the system has the name, so they'd share saves
	CHECK(GA_planRename(&c, GB "/Tetris.gb", "Pokemon", &r) == -1);
	CHECK(r.error == GA_RENAME_EXISTS);
	touch(CARD "/Roms/Game Boy Hacks (GB)/Kirby.gb");
	CHECK(GA_planRename(&c, GB "/Tetris.gb", "Kirby", &r) == -1);
	CHECK(r.error == GA_RENAME_TAKEN);
	CHECK_STR(r.clash, "Kirby.gb");
	// or cuts to it, for the saves that are under the cut name
	touch(CARD "/Roms/Game Boy Hacks (GB)/Kirby.zip");
	unlink(CARD "/Roms/Game Boy Hacks (GB)/Kirby.gb");
	CHECK(GA_planRename(&c, GB "/Tetris.gb", "Kirby", &r) == -1);
	CHECK(r.error == GA_RENAME_TAKEN);
	CHECK_STR(r.clash, "Kirby");
	CHECK(GA_planRename(&c, GB "/Linker.gb", "Kirby", &r) == 0); // it has none
	unlink(CARD "/Roms/Game Boy Hacks (GB)/Kirby.zip");
	CHECK(GA_planRename(&c, GB "/Tetris.gb", "Tetris", &r) == -1);
	CHECK(r.error == GA_RENAME_SAME);
	CHECK(GA_planRename(&c, GB "/Tetris.gb", "a/b", &r) == -1);
	CHECK(r.error == GA_RENAME_NAME);
	CHECK(GA_planRename(&c, GB "/Hacks", "Mods", &r) == -1);
	CHECK(r.error == GA_RENAME_GAME);
	CHECK(GA_planRename(&c, GB "/Emu.pak", "Other", &r) == -1);
	CHECK(r.error == GA_RENAME_GAME);
	CHECK(GA_planRename(&c, CARD "/Roms/Linked (LN)/Game.ln", "Other", &r) == -1);
	CHECK(r.error == GA_RENAME_GAME);
	GA_renameFree(&r);
}

// Running a rename: everything moves, a failure part way puts back what
// moved, and what can't be put back is named.
static void testRenameRun(void) {
	GACard c = testCard(1);
	GARename r;
	GA_renameInit(&r);
	GARenameResult result;

	CHECK(GA_planRename(&c, GB "/Tetris.gb", "Tetris DX", &r) == 0);
	// a target that turns up after the check: nothing is overwritten, and
	// the steps before it are put back
	writeText(CARD "/Saves/GB/Tetris DX.srm", "theirs");
	CHECK(GA_runRename(&r, &result) == -1);
	CHECK_STR(result.failed, CARD "/Saves/GB/Tetris.srm");
	CHECK(result.stuck == 0);
	CHECK(there(GB "/Tetris.gb"));
	CHECK(!there(GB "/Tetris DX.gb"));
	CHECK(there(CARD "/Saves/GB/Tetris.gb.sav"));
	CHECK(!there(CARD "/Saves/GB/Tetris DX.gb.sav"));
	CHECK(there(USERDATA "/GB-gambatte/Tetris.gb.cfg"));
	CHECK_STR(readText(CARD "/Saves/GB/Tetris DX.srm"), "theirs");
	unlink(CARD "/Saves/GB/Tetris DX.srm");

	CHECK(GA_runRename(&r, &result) == 0);
	CHECK(!there(GB "/Tetris.gb"));
	CHECK(there(GB "/Tetris DX.gb"));
	CHECK(there(GB "/.media/Tetris DX.png"));
	CHECK(there(CARD "/Saves/GB/Tetris DX.gb.sav"));
	CHECK(there(CARD "/.userdata/shared/GB-mgba/Tetris DX.state.auto"));
	CHECK(there(CARD "/.userdata/shared/.minui/GB/Tetris DX.gb.0.bmp"));
	CHECK(there(USERDATA "/GB-gambatte/Tetris DX.gb.cfg"));
	CHECK(there(CARD "/Saves/GB/Tetris.gbc.sav"));
	CHECK(there(CARD "/.userdata/shared/GBC-gambatte/Tetris.gb.st0"));
	// undone after the fact, as when play history can't be moved; one whose
	// old name is taken by then stays, and is named
	touch(CARD "/Saves/GB/Tetris.gb.rtc");
	memset(&result, 0, sizeof(result));
	CHECK(GA_undoRename(&r, r.count, &result) == 1);
	CHECK(result.stuck == 1);
	CHECK_STR(result.stuck_path, CARD "/Saves/GB/Tetris DX.gb.rtc");
	CHECK(there(GB "/Tetris.gb"));
	CHECK(there(CARD "/Saves/GB/Tetris.gb.sav"));
	CHECK(!there(GB "/Tetris DX.gb"));

	// a folder, its links moved as links
	CHECK(GA_planRename(&c, PS "/FF7", "Final Fantasy VII", &r) == 0);
	CHECK(GA_runRename(&r, &result) == 0);
	CHECK(there(PS "/Final Fantasy VII/Final Fantasy VII.m3u"));
	CHECK(there(PS "/Final Fantasy VII/FF7 (Disc 1).bin"));
	CHECK(!there(PS "/FF7"));
	CHECK(there(PS "/.media/Final Fantasy VII.png"));
	CHECK(there(OUTSIDE "/precious.bin"));
	char link[1024];
	ssize_t got = readlink(PS "/Final Fantasy VII/linked.bin", link, sizeof(link) - 1);
	CHECK(got > 0);
	if (got > 0) {
		link[got] = '\0';
		CHECK_STR(link, OUTSIDE "/precious.bin");
	}
	// a copy is undone by removing it
	CHECK(GA_planRename(&c, GB "/Zelda.gb", "Link", &r) == 0);
	CHECK(GA_runRename(&r, &result) == 0);
	CHECK(there(GB "/.media/Zelda.png") && there(GB "/.media/Link.png"));
	CHECK(there(CARD "/Saves/GB/Zelda.srm") && there(CARD "/Saves/GB/Link.srm"));
	CHECK(!there(CARD "/Saves/GB/Zelda.gb.sav") && there(CARD "/Saves/GB/Link.gb.sav"));
	CHECK(GA_undoRename(&r, r.count, &result) == 0);
	CHECK(!there(GB "/.media/Link.png") && there(GB "/.media/Zelda.png"));
	CHECK(there(GB "/Zelda.gb"));

	// collections follow, dropping a line that'd be there twice
	CHECK(GA_planRename(&c, GB "/Zelda.gb", "Link", &r) == 0);
	writeText(CARD "/Collections/rename.txt", "/Roms/Game Boy (GB)/Zelda.gb\n/Roms/Game Boy (GB)/Zelda.zip\r\n"
		"/Roms/Game Boy (GB)/Link.gb\n/Roms/Game Boy (GB)/Zelda.gb");
	CHECK(GA_collectionRename(CARD "/Collections/rename.txt", CARD, &r) == 2);
	CHECK_STR(readText(CARD "/Collections/rename.txt"), "/Roms/Game Boy (GB)/Zelda.zip\r\n/Roms/Game Boy (GB)/Link.gb\n");
	CHECK(GA_planRename(&c, PS "/Final Fantasy VII", "FF", &r) == 0);
	writeText(CARD "/Collections/rename.txt", "/Roms/Sony PlayStation (PS)/Final Fantasy VII/Final Fantasy VII.m3u\n"
		"/Roms/Sony PlayStation (PS)/Final Fantasy VII/FF7 (Disc 1).cue\n");
	CHECK(GA_collectionRename(CARD "/Collections/rename.txt", CARD, &r) == 2);
	CHECK_STR(readText(CARD "/Collections/rename.txt"), "/Roms/Sony PlayStation (PS)/FF/FF.m3u\n/Roms/Sony PlayStation (PS)/FF/FF7 (Disc 1).cue\n");
	CHECK(GA_collectionRename(CARD "/Collections/missing.txt", CARD, &r) == 0);
	CHECK(!there(CARD "/Collections/missing.txt"));
	GA_renameFree(&r);
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
static int fakeRecentSet(int i, const char* path) {
	if (i < 0 || i >= recent_count) return -1;
	for (int j = 0; j < recent_count; j++) {
		if (j != i && !strcmp(recents[j], path)) return -1; // there twice
	}
	snprintf(recents[i], sizeof(recents[0]), "%s", path);
	return 0;
}
static char play_moved[4096]; // "from>to" lines, card-relative
static int play_answer = 0;
static int fakePlayMovePaths(const char* const* from, const char* const* to, int count) {
	play_moved[0] = '\0';
	for (int i = 0; i < count; i++) {
		size_t at = strlen(play_moved);
		snprintf(play_moved + at, sizeof(play_moved) - at, "%s>%s\n", from[i] + strlen(CARD), to[i] + strlen(CARD));
	}
	return play_answer;
}
static char services_called[4096];
static int fakeHasCapability(const char* name) { return !strcmp(name, "favorites.move") || !strcmp(name, "tags.move"); }
static int fakeCall(const char* name, const char* args, char* out, size_t size) {
	size_t at = strlen(services_called);
	if (strstr(args, "/FF7 (Disc 1).bin")) snprintf(services_called + at, sizeof(services_called) - at, "%s disc\n", name);
	else if (!strstr(args, "/Disc") && !strstr(args, "linked")) {
		char line[2200];
		snprintf(line, sizeof(line), "%s %s\n", name, args);
		for (char* c = line; *c; c++) if (*c == '\n' && c[1]) *c = ' ';
		snprintf(services_called + at, sizeof(services_called) - at, "%s", line);
	}
	snprintf(out, size, "moved=1");
	return 0;
}

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
	.recent_set = fakeRecentSet,
	.has_capability = fakeHasCapability,
	.call = fakeCall,
	.play_move_paths = fakePlayMovePaths,
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
	CHECK(action_count == 4);
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

// Rename file from the plugin: the keyboard, the confirm, then the files,
// play time, recents, sets, collections and name override at the new paths.
static void testRenameFile(void) {
	makeCard();
	const MosaicPlugin* p = start();
	int at = find("game-actions.rename-file");
	CHECK(at >= 0);
	if (at < 0) return;
	CHECK(actions[at].button == MOSAIC_BUTTON_Y);
	CHECK_STR(actions[at].label, "RENAME FILE");
	select_(GB "/Hacks", MOSAIC_ENTRY_DIR, "Hacks");
	CHECK(!actions[at].available(NULL, NULL));
	select_(CARD "/Tools/desktop/Thing.pak", MOSAIC_ENTRY_PAK, "Thing");
	CHECK(!actions[at].available(NULL, NULL));

	// a game folder, from a disc in it, with a name override, recents and
	// collections naming it
	int rename_at = find("game-actions.rename");
	select_(PS "/FF7", MOSAIC_ENTRY_DIR, "FF7");
	actions[rename_at].run(NULL, NULL);
	kb_done("Final Fantasy", NULL);
	snprintf(recents[0], sizeof(recents[0]), "%s", PS "/FF7/FF7.m3u");
	snprintf(recents[1], sizeof(recents[1]), "%s", GB "/Zelda.gb");
	recent_count = 2;
	select_(PS "/FF7/FF7 (Disc 1).cue", MOSAIC_ENTRY_ROM, "Disc 1");
	CHECK(actions[at].available(NULL, NULL));
	actions[at].run(NULL, NULL);
	CHECK_STR(kb_title, "Rename file");
	CHECK_STR(kb_initial, "FF7");
	answer_yes = NULL;
	kb_done(NULL, NULL); // cancelled
	CHECK(answer_yes == NULL);
	actions[at].run(NULL, NULL);
	kb_done("FF7", NULL); // the same name: nothing to do
	CHECK(answer_yes == NULL);
	actions[at].run(NULL, NULL);
	kb_done("Final Fantasy VII", NULL);
	CHECK_STR(asked, "Rename FF7 to Final Fantasy VII?\nAlso moves 2 saves, 1 state and its art.");
	CHECK(there(PS "/FF7")); // nothing moves before the answer
	CHECK(answer_yes != NULL);
	if (!answer_yes) return;
	services_called[0] = '\0';
	play_answer = 1;
	reloads = 0;
	answer_yes(NULL);
	CHECK_STR(toasted, "Renamed to Final Fantasy VII");
	CHECK(reloads == 1);
	CHECK(there(PS "/Final Fantasy VII/Final Fantasy VII.m3u"));
	CHECK(there(CARD "/Saves/PS/Final Fantasy VII.m3u.sav"));
	CHECK(strstr(play_moved, "/Roms/Sony PlayStation (PS)/FF7>/Roms/Sony PlayStation (PS)/Final Fantasy VII\n"
		"/Roms/Sony PlayStation (PS)/FF7/FF7.m3u>/Roms/Sony PlayStation (PS)/Final Fantasy VII/Final Fantasy VII.m3u\n") == play_moved);
	CHECK(strstr(play_moved, "/FF7/FF7 (Disc 1).bin>/Roms/Sony PlayStation (PS)/Final Fantasy VII/FF7 (Disc 1).bin\n") != NULL);
	CHECK(strstr(services_called, "favorites.move from=" PS "/FF7/FF7.m3u to=" PS "/Final Fantasy VII/Final Fantasy VII.m3u\n"
		"tags.move from=" PS "/FF7/FF7.m3u to=" PS "/Final Fantasy VII/Final Fantasy VII.m3u\n") != NULL);
	CHECK(strstr(services_called, "favorites.move disc\ntags.move disc\n") != NULL);
	CHECK_STR(recents[0], PS "/Final Fantasy VII/Final Fantasy VII.m3u");
	CHECK_STR(recents[1], GB "/Zelda.gb");
	CHECK_STR(readText(CARD "/Collections/Favorites.txt"),
		"/Roms/Game Boy (GB)/Tetris.gb\n/Roms/Sony PlayStation (PS)/Final Fantasy VII/Final Fantasy VII.m3u\n");
	// the new file name is what shows now
	CHECK_STR(readText(USERDATA "/launcher/game-actions-names.txt"), "");

	// a clash: says which, changes nothing
	select_(GB "/Tetris.gb", MOSAIC_ENTRY_ROM, "Tetris");
	actions[at].run(NULL, NULL);
	CHECK_STR(kb_initial, "Tetris");
	answer_yes = NULL;
	play_moved[0] = '\0';
	kb_done("Tetris 2", NULL);
	CHECK_STR(toasted, "Tetris 2.gb is there already");
	CHECK(answer_yes == NULL);
	CHECK(there(GB "/Tetris.gb"));
	CHECK_STR(play_moved, "");
	actions[at].run(NULL, NULL);
	kb_done("a/b", NULL);
	CHECK_STR(toasted, "That can't be a file name");

	// play time that can't be moved: its files go back
	actions[at].run(NULL, NULL);
	kb_done("Tetris DX", NULL);
	CHECK_STR(asked, "Rename Tetris to Tetris DX?\nAlso moves 3 saves, 5 states, its art and its settings.");
	play_answer = -1;
	answer_yes(NULL);
	CHECK_STR(toasted, "Couldn't move its play time, so nothing was renamed");
	CHECK(there(GB "/Tetris.gb"));
	CHECK(!there(GB "/Tetris DX.gb"));
	CHECK(there(CARD "/Saves/GB/Tetris.gb.sav"));
	CHECK(there(GB "/.media/Tetris.png"));
	CHECK_STR(readText(CARD "/Collections/Favorites.txt"),
		"/Roms/Game Boy (GB)/Tetris.gb\n/Roms/Sony PlayStation (PS)/Final Fantasy VII/Final Fantasy VII.m3u\n");

	// a step that fails part way: the rest go back
	actions[at].run(NULL, NULL);
	kb_done("Tetris DX", NULL);
	play_answer = 1;
	touch(CARD "/Saves/GB/Tetris DX.srm");
	answer_yes(NULL);
	CHECK_STR(toasted, "Couldn't move Tetris.srm, so nothing was renamed");
	CHECK(there(GB "/Tetris.gb"));
	CHECK(!there(GB "/Tetris DX.gb"));
	unlink(CARD "/Saves/GB/Tetris DX.srm");

	// without the play history call, there's no rename file
	p->quit();
	fake.play_move_paths = NULL;
	p = start();
	CHECK(find("game-actions.rename-file") < 0);
	CHECK(find("game-actions.rename") >= 0);
	fake.play_move_paths = fakePlayMovePaths;
	p->quit();
	recent_count = 0;
}

int main(void) {
	makeCard();
	testTag();
	testGameFor();
	testPlans();
	testDelete();
	makeCard();
	testRenamePlans();
	testRenameRun();
	makeCard();
	testCollections();
	testNames();
	testPlugin();
	testRenameFile();
	(void)system("rm -rf '" CARD "' '" OUTSIDE "'");
	CHECK_DONE();
}
