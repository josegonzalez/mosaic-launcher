// The box art provider: its model against a fake set of files, then the
// plugin against a fake host and a fixture card.

#include <stdlib.h>
#include <sys/stat.h>
#include "check.h"
#include "../../plugins/art-boxart/art_boxart.c"

///////////////////////////////////////
// a fake set of files

static const char* files[32];
static int file_count = 0;
static int asked = 0; // exists calls

static void haveFiles(const char* const* paths, int count) {
	file_count = 0;
	for (int i = 0; i < count && i < 32; i++) files[file_count++] = paths[i];
}

static int fakeExists(const char* path, void* data) {
	(void)data;
	asked++;
	for (int i = 0; i < file_count; i++) {
		if (!strcmp(files[i], path)) return 1;
	}
	return 0;
}

static const char* find(const BAConfig* config, const char* path, int type, int kind) {
	static char out[BA_PATH_LEN];
	strcpy(out, "unset");
	int found = BA_find(config, path, type, kind, fakeExists, NULL, out, sizeof(out));
	if (found != (out[0] != '\0')) return "mismatch";
	return out;
}

static void testNames(void) {
	char out[BA_PATH_LEN];
	CHECK(BA_artName("/r/GB/Tetris.gb", 0, out, sizeof(out)));
	CHECK_STR(out, "Tetris");
	// only the last extension goes
	CHECK(BA_artName("/r/NES/Super Mario Bros. 3 (USA).nes", 0, out, sizeof(out)));
	CHECK_STR(out, "Super Mario Bros. 3 (USA)");
	CHECK(BA_artName("/r/GB/Tetris (World) [!].zip", 0, out, sizeof(out)));
	CHECK_STR(out, "Tetris (World) [!]");
	CHECK(BA_artName("/r/GB/Pokémon Rot (Germany).gb", 0, out, sizeof(out)));
	CHECK_STR(out, "Pokémon Rot (Germany)");
	CHECK(BA_artName("/r/P8/celeste.p8.png", 0, out, sizeof(out)));
	CHECK_STR(out, "celeste.p8");
	CHECK(BA_artName("/r/GB/README", 0, out, sizeof(out)));
	CHECK_STR(out, "README");
	// folders keep their whole name
	CHECK(BA_artName("/r/PS/Final Fantasy VII (Disc 1).m3u.d", 1, out, sizeof(out)));
	CHECK_STR(out, "Final Fantasy VII (Disc 1).m3u.d");
	CHECK(BA_artName("/r/Mr. Driller", 1, out, sizeof(out)));
	CHECK_STR(out, "Mr. Driller");
	// nothing to name
	CHECK(!BA_artName("Tetris.gb", 0, out, sizeof(out)));
	CHECK_STR(out, "");
	CHECK(!BA_artName("/r/GB/", 0, out, sizeof(out)));
	CHECK(!BA_artName("/r/GB/.gb", 0, out, sizeof(out)));
	CHECK(!BA_artName(NULL, 0, out, sizeof(out)));
	char tiny[4];
	CHECK(!BA_artName("/r/GB/Tetris.gb", 0, tiny, sizeof(tiny)));
	CHECK_STR(tiny, "");
}

static void testCarts(void) {
	CHECK(BA_isCart("/r/P8/celeste.p8.png"));
	CHECK(BA_isCart("/r/P8/CELESTE.P8.PNG"));
	CHECK(BA_isCart("/r/P8/a.b.p8.png"));
	CHECK(!BA_isCart("/r/P8/celeste.p8"));
	CHECK(!BA_isCart("/r/P8/celeste.png"));
	CHECK(!BA_isCart("/r/P8/.p8.png"));
	CHECK(!BA_isCart(".p8.png"));
	CHECK(!BA_isCart(NULL));
}

static void testToggles(void) {
	CHECK(BA_toggle("on", 0) == 1);
	CHECK(BA_toggle("off", 1) == 0);
	CHECK(BA_toggle("yes", 1) == 1);
	CHECK(BA_toggle("", 0) == 0);
	CHECK(BA_toggle(NULL, 1) == 1);
	BAConfig config;
	BA_defaults(&config);
	CHECK(!config.thumbnail);
	CHECK(config.media && config.imgs && config.res && config.images);
	CHECK(config.hide && config.cover && config.rom_art);
}

static void testEachConvention(void) {
	BAConfig config;
	BA_defaults(&config);
	const char* card[] = {
		"/r/GB/.media/box/Alpha.png",
		"/r/GB/.media/3d/Alpha.png",
		"/r/GB/.media/wheel/Alpha.png",
		"/r/GB/Imgs/Bravo.png",
		"/r/GB/.res/Charlie.png",
		"/r/GB/Images/Delta.png",
		"/r/GB/Folder/cover.png",
		"/r/P8/celeste.p8.png",
	};
	haveFiles(card, 8);
	CHECK_STR(find(&config, "/r/GB/Alpha.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_BOX), "/r/GB/.media/box/Alpha.png");
	CHECK_STR(find(&config, "/r/GB/Alpha.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_BOX_3D), "/r/GB/.media/3d/Alpha.png");
	CHECK_STR(find(&config, "/r/GB/Alpha.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_LOGO), "/r/GB/.media/wheel/Alpha.png");
	CHECK_STR(find(&config, "/r/GB/Bravo.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_BOX), "/r/GB/Imgs/Bravo.png");
	CHECK_STR(find(&config, "/r/GB/Charlie.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_BOX), "/r/GB/.res/Charlie.png");
	CHECK_STR(find(&config, "/r/GB/Delta.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_BOX), "/r/GB/Images/Delta.png");
	CHECK_STR(find(&config, "/r/GB/Folder", MOSAIC_ENTRY_DIR, MOSAIC_ART_BOX), "/r/GB/Folder/cover.png");
	CHECK_STR(find(&config, "/r/P8/celeste.p8.png", MOSAIC_ENTRY_ROM, MOSAIC_ART_BOX), "/r/P8/celeste.p8.png");

	// the other kinds come only from .media
	CHECK_STR(find(&config, "/r/GB/Bravo.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_BOX_3D), "");
	CHECK_STR(find(&config, "/r/GB/Bravo.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_LOGO), "");
	// kinds it doesn't answer, and the thumbnail while its setting is off
	CHECK_STR(find(&config, "/r/GB/Alpha.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_THUMBNAIL), "");
	CHECK_STR(find(&config, "/r/GB/Alpha.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_SCREENSHOT), "");
	CHECK_STR(find(&config, "/r/GB/Alpha.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_BACKDROP), "");
	CHECK_STR(find(&config, "/r/GB/Folder", MOSAIC_ENTRY_DIR, MOSAIC_ART_FOLDER_BG), "");
	CHECK_STR(find(&config, "/r/GB/Alpha.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_LIST_BG), "");
	CHECK_STR(find(&config, "/r/GB/Alpha.gb", MOSAIC_ENTRY_ROM, 99), "");
	// paks and toggles have none
	CHECK_STR(find(&config, "/r/GB/Alpha.gb", MOSAIC_ENTRY_PAK, MOSAIC_ART_BOX), "");
	CHECK_STR(find(&config, "/r/GB/Alpha.gb", MOSAIC_ENTRY_TOGGLE, MOSAIC_ART_BOX), "");

	// with the thumbnail setting on, the thumbnail is the box
	config.thumbnail = 1;
	CHECK_STR(find(&config, "/r/GB/Bravo.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_THUMBNAIL), "/r/GB/Imgs/Bravo.png");
	CHECK_STR(find(&config, "/r/GB/Folder", MOSAIC_ENTRY_DIR, MOSAIC_ART_THUMBNAIL), "/r/GB/Folder/cover.png");
	CHECK_STR(find(&config, "/r/P8/celeste.p8.png", MOSAIC_ENTRY_ROM, MOSAIC_ART_THUMBNAIL), "/r/P8/celeste.p8.png");
	CHECK_STR(find(&config, "/r/GB/Echo.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_THUMBNAIL), "");
}

static void testMissing(void) {
	BAConfig config;
	BA_defaults(&config);
	config.thumbnail = 1;
	haveFiles(NULL, 0);
	asked = 0;
	for (int kind = MOSAIC_ART_THUMBNAIL; kind <= MOSAIC_ART_BACKDROP; kind++) {
		CHECK_STR(find(&config, "/r/GB/Alpha.gb", MOSAIC_ENTRY_ROM, kind), "");
		CHECK_STR(find(&config, "/r/GB/Folder", MOSAIC_ENTRY_DIR, kind), "");
		CHECK_STR(find(&config, "/r/P8/celeste.p8.png", MOSAIC_ENTRY_ROM, kind), "");
	}
	CHECK(asked > 0);
	// art named for the whole file name, or another game, isn't this game's
	const char* card[] = { "/r/GB/Imgs/Alpha.gb.png", "/r/GB/Imgs/Alpha (USA).png", "/r/GB/Imgs/alpha" };
	haveFiles(card, 3);
	CHECK_STR(find(&config, "/r/GB/Alpha.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_BOX), "");
	// a path with no folder, or one too long to build, asks nothing
	asked = 0;
	CHECK_STR(find(&config, "Alpha.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_BOX), "");
	char longPath[BA_PATH_LEN + 64];
	memset(longPath, 'a', sizeof(longPath) - 1);
	longPath[0] = '/';
	longPath[sizeof(longPath) - 1] = '\0';
	CHECK_STR(find(&config, longPath, MOSAIC_ENTRY_ROM, MOSAIC_ART_BOX), "");
	CHECK(asked == 0);
	char small[16] = "unset";
	CHECK(!BA_find(&config, "/r/GB/Alpha.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_BOX, fakeExists, NULL, small, sizeof(small)));
	CHECK_STR(small, "");
}

static void testNamesInPaths(void) {
	BAConfig config;
	BA_defaults(&config);
	const char* card[] = {
		"/r/NES/Imgs/Super Mario Bros. 3 (USA).png",
		"/r/GB/.res/Tetris (World) [!].png",
		"/r/GB/Images/Pokémon Rot (Germany).png",
		"/r/PS/.media/box/Final Fantasy VII (USA).png",
		"/r/Roms/Game Boy (GB)/cover.png",
	};
	haveFiles(card, 5);
	CHECK_STR(find(&config, "/r/NES/Super Mario Bros. 3 (USA).nes", MOSAIC_ENTRY_ROM, MOSAIC_ART_BOX),
		"/r/NES/Imgs/Super Mario Bros. 3 (USA).png");
	CHECK_STR(find(&config, "/r/GB/Tetris (World) [!].zip", MOSAIC_ENTRY_ROM, MOSAIC_ART_BOX),
		"/r/GB/.res/Tetris (World) [!].png");
	CHECK_STR(find(&config, "/r/GB/Pokémon Rot (Germany).gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_BOX),
		"/r/GB/Images/Pokémon Rot (Germany).png");
	// a game folder, such as a multi-disc one, is named by its whole name
	CHECK_STR(find(&config, "/r/PS/Final Fantasy VII (USA)", MOSAIC_ENTRY_DIR, MOSAIC_ART_BOX),
		"/r/PS/.media/box/Final Fantasy VII (USA).png");
	// a system folder's cover
	CHECK_STR(find(&config, "/r/Roms/Game Boy (GB)", MOSAIC_ENTRY_DIR, MOSAIC_ART_BOX), "/r/Roms/Game Boy (GB)/cover.png");
}

static void testPrecedence(void) {
	BAConfig config;
	BA_defaults(&config);
	const char* card[] = {
		"/r/P8/celeste.p8.png",
		"/r/P8/.media/box/celeste.p8.png",
		"/r/GB/Folder/cover.png",
		"/r/GB/.media/box/Folder.png",
		"/r/GB/.media/box/Alpha.png",
		"/r/GB/Imgs/Alpha.png",
		"/r/GB/.res/Alpha.png",
		"/r/GB/Images/Alpha.png",
	};
	haveFiles(card, 8);
	// the cart is its own art first
	CHECK_STR(find(&config, "/r/P8/celeste.p8.png", MOSAIC_ENTRY_ROM, MOSAIC_ART_BOX), "/r/P8/celeste.p8.png");
	config.rom_art = 0;
	CHECK_STR(find(&config, "/r/P8/celeste.p8.png", MOSAIC_ENTRY_ROM, MOSAIC_ART_BOX), "/r/P8/.media/box/celeste.p8.png");
	// a folder's cover before the art beside it
	CHECK_STR(find(&config, "/r/GB/Folder", MOSAIC_ENTRY_DIR, MOSAIC_ART_BOX), "/r/GB/Folder/cover.png");
	config.cover = 0;
	CHECK_STR(find(&config, "/r/GB/Folder", MOSAIC_ENTRY_DIR, MOSAIC_ART_BOX), "/r/GB/.media/box/Folder.png");
	// a game's cover.png is never asked about
	config.cover = 1;
	CHECK_STR(find(&config, "/r/GB/Folder", MOSAIC_ENTRY_ROM, MOSAIC_ART_BOX), "/r/GB/.media/box/Folder.png");
	// then .media/box, Imgs, .res and Images, each one off showing the next
	CHECK_STR(find(&config, "/r/GB/Alpha.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_BOX), "/r/GB/.media/box/Alpha.png");
	config.media = 0;
	CHECK_STR(find(&config, "/r/GB/Alpha.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_BOX), "/r/GB/Imgs/Alpha.png");
	config.imgs = 0;
	CHECK_STR(find(&config, "/r/GB/Alpha.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_BOX), "/r/GB/.res/Alpha.png");
	config.res = 0;
	CHECK_STR(find(&config, "/r/GB/Alpha.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_BOX), "/r/GB/Images/Alpha.png");
	config.images = 0;
	CHECK_STR(find(&config, "/r/GB/Alpha.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_BOX), "");
	// a missing earlier one doesn't stop a later one
	BA_defaults(&config);
	const char* later[] = { "/r/GB/Images/Alpha.png" };
	haveFiles(later, 1);
	CHECK_STR(find(&config, "/r/GB/Alpha.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_BOX), "/r/GB/Images/Alpha.png");
	// .media off takes the 3D box and logo with it
	const char* media[] = { "/r/GB/.media/3d/Alpha.png", "/r/GB/.media/wheel/Alpha.png" };
	haveFiles(media, 2);
	config.media = 0;
	CHECK_STR(find(&config, "/r/GB/Alpha.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_BOX_3D), "");
	CHECK_STR(find(&config, "/r/GB/Alpha.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_LOGO), "");
}

static void testHides(void) {
	BAConfig config;
	BA_defaults(&config);
	CHECK(BA_hides(&config, "/r/GB/Images", MOSAIC_ENTRY_DIR));
	CHECK(BA_hides(&config, "/r/GB/Imgs", MOSAIC_ENTRY_DIR));
	CHECK(BA_hides(&config, "/r/GB/images", MOSAIC_ENTRY_DIR));
	CHECK(BA_hides(&config, "/r/GB/IMGS", MOSAIC_ENTRY_DIR));
	CHECK(!BA_hides(&config, "/r/GB/Images", MOSAIC_ENTRY_ROM));
	CHECK(!BA_hides(&config, "/r/GB/Images.gb", MOSAIC_ENTRY_DIR));
	CHECK(!BA_hides(&config, "/r/GB/My Images", MOSAIC_ENTRY_DIR));
	CHECK(!BA_hides(&config, "/r/GB/Tetris.gb", MOSAIC_ENTRY_ROM));
	CHECK(!BA_hides(&config, "Images", MOSAIC_ENTRY_DIR));
	CHECK(!BA_hides(&config, NULL, MOSAIC_ENTRY_DIR));
	config.images = 0;
	CHECK(!BA_hides(&config, "/r/GB/Images", MOSAIC_ENTRY_DIR));
	CHECK(BA_hides(&config, "/r/GB/Imgs", MOSAIC_ENTRY_DIR));
	config.images = 1;
	config.imgs = 0;
	CHECK(!BA_hides(&config, "/r/GB/Imgs", MOSAIC_ENTRY_DIR));
	config.imgs = 1;
	config.hide = 0;
	CHECK(!BA_hides(&config, "/r/GB/Images", MOSAIC_ENTRY_DIR));
	CHECK(!BA_hides(&config, "/r/GB/Imgs", MOSAIC_ENTRY_DIR));
}

///////////////////////////////////////
// the plugin, against a fake host and a fixture card

struct MosaicEntry {
	char path[512];
	int type;
};

static char cfg_keys[8][32];
static char cfg_values[8][32];
static int cfg_count = 0;
static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	(void)id;
	for (int i = 0; i < cfg_count; i++) {
		if (!strcmp(cfg_keys[i], key)) return cfg_values[i];
	}
	return fallback;
}
static void setCfg(const char* key, const char* value) {
	snprintf(cfg_keys[cfg_count], 32, "%s", key);
	snprintf(cfg_values[cfg_count++], 32, "%s", value);
}
static const char* fakeEntryPath(const MosaicEntry* e) { return e->path; }
static int fakeEntryType(const MosaicEntry* e) { return e->type; }

#define CARD TEST_SDCARD_PATH

static void touch(const char* rel) {
	char path[512];
	snprintf(path, sizeof(path), CARD "/%s", rel);
	FILE* f = fopen(path, "w");
	if (f) fclose(f);
}

static MosaicEntry entry(const char* rel, int type) {
	MosaicEntry e;
	snprintf(e.path, sizeof(e.path), CARD "/%s", rel);
	e.type = type;
	return e;
}

static const char* art(const MosaicPlugin* p, const char* rel, int type, int kind) {
	static char out[512];
	strcpy(out, "unset");
	MosaicEntry e = entry(rel, type);
	if (!p->art_path(&e, kind, out, sizeof(out))) return "";
	return strncmp(out, CARD "/", strlen(CARD) + 1) ? out : out + strlen(CARD) + 1;
}

static int shown(const MosaicPlugin* p, const char* rel, int type) {
	MosaicEntry e = entry(rel, type);
	return p->filter(&e, CARD "/GB");
}

static void testPlugin(void) {
	CHECK(system("rm -rf '" CARD "'") == 0);
	mkdir(CARD, 0755);
	mkdir(CARD "/GB", 0755);
	mkdir(CARD "/GB/Imgs", 0755);
	mkdir(CARD "/GB/Images", 0755);
	mkdir(CARD "/GB/.media", 0755);
	mkdir(CARD "/GB/.media/box", 0755);
	mkdir(CARD "/GB/Folder", 0755);
	// a folder where the art should be isn't art
	mkdir(CARD "/GB/Folder/cover.png", 0755);
	mkdir(CARD "/GB/Other", 0755);
	touch("GB/Imgs/Alpha.png");
	touch("GB/Images/Bravo.png");
	touch("GB/.media/box/Bravo.png");
	touch("GB/Other/cover.png");

	MosaicHost host = { 0 };
	host.size = sizeof(host);
	host.api_version = MOSAIC_API_VERSION;
	host.cfg_get = fakeCfgGet;
	host.entry_path = fakeEntryPath;
	host.entry_type = fakeEntryType;

	const MosaicPlugin* p = mosaic_plugin_get(&host);
	CHECK(p && !strcmp(p->id, "art-boxart") && p->art_path && p->filter && p->init);
	CHECK(p->init(&host) == 0);
	CHECK_STR(art(p, "GB/Alpha.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_BOX), "GB/Imgs/Alpha.png");
	CHECK_STR(art(p, "GB/Bravo.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_BOX), "GB/.media/box/Bravo.png");
	CHECK_STR(art(p, "GB/Alpha.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_THUMBNAIL), "");
	CHECK_STR(art(p, "GB/Charlie.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_BOX), "");
	CHECK_STR(art(p, "GB/Folder", MOSAIC_ENTRY_DIR, MOSAIC_ART_BOX), "");
	CHECK_STR(art(p, "GB/Other", MOSAIC_ENTRY_DIR, MOSAIC_ART_BOX), "GB/Other/cover.png");
	CHECK(!shown(p, "GB/Imgs", MOSAIC_ENTRY_DIR));
	CHECK(!shown(p, "GB/Images", MOSAIC_ENTRY_DIR));
	CHECK(shown(p, "GB/Folder", MOSAIC_ENTRY_DIR));
	CHECK(shown(p, "GB/Alpha.gb", MOSAIC_ENTRY_ROM));

	// the settings
	setCfg("thumbnail", "on");
	setCfg("media", "off");
	setCfg("hide", "off");
	setCfg("cover", "off");
	setCfg("imgs", "bogus"); // keeps its default
	CHECK(p->init(&host) == 0);
	CHECK_STR(art(p, "GB/Alpha.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_THUMBNAIL), "GB/Imgs/Alpha.png");
	CHECK_STR(art(p, "GB/Bravo.gb", MOSAIC_ENTRY_ROM, MOSAIC_ART_BOX), "GB/Images/Bravo.png");
	CHECK_STR(art(p, "GB/Other", MOSAIC_ENTRY_DIR, MOSAIC_ART_BOX), "");
	CHECK(shown(p, "GB/Imgs", MOSAIC_ENTRY_DIR));
	CHECK(shown(p, "GB/Images", MOSAIC_ENTRY_DIR));
	CHECK(system("rm -rf '" CARD "'") == 0);
}

int main(void) {
	testNames();
	testCarts();
	testToggles();
	testEachConvention();
	testMissing();
	testNamesInPaths();
	testPrecedence();
	testHides();
	testPlugin();
	CHECK_DONE();
}
