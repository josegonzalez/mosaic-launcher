// Music: the front end of musicd, a daemon of its own that plays music
// outside the launcher, so it can go on while the launcher exits for a
// game. The daemon ships beside this plugin.so; the plugin starts it, by
// its path with fork and exec, the first time something is played, and
// talks to it over a Unix socket (music_model.h has the protocol).
//
// music.player is a screen over the game list with the music folder's
// folders and tracks (MP3, FLAC and WAV) and what's playing. A plays a
// track, then the rest of its folder, or opens a folder; B goes back up,
// and closes it from the music folder. X pauses or plays on, L1 and R1 go
// to the previous and next track, L2 and R2 seek 10 seconds, SELECT turns
// shuffle on or off and START steps repeat through off, all and one (both
// saved as settings). Y opens music.lock, a full screen with the clock and
// the track, which takes every button but power until SELECT then START.
// The music.open action opens the player, from a quick menu tile or a
// game list button; music.lock and music.toggle are there for FN buttons.
//
// Over the other screens a now playing pill shows at the bottom for a few
// seconds after a track starts (the overlay setting). While music plays it
// holds off auto-sleep (the awake setting). Before a game or pak it pauses
// the music and lets go of the audio device, and plays on when the
// launcher comes back, unless the games setting is play. Before a sleep
// the music pauses; before a power-off the daemon quits.

#define _GNU_SOURCE // dladdr
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "mosaic_plugin.h"
#include "music_model.h"

#define ID "music"
#define PLAYER_ID ID ".player"
#define LOCK_ID ID ".lock"
#define OPEN_ACTION ID ".open"
#define LOCK_ACTION ID ".lock"
#define TOGGLE_ACTION ID ".toggle"
#define DAEMON_NAME "musicd"
#define REQUEST_MS 300 // the status line's wait for its answer
// other commands' wait: the daemon opens or closes the audio device before
// it answers, which can take a while
#define DEVICE_REQUEST_MS 3000
#define START_WAIT_MS 2000   // a started daemon's wait to answer
#define POLL_SCREEN_MS 500   // asking for the status while a screen shows
#define POLL_PLAYING_MS 1000 // while something plays or is paused
#define POLL_IDLE_MS 5000    // while the daemon runs with nothing queued
#define SEEK_MS 10000
#define DIM 140 // the lock screen's alpha over black

extern char** environ;

static const MosaicHost* host;
static char root[MU_PATH_MAX];
static char daemon_path[MU_PATH_MAX];
static char log_path[MU_PATH_MAX];
static int games_play = 0; // keep playing during games
static int awake = 1;
static int shuffle = 0, repeat = MU_REPEAT_OFF;

static int daemon_up = 0;
static MuStatus st;
static unsigned long next_poll = 0, last_now = 0;
static int awake_held = 0;
static char logged_now[MU_PATH_MAX + 64];

static MuOverlay overlay;
static int overlay_shown = 0;

// the player
static char dir[MU_PATH_MAX];
static MuList list;
static int selected = 0, first_row = 0, visible_rows = 1;
static int player_open = 0;
static char logged_view[MU_PATH_MAX + MU_NAME_MAX + 64];

// the lock screen
static int lock_open = 0, unlock_due = 0;
static MuUnlock unlock;
static char lock_clock[16];

static void say(const char* format, ...) __attribute__((format(printf, 1, 2)));
static void say(const char* format, ...) {
	char text[MU_PATH_MAX + 256];
	va_list args;
	va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	host->log(ID, text);
}

///////////////////////////////////////
// the daemon

// Sends a line and reads the one back. Returns 0 with the answer in
// reply, -1 when no daemon listens, -2 when it didn't answer in time.
static int request(const char* line, char* reply, size_t size, int timeout_ms) {
	if (size) reply[0] = '\0';
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) return -1;
	fcntl(fd, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
	int one = 1;
	setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
	struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", MU_SOCKET_PATH);
	if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
		close(fd);
		return -1;
	}
#ifdef MSG_NOSIGNAL
	int send_flags = MSG_NOSIGNAL;
#else
	int send_flags = 0;
#endif
	size_t len = strlen(line), sent = 0;
	while (sent < len) {
		ssize_t n = send(fd, line + sent, len - sent, send_flags);
		if (n <= 0) {
			close(fd);
			return -2;
		}
		sent += (size_t)n;
	}
	size_t got = 0;
	while (got + 1 < size) {
		ssize_t n = read(fd, reply + got, size - 1 - got);
		if (n <= 0) break;
		got += (size_t)n;
		if (memchr(reply, '\n', got)) break;
	}
	close(fd);
	reply[got] = '\0';
	return memchr(reply, '\n', got) ? 0 : -2;
}

static int sendCommand(int command, const char* arg, int timeout_ms) {
	char line[MU_LINE_MAX], reply[MU_LINE_MAX];
	if (MU_formatCommand(command, arg, line, sizeof(line)) < 0) return -1;
	int rc = request(line, reply, sizeof(reply), timeout_ms);
	if (rc == -1) daemon_up = 0;
	if (rc != 0) return rc;
	if (!strncmp(reply, "ok", 2)) return 0;
	char* end = strchr(reply, '\n');
	if (end) *end = '\0';
	say("the daemon said %s to %.*s", reply, (int)strcspn(line, "\n"), line);
	return -3;
}

static void logNowPlaying(void) {
	char text[sizeof(logged_now)];
	if (st.state == MU_STOPPED) snprintf(text, sizeof(text), "stopped");
	else {
		char title[MU_NAME_MAX], len[16];
		MU_trackTitle(st.track, title, sizeof(title));
		MU_formatTime(st.len_ms, len, sizeof(len));
		snprintf(text, sizeof(text), "%s, %s of %s%s", title, MU_stateName(st.state), len, st.held ? ", held" : "");
	}
	if (strcmp(text, logged_now) == 0) return;
	snprintf(logged_now, sizeof(logged_now), "%s", text);
	say("now: %s", text);
}

// Asks the daemon what it's doing.
static void askStatus(unsigned long now) {
	char reply[MU_LINE_MAX];
	MuStatus s;
	int rc = request("status\n", reply, sizeof(reply), REQUEST_MS);
	if (rc == 0 && MU_parseStatus(reply, &s)) {
		if (!daemon_up) say("the daemon is running");
		daemon_up = 1;
		st = s;
	} else {
		if (daemon_up) say("the daemon stopped answering");
		daemon_up = 0;
		memset(&st, 0, sizeof(st));
	}
	logNowPlaying();
	if (MuOverlay_update(&overlay, &st, now)) overlay_shown = 0;
	unsigned long wait = player_open || lock_open ? POLL_SCREEN_MS
		: !daemon_up ? 0
		: st.state != MU_STOPPED ? POLL_PLAYING_MS
		: POLL_IDLE_MS;
	next_poll = wait ? now + wait : 0;
}

static int daemonPid(void) {
	FILE* f = fopen(MU_PID_PATH, "r");
	if (!f) return -1;
	long pid = 0;
	int ok = fscanf(f, "%ld", &pid) == 1;
	fclose(f);
	return ok && pid > 1 ? (int)pid : -1;
}

// The launcher's environment without libraries preloaded into it, such as
// the desktop tests' presser, which the daemon doesn't have the symbols
// for. NULL without memory.
static char** daemonEnv(void) {
	int count = 0;
	while (environ[count]) count++;
	char** env = malloc((size_t)(count + 1) * sizeof(char*));
	if (!env) return NULL;
	int n = 0;
	for (int i = 0; i < count; i++) {
		if (!strncmp(environ[i], "LD_PRELOAD=", 11) || !strncmp(environ[i], "DYLD_INSERT_LIBRARIES=", 22)) continue;
		env[n++] = environ[i];
	}
	env[n] = NULL;
	return env;
}

// Starts the daemon in a session of its own, so it isn't the launcher's
// child or in its process group, with its output in the log file and no
// other file of the launcher's open. Waits for it to answer.
static int startDaemon(void) {
	struct stat sb;
	if (!daemon_path[0] || stat(daemon_path, &sb) != 0 || !S_ISREG(sb.st_mode) || access(daemon_path, X_OK) != 0) {
		say("no daemon at %s", daemon_path[0] ? daemon_path : "(unknown)");
		return -1;
	}
	char** env = daemonEnv();
	if (!env) return -1;
	int out = open(log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
	int in = open("/dev/null", O_RDONLY | O_CLOEXEC);
	if (out < 0) out = open("/dev/null", O_WRONLY | O_CLOEXEC);
	long max_fd = sysconf(_SC_OPEN_MAX);
	if (max_fd < 0 || max_fd > 4096) max_fd = 4096;
	char* const argv[] = { daemon_path, NULL };
	pid_t child = fork();
	if (child == 0) {
		setsid();
		pid_t grandchild = fork();
		if (grandchild != 0) _exit(grandchild < 0 ? 1 : 0);
		if (in >= 0) dup2(in, 0);
		if (out >= 0) {
			dup2(out, 1);
			dup2(out, 2);
		}
		for (int fd = 3; fd < max_fd; fd++) close(fd);
		sigset_t none;
		sigemptyset(&none);
		sigprocmask(SIG_SETMASK, &none, NULL);
		execve(daemon_path, argv, env);
		_exit(127);
	}
	free(env);
	if (in >= 0) close(in);
	if (out >= 0) close(out);
	if (child < 0) {
		say("couldn't start the daemon: %s", strerror(errno));
		return -1;
	}
	int status = 0;
	while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
	}
	for (int waited = 0; waited < START_WAIT_MS; waited += 20) {
		char reply[MU_LINE_MAX];
		if (request("status\n", reply, sizeof(reply), REQUEST_MS) == 0) {
			daemon_up = 1;
			say("started the daemon, pid %i", daemonPid());
			return 0;
		}
		usleep(20000);
	}
	say("the daemon didn't answer after it started; its log is %s", log_path);
	return -1;
}

static int ensureDaemon(void) {
	if (daemon_up || sendCommand(MU_CMD_STATUS, NULL, REQUEST_MS) == 0) {
		daemon_up = 1;
		return 0;
	}
	return startDaemon();
}

// Plays a track and the rest of its folder.
static void playPath(const char* path) {
	if (ensureDaemon() != 0) {
		if (MOSAIC_HOST_HAS(host, toast)) host->toast("Music can't play");
		return;
	}
	sendCommand(MU_CMD_REPEAT, MU_repeatName(repeat), REQUEST_MS);
	sendCommand(MU_CMD_SHUFFLE, shuffle ? "on" : "off", REQUEST_MS);
	if (sendCommand(MU_CMD_PLAY, path, DEVICE_REQUEST_MS) == 0) say("playing %s", path);
	else if (MOSAIC_HOST_HAS(host, toast)) host->toast("Music can't play that");
	askStatus(last_now);
}

static void command(int cmd, const char* arg) {
	if (!daemon_up) return;
	sendCommand(cmd, arg, DEVICE_REQUEST_MS);
	askStatus(last_now);
}

///////////////////////////////////////
// drawing

static MosaicColor themeColor(int index, unsigned int fallback, unsigned char alpha) {
	int rgb = host->theme_color(index);
	return mosaic_color(rgb < 0 ? fallback : (unsigned int)rgb, alpha);
}

static int scale(void) {
	int s = host->screen_scale();
	return s < 1 ? 1 : s;
}

static int textCentred(MosaicSurface* screen, int font, const char* text, const MosaicRect* box, int y,
	MosaicColor color) {
	char cut[MU_PATH_MAX];
	int w = host->text_truncate(font, text, box->w, cut, sizeof(cut));
	if (w < 0) return -1;
	return host->draw_text(screen, font, cut, box->x + (box->w - w) / 2, y, color, box->w);
}

static void stateLine(char* out, size_t size) {
	char pos[16], len[16];
	MU_formatTime(st.pos_ms, pos, sizeof(pos));
	MU_formatTime(st.len_ms, len, sizeof(len));
	snprintf(out, size, "%s  %s / %s%s  Repeat %s", st.state == MU_PLAYING ? "Playing" : "Paused", pos, len,
		st.shuffle ? "  Shuffle" : "", MU_repeatName(st.repeat));
}

// the hole the status pill and header widgets leave in the top row
static MosaicRect hole(const MosaicScreenView* view) {
	MosaicRect a = view->status, b = view->header;
	if (b.w <= 0) return a;
	if (a.w <= 0) return b;
	int x1 = a.x < b.x ? a.x : b.x, y1 = a.y < b.y ? a.y : b.y;
	int x2 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
	int y2 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
	return (MosaicRect){ x1, y1, x2 - x1, y2 - y1 };
}

static void logView(void) {
	char text[sizeof(logged_view)];
	if (list.count <= 0) snprintf(text, sizeof(text), "%s: empty", dir);
	else {
		snprintf(text, sizeof(text), "%s: %i of %i, %s%s", dir, selected + 1, list.count, list.items[selected].name,
			list.items[selected].is_dir ? "/" : "");
	}
	if (strcmp(text, logged_view) == 0) return;
	snprintf(logged_view, sizeof(logged_view), "%s", text);
	say("showing %s", text);
}

static int playerRender(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
	(void)data;
	const MosaicRect area = view->area;
	int s = scale();
	int pad = 10 * s, pill = 30 * s, gap = 4 * s;
	int large = host->text_height(MOSAIC_FONT_LARGE), small = host->text_height(MOSAIC_FONT_SMALL);
	MosaicColor bg = themeColor(MOSAIC_COLOR_BACKGROUND, 0x000000, 255);
	MosaicColor text = themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF, 255);
	MosaicColor text_on = themeColor(MOSAIC_COLOR_LIST_TEXT_SELECTED, 0x000000, 255);
	MosaicColor fg = themeColor(MOSAIC_COLOR_MAIN, 0xFFFFFF, 255);
	MosaicColor accent = themeColor(MOSAIC_COLOR_ACCENT, 0x9B2257, 255);
	MosaicColor hint = themeColor(MOSAIC_COLOR_HINT, 0x999999, 255);
	MosaicRect top = hole(view);

	// the background, around the status pill
	MosaicRect fill = area;
	if (top.w > 0) {
		MosaicRect left = { area.x, area.y, top.x - area.x, top.y + top.h - area.y };
		MosaicRect below = { area.x, top.y + top.h, area.w, area.y + area.h - top.y - top.h };
		MosaicRect right = { top.x + top.w, area.y, area.x + area.w - top.x - top.w, top.y + top.h - area.y };
		MosaicRect above = { top.x, area.y, top.w, top.y - area.y };
		host->fill_rect(screen, &left, bg);
		host->fill_rect(screen, &below, bg);
		if (right.w > 0) host->fill_rect(screen, &right, bg);
		if (above.h > 0) host->fill_rect(screen, &above, bg);
	} else host->fill_rect(screen, &fill, bg);

	// the folder's name in the top row
	const char* slash = strrchr(dir, '/');
	const char* name = strcmp(dir, root) == 0 ? "Music" : slash ? slash + 1 : dir;
	int title_max = (top.w > 0 ? top.x : area.x + area.w) - area.x - 3 * pad;
	if (title_max > 0) {
		char cut[MU_NAME_MAX];
		int w = host->text_truncate(MOSAIC_FONT_LARGE, name, title_max, cut, sizeof(cut));
		if (w > 0) {
			MosaicRect rect = { area.x + pad, area.y + pad, w + 2 * pad, pill };
			host->fill_round_rect(screen, &rect, pill / 2, accent);
			host->draw_text(screen, MOSAIC_FONT_LARGE, cut, rect.x + pad, rect.y + (pill - large) / 2, fg, 0);
		}
	}

	// what's playing, above the hints
	int hints_top = area.y + area.h - pad - pill;
	int panel_h = st.state != MU_STOPPED ? large + small + 3 * gap + gap : 0;
	int panel_y = hints_top - gap - panel_h;
	if (panel_h) {
		MosaicRect box = { area.x + pad, panel_y, area.w - 2 * pad, panel_h };
		char title[MU_NAME_MAX], line[128];
		MU_trackTitle(st.track, title, sizeof(title));
		textCentred(screen, MOSAIC_FONT_LARGE, title, &box, panel_y, fg);
		stateLine(line, sizeof(line));
		textCentred(screen, MOSAIC_FONT_SMALL, line, &box, panel_y + large + gap, hint);
		MosaicRect bar = { box.x, panel_y + large + small + 2 * gap, box.w, gap };
		host->fill_round_rect(screen, &bar, gap / 2, themeColor(MOSAIC_COLOR_HINT, 0x999999, 80));
		if (st.len_ms > 0) {
			long long pos = st.pos_ms > st.len_ms ? st.len_ms : st.pos_ms;
			bar.w = (int)(box.w * pos / st.len_ms);
			if (bar.w > 0) host->fill_round_rect(screen, &bar, gap / 2, accent);
		}
	}

	// the folder's entries
	int list_top = area.y + pad + pill + gap;
	if (top.w > 0 && top.y + top.h + gap > list_top) list_top = top.y + top.h + gap;
	int list_bottom = (panel_h ? panel_y : hints_top) - gap;
	visible_rows = (list_bottom - list_top) / pill;
	if (visible_rows < 1) visible_rows = 1;
	if (list.count <= 0) {
		MosaicRect box = { area.x + pad, list_top, area.w - 2 * pad, list_bottom - list_top };
		char message[MU_PATH_MAX + 32];
		snprintf(message, sizeof(message), "No music in %s", strcmp(dir, root) == 0 ? root + strlen(host->sdcard_path) : name);
		textCentred(screen, MOSAIC_FONT_LARGE, message, &box, list_top + (box.h - large) / 2, text);
	} else {
		if (selected >= list.count) selected = list.count - 1;
		first_row = MU_window(selected, list.count, visible_rows, first_row);
		for (int r = 0; r < visible_rows && first_row + r < list.count; r++) {
			int i = first_row + r;
			char label[MU_NAME_MAX + 2], cut[MU_NAME_MAX + 2];
			if (list.items[i].is_dir) snprintf(label, sizeof(label), "%s/", list.items[i].name);
			else MU_trackTitle(list.items[i].name, label, sizeof(label));
			int y = list_top + r * pill;
			int max = area.w - 4 * pad;
			int w = host->text_truncate(MOSAIC_FONT_LARGE, label, max, cut, sizeof(cut));
			if (w < 0) continue;
			int playing = !list.items[i].is_dir && st.state != MU_STOPPED;
			if (playing) {
				char path[MU_PATH_MAX];
				playing = MU_childPath(dir, list.items[i].name, path, sizeof(path)) && !strcmp(path, st.track);
			}
			if (i == selected) {
				MosaicRect rect = { area.x + pad, y, w + 2 * pad, pill };
				host->fill_round_rect(screen, &rect, pill / 2, fg);
			}
			host->draw_text(screen, MOSAIC_FONT_LARGE, cut, area.x + 2 * pad, y + (pill - large) / 2,
				i == selected ? text_on : playing ? accent : text, 0);
		}
	}

	if (MOSAIC_HOST_HAS(host, draw_hints)) {
		MosaicHint left[] = { { MOSAIC_BUTTON_B, "BACK" }, { MOSAIC_BUTTON_Y, "LOCK" } };
		int on_dir = list.count > 0 && list.items[selected].is_dir;
		MosaicHint right[] = { { MOSAIC_BUTTON_X, st.state == MU_PLAYING ? "PAUSE" : "PLAY" },
			{ MOSAIC_BUTTON_A, on_dir ? "OPEN" : "PLAY" } };
		host->draw_hints(screen, MOSAIC_HINTS_LEFT, left, 2, NULL);
		if (host->draw_hints(screen, MOSAIC_HINTS_RIGHT, right, list.count > 0 ? 2 : 1, NULL) != 0) {
			host->draw_hints(screen, MOSAIC_HINTS_RIGHT, right + 1, 1, NULL);
		}
	}
	logView();
	return 0;
}

static int lockRender(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
	(void)data;
	const MosaicRect area = view->area;
	int s = scale();
	int large = host->text_height(MOSAIC_FONT_LARGE), small = host->text_height(MOSAIC_FONT_SMALL);
	host->fill_rect(screen, &area, mosaic_color(0x000000, 255));
	MosaicColor fg = themeColor(MOSAIC_COLOR_MAIN, 0xFFFFFF, DIM);
	MosaicColor hint = themeColor(MOSAIC_COLOR_HINT, 0x999999, DIM);
	MosaicRect box = { area.x + 10 * s, area.y, area.w - 20 * s, area.h };
	int y = area.y + area.h / 2 - large * 2;
	textCentred(screen, MOSAIC_FONT_LARGE, lock_clock, &box, y, fg);
	y += large * 2;
	if (st.state != MU_STOPPED) {
		char title[MU_NAME_MAX], line[128];
		MU_trackTitle(st.track, title, sizeof(title));
		textCentred(screen, MOSAIC_FONT_LARGE, title, &box, y, fg);
		stateLine(line, sizeof(line));
		textCentred(screen, MOSAIC_FONT_SMALL, line, &box, y + large + 4 * s, hint);
	}
	textCentred(screen, MOSAIC_FONT_SMALL, "Locked: SELECT then START to unlock", &box,
		area.y + area.h - 10 * s - small, hint);
	return 0;
}

static int overlayRender(MosaicSurface* surface, const MosaicScreenView* view) {
	if (!MuOverlay_visible(&overlay, last_now)) return 0;
	if (view->screen && (!strcmp(view->screen, PLAYER_ID) || !strcmp(view->screen, LOCK_ID))) return 0;
	const MosaicRect area = view->area;
	int s = scale();
	int pad = 10 * s, pill = 30 * s;
	int h = host->text_height(MOSAIC_FONT_SMALL);
	char title[MU_NAME_MAX], text[MU_NAME_MAX + 16], cut[MU_NAME_MAX + 16];
	MU_trackTitle(st.track, title, sizeof(title));
	snprintf(text, sizeof(text), "%s: %s", st.state == MU_PLAYING ? "Playing" : "Paused", title);
	int w = host->text_truncate(MOSAIC_FONT_SMALL, text, area.w / 3 - 2 * pad, cut, sizeof(cut));
	if (w <= 0) return 0;
	MosaicRect rect = { area.x + (area.w - w - 2 * pad) / 2, area.y + area.h - pad - pill, w + 2 * pad, pill };
	host->fill_round_rect(surface, &rect, pill / 2, mosaic_color(0x000000, 200));
	host->draw_text(surface, MOSAIC_FONT_SMALL, cut, rect.x + pad, rect.y + (pill - h) / 2,
		themeColor(MOSAIC_COLOR_MAIN, 0xFFFFFF, 255), 0);
	if (!overlay_shown) {
		overlay_shown = 1;
		say("pill over %s: %s", view->screen ? view->screen : "?", cut);
	}
	return 0;
}

///////////////////////////////////////
// the player

static void listDir(const char* path, const char* select_name) {
	snprintf(dir, sizeof(dir), "%s", path);
	MU_scan(dir, &list, 1);
	selected = first_row = 0;
	for (int i = 0; select_name && i < list.count; i++) {
		if (!strcmp(list.items[i].name, select_name)) selected = i;
	}
	// the track playing, when it's here
	if (!select_name && st.state != MU_STOPPED) {
		for (int i = list.dirs; i < list.count; i++) {
			char p[MU_PATH_MAX];
			if (MU_childPath(dir, list.items[i].name, p, sizeof(p)) && !strcmp(p, st.track)) selected = i;
		}
	}
}

static int playerEnter(void* data) {
	(void)data;
	player_open = 1;
	logged_view[0] = '\0';
	askStatus(last_now);
	// back in the playing track's folder, when it's under the music folder
	char start[MU_PATH_MAX];
	snprintf(start, sizeof(start), "%s", root);
	size_t root_len = strlen(root);
	if (st.state != MU_STOPPED && !strncmp(st.track, root, root_len) && st.track[root_len] == '/') {
		const char* slash = strrchr(st.track, '/');
		snprintf(start, sizeof(start), "%.*s", (int)(slash - st.track), st.track);
	}
	listDir(start, NULL);
	return 0;
}

static void playerExit(void* data) {
	(void)data;
	player_open = 0;
	MU_listFree(&list);
}

static int playerInput(const MosaicInput* in, void* data) {
	(void)data;
#define PRESSED(b) (in->pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_##b))
#define REPEATED(b) (in->repeated & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_##b))
	int before = selected;
	if (PRESSED(B)) {
		char up[MU_PATH_MAX];
		if (!MU_parentPath(root, dir, up, sizeof(up))) return MOSAIC_SCREEN_CLOSE;
		const char* slash = strrchr(dir, '/');
		char from[MU_NAME_MAX];
		snprintf(from, sizeof(from), "%s", slash ? slash + 1 : "");
		listDir(up, from);
	} else if (PRESSED(A) && list.count > 0) {
		char path[MU_PATH_MAX];
		if (MU_childPath(dir, list.items[selected].name, path, sizeof(path))) {
			if (list.items[selected].is_dir) listDir(path, NULL);
			else playPath(path);
		}
	} else if (PRESSED(X)) {
		if (st.state != MU_STOPPED) command(MU_CMD_TOGGLE, NULL);
		else if (list.count > 0 && !list.items[selected].is_dir) {
			char path[MU_PATH_MAX];
			if (MU_childPath(dir, list.items[selected].name, path, sizeof(path))) playPath(path);
		}
	} else if (PRESSED(Y)) {
		if (host->open_screen(LOCK_ID) != 0) say("couldn't open the lock screen");
	} else if (PRESSED(L1)) {
		command(MU_CMD_PREV, NULL);
	} else if (PRESSED(R1)) {
		command(MU_CMD_NEXT, NULL);
	} else if (PRESSED(L2)) {
		command(MU_CMD_SEEK, "-10000");
	} else if (PRESSED(R2)) {
		command(MU_CMD_SEEK, "+10000");
	} else if (PRESSED(SELECT)) {
		shuffle = !shuffle;
		host->cfg_set(ID, "shuffle", shuffle ? "on" : "off");
		say("shuffle %s", shuffle ? "on" : "off");
		command(MU_CMD_SHUFFLE, shuffle ? "on" : "off");
	} else if (PRESSED(START)) {
		repeat = (repeat + 1) % 3;
		host->cfg_set(ID, "repeat", MU_repeatName(repeat));
		say("repeat %s", MU_repeatName(repeat));
		command(MU_CMD_REPEAT, MU_repeatName(repeat));
	} else if (REPEATED(UP)) {
		selected = MU_move(selected, list.count, -1, PRESSED(UP) != 0);
	} else if (REPEATED(DOWN)) {
		selected = MU_move(selected, list.count, 1, PRESSED(DOWN) != 0);
	} else if (REPEATED(LEFT)) {
		selected = MU_move(selected, list.count, -visible_rows, 0);
	} else if (REPEATED(RIGHT)) {
		selected = MU_move(selected, list.count, visible_rows, 0);
	}
	if (in->pressed || selected != before) host->request_redraw();
	return MOSAIC_SCREEN_STAY;
#undef PRESSED
#undef REPEATED
}

static int playerAnimating(void* data) {
	(void)data;
	return 0;
}

///////////////////////////////////////
// the lock screen

static void clockText(char* out, size_t size) {
	time_t t = time(NULL);
	struct tm local;
	if (localtime_r(&t, &local)) strftime(out, size, "%H:%M", &local);
	else out[0] = '\0';
}

static int lockEnter(void* data) {
	(void)data;
	lock_open = 1;
	unlock_due = 0;
	memset(&unlock, 0, sizeof(unlock));
	clockText(lock_clock, sizeof(lock_clock));
	askStatus(last_now);
	say("locked");
	return 0;
}

static void lockExit(void* data) {
	(void)data;
	lock_open = 0;
	unlock_due = 0;
}

// every button is taken in input_filter while it shows
static int lockInput(const MosaicInput* in, void* data) {
	(void)in;
	(void)data;
	return MOSAIC_SCREEN_STAY;
}

static int inputFilter(const MosaicInput* in, unsigned int* consumed) {
	if (!lock_open) return 0;
	unsigned int select_bit = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_SELECT), start_bit = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_START);
	if (MuUnlock_frame(&unlock, in->pressed, in->held, select_bit, start_bit, last_now)) unlock_due = 1;
	*consumed = ~MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_POWER);
	return 0;
}

///////////////////////////////////////
// actions

static int alwaysAvailable(void* data) {
	(void)data;
	return 1;
}

static void openPlayer(void* data) {
	(void)data;
	if (host->open_screen(PLAYER_ID) != 0) say("couldn't open the player");
}

static void openLock(void* data) {
	(void)data;
	if (host->open_screen(LOCK_ID) != 0) say("couldn't open the lock screen");
}

static void togglePlaying(void* data) {
	(void)data;
	if (!daemon_up) askStatus(last_now);
	if (st.state == MU_STOPPED) say("nothing to pause or play on");
	else command(MU_CMD_TOGGLE, NULL);
}

///////////////////////////////////////
// hooks

static void tick(unsigned long now) {
	last_now = now;
	if (next_poll && now >= next_poll) {
		int was_visible = MuOverlay_visible(&overlay, now);
		long long pos = st.pos_ms;
		int state = st.state;
		char track[MU_PATH_MAX];
		snprintf(track, sizeof(track), "%s", st.track);
		askStatus(now);
		int changed = st.state != state || strcmp(st.track, track) != 0;
		if (changed || ((player_open || lock_open) && st.pos_ms / 1000 != pos / 1000)) host->request_redraw();
		if (was_visible != MuOverlay_visible(&overlay, now)) host->request_redraw();
	} else if (overlay_shown && !MuOverlay_visible(&overlay, now)) {
		overlay_shown = 0;
		host->request_redraw();
	}
	if (lock_open) {
		char clock[16];
		clockText(clock, sizeof(clock));
		if (strcmp(clock, lock_clock) != 0) {
			snprintf(lock_clock, sizeof(lock_clock), "%s", clock);
			host->request_redraw();
		}
		if (unlock_due) {
			unlock_due = 0;
			if (host->close_screen(LOCK_ID) == 0) say("unlocked");
		}
	}
	int want_awake = awake && st.state == MU_PLAYING;
	if (want_awake != awake_held && MOSAIC_HOST_HAS(host, inhibit_sleep)) {
		awake_held = want_awake;
		host->inhibit_sleep(ID, want_awake);
	}
}

static int preLaunch(const MosaicLaunch* launch) {
	(void)launch;
	if (games_play || !daemon_up || st.state != MU_PLAYING) return MOSAIC_LAUNCH_CONTINUE;
	if (sendCommand(MU_CMD_HOLD, NULL, DEVICE_REQUEST_MS) == 0) say("paused for the launch");
	else say("the daemon didn't answer the pause for the launch");
	return MOSAIC_LAUNCH_CONTINUE;
}

static void postReturn(void) {
	if (games_play) return;
	if (!daemon_up) askStatus(last_now);
	if (daemon_up && st.held && sendCommand(MU_CMD_RELEASE, NULL, DEVICE_REQUEST_MS) == 0) say("playing on after the launch");
	askStatus(last_now);
}

static void beforeSleep(void) {
	if (!daemon_up) return;
	if (sendCommand(MU_CMD_SLEEP, NULL, DEVICE_REQUEST_MS) == 0) say("paused for sleep");
}

static void afterResume(void) {
	askStatus(last_now);
}

// Asks the daemon to quit, or else stops the one whose pid it wrote, by
// that pid.
static void beforePoweroff(void) {
	int pid = daemonPid();
	if (sendCommand(MU_CMD_QUIT, NULL, DEVICE_REQUEST_MS) == 0) {
		say("stopped the daemon for the power-off");
		return;
	}
	if (pid > 1 && kill(pid, SIGTERM) == 0) say("stopped the daemon for the power-off, pid %i", pid);
}

static void findDaemon(void) {
	daemon_path[0] = '\0';
	Dl_info info;
	if (!dladdr((const void*)&findDaemon, &info) || !info.dli_fname) return;
	const char* slash = strrchr(info.dli_fname, '/');
	if (!slash) return;
	snprintf(daemon_path, sizeof(daemon_path), "%.*s/%s", (int)(slash - info.dli_fname), info.dli_fname, DAEMON_NAME);
}

static int init(const MosaicHost* h) {
	host = h;
	daemon_up = 0;
	memset(&st, 0, sizeof(st));
	MU_listInit(&list);
	player_open = lock_open = unlock_due = awake_held = 0;
	next_poll = last_now = 0;
	logged_now[0] = logged_view[0] = '\0';
	if (!MOSAIC_HOST_HAS(host, open_screen) || !MOSAIC_HOST_HAS(host, hold_switch) || !MOSAIC_HOST_HAS(host, draw_text)) {
		say("this launcher has no plugin screens, overlays or input filters");
		return 1;
	}
	const char* folder = host->cfg_get(ID, "folder", "Music");
	if (folder[0] == '/') snprintf(root, sizeof(root), "%s", folder);
	else snprintf(root, sizeof(root), "%s/%s", host->sdcard_path, folder);
	size_t root_len = strlen(root);
	while (root_len > 1 && root[root_len - 1] == '/') root[--root_len] = '\0';
	games_play = !strcmp(host->cfg_get(ID, "games", "pause"), "play");
	awake = strcmp(host->cfg_get(ID, "awake", "on"), "off") != 0;
	shuffle = !strcmp(host->cfg_get(ID, "shuffle", "off"), "on");
	repeat = MU_parseRepeat(host->cfg_get(ID, "repeat", "off"));
	const char* overlay_setting = host->cfg_get(ID, "overlay", "5");
	MuOverlay_init(&overlay, MU_parseOverlay(overlay_setting));
	overlay_shown = 0;
	findDaemon();
	snprintf(log_path, sizeof(log_path), "%s/logs", host->userdata_path);
	mkdir(log_path, 0755);
	snprintf(log_path, sizeof(log_path), "%s/logs/music.txt", host->userdata_path);

	static const MosaicScreenHooks player = {
		.size = sizeof(MosaicScreenHooks),
		.enter = playerEnter,
		.exit = playerExit,
		.input = playerInput,
		.render = playerRender,
		.animating = playerAnimating,
	};
	static const MosaicScreenHooks lock = {
		.size = sizeof(MosaicScreenHooks),
		.flags = MOSAIC_SCREEN_FULL,
		.enter = lockEnter,
		.exit = lockExit,
		.input = lockInput,
		.render = lockRender,
		.animating = playerAnimating,
	};
	if (host->register_screen(PLAYER_ID, &player) != 0 || host->register_screen(LOCK_ID, &lock) != 0) {
		say("couldn't register its screens");
		return 1;
	}
	if (host->register_action(OPEN_ACTION, "Music", alwaysAvailable, openPlayer, NULL) != 0
		|| host->register_action(LOCK_ACTION, "Lock screen", alwaysAvailable, openLock, NULL) != 0
		|| host->register_action(TOGGLE_ACTION, "Play or pause music", alwaysAvailable, togglePlaying, NULL) != 0) {
		say("couldn't register its actions");
		return 1;
	}
	if (strcmp(host->cfg_get(ID, "tile", "on"), "off") != 0
		&& (!MOSAIC_HOST_HAS(host, quick_tile) || host->quick_tile(OPEN_ACTION, MOSAIC_QUICK_TOP, NULL, NULL, NULL) != 0)) {
		say("couldn't add the Music tile");
	}
	static const struct {
		const char* name;
		int button;
	} buttons[] = {
		{ "Y", MOSAIC_BUTTON_Y },
		{ "L2", MOSAIC_BUTTON_L2 },
		{ "R2", MOSAIC_BUTTON_R2 },
		{ "SELECT", MOSAIC_BUTTON_SELECT },
	};
	const char* button = host->cfg_get(ID, "button", "none");
	for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if (strcmp(button, buttons[i].name) != 0) continue;
		if (host->bind_button(MOSAIC_SCREEN_GAMELIST, buttons[i].button, MOSAIC_TRIGGER_PRESS, OPEN_ACTION) != 0) {
			say("couldn't bind the music button");
		}
	}

	say("music from %s, daemon %s, games %s, pill %s, shuffle %s, repeat %s", root,
		daemon_path[0] && access(daemon_path, X_OK) == 0 ? daemon_path : "missing", games_play ? "play" : "pause",
		overlay_setting, shuffle ? "on" : "off", MU_repeatName(repeat));
	// a daemon from before, such as one playing through a game
	askStatus(0);
	if (!daemon_up) say("the daemon isn't running");
	return 0;
}

static void quit(void) {
	// the daemon plays on; the launcher is only going away for a while
	MU_listFree(&list);
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.pre_launch = preLaunch,
	.post_return = postReturn,
	.tick = tick,
	.before_sleep = beforeSleep,
	.after_resume = afterResume,
	.before_poweroff = beforePoweroff,
	.input_filter = inputFilter,
	.overlay_render = overlayRender,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
