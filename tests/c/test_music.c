// The music plugin's model: which files are tracks and how they sort,
// listing a folder, the queue with shuffle and repeat, the socket
// protocol's commands and status line, and the plugin's pill timing,
// unlock press and folder browsing.

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"
#include "../../plugins/music/music_model.h"

static void testKinds(void) {
	CHECK(MU_trackKind("Song.mp3") == MU_KIND_MP3);
	CHECK(MU_trackKind("Song.MP3") == MU_KIND_MP3);
	CHECK(MU_trackKind("a.b.flac") == MU_KIND_FLAC);
	CHECK(MU_trackKind("x.Wav") == MU_KIND_WAV);
	CHECK(MU_trackKind("x.ogg") == MU_KIND_NONE);
	CHECK(MU_trackKind("mp3") == MU_KIND_NONE);
	CHECK(MU_trackKind(".mp3") == MU_KIND_NONE);
	CHECK(MU_trackKind("._Song.mp3") == MU_KIND_NONE);
	CHECK(MU_trackKind("") == MU_KIND_NONE);
	CHECK(MU_trackKind(NULL) == MU_KIND_NONE);

	char title[64];
	MU_trackTitle("/Music/Album/01 One.mp3", title, sizeof(title));
	CHECK_STR(title, "01 One");
	MU_trackTitle("Two.tracks.flac", title, sizeof(title));
	CHECK_STR(title, "Two.tracks");
	MU_trackTitle("/Music/noext", title, sizeof(title));
	CHECK_STR(title, "noext");
	MU_trackTitle("/Music/A long name.wav", title, 5);
	CHECK_STR(title, "A lo");
	MU_trackTitle(NULL, title, sizeof(title));
	CHECK_STR(title, "");
}

static void testCompare(void) {
	CHECK(MU_compareNames("2 Two", "10 Ten") < 0);
	CHECK(MU_compareNames("10 Ten", "2 Two") > 0);
	CHECK(MU_compareNames("Track 02", "Track 2") != 0);
	CHECK(MU_compareNames("track 2", "Track 3") < 0);
	CHECK(MU_compareNames("abc", "ABD") < 0);
	CHECK(MU_compareNames("ab", "abc") < 0);
	CHECK(MU_compareNames("same", "same") == 0);
	CHECK(MU_compareNames("Disc 1/9", "Disc 1/10") < 0);
	CHECK(MU_compareNames("007", "7") != 0);
	CHECK(MU_compareNames("007 a", "8 a") < 0);
}

static void touchFile(const char* dir, const char* name) {
	char path[MU_PATH_MAX];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	FILE* f = fopen(path, "w");
	if (f) fclose(f);
}

static void testScan(void) {
	char root[] = "/tmp/test_music.XXXXXX";
	CHECK(mkdtemp(root) != NULL);
	char sub[MU_PATH_MAX], deep[MU_PATH_MAX];
	snprintf(sub, sizeof(sub), "%s/Album", root);
	snprintf(deep, sizeof(deep), "%s/10 Later", root);
	mkdir(sub, 0755);
	mkdir(deep, 0755);
	touchFile(root, "10 Ten.mp3");
	touchFile(root, "2 Two.flac");
	touchFile(root, "1 one.WAV");
	touchFile(root, "notes.txt");
	touchFile(root, "._2 Two.flac");
	touchFile(root, ".hidden.mp3");
	char dotdir[MU_PATH_MAX];
	snprintf(dotdir, sizeof(dotdir), "%s/.Trash", root);
	mkdir(dotdir, 0755);

	MuList list;
	MU_listInit(&list);
	CHECK(MU_scan(root, &list, 1) == 5);
	CHECK(list.dirs == 2);
	if (list.count == 5) {
		CHECK_STR(list.items[0].name, "10 Later");
		CHECK(list.items[0].is_dir);
		CHECK_STR(list.items[1].name, "Album");
		CHECK_STR(list.items[2].name, "1 one.WAV");
		CHECK(!list.items[2].is_dir);
		CHECK_STR(list.items[3].name, "2 Two.flac");
		CHECK_STR(list.items[4].name, "10 Ten.mp3");
	}
	CHECK(MU_scan(root, &list, 0) == 3);
	CHECK(list.dirs == 0);
	if (list.count == 3) CHECK_STR(list.items[0].name, "1 one.WAV");
	CHECK(MU_scan(sub, &list, 1) == 0);
	CHECK(MU_scan("/nonexistent/music", &list, 1) == -1);
	CHECK(list.count == 0);

	// the queue from a listing with folders in it
	MU_scan(root, &list, 1);
	MuQueue q;
	MuQueue_init(&q, 1);
	CHECK(MuQueue_set(&q, root, &list, 0) == -1); // a folder
	CHECK(MuQueue_set(&q, root, &list, 5) == -1);
	CHECK(MuQueue_set(&q, root, &list, 3) == 0);
	CHECK(q.count == 3);
	CHECK(MuQueue_current(&q) == 1);
	char want[MU_PATH_MAX];
	snprintf(want, sizeof(want), "%s/2 Two.flac", root);
	CHECK_STR(q.paths[MuQueue_current(&q)], want);
	MuQueue_free(&q);
	MU_listFree(&list);

	char cmd[MU_PATH_MAX * 2];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
	CHECK(system(cmd) == 0);
}

static MuList fakeList(int dirs, int tracks) {
	MuList list;
	MU_listInit(&list);
	list.count = dirs + tracks;
	list.dirs = dirs;
	list.items = calloc((size_t)list.count, sizeof(MuItem));
	for (int i = 0; i < list.count; i++) {
		snprintf(list.items[i].name, sizeof(list.items[i].name), i < dirs ? "dir%i" : "%i.mp3", i < dirs ? i : i - dirs);
		list.items[i].is_dir = i < dirs;
	}
	return list;
}

static void testQueue(void) {
	MuList list = fakeList(1, 4);
	MuQueue q;
	MuQueue_init(&q, 7);
	CHECK(MuQueue_current(&q) == -1);
	CHECK(MuQueue_next(&q, 0) == -1);
	CHECK(MuQueue_prev(&q) == -1);
	CHECK(MuQueue_set(&q, "/M", &list, 1) == 0);
	CHECK_STR(q.paths[0], "/M/0.mp3");
	CHECK_STR(q.paths[3], "/M/3.mp3");

	// repeat off: plays to the end and stops; prev stays on the first
	CHECK(MuQueue_current(&q) == 0);
	CHECK(MuQueue_prev(&q) == 0);
	CHECK(MuQueue_next(&q, 0) == 1);
	CHECK(MuQueue_next(&q, 1) == 2);
	CHECK(MuQueue_next(&q, 0) == 3);
	CHECK(MuQueue_next(&q, 0) == -1);
	CHECK(MuQueue_next(&q, 1) == -1);
	CHECK(MuQueue_current(&q) == 3);

	// repeat all: round from the end, and back from the start
	q.repeat = MU_REPEAT_ALL;
	CHECK(MuQueue_next(&q, 0) == 0);
	CHECK(MuQueue_prev(&q) == 3);

	// repeat one: the same track at its end, the next on a press
	q.repeat = MU_REPEAT_ONE;
	CHECK(MuQueue_next(&q, 0) == 3);
	CHECK(MuQueue_next(&q, 1) == 0);
	CHECK(MuQueue_next(&q, 0) == 0);

	// shuffle: the current first, every track once, then back in order
	q.repeat = MU_REPEAT_OFF;
	MuQueue_next(&q, 1);
	CHECK(MuQueue_current(&q) == 1);
	MuQueue_setShuffle(&q, 1);
	CHECK(q.shuffle == 1);
	CHECK(q.pos == 0);
	CHECK(MuQueue_current(&q) == 1);
	int seen[4] = { 0 };
	seen[MuQueue_current(&q)]++;
	for (int i = 1; i < 4; i++) {
		int t = MuQueue_next(&q, 0);
		CHECK(t >= 0 && t < 4);
		if (t >= 0 && t < 4) seen[t]++;
	}
	for (int i = 0; i < 4; i++) CHECK(seen[i] == 1);
	CHECK(MuQueue_next(&q, 0) == -1);
	int last = MuQueue_current(&q);
	MuQueue_setShuffle(&q, 0);
	CHECK(MuQueue_current(&q) == last);
	CHECK(q.pos == last);

	// a new folder keeps shuffle on, starting from the chosen track
	MuQueue_setShuffle(&q, 1);
	CHECK(MuQueue_set(&q, "/N", &list, 4) == 0);
	CHECK(q.shuffle == 1);
	CHECK(MuQueue_current(&q) == 3);
	CHECK(q.pos == 0);

	// the same seed shuffles the same way
	MuQueue a, b;
	MuQueue_init(&a, 42);
	MuQueue_init(&b, 42);
	a.shuffle = b.shuffle = 1;
	MuQueue_set(&a, "/M", &list, 1);
	MuQueue_set(&b, "/M", &list, 1);
	for (int i = 0; i < 4; i++) CHECK(a.order[i] == b.order[i]);
	MuQueue_free(&a);
	MuQueue_free(&b);

	// no tracks
	MuList dirs = fakeList(2, 0);
	CHECK(MuQueue_set(&q, "/D", &dirs, 0) == -1);
	CHECK(q.count == 4); // kept what it had
	MU_listFree(&dirs);
	MuQueue_free(&q);
	CHECK(MuQueue_current(&q) == -1);
	MU_listFree(&list);

	CHECK(MU_parseRepeat("all") == MU_REPEAT_ALL);
	CHECK(MU_parseRepeat("one") == MU_REPEAT_ONE);
	CHECK(MU_parseRepeat("off") == MU_REPEAT_OFF);
	CHECK(MU_parseRepeat("x") == MU_REPEAT_OFF);
	CHECK(MU_parseRepeat(NULL) == MU_REPEAT_OFF);
	CHECK_STR(MU_repeatName(MU_REPEAT_ONE), "one");
}

static void testCommands(void) {
	char arg[64];
	CHECK(MU_parseCommand("status\n", arg, sizeof(arg)) == MU_CMD_STATUS);
	CHECK_STR(arg, "");
	CHECK(MU_parseCommand("play /Music/A b.mp3\r\n", arg, sizeof(arg)) == MU_CMD_PLAY);
	CHECK_STR(arg, "/Music/A b.mp3");
	CHECK(MU_parseCommand("seek -5000", arg, sizeof(arg)) == MU_CMD_SEEK);
	CHECK_STR(arg, "-5000");
	CHECK(MU_parseCommand("repeat all", arg, sizeof(arg)) == MU_CMD_REPEAT);
	CHECK(MU_parseCommand("quit", arg, sizeof(arg)) == MU_CMD_QUIT);
	CHECK(MU_parseCommand("hold", arg, sizeof(arg)) == MU_CMD_HOLD);
	CHECK(MU_parseCommand("release", arg, sizeof(arg)) == MU_CMD_RELEASE);
	CHECK(MU_parseCommand("sleep", arg, sizeof(arg)) == MU_CMD_SLEEP);
	CHECK(MU_parseCommand("statusx", arg, sizeof(arg)) == MU_CMD_NONE);
	CHECK(MU_parseCommand("stat", arg, sizeof(arg)) == MU_CMD_NONE);
	CHECK(MU_parseCommand("", arg, sizeof(arg)) == MU_CMD_NONE);
	CHECK(MU_parseCommand(NULL, arg, sizeof(arg)) == MU_CMD_NONE);
	char small[4];
	CHECK(MU_parseCommand("play /long/path.mp3", small, sizeof(small)) == MU_CMD_NONE);

	char line[128];
	CHECK(MU_formatCommand(MU_CMD_PLAY, "/Music/a.mp3", line, sizeof(line)) == 18);
	CHECK_STR(line, "play /Music/a.mp3\n");
	CHECK(MU_formatCommand(MU_CMD_TOGGLE, NULL, line, sizeof(line)) == 7);
	CHECK_STR(line, "toggle\n");
	CHECK(MU_formatCommand(MU_CMD_STATUS, "", line, sizeof(line)) == 7);
	CHECK(MU_formatCommand(MU_CMD_PLAY, "a\nquit", line, sizeof(line)) == -1);
	CHECK(MU_formatCommand(MU_CMD_PLAY, "a\r", line, sizeof(line)) == -1);
	CHECK(MU_formatCommand(99, NULL, line, sizeof(line)) == -1);
	CHECK(MU_formatCommand(MU_CMD_NONE, NULL, line, sizeof(line)) == -1);
	CHECK(MU_formatCommand(MU_CMD_PLAY, "/a/long/path", line, 8) == -1);
	// every command's line parses back to it
	for (int c = MU_CMD_STATUS; c <= MU_CMD_QUIT; c++) {
		CHECK(MU_formatCommand(c, "x y", line, sizeof(line)) > 0);
		CHECK(MU_parseCommand(line, arg, sizeof(arg)) == c);
		CHECK_STR(arg, "x y");
	}
}

static void testStatus(void) {
	MuStatus s = { 0 };
	s.state = MU_PLAYING;
	s.index = 2;
	s.count = 12;
	s.pos_ms = 65432;
	s.len_ms = 180000;
	s.shuffle = 1;
	s.repeat = MU_REPEAT_ALL;
	s.held = 0;
	snprintf(s.track, sizeof(s.track), "/Music/A\tB/track=1.mp3");
	char line[MU_LINE_MAX];
	CHECK(MU_formatStatus(&s, line, sizeof(line)) > 0);
	CHECK_STR(line, "ok\tstate=playing\tindex=2\tcount=12\tpos=65432\tlen=180000\tshuffle=1\trepeat=all\theld=0\t"
		"track=/Music/A\tB/track=1.mp3\n");
	MuStatus back;
	CHECK(MU_parseStatus(line, &back) == 1);
	CHECK(back.state == MU_PLAYING);
	CHECK(back.index == 2 && back.count == 12);
	CHECK(back.pos_ms == 65432 && back.len_ms == 180000);
	CHECK(back.shuffle == 1 && back.repeat == MU_REPEAT_ALL && back.held == 0);
	CHECK_STR(back.track, "/Music/A\tB/track=1.mp3");

	s.state = MU_PAUSED;
	s.held = 1;
	s.track[0] = '\0';
	MU_formatStatus(&s, line, sizeof(line));
	CHECK(MU_parseStatus(line, &back) == 1);
	CHECK(back.state == MU_PAUSED && back.held == 1);
	CHECK_STR(back.track, "");

	snprintf(s.track, sizeof(s.track), "a\nb");
	CHECK(MU_formatStatus(&s, line, sizeof(line)) == -1);
	s.track[0] = '\0';
	CHECK(MU_formatStatus(&s, line, 20) == -1);

	CHECK(MU_parseStatus("error no such file", &back) == 0);
	CHECK(MU_parseStatus("ok", &back) == 0);
	CHECK(MU_parseStatus("ok\tstate=playing\ttrack=x", &back) == 0); // fields missing
	CHECK(MU_parseStatus("ok\tstate=dancing\tindex=0\tcount=0\tpos=0\tlen=0\tshuffle=0\trepeat=off\theld=0\ttrack=", &back) == 0);
	CHECK(MU_parseStatus("ok\tstate=stopped\tindex=x\tcount=0\tpos=0\tlen=0\tshuffle=0\trepeat=off\theld=0\ttrack=", &back) == 0);
	CHECK(MU_parseStatus("ok\tstate=stopped\tindex=0\tcount=0\tpos=0\tlen=0\tshuffle=0\trepeat=off\theld=0\tvolume=3\ttrack=", &back) == 0);
	CHECK(MU_parseStatus("ok\tstate=stopped\tindex=0\tcount=0\tpos=0\tlen=0\tshuffle=0\trepeat=off\theld=0\ttrack=\n", &back) == 1);
	CHECK(back.state == MU_STOPPED);
	CHECK(MU_parseStatus(NULL, &back) == 0);
	CHECK_STR(MU_stateName(MU_PAUSED), "paused");
	CHECK_STR(MU_stateName(MU_STOPPED), "stopped");

	long long to = -1;
	CHECK(MU_parseSeek("+10000", 5000, 60000, &to) && to == 15000);
	CHECK(MU_parseSeek("-10000", 5000, 60000, &to) && to == 0);
	CHECK(MU_parseSeek("+10000", 55000, 60000, &to) && to == 60000);
	CHECK(MU_parseSeek("30000", 5000, 60000, &to) && to == 30000);
	CHECK(MU_parseSeek("90000", 5000, 60000, &to) && to == 60000);
	CHECK(MU_parseSeek("90000", 5000, 0, &to) && to == 90000); // length unknown
	CHECK(!MU_parseSeek("", 0, 0, &to));
	CHECK(!MU_parseSeek("+", 0, 0, &to));
	CHECK(!MU_parseSeek("5s", 0, 0, &to));
	CHECK(!MU_parseSeek(NULL, 0, 0, &to));

	char t[16];
	MU_formatTime(0, t, sizeof(t));
	CHECK_STR(t, "0:00");
	MU_formatTime(65999, t, sizeof(t));
	CHECK_STR(t, "1:05");
	MU_formatTime(3600000 + 61000, t, sizeof(t));
	CHECK_STR(t, "1:01:01");
	MU_formatTime(-5, t, sizeof(t));
	CHECK_STR(t, "0:00");
}

static void testOverlay(void) {
	CHECK(MU_parseOverlay("off") == 0);
	CHECK(MU_parseOverlay("5") == 5);
	CHECK(MU_parseOverlay("always") == MU_OVERLAY_ALWAYS);
	CHECK(MU_parseOverlay("x") == 0);
	CHECK(MU_parseOverlay(NULL) == 0);

	MuOverlay o;
	MuOverlay_init(&o, 5);
	MuStatus s = { 0 };
	CHECK(!MuOverlay_update(&o, &s, 0));
	CHECK(!MuOverlay_visible(&o, 0));
	s.state = MU_PLAYING;
	snprintf(s.track, sizeof(s.track), "/M/1.mp3");
	CHECK(MuOverlay_update(&o, &s, 1000));
	CHECK(MuOverlay_visible(&o, 1000));
	CHECK(MuOverlay_visible(&o, 5999));
	CHECK(!MuOverlay_update(&o, &s, 3000)); // the same track playing on
	CHECK(!MuOverlay_visible(&o, 6000));
	snprintf(s.track, sizeof(s.track), "/M/2.mp3");
	CHECK(MuOverlay_update(&o, &s, 7000)); // the next track
	CHECK(MuOverlay_visible(&o, 11999));
	s.state = MU_PAUSED;
	CHECK(!MuOverlay_update(&o, &s, 8000));
	CHECK(MuOverlay_visible(&o, 8000)); // paused, still in its time
	CHECK(!MuOverlay_visible(&o, 12000));
	s.state = MU_PLAYING;
	CHECK(MuOverlay_update(&o, &s, 20000)); // playing again
	CHECK(MuOverlay_visible(&o, 24000));
	s.state = MU_STOPPED;
	MuOverlay_update(&o, &s, 21000);
	CHECK(!MuOverlay_visible(&o, 21000));

	MuOverlay always;
	MuOverlay_init(&always, MU_OVERLAY_ALWAYS);
	s.state = MU_PAUSED;
	MuOverlay_update(&always, &s, 0);
	CHECK(MuOverlay_visible(&always, 999999));
	s.state = MU_STOPPED;
	MuOverlay_update(&always, &s, 0);
	CHECK(!MuOverlay_visible(&always, 0));

	MuOverlay off;
	MuOverlay_init(&off, 0);
	s.state = MU_PLAYING;
	MuOverlay_update(&off, &s, 0);
	CHECK(!MuOverlay_visible(&off, 0));
}

static void testUnlock(void) {
	const unsigned int SEL = 1u << 9, START = 1u << 8, A = 1u;
	MuUnlock u = { 0 };
	CHECK(!MuUnlock_frame(&u, START, START, SEL, START, 0));    // START alone
	CHECK(!MuUnlock_frame(&u, SEL, SEL, SEL, START, 100));      // SELECT, then
	CHECK(MuUnlock_frame(&u, START, START, SEL, START, 900));   // START within a second
	CHECK(!MuUnlock_frame(&u, START, START, SEL, START, 950));  // used up
	CHECK(!MuUnlock_frame(&u, SEL, SEL, SEL, START, 2000));
	CHECK(!MuUnlock_frame(&u, START, START, SEL, START, 3001)); // too late
	CHECK(!MuUnlock_frame(&u, SEL, SEL, SEL, START, 4000));
	CHECK(!MuUnlock_frame(&u, A, A, SEL, START, 4100));         // another button in between
	CHECK(!MuUnlock_frame(&u, START, START, SEL, START, 4200));
	CHECK(MuUnlock_frame(&u, START, START | SEL, SEL, START, 9000)); // START while SELECT is held
	CHECK(MuUnlock_frame(&u, START | SEL, START | SEL, SEL, START, 9500)); // both at once
}

static void testList(void) {
	CHECK(MU_move(0, 5, 1, 1) == 1);
	CHECK(MU_move(4, 5, 1, 1) == 0);  // round on a press
	CHECK(MU_move(4, 5, 1, 0) == 4);  // not while held
	CHECK(MU_move(0, 5, -1, 1) == 4);
	CHECK(MU_move(0, 5, -1, 0) == 0);
	CHECK(MU_move(1, 5, -5, 1) == 0); // a page stops at the end
	CHECK(MU_move(3, 5, 5, 1) == 4);
	CHECK(MU_move(0, 0, 1, 1) == 0);

	CHECK(MU_window(0, 3, 5, 0) == 0);
	CHECK(MU_window(7, 20, 5, 0) == 3);
	CHECK(MU_window(4, 20, 5, 3) == 3);
	CHECK(MU_window(2, 20, 5, 3) == 2);
	CHECK(MU_window(19, 20, 5, 0) == 15);
	CHECK(MU_window(10, 12, 5, 11) == 7);
	CHECK(MU_window(3, 20, 0, 9) == 0);
}

static void testPaths(void) {
	char out[64];
	CHECK(MU_childPath("/M", "a.mp3", out, sizeof(out)));
	CHECK_STR(out, "/M/a.mp3");
	CHECK(!MU_childPath("/M", "a long name.mp3", out, 8));
	CHECK(MU_parentPath("/sd/Music", "/sd/Music/A/B", out, sizeof(out)));
	CHECK_STR(out, "/sd/Music/A");
	CHECK(MU_parentPath("/sd/Music", "/sd/Music/A", out, sizeof(out)));
	CHECK_STR(out, "/sd/Music");
	CHECK(!MU_parentPath("/sd/Music", "/sd/Music", out, sizeof(out)));
	CHECK(!MU_parentPath("/sd/Music", "/sd/Musicals/A", out, sizeof(out)));
	CHECK(!MU_parentPath("/sd/Music", "/sd", out, sizeof(out)));
	CHECK(!MU_parentPath("/sd/Music", "/sd/Music/", out, sizeof(out)));
}

int main(void) {
	testKinds();
	testCompare();
	testScan();
	testQueue();
	testCommands();
	testStatus();
	testOverlay();
	testUnlock();
	testList();
	testPaths();
	CHECK_DONE();
}
