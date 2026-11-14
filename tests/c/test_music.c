// The music plugin's model: which files are tracks and how they sort,
// listing a folder, the queue with shuffle and repeat, its resume file,
// the socket protocol's commands and status line, and the plugin's pill
// timing and folder browsing; and its tag readers.

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"
#include "../../plugins/music/music_model.h"
#include "../../plugins/music/music_tags.h"

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

///////////////////////////////////////
// tags

// a growing buffer of test bytes
typedef struct Buf {
	unsigned char data[1 << 16];
	size_t len;
} Buf;

static void add(Buf* b, const void* p, size_t n) {
	memcpy(b->data + b->len, p, n);
	b->len += n;
}

static void addStr(Buf* b, const char* s) {
	add(b, s, strlen(s));
}

static void addByte(Buf* b, int c) {
	b->data[b->len++] = (unsigned char)c;
}

static void addBe32(Buf* b, unsigned int v) {
	addByte(b, (int)(v >> 24));
	addByte(b, (int)(v >> 16 & 0xFF));
	addByte(b, (int)(v >> 8 & 0xFF));
	addByte(b, (int)(v & 0xFF));
}

static void addLe32(Buf* b, unsigned int v) {
	addByte(b, (int)(v & 0xFF));
	addByte(b, (int)(v >> 8 & 0xFF));
	addByte(b, (int)(v >> 16 & 0xFF));
	addByte(b, (int)(v >> 24));
}

static void addSyncsafe(Buf* b, unsigned int v) {
	addByte(b, (int)(v >> 21 & 0x7F));
	addByte(b, (int)(v >> 14 & 0x7F));
	addByte(b, (int)(v >> 7 & 0x7F));
	addByte(b, (int)(v & 0x7F));
}

static const unsigned char jpeg[] = { 0xFF, 0xD8, 0xFF, 0xE0, 1, 2, 3, 4 };
static const unsigned char png[] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 9, 9 };

static int readBuf(const Buf* b, int kind, int want_picture, MuTags* tags) {
	MuSource src;
	MuMemory mem;
	MU_memorySource(&src, &mem, b->data, b->len);
	return MU_readTags(&src, kind, want_picture, tags);
}

static void testTagText(void) {
	char out[64];
	CHECK(MU_tagText(0, (const unsigned char*)"Caf\xe9", 4, out, sizeof(out)) == 5);
	CHECK_STR(out, "Caf\xc3\xa9");
	MU_tagText(0, (const unsigned char*)"  a\tb\n\x01 c   \0junk", 17, out, sizeof(out));
	CHECK_STR(out, "a b c");
	// UTF-16 with either byte order mark, without one (little-endian), and big-endian
	MU_tagText(1, (const unsigned char*)"\xFF\xFEH\0i\0", 6, out, sizeof(out));
	CHECK_STR(out, "Hi");
	MU_tagText(1, (const unsigned char*)"\xFE\xFF\0H\0i", 6, out, sizeof(out));
	CHECK_STR(out, "Hi");
	MU_tagText(1, (const unsigned char*)"H\0i\0", 4, out, sizeof(out));
	CHECK_STR(out, "Hi");
	MU_tagText(2, (const unsigned char*)"\0H\0i\0\0\0x", 8, out, sizeof(out));
	CHECK_STR(out, "Hi");
	// a surrogate pair, a lone one and an odd last byte
	MU_tagText(1, (const unsigned char*)"\xFF\xFE\x3D\xD8\x00\xDE", 6, out, sizeof(out));
	CHECK_STR(out, "\xF0\x9F\x98\x80");
	MU_tagText(1, (const unsigned char*)"\xFF\xFE\x3D\xD8" "a\0", 6, out, sizeof(out));
	CHECK_STR(out, "\xEF\xBF\xBD" "a");
	MU_tagText(1, (const unsigned char*)"\xFF\xFE" "a\0b", 5, out, sizeof(out));
	CHECK_STR(out, "a");
	// UTF-8: bad bytes, an overlong, a surrogate, a cut sequence, C1 controls
	MU_tagText(3, (const unsigned char*)"a\xFF" "b\xC0\xAF" "c\xED\xA0\x80" "d\xE2\x82", 13, out, sizeof(out));
	CHECK_STR(out, "a\xEF\xBF\xBD" "b\xEF\xBF\xBD\xEF\xBF\xBD" "c\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD" "d\xEF\xBF\xBD\xEF\xBF\xBD");
	MU_tagText(3, (const unsigned char*)"x\xC2\x85y\xE2\x82\xAC", 7, out, sizeof(out));
	CHECK_STR(out, "xy\xE2\x82\xAC");
	// cut at a character's end
	CHECK(MU_tagText(3, (const unsigned char*)"ab\xE2\x82\xAC", 5, out, 5) == 2);
	CHECK_STR(out, "ab");
	CHECK(MU_tagText(3, (const unsigned char*)"ab\xE2\x82\xAC", 5, out, 6) == 5);
	CHECK(MU_tagText(9, (const unsigned char*)"ab", 2, out, sizeof(out)) == 0);
	CHECK(MU_tagText(0, NULL, 4, out, sizeof(out)) == 0);

	unsigned char raw[16];
	CHECK(MU_base64Decode("aGVsbG8=", 8, raw, sizeof(raw)) == 5 && !memcmp(raw, "hello", 5));
	CHECK(MU_base64Decode("aGVs\nbG8", 8, raw, sizeof(raw)) == 5);
	CHECK(MU_base64Decode("aGVsbG8=", 8, raw, 4) == -1);
	CHECK(MU_base64Decode("aG!s", 4, raw, sizeof(raw)) == -1);
	CHECK(MU_base64Decode("aG=s", 4, raw, sizeof(raw)) == -1);

	CHECK(MU_pictureKind(jpeg, sizeof(jpeg)) == MU_PICTURE_JPEG);
	CHECK(MU_pictureKind(png, sizeof(png)) == MU_PICTURE_PNG);
	CHECK(MU_pictureKind(png, 4) == MU_PICTURE_NONE);
	CHECK_STR(MU_pictureExt(MU_PICTURE_PNG), "png");
}

// An ID3v2.3 or 2.4 frame.
static void id3Frame(Buf* b, int ver, const char* id, int flags, const void* body, size_t len) {
	addStr(b, id);
	if (ver == 4) addSyncsafe(b, (unsigned int)len);
	else addBe32(b, (unsigned int)len);
	addByte(b, flags >> 8);
	addByte(b, flags & 0xFF);
	add(b, body, len);
}

static void id3Header(Buf* b, int ver, int flags, unsigned int size) {
	addStr(b, "ID3");
	addByte(b, ver);
	addByte(b, 0);
	addByte(b, flags);
	addSyncsafe(b, size);
}

// Puts a tag's size in its header, once its frames are in.
static void id3Size(Buf* b, size_t start) {
	unsigned int size = (unsigned int)(b->len - start - 10);
	b->data[start + 6] = (unsigned char)(size >> 21 & 0x7F);
	b->data[start + 7] = (unsigned char)(size >> 14 & 0x7F);
	b->data[start + 8] = (unsigned char)(size >> 7 & 0x7F);
	b->data[start + 9] = (unsigned char)(size & 0x7F);
}

static void testId3(void) {
	static Buf b;
	MuTags t;

	// 2.3: Latin-1 and UTF-16 text, a picture that isn't the cover then the cover
	b.len = 0;
	id3Header(&b, 3, 0, 0);
	id3Frame(&b, 3, "TIT2", 0, "\0Caf\xe9", 5);
	id3Frame(&b, 3, "TPE1", 0, "\x01\xFF\xFEW\0h\0o\0", 9);
	id3Frame(&b, 3, "TALB", 0, "\0LP", 3);
	id3Frame(&b, 3, "TXXX", 0, "\0x", 2);
	{
		Buf* pic = malloc(sizeof(Buf));
		pic->len = 0;
		addStr(pic, "\x01image/png");
		addByte(pic, 0);
		addByte(pic, 4); // the back cover
		add(pic, "\xFF\xFEx\0\0\0", 6);
		add(pic, png, sizeof(png));
		id3Frame(&b, 3, "APIC", 0, pic->data, pic->len);
		pic->len = 0;
		add(pic, "\0image/jpeg", 11);
		addByte(pic, 0);
		addByte(pic, 3);
		addStr(pic, "front");
		addByte(pic, 0);
		add(pic, jpeg, sizeof(jpeg));
		id3Frame(&b, 3, "APIC", 0, pic->data, pic->len);
		free(pic);
	}
	add(&b, "\0\0\0\0\0\0\0\0\0\0\0\0", 12); // padding
	id3Size(&b, 0);
	add(&b, "audio", 5);
	CHECK(readBuf(&b, MU_KIND_MP3, 1, &t) == 1);
	CHECK_STR(t.title, "Caf\xc3\xa9");
	CHECK_STR(t.artist, "Who");
	CHECK_STR(t.album, "LP");
	CHECK(t.picture_kind == MU_PICTURE_JPEG && t.picture_front && t.picture_size == sizeof(jpeg));
	CHECK(t.picture && !memcmp(t.picture, jpeg, sizeof(jpeg)));
	MuTags_free(&t);
	// without the picture
	CHECK(readBuf(&b, MU_KIND_MP3, 0, &t) == 1 && !t.picture);
	CHECK_STR(t.title, "Caf\xc3\xa9");
	MuTags_free(&t);
	// a frame running past the tag ends it: here the first, so nothing is read
	b.data[10 + 6] = 0x7F;
	CHECK(readBuf(&b, MU_KIND_MP3, 1, &t) == 0);
	MuTags_free(&t);
	// and the second, keeping the first
	b.data[10 + 6] = 0;
	b.data[10 + 10 + 5 + 6] = 0x7F;
	CHECK(readBuf(&b, MU_KIND_MP3, 1, &t) == 1 && !t.artist[0] && !t.picture);
	CHECK_STR(t.title, "Caf\xc3\xa9");
	MuTags_free(&t);
	// a tag running past the file is refused
	b.len = 40;
	CHECK(readBuf(&b, MU_KIND_MP3, 1, &t) == 0 && !t.title[0]);
	MuTags_free(&t);

	// 2.4: UTF-8 and UTF-16BE, several strings, an extended header, a frame
	// of its own unsynchronised with a data length, TPE2 for the artist
	b.len = 0;
	id3Header(&b, 4, 0x40, 0);
	addSyncsafe(&b, 6);
	add(&b, "\x01\0", 2);
	id3Frame(&b, 4, "TIT2", 0, "\x03" "\xE2\x82\xAC" "uro\0Second", 14);
	id3Frame(&b, 4, "TPE2", 0, "\x02\0B\0a\0n\0d", 9);
	{
		unsigned char body[] = { 0, 0, 0, 4, 0x03, 'A', 0xFF, 0x00 };
		id3Frame(&b, 4, "TALB", 0x0003, body, sizeof(body));
	}
	id3Size(&b, 0);
	CHECK(readBuf(&b, MU_KIND_MP3, 0, &t) == 1);
	CHECK_STR(t.title, "\xE2\x82\xACuro");
	CHECK_STR(t.artist, "Band");
	CHECK_STR(t.album, "A\xEF\xBF\xBD");
	MuTags_free(&t);

	// 2.3, the whole tag unsynchronised, with an extended header
	b.len = 0;
	id3Header(&b, 3, 0xC0, 0);
	addBe32(&b, 6);
	add(&b, "\0\0\0\0\0\0", 6);
	// TIT2, 4 bytes: 0 'x' 0xFF 'y', written with 0xFF 0x00 before the 'y'
	add(&b, "TIT2\0\0\0\x04\0\0\0x\xFF\0y", 15);
	id3Size(&b, 0);
	CHECK(readBuf(&b, MU_KIND_MP3, 0, &t) == 1);
	CHECK_STR(t.title, "x\xC3\xBFy");
	MuTags_free(&t);
	// an extended header past the tag
	b.data[13] = 0x7F;
	CHECK(readBuf(&b, MU_KIND_MP3, 0, &t) == 0);
	MuTags_free(&t);

	// 2.2, with PIC, and compressed frames skipped in 2.3
	b.len = 0;
	id3Header(&b, 2, 0, 0);
	add(&b, "TT2\0\0\x04\0Two", 10);
	add(&b, "TP1\0\0\x03\0Me", 9);
	{
		unsigned char pic[64];
		size_t n = 0;
		memcpy(pic, "\0PNG\x03\0", 6);
		n = 6;
		memcpy(pic + n, png, sizeof(png));
		n += sizeof(png);
		add(&b, "PIC\0\0", 5);
		addByte(&b, (int)n);
		add(&b, pic, n);
	}
	id3Size(&b, 0);
	CHECK(readBuf(&b, MU_KIND_MP3, 1, &t) == 1);
	CHECK_STR(t.title, "Two");
	CHECK_STR(t.artist, "Me");
	CHECK(t.picture_kind == MU_PICTURE_PNG);
	MuTags_free(&t);
	b.len = 0;
	id3Header(&b, 3, 0, 0);
	id3Frame(&b, 3, "TIT2", 0x0080, "\0\0\0\x09zzz", 8);
	id3Size(&b, 0);
	CHECK(readBuf(&b, MU_KIND_MP3, 0, &t) == 0);
	MuTags_free(&t);

	// ID3v1 at the end, for what ID3v2 doesn't have
	b.len = 0;
	id3Header(&b, 3, 0, 0);
	id3Frame(&b, 3, "TIT2", 0, "\0New", 4);
	id3Size(&b, 0);
	add(&b, "audio", 5);
	{
		unsigned char v1[128];
		memset(v1, 0, sizeof(v1));
		memcpy(v1, "TAGOld title", 12);
		memcpy(v1 + 33, "Old artist    ", 14);
		memcpy(v1 + 63, "Old \xe9", 5);
		add(&b, v1, sizeof(v1));
	}
	CHECK(readBuf(&b, MU_KIND_MP3, 0, &t) == 1);
	CHECK_STR(t.title, "New");
	CHECK_STR(t.artist, "Old artist");
	CHECK_STR(t.album, "Old \xC3\xA9");
	MuTags_free(&t);
	// nothing at all
	b.len = 0;
	add(&b, "\xFF\xFB\x90\x00", 4);
	CHECK(readBuf(&b, MU_KIND_MP3, 1, &t) == 0);
	CHECK(readBuf(&b, MU_KIND_WAV, 1, &t) == 0);
	CHECK(readBuf(&b, MU_KIND_FLAC, 1, &t) == 0);
	MuTags_free(&t);
}

// A Vorbis comment header's body.
static void vorbisComment(Buf* b, const char** comments, int count) {
	addLe32(b, 6);
	addStr(b, "vendor");
	addLe32(b, (unsigned int)count);
	for (int i = 0; i < count; i++) {
		addLe32(b, (unsigned int)strlen(comments[i]));
		addStr(b, comments[i]);
	}
}

static void flacPictureBody(Buf* b, unsigned int type, const unsigned char* data, size_t len) {
	addBe32(b, type);
	addBe32(b, 9);
	addStr(b, "image/png");
	addBe32(b, 0);
	for (int i = 0; i < 4; i++) addBe32(b, 1);
	addBe32(b, (unsigned int)len);
	add(b, data, len);
}

static void flacBlock(Buf* b, int type, int last, const Buf* body) {
	addByte(b, (last ? 0x80 : 0) | type);
	addByte(b, (int)(body->len >> 16));
	addByte(b, (int)(body->len >> 8 & 0xFF));
	addByte(b, (int)(body->len & 0xFF));
	add(b, body->data, body->len);
}

static void testFlac(void) {
	static Buf b, body;
	MuTags t;
	const char* comments[] = { "tracknumber=1", "title=Fl\xC3\xA9", "ALBUMARTIST=Group", "Album=Disc", "=", "TITLE=Second" };
	b.len = 0;
	addStr(&b, "fLaC");
	body.len = 0;
	add(&body, "streaminfo........................", 34);
	flacBlock(&b, 0, 0, &body);
	body.len = 0;
	vorbisComment(&body, comments, 6);
	flacBlock(&b, 4, 0, &body);
	body.len = 0;
	flacPictureBody(&body, 3, png, sizeof(png));
	flacBlock(&b, 6, 1, &body);
	add(&b, "frames", 6);
	CHECK(readBuf(&b, MU_KIND_FLAC, 1, &t) == 1);
	CHECK_STR(t.title, "Fl\xC3\xA9");
	CHECK_STR(t.artist, "Group");
	CHECK_STR(t.album, "Disc");
	CHECK(t.picture_kind == MU_PICTURE_PNG && t.picture_size == sizeof(png));
	MuTags_free(&t);

	// behind an ID3v2 tag some writers put first
	static Buf with;
	with.len = 0;
	id3Header(&with, 3, 0, 4);
	add(&with, "\0\0\0\0", 4);
	add(&with, b.data, b.len);
	CHECK(readBuf(&with, MU_KIND_FLAC, 0, &t) == 1);
	CHECK_STR(t.title, "Fl\xC3\xA9");
	MuTags_free(&t);

	// a comment count far past the block, and a comment length past it
	size_t count_at = 4 + 4 + 34 + 4 + 4 + 6;
	b.data[count_at + 3] = 0x7F;
	CHECK(readBuf(&b, MU_KIND_FLAC, 0, &t) == 1);
	CHECK_STR(t.title, "Fl\xC3\xA9");
	MuTags_free(&t);
	b.data[count_at + 3] = 0;
	b.data[count_at + 4 + 4 + 13 + 3] = 0x40; // the title's length
	CHECK(readBuf(&b, MU_KIND_FLAC, 0, &t) == 0);
	MuTags_free(&t);
	// a block past the file
	b.len = 4 + 4 + 20;
	CHECK(readBuf(&b, MU_KIND_FLAC, 0, &t) == 0);
	MuTags_free(&t);
}

static void testTagFiles(void) {
	char dir[] = "/tmp/test_music_tags.XXXXXX";
	CHECK(mkdtemp(dir) != NULL);
	char path[MU_PATH_MAX], cover[MU_PATH_MAX];
	MuTags t;

	// a tagged MP3 on disk
	static Buf b;
	b.len = 0;
	id3Header(&b, 3, 0, 0);
	id3Frame(&b, 3, "TIT2", 0, "\0On disk", 8);
	id3Size(&b, 0);
	snprintf(path, sizeof(path), "%s/a.mp3", dir);
	FILE* f = fopen(path, "wb");
	CHECK(f && fwrite(b.data, 1, b.len, f) == b.len);
	if (f) fclose(f);
	CHECK(MU_readTagsFile(path, 1, &t) == 1);
	CHECK_STR(t.title, "On disk");
	MuTags_free(&t);
	snprintf(path, sizeof(path), "%s/a.txt", dir);
	CHECK(MU_readTagsFile(path, 1, &t) == 0);
	snprintf(path, sizeof(path), "%s/missing.mp3", dir);
	CHECK(MU_readTagsFile(path, 1, &t) == 0);

	// folder covers: none, then folder.jpg, then Cover.PNG over it, then cover.jpg
	CHECK(!MU_folderCover(dir, cover, sizeof(cover)));
	CHECK_STR(cover, "");
	snprintf(path, sizeof(path), "%s/folder.jpg", dir);
	f = fopen(path, "wb");
	if (f) {
		fwrite(jpeg, 1, sizeof(jpeg), f);
		fclose(f);
	}
	CHECK(MU_folderCover(dir, cover, sizeof(cover)) && !strcmp(cover, path));
	snprintf(path, sizeof(path), "%s/Cover.PNG", dir);
	f = fopen(path, "wb");
	if (f) {
		fwrite(png, 1, sizeof(png), f);
		fclose(f);
	}
	CHECK(MU_folderCover(dir, cover, sizeof(cover)) && !strcmp(cover, path));
	snprintf(path, sizeof(path), "%s/cover.jpg", dir);
	CHECK(mkdir(path, 0755) == 0); // a folder isn't a cover
	CHECK(MU_folderCover(dir, cover, sizeof(cover)) && strstr(cover, "Cover.PNG"));
	rmdir(path);
	CHECK(!MU_folderCover("/nonexistent/folder", cover, sizeof(cover)));

	// embedded art files
	char name[64], name2[64], art[256];
	MU_artName("/M/a.mp3", 10, 20, MU_PICTURE_JPEG, name, sizeof(name));
	MU_artName("/M/a.mp3", 10, 21, MU_PICTURE_JPEG, name2, sizeof(name2));
	CHECK(strncmp(name, "art-", 4) == 0 && strstr(name, ".jpg") && strcmp(name, name2) != 0);
	snprintf(art, sizeof(art), "%s/art", dir);
	CHECK(MU_artDir(art) == 0);
	CHECK(MU_artDir(art) == 0);
	CHECK(MU_writeArt(art, name, jpeg, sizeof(jpeg)) == 0);
	snprintf(path, sizeof(path), "%s/%s", art, name);
	struct stat st;
	CHECK(stat(path, &st) == 0 && st.st_size == (off_t)sizeof(jpeg) && (st.st_mode & 0777) == 0600);
	CHECK(MU_writeArt(art, name, png, sizeof(png)) == 0);
	CHECK(stat(path, &st) == 0 && st.st_size == (off_t)sizeof(png));
	CHECK(MU_writeArt(art, "../x", png, sizeof(png)) == -1);
	CHECK(MU_writeArt(art, "", png, sizeof(png)) == -1);
	// a link in the art folder's place isn't taken
	char link[MU_PATH_MAX];
	snprintf(link, sizeof(link), "%s/link", dir);
	CHECK(symlink(art, link) == 0);
	CHECK(MU_artDir(link) == -1);
	unlink(link);
	unlink(path);
	rmdir(art);
	snprintf(path, sizeof(path), "%s/a.mp3", dir);
	unlink(path);
	snprintf(path, sizeof(path), "%s/folder.jpg", dir);
	unlink(path);
	snprintf(path, sizeof(path), "%s/Cover.PNG", dir);
	unlink(path);
	CHECK(rmdir(dir) == 0);
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
	testList();
	testPaths();
	testTagText();
	testId3();
	testFlac();
	testTagFiles();
	CHECK_DONE();
}
