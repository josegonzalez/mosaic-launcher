// statusmodel.c: reading shell paks' status files, and which one the pill
// shows.

#include "check.h"
#include "statusmodel.h"

static int parse(const char* text, StatusFile* out, const char** why) {
	return Status_parse(text, strlen(text), out, why);
}

static void testParse(void) {
	StatusFile f;
	const char* why = "unset";
	CHECK(parse("state=running\ndone=3\ntotal=10\nlabel=Syncing\n", &f, &why) == 0);
	CHECK(why == NULL);
	CHECK(f.state == STATUS_RUNNING);
	CHECK(f.done == 3);
	CHECK(f.total == 10);
	CHECK_STR(f.label, "Syncing");

	// CRLF, spaces around keys and values, comments, blank lines, no final newline
	CHECK(parse("# from a sync pak\r\n\r\n  state = done \r\n label =  Saves backed up  ", &f, &why) == 0);
	CHECK(f.state == STATUS_DONE);
	CHECK(f.done == -1);
	CHECK(f.total == -1);
	CHECK_STR(f.label, "Saves backed up");

	CHECK(parse("state=error\n", &f, &why) == 0);
	CHECK(f.state == STATUS_ERROR);
	CHECK_STR(f.label, "");

	// unknown keys are left for newer launchers, and the last of a key wins
	CHECK(parse("state=running\ncolor=red\nstate=done\n", &f, &why) == 0);
	CHECK(f.state == STATUS_DONE);

	// done past total is cut to it
	CHECK(parse("state=running\ndone=12\ntotal=10\n", &f, &why) == 0);
	CHECK(f.done == 10);
	CHECK(parse("state=running\ndone=0\ntotal=0\n", &f, &why) == 0);
	CHECK(f.total == 0);
	CHECK(parse("state=running\ndone=999999999\n", &f, &why) == 0);
	CHECK(f.done == 999999999);
}

static void testRejected(void) {
	StatusFile f;
	const char* why;
	// missing keys
	CHECK(parse("", &f, &why) == -1);
	CHECK_STR(why, "it has no state");
	CHECK(parse("done=1\ntotal=2\nlabel=x\n", &f, &why) == -1);
	CHECK_STR(why, "it has no state");
	// malformed
	CHECK(parse("state=running\njunk\n", &f, &why) == -1);
	CHECK_STR(why, "a line has no =");
	CHECK(parse("state=running\n=3\n", &f, &why) == -1);
	CHECK_STR(why, "a line has no key");
	CHECK(parse("state=paused\n", &f, &why) == -1);
	CHECK_STR(why, "state isn't running, done or error");
	CHECK(parse("state=Running\n", &f, &why) == -1);
	CHECK(parse("STATE=running\n", &f, &why) == -1); // an unknown key, so no state
	// numbers
	const char* bad[] = { "-1", "+1", "1.5", "0x10", "", "1000000000", "12abc", " ", "1 2" };
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		char text[64];
		snprintf(text, sizeof(text), "state=running\ndone=%s\n", bad[i]);
		CHECK(parse(text, &f, &why) == -1);
		CHECK_STR(why, "done isn't a number from 0 to 999999999");
		snprintf(text, sizeof(text), "state=running\ntotal=%s\n", bad[i]);
		CHECK(parse(text, &f, &why) == -1);
		CHECK_STR(why, "total isn't a number from 0 to 999999999");
	}
	// a NUL, and oversized
	CHECK(Status_parse("state=running\0\n", 15, &f, &why) == -1);
	CHECK_STR(why, "it isn't text");
	char big[STATUS_FILE_MAX + 2];
	memset(big, '#', sizeof(big));
	memcpy(big, "state=running\n", 14);
	CHECK(Status_parse(big, STATUS_FILE_MAX, &f, &why) == 0);
	CHECK(Status_parse(big, STATUS_FILE_MAX + 1, &f, &why) == -1);
	CHECK_STR(why, "it's over 1024 bytes");
	CHECK(Status_parse(NULL, 0, &f, &why) == -1);
	CHECK_STR(why, "it can't be read");
	// why can be NULL
	CHECK(parse("nope", &f, NULL) == -1);
}

static void testSanitise(void) {
	char out[STATUS_LABEL_MAX + 1];
	StatusFile f;
	// control characters, UTF-8 and bytes that aren't UTF-8 are each one ?
	CHECK(parse("state=running\nlabel=Caf\xc3\xa9 \x1b[1m\tok\xff\n", &f, NULL) == 0);
	CHECK_STR(f.label, "Caf? ?[1m ok?");
	// a 4 byte character, and one cut short
	Status_sanitise("a\xf0\x9f\x98\x80" "b\xe2\x82", 8, 32, out, sizeof(out));
	CHECK_STR(out, "a?b??");
	// capped to STATUS_LABEL_MAX characters, then trimmed
	CHECK(parse("state=running\nlabel=0123456789012345678901234567890 23456789\n", &f, NULL) == 0);
	CHECK_STR(f.label, "0123456789012345678901234567890");
	CHECK(strlen(f.label) == 31);
	CHECK(parse("state=running\nlabel=abcdefghijabcdefghijabcdefghijabcdefghij\n", &f, NULL) == 0);
	CHECK(strlen(f.label) == STATUS_LABEL_MAX);
	// a small buffer
	char small[4];
	Status_sanitise("abcdef", 6, 32, small, sizeof(small));
	CHECK_STR(small, "abc");
}

static void testNames(void) {
	char pak[STATUS_NAME_MAX];
	CHECK(Status_fileName("sync.status", pak, sizeof(pak)) == 1);
	CHECK_STR(pak, "sync");
	CHECK(Status_fileName("Game Sync.pak.status", pak, sizeof(pak)) == 1);
	CHECK_STR(pak, "Game Sync.pak");
	CHECK(Status_fileName(".sync.status", pak, sizeof(pak)) == 0); // a temporary file to mv in place
	CHECK(Status_fileName(".status", pak, sizeof(pak)) == 0);
	CHECK(Status_fileName("sync.status.tmp", pak, sizeof(pak)) == 0);
	CHECK(Status_fileName("sync", pak, sizeof(pak)) == 0);
	CHECK(Status_fileName("a/b.status", pak, sizeof(pak)) == 0);
	CHECK(Status_fileName(NULL, pak, sizeof(pak)) == 0);
	char longest[STATUS_NAME_MAX + 8];
	memset(longest, 'a', STATUS_NAME_MAX - 1);
	strcpy(longest + STATUS_NAME_MAX - 1, ".status");
	CHECK(Status_fileName(longest, pak, sizeof(pak)) == 1);
	memset(longest, 'a', STATUS_NAME_MAX);
	strcpy(longest + STATUS_NAME_MAX, ".status");
	CHECK(Status_fileName(longest, pak, sizeof(pak)) == 0);
}

static void testText(void) {
	StatusFile f;
	char text[64];
	parse("state=running\ndone=3\ntotal=10\nlabel=Syncing\n", &f, NULL);
	Status_text(&f, "sync", text, sizeof(text));
	CHECK_STR(text, "Syncing 30%");
	// percentages round down, and stay within 0 to 100
	parse("state=running\ndone=2\ntotal=3\n", &f, NULL);
	Status_text(&f, "sync", text, sizeof(text));
	CHECK_STR(text, "sync 66%");
	parse("state=running\ntotal=3\n", &f, NULL);
	Status_text(&f, "sync", text, sizeof(text));
	CHECK_STR(text, "sync 0%");
	parse("state=running\ndone=7\ntotal=3\n", &f, NULL);
	Status_text(&f, "sync", text, sizeof(text));
	CHECK_STR(text, "sync 100%");
	parse("state=running\ndone=999999999\ntotal=999999999\n", &f, NULL);
	Status_text(&f, "sync", text, sizeof(text));
	CHECK_STR(text, "sync 100%");
	// no total, or a total of 0, shows no percentage
	parse("state=running\ndone=5\n", &f, NULL);
	Status_text(&f, "sync", text, sizeof(text));
	CHECK_STR(text, "sync");
	parse("state=running\ndone=0\ntotal=0\n", &f, NULL);
	Status_text(&f, "sync", text, sizeof(text));
	CHECK_STR(text, "sync");
	parse("state=done\nlabel=Synced\n", &f, NULL);
	Status_text(&f, "sync", text, sizeof(text));
	CHECK_STR(text, "Synced done");
	parse("state=error\n", &f, NULL);
	Status_text(&f, "sync", text, sizeof(text));
	CHECK_STR(text, "sync failed");
	// the pak's name is sanitised too
	Status_text(&f, "s\xc3\xa9nc", text, sizeof(text));
	CHECK_STR(text, "s?nc failed");

	// translated words, and the English for any left NULL
	StatusWords words = { "%s fertig", NULL, "%s : %lld %%" };
	Status_textIn(&f, "sync", &words, text, sizeof(text));
	CHECK_STR(text, "sync failed");
	parse("state=done\n", &f, NULL);
	Status_textIn(&f, "sync", &words, text, sizeof(text));
	CHECK_STR(text, "sync fertig");
	parse("state=running\ndone=1\ntotal=4\n", &f, NULL);
	Status_textIn(&f, "sync", &words, text, sizeof(text));
	CHECK_STR(text, "sync : 25 %");
	// cut short between characters: "完了" is two three-byte characters
	StatusWords cjk = { "%s \xe5\xae\x8c\xe4\xba\x86", NULL, NULL };
	parse("state=done\n", &f, NULL);
	char small[9];
	Status_textIn(&f, "sync", &cjk, small, sizeof(small));
	CHECK_STR(small, "sync \xe5\xae\x8c");
	char smaller[8];
	Status_textIn(&f, "sync", &cjk, smaller, sizeof(smaller));
	CHECK_STR(smaller, "sync ");
}

static StatusSig sig(long long mtime, long long size) {
	StatusSig s = { mtime, 0, size, 1 };
	return s;
}

// one poll: the files given as name, mtime and content, NULL-terminated
static void poll(StatusBoard* b, unsigned long now, const char* const* files, const long long* mtimes) {
	StatusBoard_begin(b);
	for (int i = 0; files[i]; i += 2) {
		StatusSlot* slot;
		StatusSig s = sig(mtimes[i / 2], (long long)strlen(files[i + 1]));
		if (StatusBoard_track(b, files[i], &s, now, &slot) == 1) {
			StatusSlot_set(slot, files[i + 1], strlen(files[i + 1]), now, NULL);
		}
	}
	StatusBoard_end(b);
}

static void testBoard(void) {
	StatusBoard b;
	memset(&b, 0, sizeof(b));
	CHECK(StatusBoard_due(&b, 0) == 1);
	CHECK(StatusBoard_due(&b, STATUS_POLL_MS - 1) == 0);
	CHECK(StatusBoard_due(&b, STATUS_POLL_MS) == 1);
	CHECK(StatusBoard_pick(&b, 1000, 5000) == NULL);

	long long wall = 100000;
	const char* one[] = { "sync", "state=running\ndone=1\ntotal=4\n", NULL };
	long long t1[] = { wall };
	poll(&b, 1000, one, t1);
	const StatusSlot* s = StatusBoard_pick(&b, 1000, wall);
	CHECK(s && !strcmp(s->name, "sync"));
	CHECK(s && s->file.done == 1);

	// unchanged, it's read once
	StatusSlot* slot;
	StatusSig same = sig(wall, (long long)strlen(one[1]));
	StatusBoard_begin(&b);
	CHECK(StatusBoard_track(&b, "sync", &same, 2000, &slot) == 0);
	StatusBoard_end(&b);
	CHECK(slot->changed_ms == 1000);
	// a new size, mtime or inode is read again
	StatusSig moved = same;
	moved.ino = 2;
	StatusBoard_begin(&b);
	CHECK(StatusBoard_track(&b, "sync", &moved, 3000, &slot) == 1);
	StatusBoard_end(&b);
	CHECK(slot->changed_ms == 3000);

	// several paks: the newest by mtime, then by name
	const char* two[] = { "sync", "state=running\n", "backup", "state=running\n", NULL };
	long long t2[] = { wall, wall + 1 };
	poll(&b, 4000, two, t2);
	s = StatusBoard_pick(&b, 4000, wall + 1);
	CHECK(s && !strcmp(s->name, "backup"));
	long long t3[] = { wall + 1, wall + 1 };
	poll(&b, 5000, two, t3);
	s = StatusBoard_pick(&b, 5000, wall + 1);
	CHECK(s && !strcmp(s->name, "backup"));
	// a rejected file is left out, and the other shows
	const char* bad[] = { "sync", "state=running\n", "backup", "state=what\n", NULL };
	long long t4[] = { wall + 1, wall + 2 };
	poll(&b, 6000, bad, t4);
	s = StatusBoard_pick(&b, 6000, wall + 2);
	CHECK(s && !strcmp(s->name, "sync"));
	// a file that's gone frees its slot
	poll(&b, 7000, one, t1);
	int used = 0;
	for (int i = 0; i < STATUS_SLOTS; i++) used += b.slots[i].name[0] != 0;
	CHECK(used == 1);
	const char* none[] = { NULL };
	poll(&b, 8000, none, NULL);
	CHECK(StatusBoard_pick(&b, 8000, wall) == NULL);
	CHECK(b.slots[0].name[0] == 0);
}

static void testFull(void) {
	StatusBoard b;
	memset(&b, 0, sizeof(b));
	StatusBoard_begin(&b);
	StatusSig s = sig(1, 1);
	StatusSlot* slot;
	char name[16];
	for (int i = 0; i < STATUS_SLOTS; i++) {
		snprintf(name, sizeof(name), "p%i", i);
		CHECK(StatusBoard_track(&b, name, &s, 0, &slot) == 1);
	}
	CHECK(StatusBoard_track(&b, "one too many", &s, 0, &slot) == -1);
	CHECK(StatusBoard_track(&b, "p3", &s, 0, &slot) == 0);
}

static void testStale(void) {
	StatusBoard b;
	memset(&b, 0, sizeof(b));
	long long wall = 100000;
	const char* running[] = { "sync", "state=running\n", NULL };
	long long fresh[] = { wall };
	unsigned long start = 50000;
	poll(&b, start, running, fresh);
	CHECK(StatusBoard_pick(&b, start, wall) != NULL);
	// unchanged for STATUS_STALE_MS by the launcher's clock
	CHECK(StatusBoard_pick(&b, start + STATUS_STALE_MS - 1, wall) != NULL);
	CHECK(StatusBoard_pick(&b, start + STATUS_STALE_MS, wall) == NULL);
	// or by its mtime, such as one left from before the launcher started
	memset(&b, 0, sizeof(b));
	long long old[] = { wall - STATUS_STALE_MS / 1000 - 1 };
	poll(&b, start, running, old);
	CHECK(StatusBoard_pick(&b, start, wall) == NULL);
	long long edge[] = { wall - STATUS_STALE_MS / 1000 };
	memset(&b, 0, sizeof(b));
	poll(&b, start, running, edge);
	CHECK(StatusBoard_pick(&b, start, wall) != NULL);
	// a file from the future goes by the launcher's clock alone
	memset(&b, 0, sizeof(b));
	long long future[] = { wall + 999999 };
	poll(&b, start, running, future);
	CHECK(StatusBoard_pick(&b, start + STATUS_STALE_MS - 1, wall) != NULL);
	CHECK(StatusBoard_pick(&b, start + STATUS_STALE_MS, wall) == NULL);
	// errors go stale the same way
	memset(&b, 0, sizeof(b));
	const char* failed[] = { "sync", "state=error\n", NULL };
	poll(&b, start, failed, fresh);
	CHECK(StatusBoard_pick(&b, start + STATUS_STALE_MS - 1, wall) != NULL);
	CHECK(StatusBoard_pick(&b, start + STATUS_STALE_MS, wall) == NULL);
}

static void testDone(void) {
	StatusBoard b;
	memset(&b, 0, sizeof(b));
	long long wall = 100000;
	unsigned long start = 50000;
	const char* running[] = { "sync", "state=running\n", NULL };
	const char* done[] = { "sync", "state=done\n", NULL };
	const char* done2[] = { "sync", "state=done\nlabel=x\n", NULL };
	long long t[] = { wall };
	poll(&b, start, running, t);
	// done shows for STATUS_DONE_MS from when it's first seen
	long long t2[] = { wall + 1 };
	poll(&b, start + 1000, done, t2);
	CHECK(StatusBoard_pick(&b, start + 1000 + STATUS_DONE_MS - 1, wall + 1) != NULL);
	CHECK(StatusBoard_pick(&b, start + 1000 + STATUS_DONE_MS, wall + 1) == NULL);
	// written again as done, it keeps its first time
	long long t3[] = { wall + 2 };
	poll(&b, start + 2000, done2, t3);
	CHECK(StatusBoard_pick(&b, start + 1000 + STATUS_DONE_MS, wall + 2) == NULL);
	// back to running and done again starts over
	long long t4[] = { wall + 3 };
	poll(&b, start + 3000, running, t4);
	long long t5[] = { wall + 4 };
	poll(&b, start + 4000, done, t5);
	CHECK(StatusBoard_pick(&b, start + 4000 + STATUS_DONE_MS - 1, wall + 4) != NULL);
	// an old done file isn't shown when the launcher starts
	memset(&b, 0, sizeof(b));
	long long old[] = { wall - STATUS_STALE_MS / 1000 - 1 };
	poll(&b, start, done, old);
	CHECK(StatusBoard_pick(&b, start, wall) == NULL);
}

static void testWrap(void) {
	// SDL_GetTicks wrapping past the top of an unsigned long
	StatusBoard b;
	memset(&b, 0, sizeof(b));
	unsigned long start = (unsigned long)-100;
	long long wall = 100000;
	const char* running[] = { "sync", "state=running\n", NULL };
	long long t[] = { wall };
	poll(&b, start, running, t);
	CHECK(StatusBoard_pick(&b, start + 500, wall) != NULL);
	CHECK(StatusBoard_pick(&b, start + STATUS_STALE_MS, wall) == NULL);
}

int main(void) {
	testParse();
	testRejected();
	testSanitise();
	testNames();
	testText();
	testBoard();
	testFull();
	testStale();
	testDone();
	testWrap();
	CHECK_DONE();
}
