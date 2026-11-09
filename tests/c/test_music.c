// The music plugin's model: which files are tracks and how they sort,
// listing a folder, the queue with shuffle and repeat, its resume file,
// the socket protocol's commands and status line, and the plugin's pill
// timing, unlock press and folder browsing.

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

static void writeText(const char* path, const char* text) {
	FILE* f = fopen(path, "w");
	if (!f) return;
	fputs(text, f);
	fclose(f);
}

// a test's exists: every path but the ones with "gone" in them
static int notGone(const char* path, void* data) {
	(*(int*)data)++;
	return strstr(path, "gone") == NULL;
}

static void testResume(void) {
	char root[] = "/tmp/test_music_resume.XXXXXX";
	CHECK(mkdtemp(root) != NULL);
	char file[MU_PATH_MAX], tmp[MU_PATH_MAX + 8];
	snprintf(file, sizeof(file), "%s/" MU_RESUME_NAME, root);
	snprintf(tmp, sizeof(tmp), "%s.tmp", file);

	// a shuffled queue written and read back as it was
	MuList list = fakeList(0, 5);
	MuQueue q;
	MuQueue_init(&q, 3);
	q.shuffle = 1;
	q.repeat = MU_REPEAT_ALL;
	CHECK(MuQueue_set(&q, "/M", &list, 2) == 0);
	MuQueue_next(&q, 1);
	MuQueue_next(&q, 1);
	int current = MuQueue_current(&q);
	CHECK(MU_writeResume(file, &q, 12345, 40000) == 0);
	CHECK(access(tmp, F_OK) != 0);
	MuQueue r;
	MuQueue_init(&r, 9);
	long long pos = -1, len = -1;
	CHECK(MU_readResume(file, &r, &pos, &len) == 1);
	CHECK(pos == 12345 && len == 40000);
	CHECK(r.count == 5 && r.pos == q.pos && r.shuffle == 1 && r.repeat == MU_REPEAT_ALL && r.seed == 9);
	for (int i = 0; i < 5 && r.count == 5; i++) {
		CHECK(r.order[i] == q.order[i]);
		CHECK_STR(r.paths[i], q.paths[i]);
	}
	CHECK(MuQueue_current(&r) == current);
	// and goes on in the same order
	CHECK(MuQueue_next(&r, 1) == MuQueue_next(&q, 1));

	// gone tracks are dropped, keeping the order and the current track
	snprintf(r.paths[r.order[0]], MU_PATH_MAX, "/M/gone a.mp3");
	snprintf(r.paths[r.order[4]], MU_PATH_MAX, "/M/gone b.mp3");
	r.pos = 2;
	int before[5];
	for (int i = 0; i < 5; i++) before[i] = r.order[i];
	char want[MU_PATH_MAX];
	snprintf(want, sizeof(want), "%s", r.paths[MuQueue_current(&r)]);
	int asked = 0;
	CHECK(MuQueue_dropMissing(&r, notGone, &asked) == 3);
	CHECK(asked == 5);
	CHECK(r.count == 3 && r.pos == 1);
	CHECK_STR(r.paths[MuQueue_current(&r)], want);
	for (int i = 0; i < 3 && r.count == 3; i++) {
		CHECK(r.order[i] >= 0 && r.order[i] < 3);
		CHECK(strstr(r.paths[r.order[i]], "gone") == NULL);
	}
	if (r.count == 3) CHECK(!strcmp(r.paths[r.order[0]], q.paths[before[1]]) && !strcmp(r.paths[r.order[2]], q.paths[before[3]]));
	// and the current one gone drops it all
	snprintf(r.paths[MuQueue_current(&r)], MU_PATH_MAX, "/M/gone c.mp3");
	CHECK(MuQueue_dropMissing(&r, notGone, &asked) == -1);
	CHECK(r.count == 0 && MuQueue_current(&r) == -1);
	CHECK(MuQueue_dropMissing(&r, notGone, &asked) == -1);

	// MU_isFile
	CHECK(MU_isFile(file, NULL));
	CHECK(!MU_isFile(root, NULL));
	CHECK(!MU_isFile("/no/such/file.mp3", NULL));

	// a place past the end is kept at the end; nothing to write is refused
	CHECK(MU_writeResume(file, &q, 50000, 40000) == 0);
	CHECK(MU_readResume(file, &r, &pos, &len) == 1 && pos == 40000);
	MuQueue empty;
	MuQueue_init(&empty, 1);
	CHECK(MU_writeResume(file, &empty, 0, 0) == -1);
	CHECK(MU_writeResume(file, &q, -1, 0) == -1);
	CHECK(MU_readResume(file, &r, &pos, &len) == 1); // left as it was
	snprintf(q.paths[0], MU_PATH_MAX, "relative.mp3");
	CHECK(MU_writeResume(file, &q, 0, 0) == -1);
	char missing[MU_PATH_MAX + 16];
	snprintf(missing, sizeof(missing), "%s/no/such/" MU_RESUME_NAME, root);
	CHECK(MU_writeResume(missing, &r, 0, 0) == -1);
	CHECK(MU_readResume(missing, &r, &pos, &len) == 0);
	CHECK(r.count == 0 && pos == 0 && len == 0);

	// what a whole file looks like
	const char* good = MU_RESUME_HEADER "\npos=5\nlen=9\nindex=1\nshuffle=1\nrepeat=one\ncount=3\norder=2,0,1\n"
		"track=/M/a.mp3\npath=/M/a.mp3\npath=/M/b.mp3\npath=/M/c.mp3\nend\n";
	writeText(file, good);
	CHECK(MU_readResume(file, &r, &pos, &len) == 1);
	CHECK(r.count == 3 && r.pos == 1 && MuQueue_current(&r) == 0 && r.repeat == MU_REPEAT_ONE && pos == 5 && len == 9);
	// the end line without its line break, and the fields in another order
	writeText(file, MU_RESUME_HEADER "\ntrack=/M/b.mp3\norder=0,1\nrepeat=off\ncount=2\nshuffle=0\nindex=1\nlen=0\npos=0\n"
		"path=/M/a.mp3\npath=/M/b.mp3\nend");
	CHECK(MU_readResume(file, &r, &pos, &len) == 1);
	CHECK(r.count == 2 && MuQueue_current(&r) == 1);

	// malformed and cut off files are refused, leaving the queue empty
	static const char* const bad[] = {
		"",
		MU_RESUME_HEADER,
		"music-resume 2\npos=5\nlen=9\nindex=1\nshuffle=1\nrepeat=one\ncount=3\norder=2,0,1\n"
		"track=/M/a.mp3\npath=/M/a.mp3\npath=/M/b.mp3\npath=/M/c.mp3\nend\n",
		// cut off in the fields, the paths and the end line
		MU_RESUME_HEADER "\npos=5\nlen=9\nindex=1\nshuffle=1\nrep",
		MU_RESUME_HEADER "\npos=5\nlen=9\nindex=1\nshuffle=1\nrepeat=one\ncount=3\norder=2,0,1\n"
		"track=/M/a.mp3\npath=/M/a.mp3\npath=/M/b",
		MU_RESUME_HEADER "\npos=5\nlen=9\nindex=1\nshuffle=1\nrepeat=one\ncount=3\norder=2,0,1\n"
		"track=/M/a.mp3\npath=/M/a.mp3\npath=/M/b.mp3\npath=/M/c.mp3\n",
		MU_RESUME_HEADER "\npos=5\nlen=9\nindex=1\nshuffle=1\nrepeat=one\ncount=3\norder=2,0,1\n"
		"track=/M/a.mp3\npath=/M/a.mp3\npath=/M/b.mp3\npath=/M/c.mp3\nen",
		// a field missing, twice, unknown or out of range
		MU_RESUME_HEADER "\nlen=9\nindex=1\nshuffle=1\nrepeat=one\ncount=3\norder=2,0,1\n"
		"track=/M/a.mp3\npath=/M/a.mp3\npath=/M/b.mp3\npath=/M/c.mp3\nend\n",
		MU_RESUME_HEADER "\npos=5\npos=6\nlen=9\nindex=1\nshuffle=1\nrepeat=one\ncount=3\norder=2,0,1\n"
		"track=/M/a.mp3\npath=/M/a.mp3\npath=/M/b.mp3\npath=/M/c.mp3\nend\n",
		MU_RESUME_HEADER "\nvolume=3\npos=5\nlen=9\nindex=1\nshuffle=1\nrepeat=one\ncount=3\norder=2,0,1\n"
		"track=/M/a.mp3\npath=/M/a.mp3\npath=/M/b.mp3\npath=/M/c.mp3\nend\n",
		MU_RESUME_HEADER "\npos=-5\nlen=9\nindex=1\nshuffle=1\nrepeat=one\ncount=3\norder=2,0,1\n"
		"track=/M/a.mp3\npath=/M/a.mp3\npath=/M/b.mp3\npath=/M/c.mp3\nend\n",
		MU_RESUME_HEADER "\npos=5\nlen=9\nindex=3\nshuffle=1\nrepeat=one\ncount=3\norder=2,0,1\n"
		"track=/M/a.mp3\npath=/M/a.mp3\npath=/M/b.mp3\npath=/M/c.mp3\nend\n",
		MU_RESUME_HEADER "\npos=5\nlen=9\nindex=1\nshuffle=2\nrepeat=one\ncount=3\norder=2,0,1\n"
		"track=/M/a.mp3\npath=/M/a.mp3\npath=/M/b.mp3\npath=/M/c.mp3\nend\n",
		MU_RESUME_HEADER "\npos=5\nlen=9\nindex=1\nshuffle=1\nrepeat=twice\ncount=3\norder=2,0,1\n"
		"track=/M/a.mp3\npath=/M/a.mp3\npath=/M/b.mp3\npath=/M/c.mp3\nend\n",
		MU_RESUME_HEADER "\npos=5\nlen=9\nindex=0\nshuffle=0\nrepeat=off\ncount=0\norder=\ntrack=/M/a.mp3\nend\n",
		// the order not a shuffle of the tracks
		MU_RESUME_HEADER "\npos=5\nlen=9\nindex=1\nshuffle=1\nrepeat=one\ncount=3\norder=2,0,0\n"
		"track=/M/a.mp3\npath=/M/a.mp3\npath=/M/b.mp3\npath=/M/c.mp3\nend\n",
		MU_RESUME_HEADER "\npos=5\nlen=9\nindex=1\nshuffle=1\nrepeat=one\ncount=3\norder=2,0\n"
		"track=/M/a.mp3\npath=/M/a.mp3\npath=/M/b.mp3\npath=/M/c.mp3\nend\n",
		MU_RESUME_HEADER "\npos=5\nlen=9\nindex=1\nshuffle=1\nrepeat=one\ncount=3\norder=2,0,1,0\n"
		"track=/M/a.mp3\npath=/M/a.mp3\npath=/M/b.mp3\npath=/M/c.mp3\nend\n",
		MU_RESUME_HEADER "\npos=5\nlen=9\nindex=1\nshuffle=1\nrepeat=one\ncount=3\norder=2,0,3\n"
		"track=/M/a.mp3\npath=/M/a.mp3\npath=/M/b.mp3\npath=/M/c.mp3\nend\n",
		MU_RESUME_HEADER "\npos=5\nlen=9\nindex=1\nshuffle=1\nrepeat=one\ncount=3\norder=2,,0\n"
		"track=/M/a.mp3\npath=/M/a.mp3\npath=/M/b.mp3\npath=/M/c.mp3\nend\n",
		MU_RESUME_HEADER "\npos=5\nlen=9\nindex=1\nshuffle=1\nrepeat=one\ncount=3\norder=99999999999999999999,0,1\n"
		"track=/M/a.mp3\npath=/M/a.mp3\npath=/M/b.mp3\npath=/M/c.mp3\nend\n",
		// the track isn't the one at index, a path isn't a full one, too few or too many paths
		MU_RESUME_HEADER "\npos=5\nlen=9\nindex=1\nshuffle=1\nrepeat=one\ncount=3\norder=2,0,1\n"
		"track=/M/b.mp3\npath=/M/a.mp3\npath=/M/b.mp3\npath=/M/c.mp3\nend\n",
		MU_RESUME_HEADER "\npos=5\nlen=9\nindex=1\nshuffle=1\nrepeat=one\ncount=3\norder=2,0,1\n"
		"track=/M/a.mp3\npath=/M/a.mp3\npath=b.mp3\npath=/M/c.mp3\nend\n",
		MU_RESUME_HEADER "\npos=5\nlen=9\nindex=1\nshuffle=1\nrepeat=one\ncount=3\norder=2,0,1\n"
		"track=/M/a.mp3\npath=/M/a.mp3\npath=/M/b.mp3\nend\n",
		MU_RESUME_HEADER "\npos=5\nlen=9\nindex=1\nshuffle=1\nrepeat=one\ncount=3\norder=2,0,1\n"
		"track=/M/a.mp3\npath=/M/a.mp3\npath=/M/b.mp3\npath=/M/c.mp3\npath=/M/d.mp3\nend\n",
		// no paths at all, or a line with no key
		MU_RESUME_HEADER "\npos=5\nlen=9\nindex=1\nshuffle=1\nrepeat=one\ncount=3\norder=2,0,1\ntrack=/M/a.mp3\nend\n",
		MU_RESUME_HEADER "\npos=5\nlen=9\nindex=1\nshuffle=1\nrepeat=one\ncount=3\norder=2,0,1\nnothing\n"
		"track=/M/a.mp3\npath=/M/a.mp3\npath=/M/b.mp3\npath=/M/c.mp3\nend\n",
	};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		writeText(file, good);
		CHECK(MU_readResume(file, &r, &pos, &len) == 1);
		writeText(file, bad[i]);
		if (MU_readResume(file, &r, &pos, &len) != 0) fprintf(stderr, "bad resume file %zu was read\n", i);
		CHECK(r.count == 0 && r.paths == NULL && r.order == NULL);
	}
	// a line longer than any it writes
	FILE* f = fopen(file, "w");
	if (f) {
		fprintf(f, "%s\npos=", MU_RESUME_HEADER);
		for (int i = 0; i < 20000; i++) fputc('1', f);
		fputs("\n", f);
		fclose(f);
	}
	CHECK(MU_readResume(file, &r, &pos, &len) == 0);

	MuQueue_free(&q);
	MuQueue_free(&r);
	MU_listFree(&list);
	char cmd[MU_PATH_MAX * 2];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
	CHECK(system(cmd) == 0);
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
	CHECK(MU_parseCommand("restore", arg, sizeof(arg)) == MU_CMD_RESTORE);
	for (int c = MU_CMD_STATUS; c <= MU_CMD_RESTORE; c++) {
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
	s.state = MU_PAUSED;
	MuOverlay_show(&off, &s, 0);
	CHECK(!MuOverlay_visible(&off, 0));

	// a paused track restored at start shows for its time, then playing on starts it again
	MuOverlay restored;
	MuOverlay_init(&restored, 5);
	s.state = MU_PAUSED;
	snprintf(s.track, sizeof(s.track), "/M/1.mp3");
	MuOverlay_show(&restored, &s, 100000);
	CHECK(MuOverlay_visible(&restored, 104999));
	CHECK(!MuOverlay_update(&restored, &s, 101000));
	CHECK(!MuOverlay_visible(&restored, 105000));
	s.state = MU_PLAYING;
	CHECK(MuOverlay_update(&restored, &s, 200000));
	CHECK(MuOverlay_visible(&restored, 204000));
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
	testResume();
	testCommands();
	testStatus();
	testOverlay();
	testUnlock();
	testList();
	testPaths();
	CHECK_DONE();
}
