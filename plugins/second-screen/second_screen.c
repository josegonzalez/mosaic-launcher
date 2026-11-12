// The second screen: mirrors the game list's selection, its name, art and
// a few .info fields, on a second display. No NextUI device has one, so
// only the desktop build shows it, in a second window, and on devices
// the plugin declines to start.
//
// The window belongs to second-screen-display, a small SDL2 program
// beside plugin.so that the plugin starts, so nothing about the
// launcher's own window, events or frame changes. The plugin draws each
// frame on a surface of its own with the host's drawing calls, in the
// launcher's fonts and theme, reads it back a few rows a tick, writes it
// to a file and pokes the program through a socket that's its stdin. The
// program shows the newest frame, and quits when the socket closes, as it
// does when the launcher quits or is killed. It's started by PLUGINS.md's
// helper rules: by its path, with no preloaded libraries, none of the
// launcher's other files open, and its output in .userdata/<platform>/
// logs/second-screen.txt.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE // dladdr
#endif

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "mosaic_plugin.h"
#include "ss_model.h"

#define ID "second-screen"
#define DISPLAY_PROGRAM "second-screen-display"
#define ART_WAIT_MS 2000   // how long a frame waits for art still loading
#define READ_BUDGET_NS 4000000L // reading the frame back, per tick

extern char** environ;

static const MosaicHost* host;
static int frame_w = 640, frame_h = 480;
static SSLayout layout;

static pid_t display_pid = -1;
static int display_fd = -1; // our end of the display program's stdin
static char frame_path[256], frame_tmp[264];

static MosaicSurface* frame = NULL;
static unsigned char* pixels = NULL; // the frame file: header and pixels
static unsigned int frame_number = 0;

// what the next frame shows, from the last selection_changed
static struct {
	int pending;     // a frame is wanted
	int empty;       // an empty folder
	unsigned long since;
	char path[1024];
	char name[SS_LINE_SIZE];
	int type;
	char info[SS_INFO_COUNT][SS_LINE_SIZE];
	int info_count;
} want;

// reading the frame back
static struct {
	int active;
	int row;
	int ticks;
	long ns;
	char summary[SS_LINE_SIZE + 256];
} reading;

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}


__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[1400];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static long elapsedNs(const struct timespec* from) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (now.tv_sec - from->tv_sec) * 1000000000L + (now.tv_nsec - from->tv_nsec);
}

// the folder plugin.so is in, where the display program is too
static int ownDir(char* out, size_t size) {
	Dl_info info;
	if (!dladdr((const void*)&ownDir, &info) || !info.dli_fname) return 0;
	const char* slash = strrchr(info.dli_fname, '/');
	if (!slash) return 0;
	int n = (int)(slash - info.dli_fname);
	return snprintf(out, size, "%.*s", n, info.dli_fname) < (int)size;
}

static void closeDisplay(void) {
	if (display_fd >= 0) close(display_fd);
	display_fd = -1;
	if (display_pid <= 0) return;
	// it quits once its stdin closes; give it a moment, then make sure
	for (int i = 0; i < 50; i++) {
		if (waitpid(display_pid, NULL, WNOHANG) != 0) {
			display_pid = -1;
			return;
		}
		struct timespec pause = { 0, 10000000L };
		nanosleep(&pause, NULL);
	}
	kill(display_pid, SIGKILL);
	waitpid(display_pid, NULL, 0);
	display_pid = -1;
}

// the launcher's environment with only what the display program needs
// (SS_keepEnv), so no preloaded library is loaded into it; NULL without
// memory
static char** displayEnv(void) {
	int count = 0;
	while (environ[count]) count++;
	char** env = malloc((size_t)(count + 1) * sizeof(char*));
	if (!env) return NULL;
	int n = 0;
	for (int i = 0; i < count; i++)
		if (SS_keepEnv(environ[i])) env[n++] = environ[i];
	env[n] = NULL;
	return env;
}

// The display program's stdout and stderr: .userdata/<platform>/logs/
// second-screen.txt, or /dev/null when that can't be opened.
static int openLog(void) {
	char path[1024];
	int fd = -1;
	if (host->userdata_path && snprintf(path, sizeof(path), "%s/logs", host->userdata_path) < (int)sizeof(path)) {
		mkdir(path, 0755);
		if (snprintf(path, sizeof(path), "%s/logs/" ID ".txt", host->userdata_path) < (int)sizeof(path))
			fd = open(path, O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0644);
		if (fd < 0) say("can't open %s for the display's log: %s", path, strerror(errno));
	}
	if (fd < 0) fd = open("/dev/null", O_WRONLY | O_CLOEXEC);
	return fd;
}

// Starts the display program with a socket as its stdin, its log as its
// stdout and stderr, and none of the launcher's other open files.
static int openDisplay(const char* program) {
	int fds[2];
	int type = SOCK_STREAM;
#ifdef SOCK_CLOEXEC
	type |= SOCK_CLOEXEC;
#endif
	if (socketpair(AF_UNIX, type, 0, fds) != 0) return 0;
	fcntl(fds[0], F_SETFD, FD_CLOEXEC);
	fcntl(fds[1], F_SETFD, FD_CLOEXEC);
	fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
#ifdef SO_NOSIGPIPE
	int on = 1;
	setsockopt(fds[0], SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
	char size_w[16], size_h[16];
	snprintf(size_w, sizeof(size_w), "%i", frame_w);
	snprintf(size_h, sizeof(size_h), "%i", frame_h);
	char* argv[] = { (char*)program, frame_path, size_w, size_h, NULL };
	char** env = displayEnv();
	int out = openLog();

	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);
	posix_spawn_file_actions_adddup2(&actions, fds[1], 0);
	if (out >= 0) {
		posix_spawn_file_actions_adddup2(&actions, out, 1);
		posix_spawn_file_actions_adddup2(&actions, out, 2);
	}
	posix_spawnattr_t attr;
	posix_spawnattr_init(&attr);
	short flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
	// nothing open in it past stdin, stdout and stderr, even files the
	// launcher opened without O_CLOEXEC; display.c closes the rest too
#if defined(POSIX_SPAWN_CLOEXEC_DEFAULT)
	flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#elif defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
	posix_spawn_file_actions_addclosefrom_np(&actions, 3);
#else
	long max_fd = sysconf(_SC_OPEN_MAX);
	if (max_fd < 0 || max_fd > 4096) max_fd = 4096;
	for (int fd = 3; fd < max_fd; fd++)
		if (fcntl(fd, F_GETFD) >= 0) posix_spawn_file_actions_addclose(&actions, fd);
#endif
	posix_spawnattr_setflags(&attr, flags);
	sigset_t none, defaults;
	sigemptyset(&none);
	posix_spawnattr_setsigmask(&attr, &none);
	sigemptyset(&defaults);
	sigaddset(&defaults, SIGHUP);
	sigaddset(&defaults, SIGINT);
	sigaddset(&defaults, SIGQUIT);
	sigaddset(&defaults, SIGTERM);
	posix_spawnattr_setsigdefault(&attr, &defaults);

	pid_t pid;
	int rc = env ? posix_spawn(&pid, program, &actions, &attr, argv, env) : ENOMEM;
	posix_spawnattr_destroy(&attr);
	posix_spawn_file_actions_destroy(&actions);
	free(env);
	if (out >= 0) close(out);
	close(fds[1]);
	if (rc != 0) {
		close(fds[0]);
		say("couldn't start %s: %s", program, strerror(rc));
		return 0;
	}
	display_pid = pid;
	display_fd = fds[0];
	return 1;
}

// tells the display program a new frame is in; it reads the newest file,
// so a poke that doesn't fit (it's behind) changes nothing
static void poke(void) {
	if (display_fd < 0) return;
	int flags = MSG_DONTWAIT;
#ifdef MSG_NOSIGNAL
	flags |= MSG_NOSIGNAL;
#endif
	if (send(display_fd, "f", 1, flags) < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
		say("can't reach the display: %s", strerror(errno));
}

// The display program quit, such as when its window was closed.
static int displayGone(void) {
	if (display_pid <= 0) return 1;
	int status = 0;
	if (waitpid(display_pid, &status, WNOHANG) != display_pid) return 0;
	display_pid = -1;
	if (WIFEXITED(status)) say("the display closed (exit %i), so nothing more shows", WEXITSTATUS(status));
	else say("the display closed (signal %i), so nothing more shows", WIFSIGNALED(status) ? WTERMSIG(status) : 0);
	closeDisplay();
	return 1;
}

static int infoGet(const char* key, char* out, size_t size, void* data) {
	return host->entry_info((const MosaicEntry*)data, key, out, size) == 1;
}

static int measure(const char* text, void* data) {
	return host->text_width(*(const int*)data, text);
}

static int selectionChanged(const MosaicEntry* entry) {
	if (display_pid <= 0) return 0;
	memset(&want, 0, sizeof(want));
	want.pending = 1;
	if (!entry) {
		want.empty = 1;
		return 0;
	}
	const char* path = host->entry_path(entry);
	const char* name = host->entry_name(entry);
	snprintf(want.path, sizeof(want.path), "%s", path ? path : "");
	snprintf(want.name, sizeof(want.name), "%s", name ? name : "");
	want.type = host->entry_type(entry);
	want.info_count = SS_infoLines(infoGet, (void*)entry, want.info, SS_INFO_COUNT);
	return 0;
}

static MosaicColor theme(int index, unsigned int fallback, unsigned char alpha) {
	int rgb = host->theme_color(index);
	return mosaic_color(rgb < 0 ? fallback : (unsigned int)rgb, alpha);
}

// Draws the frame for want, with img as its art (or none). Returns 0, or
// -1 when a drawing call fails.
static int compose(const MosaicSurface* img) {
	MosaicRect all = { 0, 0, frame_w, frame_h };
	if (host->fill_rect(frame, &all, theme(MOSAIC_COLOR_BACKGROUND, 0x000000, 255)) != 0) return -1;
	char art_note[96] = "no art";
	if (img) {
		int iw = 0, ih = 0;
		host->surface_size(img, &iw, &ih);
		MosaicRect fit = SS_fit(iw, ih, layout.art);
		MosaicSurface* scaled = fit.w > 0 ? host->surface_scale(ID, img, fit.w, fit.h, MOSAIC_FIT_STRETCH) : NULL;
		if (scaled) {
			host->blit(frame, scaled, NULL, &fit);
			host->surface_free(ID, scaled);
			snprintf(art_note, sizeof(art_note), "art %ix%i at %i,%i %ix%i", iw, ih, fit.x, fit.y, fit.w, fit.h);
		}
	} else if (layout.art.w > 0) {
		// a faint panel where the art would be
		host->fill_round_rect(frame, &layout.art, layout.margin / 2, theme(MOSAIC_COLOR_ACCENT, 0x808080, 48));
	}

	MosaicColor ink = theme(MOSAIC_COLOR_LIST_TEXT, 0xffffff, 255);
	MosaicColor accent = theme(MOSAIC_COLOR_ACCENT, 0xffffff, 255);
	const MosaicRect* t = &layout.text;
	int bottom = t->y + t->h;
	int y = t->y;
	int title_font = MOSAIC_FONT_LARGE, small_font = MOSAIC_FONT_TINY;
	int title_h = host->text_height(title_font), small_h = host->text_height(small_font);
	if (title_h <= 0 || small_h <= 0) return -1;

	char lines[SS_TITLE_LINES][SS_LINE_SIZE];
	int count = SS_wrap(want.empty ? tr("Empty folder") : want.name, t->w, SS_TITLE_LINES, measure, &title_font, lines);
	for (int i = 0; i < count && y + title_h <= bottom; i++, y += title_h)
		if (host->draw_text(frame, title_font, lines[i], t->x, y, ink, t->w) < 0) return -1;
	y += small_h / 2;
	const char* kind = want.empty ? "" : SS_kindLabel(want.type);
	if (kind[0] && y + small_h <= bottom) {
		if (host->draw_text(frame, small_font, tr(kind), t->x, y, accent, t->w) < 0) return -1;
		y += small_h + small_h / 2;
	}
	int shown = 0;
	for (int i = 0; i < want.info_count && y + small_h <= bottom; i++, y += small_h, shown++)
		if (host->draw_text(frame, small_font, want.info[i], t->x, y, ink, t->w) < 0) return -1;

	snprintf(reading.summary, sizeof(reading.summary), "%s: %s, %i title line%s, %s, %i info line%s",
		want.empty ? "an empty folder" : want.name, art_note, count, count == 1 ? "" : "s",
		kind[0] ? kind : "no kind", shown, shown == 1 ? "" : "s");
	return 0;
}

// The frame's art: the selected entry's thumbnail, or its box. 1 with
// *img (or NULL for none) when the frame can be drawn, 0 to wait for art
// still loading, or -1 when the selection has moved on, so the next
// selection_changed brings the frame to draw.
static int artFor(unsigned long now, const MosaicSurface** img) {
	*img = NULL;
	if (want.empty || !MOSAIC_HOST_HAS(host, art_get)) return 1;
	const MosaicEntry* entry = host->selected_entry();
	const char* path = entry ? host->entry_path(entry) : NULL;
	if (!path || strcmp(path, want.path)) return -1;
	static const int kinds[] = { MOSAIC_ART_THUMBNAIL, MOSAIC_ART_BOX };
	for (int i = 0; i < 2; i++) {
		int state = host->art_get(entry, kinds[i], img);
		if (state == MOSAIC_ART_READY && *img) return 1;
		*img = NULL;
		if (state == MOSAIC_ART_LOADING) return now - want.since >= ART_WAIT_MS;
	}
	return 1;
}

static void writeFrame(void) {
	size_t bytes = SS_FRAME_HEADER + (size_t)frame_w * frame_h * 4;
	SS_frameHeader(pixels, frame_number, frame_w, frame_h);
	int fd = open(frame_tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	size_t done = 0;
	while (fd >= 0 && done < bytes) {
		ssize_t n = write(fd, pixels + done, bytes - done);
		if (n <= 0) break;
		done += (size_t)n;
	}
	if (fd >= 0) close(fd);
	if (done != bytes || rename(frame_tmp, frame_path) != 0) {
		say("can't write the frame to %s: %s", frame_path, strerror(errno));
		unlink(frame_tmp);
		return;
	}
	poke();
}

// Reads the frame back, rows at a time until the tick's budget is spent.
static void readBack(void) {
	struct timespec start;
	clock_gettime(CLOCK_MONOTONIC, &start);
	unsigned char* out = pixels + SS_FRAME_HEADER;
	while (reading.row < frame_h) {
		unsigned char* p = out + (size_t)reading.row * frame_w * 4;
		for (int x = 0; x < frame_w; x++, p += 4) {
			MosaicColor c = { 0, 0, 0, 255 };
			host->read_pixel(frame, x, reading.row, &c);
			p[0] = c.r;
			p[1] = c.g;
			p[2] = c.b;
			p[3] = 255;
		}
		reading.row++;
		if (elapsedNs(&start) >= READ_BUDGET_NS) break;
	}
	reading.ns += elapsedNs(&start);
	reading.ticks++;
	if (reading.row < frame_h) return;
	reading.active = 0;
	writeFrame();
	say("frame %u: %s, read in %li ms over %i tick%s", frame_number, reading.summary, reading.ns / 1000000L,
		reading.ticks, reading.ticks == 1 ? "" : "s");
}

static void tick(unsigned long now) {
	if (displayGone()) return;
	if (reading.active) {
		readBack();
		return;
	}
	if (!want.pending) return;
	if (!want.since) want.since = now ? now : 1;
	const MosaicSurface* img;
	int ready = artFor(now, &img);
	if (!ready) return;
	want.pending = 0;
	if (ready < 0) return;
	if (compose(img) != 0) {
		say("couldn't draw the frame for %s", want.empty ? "an empty folder" : want.name);
		return;
	}
	frame_number++;
	reading.active = 1;
	reading.row = 0;
	reading.ticks = 0;
	reading.ns = 0;
	readBack();
}

static void quit(void) {
	closeDisplay();
	if (frame) host->surface_free(ID, frame);
	frame = NULL;
	free(pixels);
	pixels = NULL;
	if (frame_path[0]) unlink(frame_path);
	if (frame_tmp[0]) unlink(frame_tmp);
}

static int init(const MosaicHost* h) {
	host = h;
	static const SSWords words = { tr, NULL };
	SS_setWords(&words);
	if (!SS_hasSecondDisplay(host->platform)) {
		say("%s has no second display, so the second screen doesn't start", host->platform ? host->platform : "this device");
		return 1;
	}
	if (!MOSAIC_HOST_HAS(host, selection_settle_ms) || !MOSAIC_HOST_HAS(host, draw_text)
		|| !MOSAIC_HOST_HAS(host, entry_info) || !MOSAIC_HOST_HAS(host, theme_color)) {
		say("this launcher can't draw the frame or say when the selection changes, so the second screen doesn't start");
		return 1;
	}
	const char* size = host->cfg_get("size", SS_SIZE_DEFAULT);
	if (!SS_parseSize(size, &frame_w, &frame_h)) {
		say("size \"%s\" isn't <width>x<height>, so using %s", size, SS_SIZE_DEFAULT);
		SS_parseSize(SS_SIZE_DEFAULT, &frame_w, &frame_h);
	}
	SS_layout(frame_w, frame_h, &layout);

	char dir[768], program[800];
	if (!ownDir(dir, sizeof(dir)) || snprintf(program, sizeof(program), "%s/" DISPLAY_PROGRAM, dir) >= (int)sizeof(program)
		|| access(program, X_OK) != 0) {
		say("no " DISPLAY_PROGRAM " beside the plugin (it's built only where SDL2 is), so the second screen doesn't start");
		return 1;
	}
	snprintf(frame_path, sizeof(frame_path), "/tmp/mosaic-second-screen-%i.frame", (int)getpid());
	snprintf(frame_tmp, sizeof(frame_tmp), "%s.tmp", frame_path);
	frame = host->surface_new(ID, frame_w, frame_h);
	pixels = malloc(SS_FRAME_HEADER + (size_t)frame_w * frame_h * 4);
	if (!frame || !pixels) {
		say("no room for a %ix%i frame, so the second screen doesn't start", frame_w, frame_h);
		free(pixels);
		pixels = NULL;
		return 1;
	}
	memset(&want, 0, sizeof(want));
	memset(&reading, 0, sizeof(reading));
	if (!openDisplay(program)) {
		free(pixels);
		pixels = NULL;
		return 1;
	}
	say("showing the selection in a %ix%i window, experimental, frames in %s", frame_w, frame_h, frame_path);
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.tick = tick,
	.selection_changed = selectionChanged,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
