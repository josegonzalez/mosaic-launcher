// The gamification plugin: its model on its own, then the plugin against
// a fake host with a fixture play history.

#include <stddef.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "check.h"
#include "fakeproviders.h"
#include "words.h"
#include "../../plugins/gamification/gamification.c"

#define DIR TEST_DIR
#define DAY 86400

static int rectIs(MosaicRect r, int x, int y, int w, int h) {
	return r.x == x && r.y == y && r.w == w && r.h == h;
}

static const GMTargets TARGETS = { 30, 3, 5, 5, 300 };

///////////////////////////////////////
// the model

static void testDays(void) {
	CHECK(GM_dayOf(0) == 0);
	CHECK(GM_dayOf(DAY - 1) == 0);
	CHECK(GM_dayOf(DAY) == 1);
	CHECK(GM_dayOf(-1) == -1);
	CHECK(GM_dayStart(0) == 0);
	CHECK(GM_dayStart(20000) == 20000LL * DAY);
	CHECK(GM_dayStart(-1) == -DAY);
	// 1 March 2024, after a leap day
	CHECK(GM_dayOf(1709251200) == 19783);
	CHECK(GM_dayStart(19783) == 1709251200);

	// 1 January 1970 was a Thursday, and weeks start on Monday
	CHECK(GM_weekday(0) == 3);
	CHECK(GM_weekday(4) == 0);
	CHECK(GM_weekday(-3) == 0);
	CHECK(GM_weekday(-4) == 6);
	CHECK(GM_weekOf(0) == 0 && GM_weekOf(3) == 0);
	CHECK(GM_weekOf(4) == 1 && GM_weekOf(10) == 1 && GM_weekOf(11) == 2);
	CHECK(GM_weekOf(-3) == 0 && GM_weekOf(-4) == -1 && GM_weekOf(-10) == -1 && GM_weekOf(-11) == -2);

	// local days, five hours behind UTC
	setenv("TZ", "EST5", 1);
	tzset();
	CHECK(GM_dayOf(0) == -1);
	CHECK(GM_dayOf(5 * 3600 - 1) == -1);
	CHECK(GM_dayOf(5 * 3600) == 0);
	CHECK(GM_dayStart(0) == 5 * 3600);
	setenv("TZ", "UTC", 1);
	tzset();
}

static void testLaunch(void) {
	GMState s;
	GM_init(&s);
	CHECK(s.day_count == 0 && s.games_day == -1 && s.cheered == -1 && s.done[GM_Q_PLAY] == -1);
	CHECK(GM_day(&s, 100) == NULL);
	GM_launch(&s, 100, "/card/Roms/GB/Tetris.gb", 1, 0);
	GM_launch(&s, 100, "/card/Roms/GB/Tetris.gb", 0, 0);
	GM_launch(&s, 100, "/card/Roms/GB/Zelda.gb", 0, 1);
	const GMDay* d = GM_day(&s, 100);
	CHECK(d && d->launches == 3 && d->games == 2 && d->fresh == 1 && d->revisits == 1);
	// a new day counts its own games
	GM_launch(&s, 101, "/card/Roms/GB/Tetris.gb", 0, 0);
	d = GM_day(&s, 101);
	CHECK(d && d->launches == 1 && d->games == 1);
	CHECK(s.games_day == 101 && s.game_count == 1);
	CHECK(s.day_count == 2 && s.days[0].day == 101 && s.days[1].day == 100);
	// a launch on an older day counts as another game there
	GM_launch(&s, 100, "/card/Roms/GB/Tetris.gb", 0, 0);
	CHECK(GM_day(&s, 100)->games == 3 && s.games_day == 101);
	// past GM_TODAY_GAMES games, each launch counts as another
	GMState many;
	GM_init(&many);
	char path[64];
	for (int i = 0; i < GM_TODAY_GAMES + 3; i++) {
		snprintf(path, sizeof(path), "/card/Roms/GB/%i.gb", i);
		GM_launch(&many, 5, path, 0, 0);
	}
	GM_launch(&many, 5, "/card/Roms/GB/0.gb", 0, 0);
	CHECK(GM_day(&many, 5)->games == GM_TODAY_GAMES + 3 && many.game_count == GM_TODAY_GAMES);
	GM_launch(&many, 5, NULL, 0, 0); // no path counts as one game
	CHECK(GM_day(&many, 5)->launches == GM_TODAY_GAMES + 5);

	// the log keeps the newest GM_LOG_DAYS days
	GMState log;
	GM_init(&log);
	for (int i = 0; i < GM_LOG_DAYS + 6; i++) GM_launch(&log, 1000 + i, "/a", 0, 0);
	CHECK(log.day_count == GM_LOG_DAYS);
	CHECK(log.days[0].day == 1000 + GM_LOG_DAYS + 5);
	CHECK(log.days[GM_LOG_DAYS - 1].day == 1006);
	GM_launch(&log, 1000, "/a", 0, 0); // older than every day kept: dropped
	CHECK(GM_day(&log, 1000) == NULL && log.day_count == GM_LOG_DAYS);
	GM_launch(&log, 1010, "/a", 0, 0);
	CHECK(GM_day(&log, 1010)->launches == 2);
}

static void testPlay(void) {
	GMState s;
	GM_init(&s);
	time_t ten = GM_dayStart(10);
	// over midnight: split between the days
	GM_play(&s, ten + DAY - 600, 1800);
	CHECK(GM_day(&s, 10)->seconds == 600);
	CHECK(GM_day(&s, 11)->seconds == 1200);
	GM_play(&s, ten + 60, 60);
	CHECK(GM_day(&s, 10)->seconds == 660);
	// none, or a clock gone wrong
	GM_play(&s, ten, 0);
	GM_play(&s, ten, -5);
	GM_play(&s, ten, GM_SESSION_MAX + 1);
	CHECK(GM_day(&s, 10)->seconds == 660 && s.day_count == 2);
	// the longest session counted, over midnight
	GM_play(&s, GM_dayStart(20) - 100, GM_SESSION_MAX);
	CHECK(GM_day(&s, 19) && GM_day(&s, 19)->seconds == 100);
	CHECK(GM_day(&s, 20) && GM_day(&s, 20)->seconds == GM_SESSION_MAX - 100);
	CHECK(GM_day(&s, 21) == NULL);
}

static void testFormat(void) {
	GMState s;
	GM_init(&s);
	char text[STATE_MAX];
	CHECK(GM_format(&s, text, sizeof(text)) > 0);
	CHECK_STR(text, "best=0\ncompleted=0\ncheered=-1\n");
	GM_launch(&s, 200, "/card/Roms/GB/Tetris.gb", 1, 0);
	GM_launch(&s, 201, "/card/Roms/GB/Zelda.gb", 0, 1);
	GM_launch(&s, 201, "/card/Roms/GB/Tetris.gb", 0, 0);
	GM_play(&s, GM_dayStart(201) + 10, 1234);
	s.best_streak = 4;
	s.completed = 9;
	s.cheered = 201;
	s.done[GM_Q_GAMES] = 201;
	s.done[GM_Q_WEEK_HOURS] = 29;
	int len = GM_format(&s, text, sizeof(text));
	CHECK(len == (int)strlen(text));
	CHECK(strstr(text, "best=4\ncompleted=9\ncheered=201\ndone=games 201\ndone=week_hours 29\n"
		"day=201 1234 2 2 0 1 0\nday=200 0 1 1 1 0 0\ngames=201 ") == text);

	GMState back;
	GM_init(&back);
	CHECK(GM_parse(&back, text) == 0);
	CHECK(back.best_streak == 4 && back.completed == 9 && back.cheered == 201);
	CHECK(back.done[GM_Q_GAMES] == 201 && back.done[GM_Q_WEEK_HOURS] == 29 && back.done[GM_Q_PLAY] == -1);
	CHECK(back.day_count == 2 && back.days[0].day == 201 && back.days[0].seconds == 1234 && back.days[1].fresh == 1);
	CHECK(back.games_day == 201 && back.game_count == 2 && back.games[0] == s.games[0] && back.games[1] == s.games[1]);
	char again[STATE_MAX];
	CHECK(GM_format(&back, again, sizeof(again)) == len);
	CHECK_STR(again, text);
	// a game launched again after loading is the same game
	GM_launch(&back, 201, "/card/Roms/GB/Zelda.gb", 0, 0);
	CHECK(GM_day(&back, 201)->games == 2);

	// too small
	char tiny[20];
	CHECK(GM_format(&s, tiny, sizeof(tiny)) == -1);
	CHECK(GM_format(&s, tiny, 0) == -1);

	// lines it doesn't know, bad values and no newline at the end
	GMState junk;
	GM_init(&junk);
	GM_parse(&junk, "hello\nbest=-3\ncompleted=x\ndone=nothing 5\ndone=play 7\nday=5 -1 0 0 0 0\nday=6 10 1\n"
		"day=7 60 1 1 0 0\ngames=\ngames=7 zz\nbest=2");
	CHECK(junk.best_streak == 2 && junk.completed == 0 && junk.done[GM_Q_PLAY] == 7);
	CHECK(junk.day_count == 2 && GM_day(&junk, 7)->seconds == 60 && GM_day(&junk, 5)->seconds == 0);
	CHECK(junk.games_day == 7 && junk.game_count == 0);
	GM_parse(&junk, "");
	CHECK(junk.best_streak == 2);

	// downloads: a day's seventh number, and none on a day from before them
	GMState dl;
	GM_init(&dl);
	GM_download(&dl, 9);
	GM_download(&dl, 9);
	CHECK(GM_format(&dl, text, sizeof(text)) > 0);
	CHECK(strstr(text, "day=9 0 0 0 0 0 2\n") != NULL);
	GM_init(&dl);
	GM_parse(&dl, "day=9 0 0 0 0 0 2\nday=8 60 1 1 0 0\nday=7 60 1 1 0 0 -1\n");
	CHECK(GM_day(&dl, 9)->downloads == 2 && GM_day(&dl, 8)->downloads == 0 && GM_day(&dl, 8)->seconds == 60);
	CHECK(GM_day(&dl, 7)->downloads == 0 && GM_day(&dl, 7)->seconds == 0); // refused
}

static void testLaunchLine(void) {
	GMLaunch l = { 1700000000, 12345, 1, 0, "/card/Roms/GB/Tetris (USA) [!].gb" };
	char line[GM_PATH_MAX + 128];
	CHECK(GM_formatLaunch(&l, line, sizeof(line)) == 0);
	CHECK_STR(line, "1700000000 12345 1 0 /card/Roms/GB/Tetris (USA) [!].gb\n");
	GMLaunch back;
	CHECK(GM_parseLaunch(line, &back) == 0);
	CHECK(back.started == 1700000000 && back.mono == 12345 && back.fresh == 1 && back.revisit == 0);
	CHECK_STR(back.path, "/card/Roms/GB/Tetris (USA) [!].gb");
	CHECK(GM_formatLaunch(&l, line, 10) == -1);
	CHECK(GM_parseLaunch("1 2 3", &back) == -1);
	CHECK(GM_parseLaunch("1 2 0 0 \n", &back) == -1);
	CHECK(GM_parseLaunch("a b 0 0 /x", &back) == -1);
	CHECK(GM_parseLaunch(NULL, &back) == -1);

	CHECK(GM_session(&l, 12345 + 600) == 600);
	CHECK(GM_session(&l, 12345) == 0);
	CHECK(GM_session(&l, 12000) == -1);
	CHECK(GM_session(&l, 12345 + GM_SESSION_MAX + 1) == -1);
}

static void testStreak(void) {
	GMState s;
	GM_init(&s);
	GM_play(&s, GM_dayStart(500) + 100, 200);
	long long tracker[6] = { 100, -1, 400, 0, 1000, 10 };
	long long played[6];
	GM_played(&s, 500, tracker, 6, played);
	CHECK(played[0] == 200 && played[1] == 0 && played[2] == 400 && played[3] == 0 && played[4] == 1000);
	GM_played(&s, 500, NULL, 6, played);
	CHECK(played[0] == 200 && played[2] == 0);

	// today hasn't the minutes yet: the streak runs to yesterday
	long long a[] = { 100, 600, 600, 0, 600 };
	CHECK(GM_streak(a, 5, 300) == 2);
	CHECK(GM_streak(a, 5, 100) == 3);
	CHECK(GM_streak(a, 5, 601) == 0);
	CHECK(GM_longest(a, 5, 300) == 2);
	CHECK(GM_longest(a, 5, 100) == 3);
	long long b[] = { 0, 0, 600 };
	CHECK(GM_streak(b, 3, 300) == 0);
	CHECK(GM_longest(b, 3, 300) == 1);
	long long c[] = { 1, 1, 1, 1 };
	CHECK(GM_streak(c, 4, 0) == 4); // need 0 still asks for some play
	long long none[] = { 0 };
	CHECK(GM_streak(none, 1, 0) == 0 && GM_longest(none, 1, 0) == 0);
	CHECK(GM_streak(a, 0, 300) == 0);
}

static void testProgress(void) {
	// day 4 is a Monday, so day 6 is a Wednesday, the third day of its week
	GMState s;
	GM_init(&s);
	GM_launch(&s, 6, "/a", 1, 0);
	GM_launch(&s, 6, "/b", 0, 0);
	GM_launch(&s, 5, "/c", 0, 1);
	long long played[10] = { 1900, 0, 7200, 9999, 0 };
	GMProgress p[GM_QUESTS];
	GM_progress(&s, 6, played, 10, &TARGETS, p);
	CHECK(p[GM_Q_PLAY].value == 31 && p[GM_Q_PLAY].target == 30 && p[GM_Q_PLAY].done);
	CHECK(p[GM_Q_GAMES].value == 2 && p[GM_Q_GAMES].target == 3 && !p[GM_Q_GAMES].done);
	CHECK(p[GM_Q_FRESH].value == 1 && p[GM_Q_FRESH].done);
	CHECK(p[GM_Q_REVISIT].value == 0 && !p[GM_Q_REVISIT].done); // that was yesterday
	CHECK(p[GM_Q_DOWNLOAD].value == 0 && p[GM_Q_DOWNLOAD].target == 1 && !p[GM_Q_DOWNLOAD].done);
	// Monday to Wednesday: the Sunday before (played[3]) is last week's
	CHECK(p[GM_Q_WEEK_DAYS].value == 2 && p[GM_Q_WEEK_DAYS].target == 5 && !p[GM_Q_WEEK_DAYS].done);
	CHECK(p[GM_Q_WEEK_HOURS].value == 151 && p[GM_Q_WEEK_HOURS].target == 300 && !p[GM_Q_WEEK_HOURS].done);

	// no log for today, and a zero target asks for one
	GMTargets zero = { 0, 0, 0, 0, 300 };
	long long nothing[1] = { 0 };
	GM_progress(&s, 40, nothing, 1, &zero, p);
	CHECK(p[GM_Q_PLAY].target == 1 && !p[GM_Q_PLAY].done && p[GM_Q_GAMES].value == 0);

	// settling: each quest once in its day or week
	int newly[GM_QUESTS];
	GM_progress(&s, 6, played, 10, &TARGETS, p);
	CHECK(GM_settle(&s, 6, p, 3, newly) == 2);
	CHECK(newly[0] == GM_Q_PLAY && newly[1] == GM_Q_FRESH);
	CHECK(s.completed == 2 && s.done[GM_Q_PLAY] == 6 && s.best_streak == 3);
	CHECK(GM_settle(&s, 6, p, 2, newly) == 0);
	CHECK(s.completed == 2 && s.best_streak == 3);
	// the next day, the daily ones again
	CHECK(GM_settle(&s, 7, p, 1, newly) == 2);
	CHECK(s.completed == 4);
	// a weekly one once a week
	p[GM_Q_WEEK_DAYS].done = 1;
	CHECK(GM_settle(&s, 8, p, 0, newly) == 3 && newly[2] == GM_Q_WEEK_DAYS);
	CHECK(s.done[GM_Q_WEEK_DAYS] == GM_weekOf(8));
	p[GM_Q_PLAY].done = p[GM_Q_FRESH].done = 0;
	CHECK(GM_settle(&s, 10, p, 0, newly) == 0); // still the same week
	CHECK(GM_settle(&s, 11, p, 0, newly) == 1); // a Monday
	CHECK(s.completed == 8);

	// the RomM quest: a download does it, but only while it's offered
	GMState r;
	GM_init(&r);
	GM_download(&r, 6);
	GM_progress(&r, 6, played, 10, &TARGETS, p);
	CHECK(p[GM_Q_DOWNLOAD].value == 1 && p[GM_Q_DOWNLOAD].done);
	r.off[GM_Q_DOWNLOAD] = 1;
	CHECK(!GM_running(&r, 6, GM_Q_DOWNLOAD) && GM_running(&r, 6, GM_Q_PLAY));
	CHECK(GM_settle(&r, 6, p, 0, newly) == 1 && newly[0] == GM_Q_PLAY); // not the download
	CHECK(r.done[GM_Q_DOWNLOAD] == -1);
	r.off[GM_Q_DOWNLOAD] = 0;
	CHECK(GM_settle(&r, 6, p, 0, newly) == 1 && newly[0] == GM_Q_DOWNLOAD);
	CHECK(r.done[GM_Q_DOWNLOAD] == 6 && r.completed == 2);
}

// Picking the quests against a play time limit.
static void testPickQuests(void) {
	GMState s;
	GM_init(&s);
	CHECK(s.picked[GM_Q_PLAY] == -1 && s.left_out[GM_Q_WEEK_HOURS] == -1);
	CHECK(GM_timed(GM_Q_PLAY) && GM_timed(GM_Q_WEEK_HOURS) && !GM_timed(GM_Q_GAMES) && !GM_timed(GM_Q_WEEK_DAYS));
	CHECK(GM_period(GM_Q_PLAY, 8) == 8 && GM_period(GM_Q_WEEK_DAYS, 8) == GM_weekOf(8));

	// day 6 is a Wednesday: four more days in the week
	CHECK(GM_weekday(6) == 2);
	CHECK(GM_weekLeft(6, 900, 60) == 900 + 4 * 3600);
	CHECK(GM_weekLeft(10, 900, 60) == 900); // a Sunday
	CHECK(GM_weekLeft(6, 900, 0) == 900);
	CHECK(GM_weekLeft(6, -1, 60) == -1);

	GMProgress p[GM_QUESTS];
	for (int i = 0; i < GM_QUESTS; i++) p[i] = (GMProgress){ 0, 1, 0 };
	p[GM_Q_PLAY] = (GMProgress){ 10, 30, 0 };
	p[GM_Q_WEEK_HOURS] = (GMProgress){ 60, 300, 0 };
	int left[GM_QUESTS], count = -1;

	// 15 minutes left today and an hour a day: the daily play quest's 20
	// minutes to go don't fit, the weekly one's 4 hours do
	CHECK(GM_pick(&s, 6, p, 900, GM_weekLeft(6, 900, 60), left, &count) == GM_QUESTS);
	CHECK(count == 1 && left[0] == GM_Q_PLAY);
	CHECK(!GM_running(&s, 6, GM_Q_PLAY));
	for (int i = 1; i < GM_QUESTS; i++) CHECK(GM_running(&s, 6, i));
	// later that day, once picked: left out still, with no limit too
	CHECK(GM_pick(&s, 6, p, -1, -1, left, &count) == 0 && count == 0);
	CHECK(!GM_running(&s, 6, GM_Q_PLAY));
	// done, it isn't completed
	p[GM_Q_PLAY].done = 1;
	int newly[GM_QUESTS];
	CHECK(GM_settle(&s, 6, p, 0, newly) == 0);
	CHECK(s.done[GM_Q_PLAY] == -1);
	p[GM_Q_PLAY] = (GMProgress){ 0, 30, 0 };

	// the next day the daily quests are picked again, and with no time
	// left the weekly ones, picked this week, keep running
	CHECK(GM_pick(&s, 7, p, 0, GM_weekLeft(7, 0, 60), left, &count) == 5);
	CHECK(count == 1 && left[0] == GM_Q_PLAY);
	CHECK(GM_running(&s, 7, GM_Q_WEEK_HOURS));
	// the day after, with no limit: every daily one runs
	CHECK(GM_pick(&s, 8, p, -1, -1, left, &count) == 5 && count == 0);
	CHECK(GM_running(&s, 8, GM_Q_PLAY));

	// a new week with no time: the weekly play quest is left out, a done
	// quest and the other quests aren't
	p[GM_Q_PLAY].done = 1;
	CHECK(GM_pick(&s, 11, p, 0, GM_weekLeft(11, 0, 0), left, &count) == GM_QUESTS);
	CHECK(count == 1 && left[0] == GM_Q_WEEK_HOURS);
	CHECK(GM_running(&s, 11, GM_Q_PLAY) && GM_running(&s, 11, GM_Q_WEEK_DAYS) && !GM_running(&s, 11, GM_Q_WEEK_HOURS));
	// exactly enough left fits
	GM_init(&s);
	p[GM_Q_PLAY] = (GMProgress){ 10, 30, 0 };
	CHECK(GM_pick(&s, 6, p, 1200, -1, left, &count) == GM_QUESTS && count == 0);

	// kept in the state file
	GM_init(&s);
	GM_pick(&s, 6, p, 60, 60, left, &count);
	CHECK(count == 2);
	char text[1024];
	CHECK(GM_format(&s, text, sizeof(text)) > 0);
	CHECK(strstr(text, "picked=play 6\n") && strstr(text, "left_out=play 6\n"));
	CHECK(strstr(text, "picked=week_hours 1\n") && strstr(text, "left_out=week_hours 1\n"));
	CHECK(!strstr(text, "left_out=games"));
	GMState back;
	GM_init(&back);
	GM_parse(&back, text);
	for (int i = 0; i < GM_QUESTS; i++) CHECK(back.picked[i] == s.picked[i] && back.left_out[i] == s.left_out[i]);
	CHECK(!GM_running(&back, 6, GM_Q_PLAY) && GM_running(&back, 7, GM_Q_PLAY));
}

static void testText(void) {
	char out[128];
	GM_duration(0, out, sizeof(out));
	CHECK_STR(out, "0m");
	GM_duration(45, out, sizeof(out));
	CHECK_STR(out, "45m");
	GM_duration(120, out, sizeof(out));
	CHECK_STR(out, "2h");
	GM_duration(125, out, sizeof(out));
	CHECK_STR(out, "2h 5m");
	GM_duration(-4, out, sizeof(out));
	CHECK_STR(out, "0m");

	GM_title(GM_Q_PLAY, &TARGETS, out, sizeof(out));
	CHECK_STR(out, "Play for 30 minutes today");
	GMTargets hour = TARGETS;
	hour.daily_minutes = 60;
	hour.week_hours = 1;
	GM_title(GM_Q_PLAY, &hour, out, sizeof(out));
	CHECK_STR(out, "Play for 1 hour today");
	hour.daily_minutes = 120;
	GM_title(GM_Q_PLAY, &hour, out, sizeof(out));
	CHECK_STR(out, "Play for 2 hours today");
	GM_title(GM_Q_GAMES, &TARGETS, out, sizeof(out));
	CHECK_STR(out, "Play 3 different games today");
	GM_title(GM_Q_FRESH, &TARGETS, out, sizeof(out));
	CHECK_STR(out, "Start a game you've never played");
	GM_title(GM_Q_REVISIT, &TARGETS, out, sizeof(out));
	CHECK_STR(out, "Go back to a game you haven't played in 30 days");
	GM_title(GM_Q_DOWNLOAD, &TARGETS, out, sizeof(out));
	CHECK_STR(out, "Download a game from RomM");
	GM_title(GM_Q_WEEK_DAYS, &TARGETS, out, sizeof(out));
	CHECK_STR(out, "Play on 5 days this week");
	GM_title(GM_Q_WEEK_HOURS, &TARGETS, out, sizeof(out));
	CHECK_STR(out, "Play for 5 hours this week");
	GM_title(GM_Q_WEEK_HOURS, &hour, out, sizeof(out));
	CHECK_STR(out, "Play for 1 hour this week");
	GM_title(GM_QUESTS, &TARGETS, out, sizeof(out));
	CHECK_STR(out, "");

	CHECK_STR(GM_questId(GM_Q_PLAY), "play");
	CHECK_STR(GM_questId(GM_Q_WEEK_HOURS), "week_hours");
	CHECK(GM_questId(GM_QUESTS) == NULL && GM_questId(-1) == NULL);
	CHECK(!GM_weekly(GM_Q_REVISIT) && GM_weekly(GM_Q_WEEK_DAYS));

	GMProgress p = { 12, 30, 0 };
	GM_value(GM_Q_PLAY, &p, out, sizeof(out));
	CHECK_STR(out, "12m / 30m");
	p = (GMProgress){ 125, 300, 0 };
	GM_value(GM_Q_WEEK_HOURS, &p, out, sizeof(out));
	CHECK_STR(out, "2h 5m / 5h");
	p = (GMProgress){ 1, 3, 0 };
	GM_value(GM_Q_GAMES, &p, out, sizeof(out));
	CHECK_STR(out, "1 / 3");
	p.done = 1;
	GM_value(GM_Q_GAMES, &p, out, sizeof(out));
	CHECK_STR(out, "Done");

	GM_streakText(0, out, sizeof(out));
	CHECK_STR(out, "No streak");
	GM_streakText(1, out, sizeof(out));
	CHECK_STR(out, "1 day streak");
	GM_streakText(4, out, sizeof(out));
	CHECK_STR(out, "4 day streak");
	GM_summary(1, 1, out, sizeof(out));
	CHECK_STR(out, "Best 1 day \xc2\xb7 1 quest done");
	GM_summary(7, 12, out, sizeof(out));
	CHECK_STR(out, "Best 7 days \xc2\xb7 12 quests done");

	int one[] = { GM_Q_GAMES }, two[] = { GM_Q_PLAY, GM_Q_FRESH };
	GM_toast(one, 1, &TARGETS, 3, 1, out, sizeof(out));
	CHECK_STR(out, "Quest done: Play 3 different games today");
	GM_toast(two, 2, &TARGETS, 3, 1, out, sizeof(out));
	CHECK_STR(out, "2 quests done");
	GM_toast(NULL, 0, &TARGETS, 3, 1, out, sizeof(out));
	CHECK_STR(out, "3 day streak");
	GM_toast(NULL, 0, &TARGETS, 3, 0, out, sizeof(out));
	CHECK_STR(out, "");
	GM_toast(NULL, 0, &TARGETS, 0, 1, out, sizeof(out));
	CHECK_STR(out, "");
}

// the text in the words it's given, then English again
static void testWords(void) {
	static const char* const words[] = { "%lldm", "%lld min", "Done", "Hecho", "No streak", "Sin racha",
		"Quest done: %s", "Misi\xc3\xb3n cumplida: %s", NULL };
	static const char* const plurals[] = {
		"Play %i different games today", "Juega a %i juego distinto hoy", "Juega a %i juegos distintos hoy",
		"%i day streak", "Racha de %i d\xc3\xad" "a", "Racha de %i d\xc3\xad" "as",
		"Best %i day", "Mejor: %i d\xc3\xad" "a", "Mejor: %i d\xc3\xad" "as",
		"%i quest done", "%i misi\xc3\xb3n cumplida", "%i misiones cumplidas", NULL };
	fake_words = words;
	fake_plurals = plurals;
	GMWords set = { fakeTr, fakePlural };
	GM_setWords(&set);
	char out[256];
	GMTargets one_game = TARGETS;
	one_game.daily_games = 1;
	GM_title(GM_Q_GAMES, &one_game, out, sizeof(out));
	CHECK_STR(out, "Juega a 1 juego distinto hoy");
	GM_title(GM_Q_GAMES, &TARGETS, out, sizeof(out));
	CHECK_STR(out, "Juega a 3 juegos distintos hoy");
	GMProgress p = { 12, 30, 0 };
	GM_value(GM_Q_PLAY, &p, out, sizeof(out));
	CHECK_STR(out, "12 min / 30 min");
	p.done = 1;
	GM_value(GM_Q_PLAY, &p, out, sizeof(out));
	CHECK_STR(out, "Hecho");
	GM_streakText(1, out, sizeof(out));
	CHECK_STR(out, "Racha de 1 d\xc3\xad" "a");
	GM_streakText(0, out, sizeof(out));
	CHECK_STR(out, "Sin racha");
	GM_summary(7, 1, out, sizeof(out));
	CHECK_STR(out, "Mejor: 7 d\xc3\xad" "as \xc2\xb7 1 misi\xc3\xb3n cumplida");
	int one[] = { GM_Q_GAMES }, two[] = { GM_Q_PLAY, GM_Q_FRESH };
	GM_toast(one, 1, &TARGETS, 3, 1, out, sizeof(out));
	CHECK_STR(out, "Misi\xc3\xb3n cumplida: Juega a 3 juegos distintos hoy");
	GM_toast(two, 2, &TARGETS, 3, 1, out, sizeof(out));
	CHECK_STR(out, "2 misiones cumplidas");
	GM_setWords(NULL);
	GM_title(GM_Q_GAMES, &one_game, out, sizeof(out));
	CHECK_STR(out, "Play 1 different games today");
	GM_toast(one, 1, &TARGETS, 3, 1, out, sizeof(out));
	CHECK_STR(out, "Quest done: Play 3 different games today");
	GM_summary(1, 1, out, sizeof(out));
	CHECK_STR(out, "Best 1 day \xc2\xb7 1 quest done");
}

static void testLayout(void) {
	CHECK(GM_scroll(0, -1, 3, 6) == 0);
	CHECK(GM_scroll(0, 1, 3, 6) == 1);
	CHECK(GM_scroll(3, 1, 3, 6) == 3); // the last page
	CHECK(GM_scroll(7, 0, 3, 6) == 3);
	CHECK(GM_scroll(2, 1, 6, 6) == 0); // all fit
	CHECK(GM_scroll(2, 1, 0, 6) == 3);
	CHECK(GM_scroll(5, 1, 0, 6) == 5);

	CHECK(GM_barWidth(0, 30, 100) == 0);
	CHECK(GM_barWidth(15, 30, 100) == 50);
	CHECK(GM_barWidth(1, 1000, 100) == 1);
	CHECK(GM_barWidth(40, 30, 100) == 100);
	CHECK(GM_barWidth(10, 0, 100) == 0);
	CHECK(GM_barWidth(10, 30, 0) == 0);

	MosaicRect area = { 0, 0, 640, 480 }, none = { 0, 0, 0, 0 };
	MosaicRect status = { 500, 10, 130, 30 };
	MosaicRect hole = GM_hole(&status, NULL);
	CHECK(rectIs(hole, 500, 10, 130, 30));
	MosaicRect header = { 400, 10, 90, 30 };
	CHECK(rectIs(GM_hole(&status, &header), 400, 10, 230, 30));
	CHECK(rectIs(GM_hole(NULL, &header), 400, 10, 90, 30));
	CHECK(GM_hole(NULL, NULL).w == 0);
	CHECK(rectIs(GM_topRoom(&area, &hole, 1), 10, 10, 480, 30));
	CHECK(rectIs(GM_topRoom(&area, &none, 1), 10, 10, 620, 30));
	CHECK(GM_topRoom(&area, &hole, 0).w == 0);
	CHECK(rectIs(GM_room(&area, &hole, 1), 10, 50, 620, 380));
	MosaicRect tiny = { 0, 0, 100, 100 };
	CHECK(GM_room(&tiny, &none, 1).w == 0);

	MosaicRect parts[4];
	CHECK(GM_avoid(&area, &none, parts) == 1 && rectIs(parts[0], 0, 0, 640, 480));
	CHECK(GM_avoid(&area, &hole, parts) == 4);
	CHECK(rectIs(parts[0], 0, 0, 640, 10) && rectIs(parts[1], 0, 10, 500, 30) && rectIs(parts[2], 630, 10, 10, 30)
		&& rectIs(parts[3], 0, 40, 640, 440));
	CHECK(GM_avoid(&none, &hole, parts) == 0);
}

///////////////////////////////////////
// the plugin against a fake host

struct MosaicSurface {
	int w, h;
};

static MosaicSurface screen_surface = { 1024, 768 };
static const MosaicRect AREA = { 0, 0, 1024, 768 };
static const MosaicRect STATUS = { 800, 10, 214, 30 };

static MosaicHost fake_host;
static MosaicScreenHooks registered;
static char registered_id[64];
static char action_id[64], action_label[64], tile_id[64];
static void (*action_run)(const char*, void*);
static int bound_button = -1;
static char last_log[1200];
static char logs[8192];
static char opened_screen[64];
static int redraws = 0;
static int hints_left_count = 0;
static char toast_text[300];
static const char* button_setting = "none";
static const char* toasts_setting = "on";
static int fills_into_hole = 0, texts_into_hole = 0;
static long long tracker_days[GM_HISTORY_DAYS];
static int tracker_known = 1;
static long long fake_mono = 1000;
static time_t fake_wall = 0;
static int fake_count = 0;
static long long fake_last = 0;

static int meetsHole(const MosaicRect* r) {
	return r->x < STATUS.x + STATUS.w && STATUS.x < r->x + r->w && r->y < STATUS.y + STATUS.h && STATUS.y < r->y + r->h;
}

static void fakeLog(const char* id, const char* message) {
	snprintf(last_log, sizeof(last_log), "%s", message);
	size_t used = strlen(logs);
	snprintf(logs + used, sizeof(logs) - used, "%s\n", message);
}

static int logged(const char* text) {
	return strstr(logs, text) != NULL;
}

static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (strcmp(key, "button") == 0) return button_setting;
	if (strcmp(key, "toasts") == 0) return toasts_setting;
	if (strcmp(key, "daily_minutes") == 0) return "20";
	if (strcmp(key, "daily_games") == 0) return "lots"; // bad: the default
	return fallback;
}

static void fakeRedraw(void) {
	redraws++;
}

static int fakeRegister(const char* id, const MosaicScreenHooks* hooks) {
	snprintf(registered_id, sizeof(registered_id), "%s", id);
	registered = *hooks;
	return 0;
}

static int fakeAction(const char* id, const char* label, int flags, int (*available)(const char*, void*), void (*run)(const char*, void*), void* data) {
	(void)flags;
	(void)data;
	snprintf(action_id, sizeof(action_id), "%s", id);
	snprintf(action_label, sizeof(action_label), "%s", label);
	CHECK(available(NULL, NULL) == 1);
	action_run = run;
	return 0;
}

static int fakeTile(const char* id, int row, const char* icon, const char* icon_off, int (*is_on)(void*)) {
	(void)icon;
	(void)icon_off;
	(void)is_on;
	CHECK(row == MOSAIC_QUICK_TOP);
	snprintf(tile_id, sizeof(tile_id), "%s", id);
	return 0;
}

static int fakeBind(int screen, int button, int trigger, const char* id) {
	CHECK(screen == MOSAIC_SCREEN_GAMELIST && trigger == MOSAIC_TRIGGER_PRESS);
	CHECK_STR(id, "gamification.open");
	bound_button = button;
	return 0;
}

static int fakeOpenScreen(const char* id) {
	snprintf(opened_screen, sizeof(opened_screen), "%s", id);
	return 0;
}

static void fakeToast(const char* message) {
	snprintf(toast_text, sizeof(toast_text), "%s", message);
}

static int fakeThemeColor(int index) {
	return index == MOSAIC_COLOR_ACCENT ? 0x9B2257 : -1;
}

static int fakeScale(void) {
	return 2;
}

static int fakeFill(MosaicSurface* target, const MosaicRect* rect, MosaicColor color) {
	(void)color;
	CHECK(target == &screen_surface);
	if (meetsHole(rect)) fills_into_hole++;
	return 0;
}

static int fakeFillRound(MosaicSurface* target, const MosaicRect* rect, int radius, MosaicColor color) {
	(void)radius;
	return fakeFill(target, rect, color);
}

static int fakeTextWidth(int font, const char* text) {
	(void)font;
	return (int)strlen(text) * 10;
}

static int fakeTextHeight(int font) {
	return font == MOSAIC_FONT_LARGE ? 32 : 24;
}

static int fakeTruncate(int font, const char* text, int max_width, char* out, size_t size) {
	(void)font;
	size_t n = (size_t)(max_width / 10);
	snprintf(out, size, "%.*s", (int)n, text);
	return (int)strlen(out) * 10;
}

static int fakeDrawText(MosaicSurface* target, int font, const char* text, int x, int y, MosaicColor color, int max_width) {
	(void)color;
	CHECK(target == &screen_surface);
	int w = fakeTextWidth(font, text);
	if (w > max_width) w = max_width;
	MosaicRect r = { x, y, w, fakeTextHeight(font) };
	if (w > 0 && meetsHole(&r)) texts_into_hole++;
	return w;
}

static int fakeDrawHints(MosaicSurface* screen, int group, const MosaicHint* hints, int count, MosaicRect* drawn) {
	(void)drawn;
	CHECK(screen == &screen_surface);
	CHECK(group == MOSAIC_HINTS_LEFT && count == 1);
	CHECK(hints[0].button == MOSAIC_BUTTON_B);
	CHECK_STR(hints[0].label, "BACK");
	hints_left_count = count;
	return 0;
}

static long long fakeTimeOn(int days_ago) {
	if (!tracker_known) return MOSAIC_PLAY_UNKNOWN;
	return days_ago >= 0 && days_ago < GM_HISTORY_DAYS ? tracker_days[days_ago] : 0;
}

static int fakeCount(const char* path) {
	(void)path;
	return tracker_known ? fake_count : MOSAIC_PLAY_UNKNOWN;
}

static long long fakeLast(const char* path) {
	(void)path;
	return tracker_known ? fake_last : MOSAIC_PLAY_UNKNOWN;
}

static long long fakePlayTime(const char* path) {
	(void)path;
	return tracker_known ? fake_count * 600LL : MOSAIC_PLAY_UNKNOWN;
}

static long long fakeMono(void) {
	return fake_mono;
}

static time_t fakeWall(void) {
	return fake_wall;
}

static void setupHost(void) {
	memset(&fake_host, 0, sizeof(fake_host));
	fake_host.size = sizeof(MosaicHost);
	fake_host.api_version = MOSAIC_API_VERSION;
	fake_host.userdata_path = DIR;
	fake_host.log = fakeLog;
	fake_host.cfg_get = fakeCfgGet;
	fake_host.request_redraw = fakeRedraw;
	fake_host.register_action = fakeAction;
	fake_host.bind_button = fakeBind;
	fake_host.toast = fakeToast;
	fake_host.quick_tile = fakeTile;
	fake_host.register_screen = fakeRegister;
	fake_host.open_screen = fakeOpenScreen;
	fake_host.theme_color = fakeThemeColor;
	fake_host.screen_scale = fakeScale;
	fake_host.fill_rect = fakeFill;
	fake_host.fill_round_rect = fakeFillRound;
	fake_host.text_width = fakeTextWidth;
	fake_host.text_height = fakeTextHeight;
	fake_host.text_truncate = fakeTruncate;
	fake_host.draw_text = fakeDrawText;
	fake_host.draw_hints = fakeDrawHints;
	fake_host.play_time_on = fakeTimeOn;
	fake_host.play_time = fakePlayTime;
	fake_host.play_count = fakeCount;
	fake_host.last_played = fakeLast;
	mono_now = fakeMono;
	wall_now = fakeWall;
}

static const MosaicPlugin* start(void) {
	setupHost();
	tile_id[0] = '\0';
	bound_button = -1;
	logs[0] = '\0';
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	CHECK(p->init(&fake_host) == 0);
	return p;
}

static void draw(void) {
	MosaicScreenView v;
	memset(&v, 0, sizeof(v));
	v.size = sizeof(v);
	v.area = AREA;
	v.status = STATUS;
	v.screen = SCREEN_ID;
	hints_left_count = 0;
	CHECK(registered.render(&screen_surface, &v, registered.data) == 0);
}

static int press(int button) {
	MosaicInput in = { sizeof(MosaicInput), MOSAIC_BUTTON_BIT(button), MOSAIC_BUTTON_BIT(button), 0,
		MOSAIC_BUTTON_BIT(button) };
	return registered.input(&in, registered.data);
}

static void reset(void) {
	unlink(DIR "/launcher/gamification/state.txt");
	unlink(GM_LAUNCH_FILE);
	memset(tracker_days, 0, sizeof(tracker_days));
	tracker_known = 1;
	toast_text[0] = '\0';
	fake_count = 0;
	fake_last = 0;
}

static void testInit(void) {
	reset();
	start();
	CHECK_STR(registered_id, "gamification.main");
	CHECK(registered.flags == 0 && registered.enter && registered.input && registered.render);
	CHECK_STR(action_id, "gamification.open");
	CHECK_STR(action_label, "Quests");
	CHECK_STR(tile_id, "gamification.open");
	CHECK(bound_button == -1);
	CHECK(logged("quests: play 20 minutes, 3 games, 5 days and 5 hours a week; streak at 5 minutes"));
	button_setting = "SELECT";
	start();
	CHECK(bound_button == MOSAIC_BUTTON_SELECT);
	button_setting = "none";
	action_run(NULL, NULL);
	CHECK_STR(opened_screen, "gamification.main");

	// an older launcher, without button hints
	setupHost();
	fake_host.size = offsetof(MosaicHost, draw_hints);
	CHECK(mosaic_plugin_get(&fake_host)->init(&fake_host) == 1);
}

static void testLaunches(void) {
	reset();
	mkdir(DIR, 0755);
	mkdir(DIR "/launcher", 0755);
	// Wednesday 10 January 2024, 10:00 UTC; the tracker has yesterday and
	// the day before at 10 minutes each
	fake_wall = 1704880800;
	int wednesday = GM_dayOf(fake_wall);
	CHECK(GM_weekday(wednesday) == 2);
	tracker_days[1] = 600;
	tracker_days[2] = 600;
	const MosaicPlugin* p = start();

	// a pak isn't counted, and leaves no launch behind
	MosaicLaunch pak = { "/card/Tools/desktop/Files.pak", MOSAIC_ENTRY_PAK };
	CHECK(p->pre_launch(&pak) == MOSAIC_LAUNCH_CONTINUE);
	struct stat st;
	CHECK(stat(GM_LAUNCH_FILE, &st) != 0);

	// a game never played before
	MosaicLaunch rom = { "/card/Roms/GB/Tetris.gb", MOSAIC_ENTRY_ROM };
	CHECK(p->pre_launch(&rom) == MOSAIC_LAUNCH_CONTINUE);
	CHECK(logged("launch /card/Roms/GB/Tetris.gb: fresh 1, revisit 0"));
	CHECK(stat(GM_LAUNCH_FILE, &st) == 0);

	// it ran for 25 minutes; the launcher starts again
	fake_mono += 1500;
	fake_wall += 1500;
	logs[0] = '\0';
	p = start();
	p->post_return();
	CHECK(logged("return /card/Roms/GB/Tetris.gb: 1500 seconds"));
	CHECK(logged("quest done: play (Play for 20 minutes today)"));
	CHECK(logged("quest done: fresh (Start a game you've never played)"));
	CHECK(logged("toast: 2 quests done"));
	CHECK_STR(toast_text, "2 quests done");
	CHECK(stat(GM_LAUNCH_FILE, &st) != 0);
	CHECK(state.completed == 2 && state.best_streak == 3);

	// the screen
	toast_text[0] = '\0';
	CHECK(registered.enter(NULL) == 0);
	fills_into_hole = texts_into_hole = 0;
	draw();
	CHECK(logged("showing 3 day streak, Best 3 days \xc2\xb7 2 quests done; play 25/20 done, games 1/3, fresh 1/1 done, "
		"revisit 0/1, week_days 3/5, week_hours 45/300"));
	CHECK(hints_left_count == 1);
	CHECK(fills_into_hole == 0 && texts_into_hole == 0);
	// it all fits at this size, so up and down don't move it
	redraws = 0;
	CHECK(press(MOSAIC_BUTTON_DOWN) == MOSAIC_SCREEN_STAY);
	CHECK(redraws == 0 && first == 0);
	CHECK(press(MOSAIC_BUTTON_B) == MOSAIC_SCREEN_CLOSE);

	// a second launch the same day: an old favourite, back after 40 days
	fake_count = 4;
	fake_last = fake_wall - 40LL * DAY;
	MosaicLaunch zelda = { "/card/Roms/GB/Zelda.gb", MOSAIC_ENTRY_ROM };
	CHECK(p->pre_launch(&zelda) == MOSAIC_LAUNCH_CONTINUE);
	CHECK(logged("launch /card/Roms/GB/Zelda.gb: fresh 0, revisit 1"));
	fake_mono += 60;
	fake_wall += 60;
	logs[0] = '\0';
	p = start(); // loads the state the first start saved
	CHECK(state.completed == 2 && GM_day(&state, wednesday)->launches == 1);
	p->post_return();
	CHECK(logged("return /card/Roms/GB/Zelda.gb: 60 seconds"));
	CHECK(logged("toast: Quest done: Go back to a game you haven't played in 30 days"));
	CHECK(GM_day(&state, wednesday)->games == 2 && GM_day(&state, wednesday)->seconds == 1560);

	// a return without a launch: nothing new, and no toast
	toast_text[0] = '\0';
	logs[0] = '\0';
	p = start();
	p->post_return();
	CHECK(!logged("return ") && toast_text[0] == '\0');

	// the next day: the streak is cheered once, with the toasts off it's
	// only logged
	fake_wall += DAY;
	memmove(&tracker_days[1], &tracker_days[0], sizeof(tracker_days) - sizeof(tracker_days[0]));
	tracker_days[0] = 0;
	fake_last = fake_wall - DAY;
	CHECK(p->pre_launch(&rom) == MOSAIC_LAUNCH_CONTINUE);
	fake_mono += 400;
	fake_wall += 400;
	toasts_setting = "off";
	logs[0] = '\0';
	p = start();
	p->post_return();
	CHECK(logged("toast: 4 day streak"));
	CHECK(toast_text[0] == '\0');
	CHECK(state.cheered == wednesday + 1);
	CHECK(p->pre_launch(&rom) == MOSAIC_LAUNCH_CONTINUE);
	fake_mono += 400;
	fake_wall += 400;
	logs[0] = '\0';
	p = start();
	p->post_return();
	CHECK(!logged("toast: "));
	toasts_setting = "on";

	// a launch whose clock is off is dropped
	CHECK(p->pre_launch(&rom) == MOSAIC_LAUNCH_CONTINUE);
	fake_wall -= 1000;
	logs[0] = '\0';
	p = start();
	p->post_return();
	CHECK(logged("dropped the launch of /card/Roms/GB/Tetris.gb: its clock is off"));
	CHECK(!logged("return "));
}

static void testNoTracker(void) {
	reset();
	tracker_known = 0;
	fake_wall = 1704880800;
	const MosaicPlugin* p = start();
	MosaicLaunch rom = { "/card/Roms/GB/Tetris.gb", MOSAIC_ENTRY_ROM };
	CHECK(p->pre_launch(&rom) == MOSAIC_LAUNCH_CONTINUE);
	CHECK(logged("launch /card/Roms/GB/Tetris.gb: fresh 0, revisit 0"));
	fake_mono += 400;
	fake_wall += 400;
	logs[0] = '\0';
	p = start();
	p->post_return();
	CHECK(logged("toast: 1 day streak"));
	CHECK(registered.enter(NULL) == 0);
	draw();
	CHECK(logged("showing 1 day streak, Best 1 day \xc2\xb7 0 quests done; play 6/20, games 1/3, fresh 0/1, revisit 0/1, "
		"week_days 1/5, week_hours 6/300"));
}

// playtime-limit's limit.left, as it answers with a limit set
static long long limit_left = 600;
static int fakeLimitLeft(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	(void)args;
	snprintf(out, size, "state=ok\nlimit=90\ngranted=0\nleft=%lld\ntext=10m left", limit_left);
	return 0;
}

// row-badges' games.played
static int played_answer = 1;
static int fakePlayed(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	char path[512];
	CHECK(fakeEventValue(args, "path", path, sizeof(path)) > 0);
	snprintf(out, size, "played=%i", played_answer);
	return 0;
}

static void testServices(void) {
	reset();
	// Wednesday 10 January 2024, 10:00 UTC, with 10 minutes left of a
	// 90 minute limit: the daily 20 minutes don't fit, and the week's 5
	// hours do (10 minutes and 90 on each of four more days)
	fake_wall = 1704880800;
	fake_provider_count = 0;
	fakeProvide("limit.left", fakeLimitLeft);
	fakeProvide("games.played", fakePlayed);
	const MosaicPlugin* p = start();
	fake_host.call = fakeCall;
	fake_host.event_value = fakeEventValue;
	CHECK(registered.enter(NULL) == 0);
	CHECK(logged("left out play: it needs 20 minutes, and the limit leaves 10"));
	CHECK(!logged("left out week_hours"));
	draw();
	CHECK(logged("showing No streak, Best 0 days \xc2\xb7 0 quests done; play 0/20 left out, games 0/3, fresh 0/1, revisit 0/1, "
		"week_days 0/5, week_hours 0/300"));
	// the RomM quest isn't offered without romm
	CHECK(running_count == GM_QUESTS - 2 && running[0] == GM_Q_GAMES);

	// kept for the day, though there's more time now
	limit_left = 7200;
	logs[0] = '\0';
	p = start();
	fake_host.call = fakeCall;
	fake_host.event_value = fakeEventValue;
	CHECK(registered.enter(NULL) == 0);
	draw();
	CHECK(logged("play 0/20 left out"));
	CHECK(!logged("left out play"));

	// a game the history hasn't seen, but games.played calls played
	fake_count = 0;
	MosaicLaunch rom = { "/card/Roms/GB/Tetris.gb", MOSAIC_ENTRY_ROM };
	CHECK(p->pre_launch(&rom) == MOSAIC_LAUNCH_CONTINUE);
	CHECK(logged("launch /card/Roms/GB/Tetris.gb: fresh 0, revisit 0"));
	// and one the history has, that games.played doesn't
	played_answer = 0;
	fake_count = 2;
	CHECK(p->pre_launch(&rom) == MOSAIC_LAUNCH_CONTINUE);
	CHECK(logged("launch /card/Roms/GB/Tetris.gb: fresh 1, revisit 0"));
	unlink(GM_LAUNCH_FILE);

	// the next day, without playtime-limit: every quest runs
	fake_provider_count = 0;
	fakeProvide("games.played", fakePlayed);
	fake_wall += DAY;
	logs[0] = '\0';
	p = start();
	fake_host.call = fakeCall;
	fake_host.event_value = fakeEventValue;
	CHECK(registered.enter(NULL) == 0);
	draw();
	CHECK(logged("play 0/20, games"));
	CHECK(running_count == GM_QUESTS - 1);

	fake_provider_count = 0;
	played_answer = 1;
	limit_left = 600;
	fake_count = 0;
}

static int romm_server = 1;
static int fakeRommSynced(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	(void)args;
	snprintf(out, size, "server=%d\n", romm_server);
	return 0;
}

static void (*download_done)(const char*, const char*, void*);
static int fakeSubscribe(const char* name, void (*on_event)(const char*, const char*, void*), void* data) {
	(void)data;
	if (!strcmp(name, "romm.download_done")) download_done = on_event;
	return 0;
}

static int fakeSetNames(void (*each)(const char*, void*), void* data) {
	(void)each;
	(void)data;
	return 0;
}

static const MosaicPlugin* startWithRomm(void) {
	setupHost();
	fake_host.call = fakeCall;
	fake_host.event_value = fakeEventValue;
	fake_host.subscribe = fakeSubscribe;
	fake_host.set_names = fakeSetNames;
	tile_id[0] = '\0';
	logs[0] = '\0';
	download_done = NULL;
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	CHECK(p->init(&fake_host) == 0);
	return p;
}

static void testRomm(void) {
	reset();
	fake_wall = 1704880800;
	fake_provider_count = 0;
	fakeProvide("romm.synced", fakeRommSynced);
	startWithRomm();
	CHECK(download_done != NULL);
	CHECK(registered.enter(NULL) == 0);
	draw();
	CHECK(logged("revisit 0/1, download 0/1, week_days 0/5"));
	CHECK(running_count == GM_QUESTS && running[4] == GM_Q_DOWNLOAD);
	// a download completes it, with a toast
	download_done("romm.download_done", "path=/card/Roms/GB/Tetris.gb\nname=Tetris", NULL);
	CHECK(logged("download /card/Roms/GB/Tetris.gb"));
	CHECK(logged("quest done: download (Download a game from RomM)"));
	CHECK_STR(toast_text, "Quest done: Download a game from RomM");
	CHECK(GM_day(&state, GM_dayOf(fake_wall))->downloads == 1);
	// a second one the same day counts, without completing it again
	toast_text[0] = '\0';
	download_done("romm.download_done", "path=/card/Roms/GB/Zelda.gb", NULL);
	CHECK(GM_day(&state, GM_dayOf(fake_wall))->downloads == 2 && toast_text[0] == '\0');
	draw();
	CHECK(logged("download 2/1 done"));

	// romm without a server: not offered, and a download still counts
	reset();
	romm_server = 0;
	startWithRomm();
	CHECK(registered.enter(NULL) == 0);
	draw();
	CHECK(logged("revisit 0/1, week_days 0/5"));
	CHECK(running_count == GM_QUESTS - 1);
	download_done("romm.download_done", "path=/card/Roms/GB/Tetris.gb", NULL);
	CHECK(GM_day(&state, GM_dayOf(fake_wall))->downloads == 1 && toast_text[0] == '\0');
	CHECK(state.done[GM_Q_DOWNLOAD] == -1);
	romm_server = 1;
	fake_provider_count = 0;
}

static void testScroll(void) {
	reset();
	fake_wall = 1704880800;
	start();
	CHECK(registered.enter(NULL) == 0);
	// a short screen fits a few lines
	MosaicScreenView v;
	memset(&v, 0, sizeof(v));
	v.size = sizeof(v);
	v.area = (MosaicRect){ 0, 0, 640, 360 };
	v.screen = SCREEN_ID;
	CHECK(registered.render(&screen_surface, &v, registered.data) == 0);
	CHECK(fit_rows == 2);
	redraws = 0;
	CHECK(press(MOSAIC_BUTTON_UP) == MOSAIC_SCREEN_STAY && first == 0 && redraws == 0);
	CHECK(press(MOSAIC_BUTTON_DOWN) == MOSAIC_SCREEN_STAY && first == 1 && redraws == 1);
	for (int i = 0; i < 10; i++) press(MOSAIC_BUTTON_DOWN);
	CHECK(first == running_count - 2);
	CHECK(press(MOSAIC_BUTTON_UP) == MOSAIC_SCREEN_STAY && first == running_count - 3);
	// enter starts at the top again
	CHECK(registered.enter(NULL) == 0 && first == 0);
}

int main(void) {
	setenv("TZ", "UTC", 1);
	tzset();
	testDays();
	testLaunch();
	testPlay();
	testFormat();
	testLaunchLine();
	testStreak();
	testProgress();
	testPickQuests();
	testText();
	testWords();
	testLayout();
	testInit();
	testLaunches();
	testNoTracker();
	testServices();
	testRomm();
	testScroll();
	CHECK_DONE();
}
