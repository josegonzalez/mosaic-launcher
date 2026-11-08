// infomodel.c: reading .info metadata sidecars.

#include <stdlib.h>
#include "check.h"
#include "infomodel.h"

static InfoFile* parse(const char* text, const char** why) {
	return Info_parse(text, strlen(text), why);
}

static void testParse(void) {
	const char* why = "unset";
	InfoFile* info = parse("name=Super Mario World\nyear=1990\npublisher=Nintendo\ngenre=Platformer\n", &why);
	CHECK(info != NULL);
	CHECK(why == NULL);
	CHECK(info && info->count == 4);
	CHECK_STR(Info_get(info, "name"), "Super Mario World");
	CHECK_STR(Info_get(info, "year"), "1990");
	CHECK_STR(Info_get(info, "publisher"), "Nintendo");
	CHECK_STR(Info_get(info, "genre"), "Platformer");
	// keys match without case
	CHECK_STR(Info_get(info, "YEAR"), "1990");
	CHECK(Info_get(info, "players") == NULL);
	CHECK(Info_get(info, NULL) == NULL);
	CHECK(Info_get(NULL, "year") == NULL);
	free(info);

	// CRLF, comments, blank lines, spaces around keys and values, an = in a
	// value, an empty value, no final newline
	info = parse("# scraped\r\n\r\n  Year = 1991 \r\nPlayers=1-2\r\nnote=a=b\r\nempty=\r\nlast=x", &why);
	CHECK(info != NULL);
	CHECK_STR(Info_get(info, "year"), "1991");
	CHECK_STR(info->keys[0], "year"); // stored lower case
	CHECK_STR(Info_get(info, "players"), "1-2");
	CHECK_STR(Info_get(info, "note"), "a=b");
	CHECK_STR(Info_get(info, "empty"), "");
	CHECK_STR(Info_get(info, "last"), "x");
	free(info);

	// the last of a key wins, keeping its place
	info = parse("year=1990\ngenre=RPG\nYEAR=1991\n", &why);
	CHECK(info && info->count == 2);
	CHECK_STR(Info_get(info, "year"), "1991");
	free(info);
}

static void testSkipped(void) {
	const char* why;
	// lines without an =, an empty or long key, or odd characters in a key
	char longkey[64];
	memset(longkey, 'k', INFO_KEY_MAX + 1);
	strcpy(longkey + INFO_KEY_MAX + 1, "=v\n");
	char text[256];
	snprintf(text, sizeof(text), "junk\n=nokey\nbad key=1\nk\xc3\xa9y=2\n%sok=1\n", longkey);
	InfoFile* info = parse(text, &why);
	CHECK(info && info->count == 1);
	CHECK_STR(Info_get(info, "ok"), "1");
	free(info);
	// a key of INFO_KEY_MAX is fine, and so are _, - and .
	memset(longkey, 'k', INFO_KEY_MAX);
	strcpy(longkey + INFO_KEY_MAX, "=v\nrelease.date=1\nx_y-z=2\n");
	info = parse(longkey, &why);
	CHECK(info && info->count == 3);
	free(info);

	// files with nothing to keep
	CHECK(parse("", &why) == NULL);
	CHECK_STR(why, "it has no keys");
	CHECK(parse("# just a comment\n\njunk\n", &why) == NULL);
	CHECK_STR(why, "it has no keys");
	CHECK(Info_parse("year=1990\0\n", 11, &why) == NULL);
	CHECK_STR(why, "it isn't text");
	CHECK(Info_parse(NULL, 0, &why) == NULL);
	CHECK_STR(why, "it can't be read");
	CHECK(parse("", NULL) == NULL);
}

static void testCaps(void) {
	const char* why;
	// oversized
	char big[INFO_FILE_MAX + 1];
	memset(big, '#', sizeof(big));
	memcpy(big, "year=1990\n", 10);
	InfoFile* info = Info_parse(big, INFO_FILE_MAX, &why);
	CHECK(info != NULL);
	free(info);
	CHECK(Info_parse(big, INFO_FILE_MAX + 1, &why) == NULL);
	CHECK_STR(why, "it's over 4096 bytes");

	// INFO_KEYS_MAX keys, with later new keys left out but known ones still changing
	char many[2048] = "";
	for (int i = 0; i < INFO_KEYS_MAX + 5; i++) {
		char line[32];
		snprintf(line, sizeof(line), "k%i=%i\n", i, i);
		strcat(many, line);
	}
	strcat(many, "k0=again\n");
	info = parse(many, &why);
	CHECK(info && info->count == INFO_KEYS_MAX);
	CHECK_STR(Info_get(info, "k0"), "again");
	CHECK(Info_get(info, "k31") != NULL);
	CHECK(Info_get(info, "k32") == NULL);
	free(info);

	// values are cut to INFO_VALUE_MAX bytes, between characters
	char value[600] = "description=";
	size_t n = strlen(value);
	memset(value + n, 'a', INFO_VALUE_MAX + 50);
	value[n + INFO_VALUE_MAX + 50] = '\0';
	info = parse(value, &why);
	CHECK(info && strlen(Info_get(info, "description")) == INFO_VALUE_MAX);
	free(info);
	// a 2 byte character across the cut is left out whole
	memset(value + n, 'a', INFO_VALUE_MAX - 1);
	strcpy(value + n + INFO_VALUE_MAX - 1, "\xc3\xa9zz");
	info = parse(value, &why);
	CHECK(info && strlen(Info_get(info, "description")) == INFO_VALUE_MAX - 1);
	free(info);

	// control characters are spaces; UTF-8 is kept
	info = parse("title=Pok\xc3\xa9mon\x1b[0m\tRed\x7f!\n", &why);
	CHECK_STR(Info_get(info, "title"), "Pok\xc3\xa9mon [0m Red !");
	free(info);
}

static void testPath(void) {
	char path[256];
	Info_path("/mnt/SDCARD/Roms/Game Boy (GB)/Tetris (World).gb", path, sizeof(path));
	CHECK_STR(path, "/mnt/SDCARD/Roms/Game Boy (GB)/.media/Tetris (World).info");
	// only the last extension goes, as for thumbnails
	Info_path("/sd/Roms/PS (PS)/Game.bin.m3u", path, sizeof(path));
	CHECK_STR(path, "/sd/Roms/PS (PS)/.media/Game.bin.info");
	Info_path("/sd/Roms/Arcade (FBN)/noext", path, sizeof(path));
	CHECK_STR(path, "/sd/Roms/Arcade (FBN)/.media/noext.info");
	Info_path("Game.gb", path, sizeof(path));
	CHECK_STR(path, "./.media/Game.info");
	Info_path(NULL, path, sizeof(path));
	CHECK_STR(path, "");
}

static void testCopy(void) {
	char out[8];
	CHECK(Info_copy("1990", out, sizeof(out)) == 1);
	CHECK_STR(out, "1990");
	CHECK(Info_copy("abcdefghij", out, sizeof(out)) == 1);
	CHECK_STR(out, "abcdefg");
	// cut between characters
	CHECK(Info_copy("abcdef\xc3\xa9", out, sizeof(out)) == 1);
	CHECK_STR(out, "abcdef");
	CHECK(Info_copy("abcde\xc3\xa9z", out, sizeof(out)) == 1);
	CHECK_STR(out, "abcde\xc3\xa9");
	CHECK(Info_copy(NULL, out, sizeof(out)) == 0);
	CHECK_STR(out, "");
	char one[1];
	CHECK(Info_copy("x", one, sizeof(one)) == 1);
	CHECK_STR(one, "");
}

int main(void) {
	testParse();
	testSkipped();
	testCaps();
	testPath();
	testCopy();
	CHECK_DONE();
}
