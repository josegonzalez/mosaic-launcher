// Tasks: shell scripts of the user's own from Tasks/ on the card.
//
// Tasks/*.sh show in a Tasks folder on the first screen, and A runs one
// with no game. Tasks/Game/*.sh are on Y for the selected game, which they
// get as $1, and Tasks/Game/<tag>/*.sh only for the games of the system
// with that tag. Each is an action of its own, so with game-actions,
// favorites and others on Y, Y opens a menu of them all.
//
// A running task shows a wait screen until it ends, and B stops it, with
// its whole process group. Its last line of output shows as a toast, with
// its exit status, and all of it is in .userdata/<platform>/logs/tasks/,
// one file per task, kept from its latest run. tasks_model.c finds the
// tasks, and tasks_run.c runs them.
//
// When the card has no Tasks folder, the examples in this plugin's
// res/tasks/ are copied there, so there's something to try.

#define _GNU_SOURCE // dladdr
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "mosaic_plugin.h"
#include "tasks_model.h"
#include "tasks_run.h"

#define ID "tasks"
#define SCREEN_ID "tasks.running"
#define ENV_MAX 512
#define LINE_MAX 160    // of the last line, in the toast
#define WAIT_OPEN_MS 3000 // to open the wait screen while a dialog shows

extern char** environ;

static const MosaicHost* host;
static char tasks_dir[TK_PATH_MAX]; // <sdcard>/Tasks, also the folder's path
static char logs_dir[TK_PATH_MAX];  // <userdata>/logs/tasks
static TKList global, game;

// the task asked for, waiting for tick to open the wait screen
static TKTask pending;
static char pending_game[TK_PATH_MAX];
static int has_pending;
static int start_failed; // the wait screen's enter couldn't start it
static unsigned long pending_since;

// the task running, or the last one
static TKTask current;
static char current_game[TK_PATH_MAX];
static char current_game_name[TK_NAME_MAX];
static TKRun run;
static int awake_held;
static char shown[64]; // what the wait screen last logged

static void say(const char* message) {
	host->log(ID, message);
}

static void sayf(const char* format, ...) __attribute__((format(printf, 1, 2)));
static void sayf(const char* format, ...) {
	char message[TK_PATH_MAX * 2 + 256];
	va_list args;
	va_start(args, format);
	vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	say(message);
}

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static void toast(const char* message) {
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(message);
}

static void hold(int on) {
	if (awake_held == on || !MOSAIC_HOST_HAS(host, inhibit_sleep)) return;
	awake_held = on;
	host->inhibit_sleep(ID, on);
}

static void makeDirs(const char* path) {
	char dir[TK_PATH_MAX];
	snprintf(dir, sizeof(dir), "%s", path);
	for (char* p = dir + 1; *p; p++) {
		if (*p != '/') continue;
		*p = '\0';
		mkdir(dir, 0755);
		*p = '/';
	}
	mkdir(dir, 0755);
}

///////////////////////////////////////
// the examples

static int copyFile(const char* from, const char* to) {
	FILE* in = fopen(from, "rb");
	if (!in) return -1;
	FILE* out = fopen(to, "wb");
	if (!out) {
		fclose(in);
		return -1;
	}
	char buffer[4096];
	size_t n;
	int ok = 1;
	while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0) {
		if (fwrite(buffer, 1, n, out) != n) ok = 0;
	}
	fclose(in);
	if (fclose(out) != 0) ok = 0;
	return ok ? 0 : -1;
}

// Copies from's files and folders into to, depth folders deep.
static int copyTree(const char* from, const char* to, int depth) {
	DIR* d = opendir(from);
	if (!d) return 0;
	makeDirs(to);
	int copied = 0;
	struct dirent* e;
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.') continue;
		char a[TK_PATH_MAX], b[TK_PATH_MAX];
		snprintf(a, sizeof(a), "%.*s/%s", (int)(sizeof(a) / 2), from, e->d_name);
		snprintf(b, sizeof(b), "%.*s/%s", (int)(sizeof(b) / 2), to, e->d_name);
		struct stat st;
		if (stat(a, &st) != 0) continue;
		if (S_ISDIR(st.st_mode)) {
			if (depth > 0) copied += copyTree(a, b, depth - 1);
		} else if (S_ISREG(st.st_mode)) {
			if (copyFile(a, b) == 0) copied++;
			else sayf("couldn't copy the example %s", e->d_name);
		}
	}
	closedir(d);
	return copied;
}

// The examples are in res/tasks/ in the plugin's folder, two up from the
// lib/<arch>/ its plugin.so is in.
static void copyExamples(void) {
	struct stat st;
	if (stat(tasks_dir, &st) == 0) return;
	Dl_info info;
	if (!dladdr((const void*)&copyExamples, &info) || !info.dli_fname) return;
	char examples[TK_PATH_MAX];
	snprintf(examples, sizeof(examples), "%s", info.dli_fname);
	for (int up = 0; up < 3; up++) {
		char* slash = strrchr(examples, '/');
		if (!slash) return;
		*slash = '\0';
	}
	size_t len = strlen(examples);
	snprintf(examples + len, sizeof(examples) - len, "/res/tasks");
	int copied = copyTree(examples, tasks_dir, 2);
	sayf("copied %d example tasks into %s", copied, tasks_dir);
}

///////////////////////////////////////
// running

static const char* baseName(const char* path) {
	const char* slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

// The game's name without its folder or extension, for the wait screen.
static void gameName(const char* path, char* out, size_t size) {
	// a long name is cut to fit
	snprintf(out, size, "%.*s", (int)(size - 1), baseName(path));
	char* dot = strrchr(out, '.');
	if (dot && dot != out) *dot = '\0';
}

static void queue(const TKTask* task, const char* game_path) {
	if (run.pid > 0 || has_pending) {
		toast(tr("A task is already running"));
		return;
	}
	pending = *task;
	snprintf(pending_game, sizeof(pending_game), "%s", game_path ? game_path : "");
	has_pending = 1;
	pending_since = 0;
}

// The wait screen's enter starts the task, so it shows only for one that
// started.
static int startPending(void) {
	current = pending;
	snprintf(current_game, sizeof(current_game), "%s", pending_game);
	current_game_name[0] = '\0';
	if (current_game[0]) gameName(current_game, current_game_name, sizeof(current_game_name));

	char log_name[TK_NAME_MAX];
	TK_logName(tasks_dir, current.path, log_name, sizeof(log_name));
	if (!log_name[0]) snprintf(log_name, sizeof(log_name), "%s", current.name);
	char log_path[TK_PATH_MAX];
	snprintf(log_path, sizeof(log_path), "%.700s/%.200s.txt", logs_dir, log_name);
	makeDirs(logs_dir);

	char header[TK_PATH_MAX + TK_NAME_MAX + 64];
	time_t now = time(NULL);
	struct tm tm;
	char when[32];
	strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", localtime_r(&now, &tm));
	if (current_game[0]) snprintf(header, sizeof(header), "== %s for %s, %s\n", current.name, current_game, when);
	else snprintf(header, sizeof(header), "== %s, %s\n", current.name, when);

	char sd[TK_PATH_MAX + 16], ud[TK_PATH_MAX + 16], platform[128];
	snprintf(sd, sizeof(sd), "SDCARD_PATH=%s", host->sdcard_path);
	snprintf(ud, sizeof(ud), "USERDATA_PATH=%s", host->userdata_path);
	snprintf(platform, sizeof(platform), "PLATFORM=%s", host->platform ? host->platform : "");
	const char* set[] = { sd, ud, platform };
	static char* env[ENV_MAX];
	TK_envBuild(environ, set, 3, env, ENV_MAX);

	char error[256];
	if (TK_start(&run, current.path, current_game[0] ? current_game : NULL, env, log_path, header, error, sizeof(error)) != 0) {
		sayf("couldn't run %s: %s", current.path, error);
		start_failed = 1;
		return -1;
	}
	if (current_game[0]) sayf("running %s for %s, pid %d, logging to %s", current.path, current_game, (int)run.pid, log_path);
	else sayf("running %s, pid %d, logging to %s", current.path, (int)run.pid, log_path);
	hold(1);
	return 0;
}

static void ended(void) {
	hold(0);
	char tail[4096];
	size_t len = TK_outputTail(&run, tail, sizeof(tail));
	char line[LINE_MAX];
	TK_lastLine(tail, len, line, sizeof(line));
	char message[LINE_MAX + 128];
	switch (run.how) {
	case TK_EXITED:
		if (line[0]) snprintf(message, sizeof(message), tr("%s (exit %d)"), line, run.code);
		else snprintf(message, sizeof(message), tr("%s finished (exit %d)"), current.name, run.code);
		break;
	case TK_STOPPED:
		snprintf(message, sizeof(message), tr("%s stopped"), current.name);
		break;
	default:
		snprintf(message, sizeof(message), tr("%s was killed by signal %d"), current.name, run.code);
		break;
	}
	if (run.how == TK_EXITED) sayf("%s exited with %d, last line \"%s\"", current.name, run.code, line);
	else if (run.how == TK_STOPPED) sayf("%s stopped", current.name);
	else sayf("%s killed by signal %d", current.name, run.code);
	sayf("toast: %s", message);
	toast(message);
}

///////////////////////////////////////
// the wait screen

static MosaicColor themeColor(int index, unsigned int fallback) {
	int rgb = host->theme_color(index);
	return mosaic_color(rgb < 0 ? fallback : (unsigned int)rgb, 255);
}

static void centred(MosaicSurface* screen, int font, const char* text, int y, int width, MosaicColor color) {
	char cut[TK_PATH_MAX];
	int w = host->text_truncate(font, text, width - 40, cut, sizeof(cut));
	if (w < 0) return;
	host->draw_text(screen, font, cut, (width - w) / 2, y, color, width);
}

static int screenEnter(void* data) {
	(void)data;
	shown[0] = '\0';
	return startPending();
}

// However the screen closes while the task runs, such as for the launcher
// quitting, the task goes with it.
static void screenExit(void* data) {
	(void)data;
	if (run.pid <= 0) return;
	say("the wait screen closed with the task running; killing it");
	TK_kill(&run);
	ended();
}

static int screenInput(const MosaicInput* in, void* data) {
	(void)data;
	if ((in->pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B)) && run.pid > 0 && !run.stopping) {
		sayf("stopping %s, pid %d, and its group", current.name, (int)run.pid);
		TK_stop(&run);
		host->request_redraw();
	}
	return MOSAIC_SCREEN_STAY;
}

static int screenAnimating(void* data) {
	(void)data;
	return run.pid > 0;
}

static int screenRender(MosaicSurface* screen, const MosaicScreenView* v, void* data) {
	(void)data;
	// all but the status pill and header widgets, already drawn
	MosaicColor bg = themeColor(MOSAIC_COLOR_BACKGROUND, 0x000000);
	int top = 0, left = v->area.w;
	if (v->status.w > 0 && v->status.y + v->status.h > top) top = v->status.y + v->status.h;
	if (v->header.w > 0 && v->header.y + v->header.h > top) top = v->header.y + v->header.h;
	if (v->status.w > 0 && v->status.x < left) left = v->status.x;
	if (v->header.w > 0 && v->header.x < left) left = v->header.x;
	MosaicRect below = { 0, top, v->area.w, v->area.h - top };
	host->fill_rect(screen, &below, bg);
	MosaicRect band = { 0, 0, left, top };
	if (band.w > 0 && band.h > 0) host->fill_rect(screen, &band, bg);
	int scale = host->screen_scale();
	if (scale < 1) scale = 1;
	MosaicColor ink = themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF);
	MosaicColor dim = themeColor(MOSAIC_COLOR_MAIN, 0xFFFFFF);
	int line = host->text_height(MOSAIC_FONT_LARGE);
	int small = host->text_height(MOSAIC_FONT_SMALL);
	int y = v->area.h / 2 - 2 * line;
	centred(screen, MOSAIC_FONT_LARGE, current.name, y, v->area.w, ink);
	y += line + 4 * scale;
	if (current_game_name[0]) {
		centred(screen, MOSAIC_FONT_SMALL, current_game_name, y, v->area.w, dim);
		y += small + 4 * scale;
	}
	y += 8 * scale;
	long long seconds = run.started ? (TK_nowMs() - run.started) / 1000 : 0;
	char status[128];
	snprintf(status, sizeof(status), run.stopping ? tr("Stopping... %lld:%02lld") : tr("Running... %lld:%02lld"),
		seconds / 60, seconds % 60);
	centred(screen, MOSAIC_FONT_SMALL, status, y, v->area.w, dim);
	if (MOSAIC_HOST_HAS(host, draw_hints) && !run.stopping) {
		MosaicHint stop = { MOSAIC_BUTTON_B, "STOP" };
		host->draw_hints(screen, MOSAIC_HINTS_LEFT, &stop, 1, NULL);
	}
	const char* now = run.stopping ? "stopping" : "running";
	if (strcmp(now, shown) != 0) {
		snprintf(shown, sizeof(shown), "%s", now);
		sayf("wait screen: %s %s", now, current.name);
	}
	return 0;
}

///////////////////////////////////////
// the Tasks folder and the game list's actions

static void rootEntries(MosaicList* root) {
	TK_scan(tasks_dir, &global, NULL);
	if (!global.count) return;
	host->list_push(root, host->entry_new(tasks_dir, MOSAIC_ENTRY_DIR, tr("Tasks")));
}

static MosaicList* virtualDir(const char* path) {
	if (!path || strcmp(path, tasks_dir) != 0) return NULL;
	TK_scan(tasks_dir, &global, NULL);
	MosaicList* list = host->list_new();
	for (int i = 0; i < global.count; i++) {
		host->list_push(list, host->entry_new(global.items[i].path, MOSAIC_ENTRY_PAK, global.items[i].name));
	}
	sayf("Tasks folder: %d tasks", global.count);
	return list;
}

// A from the Tasks folder launches a task's entry as a pak would; tick
// runs it instead.
static int preLaunch(const MosaicLaunch* launch) {
	if (!launch->path || launch->type != MOSAIC_ENTRY_PAK) return MOSAIC_LAUNCH_CONTINUE;
	size_t len = strlen(tasks_dir);
	if (strncmp(launch->path, tasks_dir, len) != 0 || launch->path[len] != '/') return MOSAIC_LAUNCH_CONTINUE;
	TK_scan(tasks_dir, &global, NULL);
	for (int i = 0; i < global.count; i++) {
		if (strcmp(global.items[i].path, launch->path) != 0) continue;
		queue(&global.items[i], NULL);
		return MOSAIC_LAUNCH_CANCEL;
	}
	return MOSAIC_LAUNCH_CONTINUE;
}

static int gameTaskAvailable(const char* path, void* data) {
	const TKTask* task = data;
	char tag[TK_TAG_MAX];
	if (!TK_systemTag(host->sdcard_path, path, tag, sizeof(tag))) return 0;
	return TK_appliesTo(task, tag) && TK_isGame(host->sdcard_path, path);
}

static void gameTaskRun(const char* path, void* data) {
	const TKTask* task = data;
	if (!gameTaskAvailable(path, data)) return;
	queue(task, path);
}

// Each game task's action id: tasks.game.<its name>, or for one limited to
// a system tasks.game.<tag>.<its name>, made of letters, digits and dashes.
static void actionId(const TKTask* task, char* out, size_t size) {
	char raw[TK_NAME_MAX + TK_TAG_MAX + 2];
	if (task->tag[0]) snprintf(raw, sizeof(raw), "%s.%s", task->tag, task->name);
	else snprintf(raw, sizeof(raw), "%s", task->name);
	size_t n = (size_t)snprintf(out, size, "tasks.game.");
	for (const char* p = raw; *p && n + 1 < size; p++) {
		char c = *p;
		if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.')) c = '-';
		out[n++] = c;
	}
	out[n] = '\0';
}

static void registerGameTasks(void) {
	TK_scan(tasks_dir, NULL, &game);
	for (int i = 0; i < game.count; i++) {
		char id[TK_NAME_MAX + TK_TAG_MAX + 16];
		actionId(&game.items[i], id, sizeof(id));
		// the same name twice, such as "a b" and "a-b"
		for (int k = 0; k < i; k++) {
			char other[sizeof(id)];
			actionId(&game.items[k], other, sizeof(other));
			if (!strcmp(id, other)) {
				size_t len = strlen(id);
				snprintf(id + len, sizeof(id) - len, "-%d", i);
				break;
			}
		}
		if (host->register_action(id, game.items[i].name, MOSAIC_ACTION_PATH, gameTaskAvailable, gameTaskRun, &game.items[i]) != 0
				|| host->bind_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_Y, MOSAIC_TRIGGER_PRESS, id) != 0) {
			sayf("couldn't put %s on Y", id);
			continue;
		}
		if (game.items[i].tag[0]) sayf("game task %s on Y as %s, for %s games", game.items[i].name, id, game.items[i].tag);
		else sayf("game task %s on Y as %s", game.items[i].name, id);
	}
}

///////////////////////////////////////
// hooks

static void tick(unsigned long now) {
	if (run.pid > 0) {
		if (TK_poll(&run)) {
			if (host->close_screen(SCREEN_ID) != 0) say("couldn't close the wait screen");
			ended();
		}
		return;
	}
	if (!has_pending) return;
	if (!pending_since) pending_since = now ? now : 1;
	start_failed = 0;
	if (host->open_screen(SCREEN_ID) == 0) {
		has_pending = 0;
		return;
	}
	if (start_failed) {
		has_pending = 0;
		char message[TK_NAME_MAX + 64];
		snprintf(message, sizeof(message), tr("Couldn't run %s"), pending.name);
		toast(message);
	} else if (now - pending_since > WAIT_OPEN_MS) {
		// still refused, such as while a dialog shows
		has_pending = 0;
		sayf("couldn't open the wait screen for %s", pending.name);
	}
}

static void stopAll(void) {
	has_pending = 0;
	if (run.pid > 0) {
		sayf("killing %s, pid %d, and its group", current.name, (int)run.pid);
		TK_kill(&run);
	}
	hold(0);
}

static void quit(void) {
	stopAll();
}

static int init(const MosaicHost* h) {
	host = h;
	memset(&run, 0, sizeof(run));
	memset(&current, 0, sizeof(current));
	has_pending = awake_held = 0;
	if (!MOSAIC_HOST_HAS(host, open_screen) || !MOSAIC_HOST_HAS(host, draw_text)) {
		say("this launcher can't show a wait screen");
		return -1;
	}
	snprintf(tasks_dir, sizeof(tasks_dir), "%s/Tasks", host->sdcard_path);
	snprintf(logs_dir, sizeof(logs_dir), "%s/logs/tasks", host->userdata_path);
	static const MosaicScreenHooks hooks = {
		.size = sizeof(MosaicScreenHooks),
		.enter = screenEnter,
		.exit = screenExit,
		.input = screenInput,
		.render = screenRender,
		.animating = screenAnimating,
	};
	if (host->register_screen(SCREEN_ID, &hooks) != 0) {
		say("couldn't register its wait screen");
		return -1;
	}
	copyExamples();
	TK_scan(tasks_dir, &global, NULL);
	registerGameTasks();
	sayf("%d tasks, %d game tasks in %s", global.count, game.count, tasks_dir);
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.root_entries = rootEntries,
	.virtual_dir = virtualDir,
	.pre_launch = preLaunch,
	.tick = tick,
	.before_poweroff = stopAll,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
