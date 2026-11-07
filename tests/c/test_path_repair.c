// The path repair plugin: its model on its own, then the plugin against a
// fake host on a fixture card.

#include <stdlib.h>
#include "check.h"
#include "../../plugins/path-repair/path_repair.c"

#define CARD TEST_SDCARD_PATH
#define GB CARD "/Roms/Game Boy (GB)"
#define PS CARD "/Roms/Sony PlayStation (PS)"
#define FAVORITES CARD "/Collections/Favorites.txt"
#define RPG CARD "/Collections/RPG.txt"

static char cfg_recents[8] = "", cfg_collections[8] = "";
static int logged = 0;

static void fakeLog(const char* id, const char* m) {
	(void)id;
	(void)m;
	logged++;
}
static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	(void)id;
	if (!strcmp(key, "recents") && cfg_recents[0]) return cfg_recents;
	if (!strcmp(key, "collections") && cfg_collections[0]) return cfg_collections;
	return fallback;
}

static NUIHost fake = {
	.size = sizeof(NUIHost),
	.api_version = NUI_API_VERSION,
	.platform = "desktop",
	.sdcard_path = CARD,
	.userdata_path = CARD "/.userdata/desktop",
	.log = fakeLog,
	.cfg_get = fakeCfgGet,
};

static void writeText(const char* path, const char* text) {
	FILE* f = fopen(path, "w");
	if (!f) {
		fprintf(stderr, "couldn't write %s\n", path);
		exit(1);
	}
	fputs(text, f);
	fclose(f);
}

static char text[4096];
static const char* readText(const char* path) {
	text[0] = '\0';
	FILE* f = fopen(path, "r");
	if (!f) return text;
	size_t n = fread(text, 1, sizeof(text) - 1, f);
	text[n] = '\0';
	fclose(f);
	return text;
}

static const char* key(const char* name) {
	static char out[256];
	Repair_key(name, out, sizeof(out));
	return out;
}

static void testKey(void) {
	// accents, composed and decomposed (e + U+0301), fold to the plain letter
	CHECK_STR(key("Pok\xC3\xA9mon Red.gb"), "pokemonred.gb");
	CHECK_STR(key("Poke\xCC\x81mon Red.gb"), "pokemonred.gb");
	CHECK_STR(key("Pokemon Red.gb"), "pokemonred.gb");
	// case, spaces and punctuation don't count
	CHECK_STR(key("POKEMON - Red!.GB"), "pokemonred.gb");
	CHECK_STR(key("Super Mario Bros. 3.nes"), "supermariobros3.nes");
	CHECK_STR(key("Zelda: Link's Awakening (USA, Europe).gb"), "zeldalinksawakeningusaeurope.gb");
	// Latin Extended-A and letters that fold to two
	CHECK_STR(key("\xC5\x81\xC3\xB3" "d\xC5\xBA \xC3\x86on Stra\xC3\x9F" "e.sfc"), "lodzaeonstrasse.sfc");
	CHECK_STR(key("\xC3\x89\xC3\x80\xC3\x96\xC3\x9C\xC3\x91"), "eaoun");
	// other scripts are kept as they are
	CHECK_STR(key("\xE3\x83\x9D\xE3\x82\xB1\xE3\x83\xA2\xE3\x83\xB3 Red.gb"), "\xE3\x83\x9D\xE3\x82\xB1\xE3\x83\xA2\xE3\x83\xB3red.gb");
	// a folder's dot isn't an extension unless letters or digits follow it
	CHECK_STR(key("Dr. Mario"), "drmario");
	CHECK_STR(key(".hidden"), "hidden");
	CHECK_STR(key(""), "");
	// broken UTF-8 is kept byte for byte, not read past
	CHECK_STR(key("A\xC3"), "a\xC3");
	// too long to fit is no key at all
	char small[4];
	Repair_key("Tetris.gb", small, sizeof(small));
	CHECK_STR(small, "");
}

static void testPick(void) {
	const char* names[] = {"Pokemon Red.gb", "Pokemon Blue.gb", "Tetris.gbc", "Tetris (USA).gb"};
	CHECK(Repair_pick("Pok\xC3\xA9mon Red.gb", names, 4) == 0);
	CHECK(Repair_pick("pokemon blue.GB", names, 4) == 1);
	// the extension has to match
	CHECK(Repair_pick("Tetris.gb", names, 4) == -1);
	CHECK(Repair_pick("Tetris.GBC", names, 4) == 2);
	CHECK(Repair_pick("Pokemon Yellow.gb", names, 4) == -1);

	// two matches is ambiguous, and left alone
	const char* twice[] = {"Pokemon Red.gb", "POKEMON RED.gb"};
	CHECK(Repair_pick("Pok\xC3\xA9mon Red.gb", twice, 2) == -1);
	// the missing name itself never counts
	const char* same[] = {"Pokemon Red.gb", "Pokemon-Red.gb"};
	CHECK(Repair_pick("Pokemon Red.gb", same, 2) == 1);
	// a name with nothing to match by matches nothing
	const char* dots[] = {"!!!.gb", "---.gb"};
	CHECK(Repair_pick("???.gb", dots, 2) == -1);
	CHECK(Repair_pick("x.gb", NULL, 0) == -1);
}

static void testFixed(void) {
	CHECK(Repair_fixedLength("/Roms/Game Boy (GB)/Tetris.gb") == strlen("/Roms/Game Boy (GB)"));
	CHECK(Repair_fixedLength("/Roms/PS/Game/Game.m3u") == strlen("/Roms/PS"));
	CHECK(Repair_fixedLength("/Tools/tg5040/Files.pak") == strlen("/Tools/tg5040"));
	CHECK(Repair_fixedLength("/Roms/GB") == 0);
	CHECK(Repair_fixedLength("/Roms/GB/") == 0);
	CHECK(Repair_fixedLength("/Tetris.gb") == 0);
	CHECK(Repair_fixedLength("Roms/GB/Tetris.gb") == 0);
	CHECK(Repair_fixedLength("//GB/Tetris.gb") == 0);
	CHECK(Repair_fixedLength(NULL) == 0);
}

static void card(void) {
	CHECK(system("rm -rf '" CARD "' && mkdir -p '" GB "' '" PS "/Final Fantasy VII (USA)' '" CARD "/Collections' '" CARD "/Tools/desktop/File Manager.pak'") == 0);
	writeText(GB "/Pokemon Red.gb", "");
	writeText(GB "/Pokemon Blue.gb", "");
	writeText(GB "/Pokemon-Blue.gb", ""); // makes Blue ambiguous
	writeText(GB "/Tetris.gb", "");
	writeText(GB "/Super Mario Land 2 - 6 Golden Coins.gb", "");
	writeText(PS "/Final Fantasy VII (USA)/Final Fantasy VII (USA).m3u", "");
}

static void testCollections(void) {
	card();
	writeText(FAVORITES,
		"/Roms/Game Boy (GB)/Pok\xC3\xA9mon Red.gb\n"           // renamed: repaired
		"/Roms/Game Boy (GB)/Tetris.gb\n"                       // fine
		"/Roms/Game Boy (GB)/Pok\xC3\xA9mon Blue.gb\r\n"        // two matches: left alone
		"/Roms/Game Boy (GB)/Zelda.gb\n"                        // gone: left alone
		"/Roms/Game Boy (GB)/Super Mario Land 2: 6 Golden Coins.gb\n"
		"/Roms/Sony PlayStation (PS)/Final Fantasy 7/Final Fantasy VII (USA).m3u\n" // folder renamed, no match
		"/Roms/Sony PlayStation (PS)/final fantasy vii usa/Final Fantasy VII (USA).m3u\n"
		"/Roms/GB/Tetris.gb\n");                                // system folder renamed: left alone
	writeText(RPG,
		"/Roms/Game Boy (GB)/Pokemon Red.gb\n"
		"/Roms/Game Boy (GB)/POKEMON-RED.gb\n");                // its match is already there: dropped
	writeText(CARD "/Collections/map.txt", "/Roms/Game Boy (GB)/Pokemon Red.gb\tRed\n");

	logged = 0;
	CHECK(plugin.init(&fake) == 0);
	CHECK_STR(readText(FAVORITES),
		"/Roms/Game Boy (GB)/Pokemon Red.gb\n"
		"/Roms/Game Boy (GB)/Tetris.gb\n"
		"/Roms/Game Boy (GB)/Pok\xC3\xA9mon Blue.gb\n"
		"/Roms/Game Boy (GB)/Zelda.gb\n"
		"/Roms/Game Boy (GB)/Super Mario Land 2 - 6 Golden Coins.gb\n"
		"/Roms/Sony PlayStation (PS)/Final Fantasy 7/Final Fantasy VII (USA).m3u\n"
		"/Roms/Sony PlayStation (PS)/Final Fantasy VII (USA)/Final Fantasy VII (USA).m3u\n"
		"/Roms/GB/Tetris.gb\n");
	CHECK_STR(readText(RPG), "/Roms/Game Boy (GB)/Pokemon Red.gb\n");
	CHECK_STR(readText(CARD "/Collections/map.txt"), "/Roms/Game Boy (GB)/Pokemon Red.gb\tRed\n");
	CHECK(logged == 4);
	CHECK(listed_count == 0); // forgotten after the pass

	// nothing broken that can be repaired: the file isn't rewritten
	struct stat before, after;
	CHECK(stat(FAVORITES, &before) == 0);
	CHECK(system("touch -t 200001010000 '" FAVORITES "'") == 0);
	CHECK(stat(FAVORITES, &before) == 0);
	CHECK(plugin.init(&fake) == 0);
	CHECK(stat(FAVORITES, &after) == 0);
	CHECK(after.st_mtime == before.st_mtime);

	// the setting turns it off
	writeText(RPG, "/Roms/Game Boy (GB)/pokemon_red.gb\n");
	snprintf(cfg_collections, sizeof(cfg_collections), "off");
	CHECK(plugin.init(&fake) == 0);
	CHECK_STR(readText(RPG), "/Roms/Game Boy (GB)/pokemon_red.gb\n");
	cfg_collections[0] = '\0';
	CHECK(plugin.init(&fake) == 0);
	CHECK_STR(readText(RPG), "/Roms/Game Boy (GB)/Pokemon Red.gb\n");
}

static void testRecents(void) {
	card();
	CHECK(plugin.init(&fake) == 0);
	char out[1024];
	CHECK(repairPath(GB "/Pok\xC3\xA9mon Red.gb", out, sizeof(out)) == 1);
	CHECK_STR(out, GB "/Pokemon Red.gb");
	CHECK(repairPath(GB "/Pok\xC3\xA9mon Blue.gb", out, sizeof(out)) == 0);
	CHECK(repairPath(GB "/Zelda.gb", out, sizeof(out)) == 0);
	CHECK(repairPath(CARD "/Tools/desktop/FileManager.pak", out, sizeof(out)) == 1);
	CHECK_STR(out, CARD "/Tools/desktop/File Manager.pak");
	// off the card, or too long to write back
	CHECK(repairPath("/elsewhere/Roms/GB/Tetris.gb", out, sizeof(out)) == 0);
	char tiny[8];
	CHECK(repairPath(GB "/Tetris!.gb", tiny, sizeof(tiny)) == 0);
	// one folder listed for several recents in it, forgotten on the next frame
	CHECK(listed_count == 2);
	plugin.tick(0);
	CHECK(listed_count == 0);

	snprintf(cfg_recents, sizeof(cfg_recents), "off");
	CHECK(plugin.init(&fake) == 0);
	CHECK(repairPath(GB "/Pok\xC3\xA9mon Red.gb", out, sizeof(out)) == 0);
	cfg_recents[0] = '\0';
	plugin.quit();
}

int main(void) {
	testKey();
	testPick();
	testFixed();
	CHECK(nextui_plugin_get(&fake) == &plugin);
	CHECK(plugin.repair_path == repairPath);
	testCollections();
	testRecents();
	CHECK_DONE();
}
