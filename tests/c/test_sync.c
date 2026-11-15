// sync's model: which files sync and where, the listings, the record,
// newest wins with .conflict copies, removals with .deleted copies, and
// leaving romm's files to romm.

#include <stdlib.h>

#include "check.h"
#include "sync_model.h"

static SyFile file(const char* rel, long long size, long long mtime) {
	SyFile f;
	memset(&f, 0, sizeof(f));
	snprintf(f.rel, sizeof(f.rel), "%s", rel);
	f.size = size;
	f.mtime = mtime;
	return f;
}

static SyBase base(const char* rel, long long lsize, long long lmtime, long long rsize, long long rmtime) {
	SyBase b;
	memset(&b, 0, sizeof(b));
	snprintf(b.rel, sizeof(b.rel), "%s", rel);
	b.lsize = lsize;
	b.lmtime = lmtime;
	b.rsize = rsize;
	b.rmtime = rmtime;
	return b;
}

static int onlyStates(const char* rel, void* data) {
	(void)data;
	return Sy_kind(rel) == SY_STATE;
}

static int failsGB(const char* rel, void* data) {
	(void)data;
	return !strcmp(rel, "Saves/GB/b.sav");
}

static void paths(void) {
	CHECK(Sy_syncable("Pokemon Red (USA).sav"));
	CHECK(!Sy_syncable(".hidden"));
	CHECK(!Sy_syncable("a.sav.conflict"));
	CHECK(!Sy_syncable("a.sav.deleted"));
	CHECK(!Sy_syncable("a.sav.part"));
	CHECK(!Sy_syncable("a.sav.tmp"));
	CHECK(!Sy_syncable("a/b"));
	CHECK(!Sy_syncable(""));
	CHECK(Sy_stateDir("GB-gambatte"));
	CHECK(!Sy_stateDir(".minui"));
	CHECK(!Sy_stateDir("GB"));
	CHECK(!Sy_stateDir("-x"));
	CHECK(!Sy_stateDir("x-"));

	CHECK(Sy_kind("Saves/GB/a.sav") == SY_SAVE);
	CHECK(Sy_kind("States/GB-gambatte/a.gb.st0") == SY_STATE);
	CHECK(Sy_kind("States/GB/a.st0") < 0);
	CHECK(Sy_kind("Saves/GB/x/a.sav") < 0);
	CHECK(Sy_kind("Saves/a.sav") < 0);
	CHECK(Sy_kind("Other/GB/a.sav") < 0);
	CHECK(Sy_kind("Saves/../a.sav") < 0);
	CHECK(Sy_kind("Saves/GB/.a.sav") < 0);

	char out[SY_PATH_MAX];
	CHECK(Sy_localPath("/sd", "Saves/GB/a.sav", out, sizeof(out)) == 0);
	CHECK_STR(out, "/sd/Saves/GB/a.sav");
	CHECK(Sy_localPath("/sd", "States/GB-gambatte/a.gb.st0", out, sizeof(out)) == 0);
	CHECK_STR(out, "/sd/.userdata/shared/GB-gambatte/a.gb.st0");
	CHECK(Sy_localPath("/sd", "States/.minui/a", out, sizeof(out)) != 0);
	CHECK(Sy_localPath("/sd", "Saves/GB/a.sav", out, 10) != 0);
	CHECK(Sy_relOf("/sd", "/sd/.userdata/shared/GB-gambatte/a.gb.st0", out, sizeof(out)) == 0);
	CHECK_STR(out, "States/GB-gambatte/a.gb.st0");
	CHECK(Sy_relOf("/sd", "/sd/Saves/GB/a.sav", out, sizeof(out)) == 0);
	CHECK_STR(out, "Saves/GB/a.sav");
	CHECK(Sy_relOf("/sd", "/sd/.userdata/shared/game_logs.sqlite", out, sizeof(out)) != 0);
	CHECK(Sy_relOf("/sd", "/other/Saves/GB/a.sav", out, sizeof(out)) != 0);

	char tag[64], name[256];
	CHECK(Sy_gameOf("/sd", "/sd/Roms/Game Boy (GB)/Pokemon Red (USA).gb", tag, sizeof(tag), name, sizeof(name)) == 0);
	CHECK_STR(tag, "GB");
	CHECK_STR(name, "Pokemon Red (USA)");
	CHECK(Sy_gameOf("/sd", "/sd/Roms/Sony PlayStation (PS)/Game (USA)/Game (USA).m3u", tag, sizeof(tag), name, sizeof(name)) == 0);
	CHECK_STR(tag, "PS");
	CHECK_STR(name, "Game (USA)");
	CHECK(Sy_gameOf("/sd", "/sd/Roms/Untagged/a.gb", tag, sizeof(tag), name, sizeof(name)) != 0);
	CHECK(Sy_gameOf("/sd", "/sd/Tools/a.pak", tag, sizeof(tag), name, sizeof(name)) != 0);

	CHECK(Sy_ofGame("Saves/GB/Pokemon Red (USA).sav", "GB", "Pokemon Red (USA)"));
	CHECK(Sy_ofGame("Saves/GB/Pokemon Red (USA).gb.sav", "GB", "Pokemon Red (USA)"));
	CHECK(Sy_ofGame("States/GB-gambatte/Pokemon Red (USA).gb.st0", "GB", "Pokemon Red (USA)"));
	CHECK(!Sy_ofGame("Saves/GB/Pokemon Red (USA) [Hack].sav", "GB", "Pokemon Red (USA)"));
	CHECK(!Sy_ofGame("Saves/GBC/Pokemon Red (USA).sav", "GB", "Pokemon Red (USA)"));
	CHECK(!Sy_ofGame("States/GBC-gambatte/Pokemon Red (USA).gb.st0", "GB", "Pokemon Red (USA)"));

	CHECK(Sy_remoteJoin("gdrive:Mosaic", "Saves", out, sizeof(out)) == 0);
	CHECK_STR(out, "gdrive:Mosaic/Saves");
	CHECK(Sy_remoteJoin("gdrive:", "Saves", out, sizeof(out)) == 0);
	CHECK_STR(out, "gdrive:Saves");
	CHECK(Sy_remoteJoin("/mnt/x/", "Saves", out, sizeof(out)) == 0);
	CHECK_STR(out, "/mnt/x/Saves");
}

static void listings(void) {
	CHECK(Sy_utc(1970, 1, 1, 0, 0, 0) == 0);
	CHECK(Sy_utc(2026, 11, 15, 10, 0, 0) == 1794736800LL);
	CHECK(Sy_utc(2000, 2, 29, 0, 0, 0) == 951782400LL);
	CHECK(Sy_utc(2026, 13, 1, 0, 0, 0) < 0);

	SyFile f;
	CHECK(Sy_parseRclone("Saves/GB/Pokemon Red (USA).sav\t12345\t2026-11-15 10:00:00", &f) == 0);
	CHECK_STR(f.rel, "Saves/GB/Pokemon Red (USA).sav");
	CHECK(f.size == 12345);
	CHECK(f.mtime == 1794736800LL);
	CHECK(Sy_parseRclone("Saves/GB/a.sav\t1\t2026-11-15 10:00:00\r\n", &f) == 0);
	CHECK_STR(f.rel, "Saves/GB/a.sav");
	CHECK(Sy_parseRclone("Saves/GB/a.sav\tx\t2026-11-15 10:00:00", &f) != 0);
	CHECK(Sy_parseRclone("Saves/GB/a.sav\t1\t2026-11-15", &f) != 0);
	CHECK(Sy_parseRclone("Saves/a.sav\t1\t2026-11-15 10:00:00", &f) != 0);
	CHECK(Sy_parseRclone("Saves/GB/a.sav.conflict\t1\t2026-11-15 10:00:00", &f) != 0);

	SyBase b = base("Saves/GB/a b.sav", 10, 20, 11, 21), back;
	char line[SY_PATH_MAX + 128];
	CHECK(Sy_baseFormat(&b, line, sizeof(line)) > 0);
	CHECK_STR(line, "10\t20\t11\t21\tSaves/GB/a b.sav\n");
	CHECK(Sy_baseParse(line, &back) == 0);
	CHECK_STR(back.rel, "Saves/GB/a b.sav");
	CHECK(back.lsize == 10 && back.lmtime == 20 && back.rsize == 11 && back.rmtime == 21);
	CHECK(Sy_baseParse("10\t20\t11\tSaves/GB/a.sav\n", &back) != 0);
	CHECK(Sy_baseParse("10\t20\t11\t21\tElsewhere/a\n", &back) != 0);
}

static void decide(void) {
	SyFile l = file("Saves/GB/a.sav", 10, 1000), r = file("Saves/GB/a.sav", 10, 1000);
	SyBase b = base("Saves/GB/a.sav", 10, 1000, 10, 1000);
	// only on one side and not in the record: new, so copied there
	CHECK(Sy_decide(&l, NULL, NULL) == SY_UPLOAD);
	CHECK(Sy_decide(NULL, &r, NULL) == SY_DOWNLOAD);
	// in the record: removed from the other side, so removed here too
	CHECK(Sy_decide(&l, NULL, &b) == SY_DELETE_LOCAL);
	CHECK(Sy_decide(NULL, &r, &b) == SY_DELETE_REMOTE);
	CHECK(Sy_decide(NULL, NULL, &b) == SY_NONE);
	// unless it changed on this side since: the changed copy goes back
	SyFile changed = file("Saves/GB/a.sav", 11, 1500);
	CHECK(Sy_decide(&changed, NULL, &b) == SY_UPLOAD);
	CHECK(Sy_decide(NULL, &changed, &b) == SY_DOWNLOAD);
	changed = file("Saves/GB/a.sav", 10, 1001); // only its time
	CHECK(Sy_decide(&changed, NULL, &b) == SY_UPLOAD);
	CHECK(Sy_decide(NULL, &changed, &b) == SY_DOWNLOAD);
	// in step
	CHECK(Sy_decide(&l, &r, &b) == SY_NONE);
	// the same on both sides with no record, or within FAT's 2 seconds
	CHECK(Sy_decide(&l, &r, NULL) == SY_SAME);
	r.mtime = 1002;
	CHECK(Sy_decide(&l, &r, NULL) == SY_SAME);
	// changed here only, or there only
	l = file("Saves/GB/a.sav", 12, 2000);
	r = file("Saves/GB/a.sav", 10, 1000);
	CHECK(Sy_decide(&l, &r, &b) == SY_UPLOAD);
	l = file("Saves/GB/a.sav", 10, 1000);
	r = file("Saves/GB/a.sav", 12, 900); // even older: it changed since
	CHECK(Sy_decide(&l, &r, &b) == SY_DOWNLOAD);
	// changed on both sides: the newer wins, the other kept
	l = file("Saves/GB/a.sav", 12, 3000);
	r = file("Saves/GB/a.sav", 14, 2000);
	CHECK(Sy_decide(&l, &r, &b) == SY_UPLOAD_KEEP);
	r.mtime = 4000;
	CHECK(Sy_decide(&l, &r, &b) == SY_DOWNLOAD_KEEP);
	// a tie goes to this device's
	r.mtime = 3001;
	CHECK(Sy_decide(&l, &r, &b) == SY_UPLOAD_KEEP);
	// no record and different: both changed
	CHECK(Sy_decide(&l, &r, NULL) == SY_UPLOAD_KEEP);
	r.mtime = 9000;
	CHECK(Sy_decide(&l, &r, NULL) == SY_DOWNLOAD_KEEP);
	// both changed to the same file
	l = file("Saves/GB/a.sav", 20, 5000);
	r = file("Saves/GB/a.sav", 20, 5001);
	CHECK(Sy_decide(&l, &r, &b) == SY_SAME);
}

static void plan(void) {
	SyFile local[] = {
		file("Saves/GB/a.sav", 10, 1000),   // in step
		file("Saves/GB/b.sav", 12, 2000),   // changed here
		file("Saves/GB/c.sav", 5, 500),     // only here
		file("Saves/GB/e.sav", 30, 3000),   // changed on both, this one newer
		file("States/GB-gambatte/s.st0", 7, 700),
		file("Saves/GB/f.sav", 4, 400),     // removed there since the last sync
	};
	SyFile remote[] = {
		file("Saves/GB/a.sav", 10, 1000),
		file("Saves/GB/b.sav", 10, 1000),
		file("Saves/GB/d.sav", 8, 800),     // only there
		file("Saves/GB/e.sav", 31, 2500),
		file("States/GB-gambatte/s.st0", 9, 9000), // changed there
		file("Saves/GB/g.sav", 6, 600),     // removed here since the last sync
		file("Saves/GB/h.sav", 7, 999),     // removed here, but changed there since
	};
	SyBase record[] = {
		base("Saves/GB/a.sav", 10, 1000, 10, 1000),
		base("Saves/GB/b.sav", 10, 1000, 10, 1000),
		base("Saves/GB/e.sav", 10, 1000, 10, 1000),
		base("Saves/GB/f.sav", 4, 400, 4, 400),
		base("Saves/GB/g.sav", 6, 600, 6, 600),
		base("Saves/GB/gone.sav", 1, 1, 1, 1),
		base("Saves/GB/h.sav", 7, 700, 7, 700),
		base("States/GB-gambatte/s.st0", 7, 700, 7, 700),
	};
	int nl = 6, nr = 7, nb = 8;
	Sy_sortFiles(local, nl);
	Sy_sortFiles(remote, nr);
	Sy_sortBase(record, nb);
	CHECK(Sy_findFile(local, nl, "Saves/GB/c.sav") != NULL);
	CHECK(Sy_findFile(local, nl, "Saves/GB/d.sav") == NULL);
	CHECK(Sy_findBase(record, nb, "Saves/GB/gone.sav") != NULL);

	SyStep steps[16];
	int n = Sy_plan(local, nl, remote, nr, record, nb, NULL, NULL, steps, 16);
	CHECK(n == 8);
	CHECK_STR(steps[0].rel, "Saves/GB/b.sav");
	CHECK(steps[0].action == SY_UPLOAD);
	CHECK_STR(steps[1].rel, "Saves/GB/c.sav");
	CHECK(steps[1].action == SY_UPLOAD);
	CHECK_STR(steps[2].rel, "Saves/GB/d.sav");
	CHECK(steps[2].action == SY_DOWNLOAD);
	CHECK_STR(steps[3].rel, "Saves/GB/e.sav");
	CHECK(steps[3].action == SY_UPLOAD_KEEP);
	CHECK_STR(steps[4].rel, "Saves/GB/f.sav");
	CHECK(steps[4].action == SY_DELETE_LOCAL);
	CHECK_STR(steps[5].rel, "Saves/GB/g.sav");
	CHECK(steps[5].action == SY_DELETE_REMOTE);
	CHECK_STR(steps[6].rel, "Saves/GB/h.sav");
	CHECK(steps[6].action == SY_DOWNLOAD);
	CHECK_STR(steps[7].rel, "States/GB-gambatte/s.st0");
	CHECK(steps[7].action == SY_DOWNLOAD);
	// only what want keeps
	n = Sy_plan(local, nl, remote, nr, record, nb, onlyStates, NULL, steps, 16);
	CHECK(n == 1);
	CHECK_STR(steps[0].rel, "States/GB-gambatte/s.st0");
	// at most max
	CHECK(Sy_plan(local, nl, remote, nr, record, nb, NULL, NULL, steps, 2) == 2);

	// the record after: b failed, so it keeps its old one; f, g and gone
	// are on neither side, so their records go; the rest are as the lists
	// after the run say
	SyFile after_local[] = {
		file("Saves/GB/a.sav", 10, 1000),
		file("Saves/GB/b.sav", 12, 2000),
		file("Saves/GB/c.sav", 5, 500),
		file("Saves/GB/d.sav", 8, 800),
		file("Saves/GB/e.sav", 30, 3000),
		file("Saves/GB/h.sav", 7, 999),
		file("States/GB-gambatte/s.st0", 9, 9000),
	};
	SyFile after_remote[] = {
		file("Saves/GB/a.sav", 10, 1000),
		file("Saves/GB/b.sav", 10, 1000),
		file("Saves/GB/c.sav", 5, 501),
		file("Saves/GB/d.sav", 8, 800),
		file("Saves/GB/e.sav", 30, 3000),
		file("Saves/GB/h.sav", 7, 999),
		file("States/GB-gambatte/s.st0", 9, 9000),
	};
	SyBase next[16];
	int m = Sy_baseNext(record, nb, after_local, 7, after_remote, 7, NULL, failsGB, NULL, next, 16);
	CHECK(m == 7);
	CHECK(Sy_findBase(next, m, "Saves/GB/f.sav") == NULL);
	CHECK(Sy_findBase(next, m, "Saves/GB/g.sav") == NULL);
	CHECK(Sy_findBase(next, m, "Saves/GB/gone.sav") == NULL);
	const SyBase* e = Sy_findBase(next, m, "Saves/GB/b.sav");
	CHECK(e && e->lsize == 10 && e->lmtime == 1000);
	e = Sy_findBase(next, m, "Saves/GB/c.sav");
	CHECK(e && e->lsize == 5 && e->rmtime == 501);
	e = Sy_findBase(next, m, "Saves/GB/h.sav");
	CHECK(e && e->lmtime == 999 && e->rmtime == 999);
	e = Sy_findBase(next, m, "States/GB-gambatte/s.st0");
	CHECK(e && e->lmtime == 9000 && e->rmtime == 9000);
	// so the next run has nothing to do but b
	n = Sy_plan(after_local, 7, after_remote, 7, next, m, NULL, NULL, steps, 16);
	CHECK(n == 1);
	CHECK_STR(steps[0].rel, "Saves/GB/b.sav");
	// a file want leaves out keeps its old record, even when it's changed
	m = Sy_baseNext(record, nb, after_local, 7, after_remote, 7, onlyStates, NULL, NULL, next, 16);
	e = Sy_findBase(next, m, "Saves/GB/e.sav");
	CHECK(e && e->lsize == 10);
	CHECK(Sy_findBase(next, m, "Saves/GB/c.sav") == NULL);
	CHECK(Sy_findBase(next, m, "Saves/GB/gone.sav") != NULL);

	// files on neither side have nothing to do, and their records go
	SyFile only_here[] = { file("Saves/GB/f.sav", 4, 400) };
	n = Sy_plan(only_here, 1, NULL, 0, record, nb, NULL, NULL, steps, 16);
	CHECK(n == 1);
	CHECK(steps[0].action == SY_DELETE_LOCAL);
	m = Sy_baseNext(record, nb, NULL, 0, NULL, 0, NULL, NULL, NULL, next, 16);
	CHECK(m == 0);
}

static void romm(void) {
	CHECK(Sy_replyInt("server=1\nsaves=1\nstates=0\n", "saves", -1) == 1);
	CHECK(Sy_replyInt("server=1\nsaves=1\nstates=0\n", "states", -1) == 0);
	CHECK(Sy_replyInt("server=1\nsaves=1\n", "states", -1) == -1);
	CHECK(Sy_replyInt("xsaves=1\n", "saves", -1) == -1);
	CHECK(Sy_replyInt(NULL, "saves", -1) == -1);

	const char* game = "server=1\nsaves=1\nstates=0\nsynced=0\ngame=1\n";
	const char* not_game = "server=1\nsaves=1\nstates=1\nsynced=0\ngame=0\n";
	const char* synced = "server=1\nsaves=1\nstates=1\nsynced=1\nhow=upload\nat=5\ngame=0\n";
	const char* off = "server=1\nsaves=0\nstates=0\nsynced=1\nhow=upload\nat=5\ngame=1\n";
	const char* no_server = "server=0\nsaves=0\nstates=0\nsynced=0\ngame=0\n";
	// a game from RomM: its saves, which romm uploads, but not its states
	CHECK(Sy_rommSkips(game, NULL, SY_SAVE));
	CHECK(!Sy_rommSkips(game, NULL, SY_STATE));
	// a file romm last uploaded, of a kind it uploads
	CHECK(Sy_rommSkips(NULL, synced, SY_SAVE));
	CHECK(Sy_rommSkips(NULL, synced, SY_STATE));
	// romm not uploading, no server, or a file it doesn't know
	CHECK(!Sy_rommSkips(off, off, SY_SAVE));
	CHECK(!Sy_rommSkips(no_server, no_server, SY_SAVE));
	CHECK(!Sy_rommSkips(NULL, not_game, SY_SAVE));
	CHECK(!Sy_rommSkips(not_game, NULL, SY_SAVE));
	CHECK(!Sy_rommSkips(NULL, NULL, SY_SAVE));
	// without romm, call gives "" for the reply
	CHECK(!Sy_rommSkips("", "", SY_SAVE));
}

static void syncthing(void) {
	CHECK(Sy_deviceIdValid("MFZWI3D-BONSGYC-YLTMRWG-C43ENR5-QXGZDMM-FZWI3DP-BONSGYY-LTMRWAD"));
	CHECK(Sy_deviceIdValid("mfzwi3d-bonsgyc-yltmrwg-c43enr5-qxgzdmm-fzwi3dp-bonsgyy-ltmrwad"));
	CHECK(Sy_deviceIdValid("MFZWI3DBONSGYCYLTMRWGC43ENR5QXGZDMMFZWI3DPBONSGYYLTMRWAD"));
	CHECK(!Sy_deviceIdValid("MFZWI3D-BONSGYC-YLTMRWG-C43ENR5-QXGZDMM-FZWI3DP-BONSGYY-LTMRWA1"));
	CHECK(!Sy_deviceIdValid("MFZWI3D-BONSGYC"));
	CHECK(!Sy_deviceIdValid(""));
	CHECK(!Sy_deviceIdValid(NULL));

	char v[128];
	const char* json = "{\"myID\": \"ABC-DEF\", \"state\":\"idle\",\"needFiles\": 3, \"connected\":true, \"x\":{\"y\":1}}";
	CHECK(Sy_jsonValue(json, "myID", v, sizeof(v)) == 7);
	CHECK_STR(v, "ABC-DEF");
	CHECK(Sy_jsonValue(json, "state", v, sizeof(v)) > 0);
	CHECK_STR(v, "idle");
	CHECK(Sy_jsonValue(json, "needFiles", v, sizeof(v)) > 0);
	CHECK_STR(v, "3");
	CHECK(Sy_jsonValue(json, "connected", v, sizeof(v)) > 0);
	CHECK_STR(v, "true");
	CHECK(Sy_jsonValue(json, "x", v, sizeof(v)) < 0);
	CHECK(Sy_jsonValue(json, "missing", v, sizeof(v)) < 0);
	CHECK(Sy_jsonValue("{\"completion\":99.5}", "completion", v, sizeof(v)) > 0);
	CHECK_STR(v, "99.5");

	CHECK(Sy_jsonEscape("/sd/a \"b\"\\c", v, sizeof(v)) == 0);
	CHECK_STR(v, "/sd/a \\\"b\\\"\\\\c");
	CHECK(Sy_jsonEscape("a\nb", v, sizeof(v)) != 0);
	CHECK(Sy_jsonEscape("abcdef", v, 4) != 0);

	CHECK(Sy_ignoreEscape("/GB/Game [!] (USA).sav", v, sizeof(v)) == 0);
	CHECK_STR(v, "/GB/Game \\[!\\] (USA).sav");
	CHECK(Sy_ignoreEscape("Game *?{x}.sav", v, sizeof(v)) == 0);
	CHECK_STR(v, "Game \\*\\?\\{x\\}.sav");
	CHECK(Sy_ignoreEscape("!a#", v, sizeof(v)) == 0);
	CHECK_STR(v, "\\!a#");
	CHECK(Sy_ignoreEscape("abc", v, 3) != 0);
}

int main(void) {
	paths();
	listings();
	decide();
	plan();
	romm();
	syncthing();
	CHECK_DONE();
}
