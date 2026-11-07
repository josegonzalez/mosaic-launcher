// Reading and writing a plugin's key=value settings file.

#include <stdio.h>
#include <stdlib.h>
#include "check.h"
#include "cfgfile.h"

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

int main(void) {
	char path[512];
	snprintf(path, sizeof(path), "%s/test.cfg", TEST_DIR);
	remove(path);
	char value[128];
	char text[512];

	// a missing file gives the fallback
	CHECK(Cfg_get(path, "collection", "Favorites", value, sizeof(value)) == 0);
	CHECK_STR(value, "Favorites");
	CHECK(Cfg_get(path, "collection", NULL, value, sizeof(value)) == 0);
	CHECK_STR(value, "");

	// the last line for a key wins, and prefixes don't match
	writeFile(path, "collection=One\ncollections=Wrong\r\ncollection=Two\n");
	CHECK(Cfg_get(path, "collection", "Favorites", value, sizeof(value)) == 1);
	CHECK_STR(value, "Two");
	CHECK(Cfg_get(path, "collect", "x", value, sizeof(value)) == 0);
	CHECK_STR(value, "x");

	// setting keeps the other lines in order and collapses duplicates
	CHECK(Cfg_set(path, "collection", "Mine") == 0);
	readFile(path, text, sizeof(text));
	CHECK_STR(text, "collections=Wrong\ncollection=Mine\n");
	CHECK(Cfg_set(path, "badge", "on") == 0);
	readFile(path, text, sizeof(text));
	CHECK_STR(text, "collections=Wrong\ncollection=Mine\nbadge=on\n");

	// an empty value is a value
	CHECK(Cfg_set(path, "badge", "") == 0);
	CHECK(Cfg_get(path, "badge", "on", value, sizeof(value)) == 1);
	CHECK_STR(value, "");

	// bad keys and values are refused, and leave the file alone
	CHECK(Cfg_set(path, "a=b", "x") == -1);
	CHECK(Cfg_set(path, "", "x") == -1);
	CHECK(Cfg_set(path, "key", "two\nlines") == -1);
	readFile(path, text, sizeof(text));
	CHECK_STR(text, "collections=Wrong\ncollection=Mine\nbadge=\n");

	// a new file is created
	remove(path);
	CHECK(Cfg_set(path, "only", "1") == 0);
	readFile(path, text, sizeof(text));
	CHECK_STR(text, "only=1\n");

	remove(path);
	CHECK_DONE();
}
