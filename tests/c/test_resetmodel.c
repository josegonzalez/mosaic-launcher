// Resetting plugin settings to their defaults: which keys go, and which
// files change.

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "check.h"
#include "resetmodel.h"

static void writeFile(const char* path, const char* text) {
	FILE* f = fopen(path, "w");
	fputs(text, f);
	fclose(f);
}

static void readFile(const char* path, char* out, size_t size) {
	FILE* f = fopen(path, "r");
	size_t n = f ? fread(out, 1, size - 1, f) : 0;
	out[n] = '\0';
	if (f) fclose(f);
}

static int exists(const char* path) {
	FILE* f = fopen(path, "r");
	if (f) fclose(f);
	return f != NULL;
}

static int logged = 0;
static void countLog(const char* message) {
	(void)message;
	logged++;
}

int main(void) {
	PluginManifest m[3];
	memset(m, 0, sizeof(m));
	CHECK(Manifest_parse("id=alpha\napi=1\nsetting.mode=choice|Mode|a|a,b\nsetting.on=toggle|On|on\n"
		"setting.api_key=text|Key|||secret\n", &m[0]) == 0);
	CHECK(Manifest_parse("id=bravo\napi=1\nsetting.theme=text|Theme|\n", &m[1]) == 0);
	CHECK(Manifest_parse("id=charlie\napi=1\n", &m[2]) == 0);

	// settings are listed and not secret; state keys aren't settings
	CHECK(Reset_isSetting(&m[0], "mode"));
	CHECK(Reset_isSetting(&m[0], "on"));
	CHECK(!Reset_isSetting(&m[0], "api_key"));
	CHECK(!Reset_isSetting(&m[0], "seen"));
	CHECK(!Reset_isSetting(&m[0], ""));
	CHECK(!Reset_isSetting(&m[0], NULL));
	CHECK(!Reset_isSetting(NULL, "mode"));

	// the text: settings and blank lines go, the rest stays in order
	int removed = -1;
	char* text = Reset_cfgText("mode=b\nseen=1\n\non=off\r\napi_key=xyz\nlast=/Roms/a.gb\nnoequals\n", &m[0], &removed);
	CHECK_STR(text, "seen=1\napi_key=xyz\nlast=/Roms/a.gb\nnoequals\n");
	CHECK(removed == 2);
	free(text);
	text = Reset_cfgText("mode=b\non=off", &m[0], &removed);
	CHECK_STR(text, "");
	CHECK(removed == 2);
	free(text);
	text = Reset_cfgText(NULL, &m[0], &removed);
	CHECK_STR(text, "");
	CHECK(removed == 0);
	free(text);
	// a key longer than any setting's is kept
	text = Reset_cfgText("modemodemodemodemodemodemodemodemodemodemodemodemodemode=1\n", &m[0], &removed);
	CHECK_STR(text, "modemodemodemodemodemodemodemodemodemodemodemodemodemode=1\n");
	CHECK(removed == 0);
	free(text);

	// the folder: plugins.txt goes, alpha's cfg keeps its state, bravo's
	// had only a setting so it goes, charlie's has no settings and stays,
	// as do the PIN, settings.txt and other files and other plugins' cfgs
	char dir[512], path[600], got[512];
	snprintf(dir, sizeof(dir), "%s/reset-state", TEST_DIR);
	mkdir(dir, 0755);
	const char* names[] = { "plugins.txt", "alpha.cfg", "bravo.cfg", "charlie.cfg", "pin.txt", "settings.txt", "tags.txt", "delta.cfg" };
	const char* texts[] = { "alpha=on\nbravo=off\n", "mode=b\nseen=1\napi_key=xyz\n", "theme=dark\n", "x=1\n", "4826\n", "lock_chord=on\n",
		"Kids\n", "mode=b\n" };
	for (int i = 0; i < 8; i++) {
		snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
		writeFile(path, texts[i]);
	}
	logged = 0;
	CHECK(Reset_apply(dir, m, 3, countLog) == 3);
	CHECK(logged == 3);
	snprintf(path, sizeof(path), "%s/plugins.txt", dir);
	CHECK(!exists(path));
	snprintf(path, sizeof(path), "%s/alpha.cfg", dir);
	readFile(path, got, sizeof(got));
	CHECK_STR(got, "seen=1\napi_key=xyz\n");
	snprintf(path, sizeof(path), "%s/bravo.cfg", dir);
	CHECK(!exists(path));
	for (int i = 3; i < 8; i++) {
		snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
		readFile(path, got, sizeof(got));
		CHECK_STR(got, texts[i]);
	}
	snprintf(path, sizeof(path), "%s/alpha.cfg.tmp", dir);
	CHECK(!exists(path));

	// again: nothing left to change, and no plugins.txt is no error
	CHECK(Reset_apply(dir, m, 3, NULL) == 0);
	CHECK(Reset_apply(NULL, m, 3, NULL) == -1);
	// a folder that isn't there has nothing to reset
	snprintf(path, sizeof(path), "%s/reset-missing", TEST_DIR);
	CHECK(Reset_apply(path, m, 3, NULL) == 0);

	for (int i = 0; i < 3; i++) Manifest_free(&m[i]);
	CHECK_DONE();
}
