// Stock's palettes for plugins, against stock's own palette.c and config.c
// and a fixture card: the list is the one Settings builds (built-ins, then
// the card's, each folder in the order it's read), and the selected one
// is the first with the name Settings saved, or -1 for Custom.

#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "config.h"
#include "defines.h"
#include "mosaic_plugin.h"
#include "palettes.h"
#include "check.h"

#define BUILTIN RES_PATH "/palettes"
#define CARD SDCARD_PATH "/Palettes"
#define SETTINGS_FILE SHARED_USERDATA_PATH "/minuisettings.txt"

static void writeFile(const char* path, const char* text) {
	FILE* file = fopen(path, "w");
	fputs(text, file);
	fclose(file);
}

static void clearDir(const char* dir) {
	DIR* d = opendir(dir);
	if (!d) return;
	struct dirent* e;
	char path[1024];
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.' && (!e->d_name[1] || (e->d_name[1] == '.' && !e->d_name[2]))) continue;
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		unlink(path);
	}
	closedir(d);
}

// the names Settings would list, in readdir order, as its own code reads them
static int expected(char names[][64], int max) {
	const char* dirs[] = {BUILTIN, CARD};
	int n = 0;
	for (int d = 0; d < 2; d++) {
		DIR* dir = opendir(dirs[d]);
		if (!dir) continue;
		struct dirent* e;
		while ((e = readdir(dir)) && n < max) {
			size_t len = strlen(e->d_name);
			if (e->d_name[0] == '.' || len < 5 || strcmp(e->d_name + len - 4, ".txt") != 0) continue;
			if (strstr(e->d_name, "newer")) continue; // version=2
			// the fixtures put their name= in the file name with _ for spaces
			snprintf(names[n], 64, "%.*s", (int)(len - 4), e->d_name);
			for (char* c = names[n]; *c; c++) if (*c == '_') *c = ' ';
			n++;
		}
		closedir(dir);
	}
	return n;
}

static void loadSettings(const char* text) {
	writeFile(SETTINGS_FILE, text);
	CFG_init(NULL, NULL);
}

int main(void) {
	mkdir(TEST_SDCARD_PATH, 0755);
	mkdir(TEST_SDCARD_PATH "/.system", 0755);
	mkdir(RES_PATH, 0755);
	mkdir(BUILTIN, 0755);
	mkdir(CARD, 0755);
	mkdir(TEST_SDCARD_PATH "/.userdata", 0755);
	mkdir(SHARED_USERDATA_PATH, 0755);
	setenv("SHARED_USERDATA_PATH", SHARED_USERDATA_PATH, 1);
	clearDir(BUILTIN);
	clearDir(CARD);

	// none anywhere
	loadSettings("");
	CHECK(Palettes_count() == 0);
	CHECK(Palettes_selected() == -1);
	MosaicPalette p = {.size = sizeof(p)};
	CHECK(Palettes_info(0, &p) == -1);

	writeFile(BUILTIN "/Ink_Gold.txt", "name=Ink Gold\ncolor1=0x112233\ncolor7=0x445566FF\n");
	writeFile(BUILTIN "/Default.txt", "version=1\nname=Default\n");
	writeFile(BUILTIN "/Mossy_Sage.txt", "color2=0xABCDEF80\n");
	writeFile(CARD "/Mine.txt", "name=Mine\ncolor4=0x010203\n");
	writeFile(CARD "/newer.txt", "version=2\nname=Newer\n");
	writeFile(CARD "/Default.txt", "name=Default\ncolor1=0x0000FF\n");
	writeFile(CARD "/notes.md", "name=Notes\n");
	writeFile(CARD "/.hidden.txt", "name=Hidden\n");

	char want[16][64];
	int n = expected(want, 16);
	CHECK(n == 5);
	CHECK(Palettes_count() == n);
	int builtins = 0;
	for (int i = 0; i < n; i++) {
		MosaicPalette got = {.size = sizeof(got)};
		CHECK(Palettes_info(i, &got) == 0);
		CHECK_STR(got.name, want[i]);
		CHECK(got.size == sizeof(got));
		// built-ins first, then the card's
		if (got.builtin) {
			CHECK(builtins == i);
			builtins++;
			CHECK(strncmp(got.path, BUILTIN "/", strlen(BUILTIN) + 1) == 0);
		} else {
			CHECK(strncmp(got.path, CARD "/", strlen(CARD) + 1) == 0);
		}
		if (!strcmp(got.name, "Ink Gold")) {
			CHECK(got.colors[0] == 0x112233);
			CHECK(got.colors[6] == 0x445566);
			CHECK(got.colors[1] == 0x9b2257); // stock's default
		}
		if (!strcmp(got.name, "Mossy Sage")) CHECK(got.colors[1] == 0xABCDEF); // alpha dropped
		if (!strcmp(got.name, "Mine")) CHECK(got.colors[3] == 0x010203);
	}
	CHECK(builtins == 3);
	CHECK(Palettes_info(n, &p) == -1);
	CHECK(Palettes_info(-1, &p) == -1);
	CHECK(Palettes_info(0, NULL) == -1);

	// a plugin built against a smaller MosaicPalette gets only its fields
	struct {
		MosaicPalette head;
		char past[8];
	} small;
	memset(&small, 0x5A, sizeof(small));
	small.head.size = offsetof(MosaicPalette, colors);
	CHECK(Palettes_info(0, &small.head) == 0);
	CHECK_STR(small.head.name, want[0]);
	CHECK(((unsigned char*)&small.head)[offsetof(MosaicPalette, colors)] == 0x5A);

	// Settings' choice: the first palette with its name, the built-in Default
	loadSettings("palette=Default\n");
	int sel = Palettes_selected();
	CHECK(sel >= 0 && sel < builtins);
	CHECK(sel >= 0 && !strcmp(want[sel], "Default"));
	loadSettings("palette=Mine\n");
	sel = Palettes_selected();
	CHECK(sel >= builtins && !strcmp(want[sel], "Mine"));
	// Custom: none, a name that isn't listed, another case, or a newer format's
	loadSettings("palette=\n");
	CHECK(Palettes_selected() == -1);
	loadSettings("");
	CHECK(Palettes_selected() == -1);
	loadSettings("palette=Gone\n");
	CHECK(Palettes_selected() == -1);
	loadSettings("palette=mine\n");
	CHECK(Palettes_selected() == -1);
	loadSettings("palette=Newer\n");
	CHECK(Palettes_selected() == -1);

	// reading again sees a removed file gone
	unlink(CARD "/Mine.txt");
	loadSettings("palette=Mine\n");
	CHECK(Palettes_count() == n - 1);
	CHECK(Palettes_selected() == -1);

	// nothing here writes stock's settings
	struct stat before, after;
	loadSettings("palette=Default\n");
	stat(SETTINGS_FILE, &before);
	Palettes_count();
	Palettes_selected();
	Palettes_info(0, &p);
	stat(SETTINGS_FILE, &after);
	CHECK(before.st_size == after.st_size && before.st_mtime == after.st_mtime);

	// the model's find
	const char* names[] = {"A", "B", "A"};
	CHECK(Palettes_find(names, 3, "A") == 0);
	CHECK(Palettes_find(names, 3, "B") == 1);
	CHECK(Palettes_find(names, 3, "") == -1);
	CHECK(Palettes_find(names, 3, NULL) == -1);
	CHECK(Palettes_find(names, 3, "a") == -1);
	CHECK(Palettes_rgb(0x11223344u) == 0x112233);

	CHECK_DONE();
}
