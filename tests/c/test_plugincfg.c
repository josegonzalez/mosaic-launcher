// Plugins' own settings and what other plugins may read of them, and the
// caller the host's cfg calls go by.

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include "check.h"
#include "caller.h"
#include "plugincfg.h"

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

static const char* seen_on_thread = "unset";

static void* otherThread(void* data) {
	(void)data;
	seen_on_thread = Caller_current();
	return NULL;
}

static const char* returnsInside(void) {
	AS_CALLER("inner") return Caller_current();
	return "not reached";
}

int main(void) {
	char owner_path[512], reader_path[512];
	snprintf(owner_path, sizeof(owner_path), "%s/plugincfg-owner.cfg", TEST_DIR);
	snprintf(reader_path, sizeof(reader_path), "%s/plugincfg-reader.cfg", TEST_DIR);
	remove(reader_path);
	char text[512];

	PluginManifest m = { .bundled = 1 };
	CHECK(Manifest_parse(
		"id=owner\n"
		"setting.dot=toggle|Dot|off||public\n"
		"setting.note=text|Note|\n"
		"setting.code=text|Code|||secret,public\n"
		"setting.limit=choice|Limit|off|off,15|protected,public\n", &m) == 0);
	writeFile(owner_path, "dot=off\nnote=private\ncode=1234\nlimit=15\nextra=unlisted\ndot=on\n");

	PluginCfg owner, reader;
	PluginCfg_init(&owner, owner_path);
	PluginCfg_init(&reader, reader_path);

	// a plugin's own cfg_get sees all of its settings; the last line for a key wins
	CHECK_STR(PluginCfg_get(&owner, "dot", "x"), "on");
	CHECK_STR(PluginCfg_get(&owner, "note", "x"), "private");
	CHECK_STR(PluginCfg_get(&owner, "code", "x"), "1234");
	CHECK_STR(PluginCfg_get(&owner, "limit", "x"), "15");
	CHECK_STR(PluginCfg_get(&owner, "extra", "x"), "unlisted");
	CHECK_STR(PluginCfg_get(&owner, "missing", "x"), "x");
	CHECK(PluginCfg_get(&owner, NULL, NULL) == NULL);

	// another plugin reads only what the schema marks public
	CHECK_STR(PluginCfg_readPublic(&owner, &m, "dot", "x"), "on");
	CHECK_STR(PluginCfg_readPublic(&owner, &m, "note", "x"), "x");  // private
	CHECK_STR(PluginCfg_readPublic(&owner, &m, "code", "x"), "x");  // secret, so never public
	CHECK_STR(PluginCfg_readPublic(&owner, &m, "limit", "x"), "x"); // protected, so never public
	CHECK_STR(PluginCfg_readPublic(&owner, &m, "extra", "x"), "x"); // not in the schema
	CHECK_STR(PluginCfg_readPublic(&owner, NULL, "dot", "x"), "x");
	CHECK_STR(PluginCfg_readPublic(&owner, &m, NULL, "x"), "x");

	// a plugin's cfg_set only ever writes its own file
	CHECK(PluginCfg_set(&reader, "dot", "off") == 0);
	CHECK_STR(PluginCfg_get(&reader, "dot", "x"), "off");
	CHECK_STR(PluginCfg_get(&owner, "dot", "x"), "on");
	readFile(reader_path, text, sizeof(text));
	CHECK_STR(text, "dot=off\n");
	readFile(owner_path, text, sizeof(text));
	CHECK_STR(text, "dot=off\nnote=private\ncode=1234\nlimit=15\nextra=unlisted\ndot=on\n");

	// setting a key keeps the others, and the file is written in full
	CHECK(PluginCfg_set(&owner, "note", "changed") == 0);
	readFile(owner_path, text, sizeof(text));
	CHECK_STR(text, "dot=on\nnote=changed\ncode=1234\nlimit=15\nextra=unlisted\n");
	CHECK(PluginCfg_set(&owner, "a=b", "c") == -1);
	CHECK(PluginCfg_set(&owner, "a", "two\nlines") == -1);
	CHECK(PluginCfg_set(&owner, NULL, "c") == -1);
	CHECK(PluginCfg_set(&owner, "a", NULL) == -1);
	CHECK_STR(PluginCfg_get(&owner, "a", "unset"), "unset");

	// a freed cfg reads its file again
	PluginCfg_free(&owner);
	CHECK_STR(PluginCfg_get(&owner, "note", "x"), "changed");
	PluginCfg_free(&owner);
	PluginCfg_free(&reader);

	// the caller: none until the launcher runs a plugin's code
	CHECK(Caller_current() == NULL);
	AS_CALLER("outer") {
		CHECK_STR(Caller_current(), "outer");
		// a host call that runs another plugin's hook, which comes back
		AS_CALLER("inner") CHECK_STR(Caller_current(), "inner");
		CHECK_STR(Caller_current(), "outer");
		AS_CALLER(NULL) CHECK(Caller_current() == NULL);
		CHECK_STR(Caller_current(), "outer");
		// a plugin's own thread has none, so cfg calls from it are refused
		pthread_t thread;
		CHECK(pthread_create(&thread, NULL, otherThread, NULL) == 0);
		pthread_join(thread, NULL);
		CHECK(seen_on_thread == NULL);
	}
	CHECK(Caller_current() == NULL);
	// leaving by a return puts the one before back too
	CHECK_STR(returnsInside(), "inner");
	CHECK(Caller_current() == NULL);

	remove(owner_path);
	remove(reader_path);
	CHECK_DONE();
}
