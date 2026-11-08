// The launcher's PIN file, and moving simple mode's old PIN into it.

#include <stdio.h>
#include <stdlib.h>
#include "check.h"
#include "cfgfile.h"
#include "pin.h"

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

int main(void) {
	char path[512], cfg[512], text[512], pin[PIN_LEN];
	snprintf(path, sizeof(path), "%s/pin.txt", TEST_DIR);
	snprintf(cfg, sizeof(cfg), "%s/pin-simple-mode.cfg", TEST_DIR);
	remove(path);
	remove(cfg);

	// cleaning
	Pin_clean(" 1234\r\n", pin);
	CHECK_STR(pin, "1234");
	Pin_clean(NULL, pin);
	CHECK_STR(pin, "");
	Pin_clean(" \t ", pin);
	CHECK_STR(pin, "");
	char lng[200];
	memset(lng, '7', sizeof(lng) - 1);
	lng[sizeof(lng) - 1] = '\0';
	Pin_clean(lng, pin);
	CHECK(strlen(pin) == PIN_LEN - 1);

	// no file: no PIN, and nothing matches
	CHECK(!Pin_isSet(path));
	CHECK(Pin_read(path, pin) == 0);
	CHECK_STR(pin, "");
	CHECK(!Pin_matches(path, ""));
	CHECK(!Pin_matches(path, "1234"));

	// set, read back, matched with blanks trimmed
	CHECK(Pin_write(path, " 4826 ") == 0);
	readFile(path, text, sizeof(text));
	CHECK_STR(text, "4826\n");
	CHECK(Pin_isSet(path));
	CHECK(Pin_read(path, pin) == 1);
	CHECK_STR(pin, "4826");
	CHECK(Pin_matches(path, "4826"));
	CHECK(Pin_matches(path, " 4826\n"));
	CHECK(!Pin_matches(path, "482"));
	CHECK(!Pin_matches(path, "48260"));
	CHECK(!Pin_matches(path, ""));
	CHECK(!Pin_matches(path, NULL));

	// a file edited on a computer, with a line break of its own
	writeFile(path, "  ab12\r\n");
	CHECK(Pin_matches(path, "ab12"));
	CHECK(!Pin_matches(path, "AB12")); // case counts
	writeFile(path, "\n");
	CHECK(!Pin_isSet(path)); // blank is none

	// empty removes the file; removing a missing file is fine
	CHECK(Pin_write(path, "1111") == 0);
	CHECK(Pin_write(path, "  ") == 0);
	CHECK(!exists(path));
	CHECK(Pin_write(path, "") == 0);

	// simple mode's old PIN moves over and is blanked in its cfg
	writeFile(cfg, "on=on\npin= 2468 \nsettings=off\n");
	CHECK(Pin_adopt(path, cfg) == 1);
	CHECK(Pin_matches(path, "2468"));
	CHECK(Cfg_get(cfg, "pin", "x", text, sizeof(text)) == 1);
	CHECK_STR(text, "");
	CHECK(Cfg_get(cfg, "settings", "", text, sizeof(text)) == 1);
	CHECK_STR(text, "off");
	CHECK(Pin_adopt(path, cfg) == 0); // nothing left to move

	// with a PIN already set, the old one is only blanked
	writeFile(cfg, "pin=1357\n");
	CHECK(Pin_adopt(path, cfg) == 0);
	CHECK(Pin_matches(path, "2468"));
	CHECK(Cfg_get(cfg, "pin", "x", text, sizeof(text)) == 1);
	CHECK_STR(text, "");

	// no cfg, or no pin in it: nothing happens
	remove(cfg);
	remove(path);
	CHECK(Pin_adopt(path, cfg) == 0);
	CHECK(!exists(cfg));
	writeFile(cfg, "on=off\n");
	CHECK(Pin_adopt(path, cfg) == 0);
	CHECK(!Pin_isSet(path));
	readFile(cfg, text, sizeof(text));
	CHECK_STR(text, "on=off\n");

	remove(path);
	remove(cfg);
	CHECK_DONE();
}
