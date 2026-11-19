// System info: a read-only screen with the battery, Wi-Fi, IP addresses,
// storage on the card, memory, uptime, the platform and stock's version,
// and a graph of the battery over the games battery-logger logged.
//
// The system-info.open action opens it, from a System info tile on the
// quick menu (the "tile" setting) or a game list button (the "button"
// setting), or button-remap or an FN button (action:system-info.open). B
// closes it. It never changes a setting. The graph sits under the values
// when there's room, at the smaller font if need be; otherwise it has a
// page of its own, which A moves to and back from and B leaves.
//
// While the screen shows, tick reads the values again once a second and
// asks for a redraw only when one's text changes. The battery comes from
// the host's battery_level and is_charging, the IP addresses from
// getifaddrs (one per interface, IPv4 first, without loopback), storage
// from statvfs on the card, memory from /proc/meminfo, or sysinfo, uptime
// from /proc/uptime, or sysinfo, and the version from .system/version.txt
// on the card. A value the platform doesn't have, such as the battery or
// /proc on desktop, shows "unavailable". Wi-Fi comes from the host's
// wifi_supported, wifi_enabled and wifi_connection, asked each time: the
// network, its address and signal, or off, not connected, or checking
// until the host has first looked.
//
// The graph asks battery-logger's battery.history service for its
// sessions when the screen opens: the last games' levels back to back by
// play time, the time between games left out. Without sessions, as with
// battery-logger off, it says there's no history.
//
// Y opens the Storage page (system-info.storage): each system's size on
// the card, split into games, saves and states, and art, largest first,
// then BIOS and Screenshots. It's counted on a thread of the plugin's own
// as the page opens, with a progress line, and kept until a game or pak
// has run (launcher.return), when the next opening counts again, showing
// the old sizes meanwhile. Up and down scroll it.
//
// X opens the input tester (system-info.input_test): the device's buttons,
// lit while they're held, the sticks' dots and raw values where the
// device has them (host->stick), and the last presses. While it's on top
// its input_filter takes every button but the power button, MENU and the
// FN buttons too, so nothing else runs from them; the power button still
// sleeps and powers off as usual, and shows its press. Holding B and START
// together for a second leaves it, and their releases are taken too.

#include <arpa/inet.h>
#include <pthread.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#ifdef __linux__
#include <sys/sysinfo.h>
#endif
#include "mosaic_plugin.h"
#include "info_model.h"
#include "input_model.h"
#include "storage_model.h"

// English kept in data and translated where it's drawn, for make pot
#define TR_MARK(text) (text)

#define ID "system-info"
#define SCREEN_ID ID ".main"
#define ACTION_ID ID ".open"
#define GRAPH_COLUMN 2 // each bar's width, in stock's units
#define GRAPH_COLUMNS_MAX 1024
#define HINTS_H 40 // the hints' pill and the padding under it, in stock's units
#define STORAGE_ID ID ".storage"
#define INPUT_ID ID ".input"
#define INPUT_ACTION_ID ID ".input_test"
#define INFO_TITLE "System info"
#define GRAPH_TITLE "Battery history"

static const MosaicHost* host;
static int showing = 0;
static unsigned long next_read = 0;
static SiValues values;
static SiHistory history;
static int graph_levels[GRAPH_COLUMNS_MAX];
static char graph_logged[160];
static char page_logged[64];
static int own_page = 0; // 1 when the graph doesn't fit under the rows and has a page of its own
static int on_graph = 0; // 1 on that page

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static const char* trPlural(const char* singular, const char* plural, unsigned long n) {
	if (MOSAIC_HOST_HAS(host, tr_plural)) return host->tr_plural(ID, NULL, singular, plural, n);
	return n == 1 ? singular : plural;
}

static void say(const char* message) {
	host->log(ID, message);
}

///////////////////////////////////////
// reading

// The whole file at path, NUL-terminated, to free, or NULL.
static char* readAll(const char* path, size_t limit) {
	FILE* f = fopen(path, "r");
	if (!f) return NULL;
	char* text = malloc(limit + 1);
	size_t n = text ? fread(text, 1, limit, f) : 0;
	fclose(f);
	if (text) text[n] = '\0';
	return text;
}

static void readBattery(char* out, size_t size) {
	int level = MOSAIC_HOST_HAS(host, battery_level) ? host->battery_level() : -1;
	int charging = level >= 0 && MOSAIC_HOST_HAS(host, is_charging) ? host->is_charging() : 0;
	Si_formatBattery(level, charging, out, size);
}

static void readWifi(char* out, size_t size) {
	if (!MOSAIC_HOST_HAS(host, wifi_connection)) {
		Si_formatWifi(0, MOSAIC_WIFI_UNAVAILABLE, MOSAIC_WIFI_UNAVAILABLE, NULL, out, size);
		return;
	}
	MosaicWifi w;
	memset(&w, 0, sizeof(w));
	w.size = sizeof(w);
	int supported = host->wifi_supported();
	int enabled = supported == 1 ? host->wifi_enabled() : MOSAIC_WIFI_UNAVAILABLE;
	int rc = supported == 1 ? host->wifi_connection(&w) : MOSAIC_WIFI_UNAVAILABLE;
	Si_formatWifi(supported, enabled, rc, &w, out, size);
}

static void readAddrs(char* out, size_t size) {
	SiAddrs addrs;
	Si_addrsInit(&addrs);
	struct ifaddrs* list = NULL;
	if (getifaddrs(&list) != 0) {
		snprintf(out, size, "unavailable");
		return;
	}
	static const int families[] = { AF_INET, AF_INET6 };
	for (size_t f = 0; f < sizeof(families) / sizeof(families[0]); f++) {
		for (struct ifaddrs* i = list; i; i = i->ifa_next) {
			if (!i->ifa_addr || i->ifa_addr->sa_family != families[f] || !(i->ifa_flags & IFF_UP)) continue;
			char text[INET6_ADDRSTRLEN] = "";
			const void* where = families[f] == AF_INET ? (const void*)&((struct sockaddr_in*)i->ifa_addr)->sin_addr
				: (const void*)&((struct sockaddr_in6*)i->ifa_addr)->sin6_addr;
			if (inet_ntop(families[f], where, text, sizeof(text))) Si_addrsAdd(&addrs, i->ifa_name, text);
		}
	}
	freeifaddrs(list);
	Si_formatAddrs(&addrs, out, size);
}

static void readStorage(char* out, size_t size) {
	struct statvfs st;
	if (statvfs(host->sdcard_path, &st) != 0) {
		snprintf(out, size, "unavailable");
		return;
	}
	unsigned long long unit = st.f_frsize ? st.f_frsize : st.f_bsize;
	Si_formatStorage((unsigned long long)st.f_bavail * unit, (unsigned long long)st.f_blocks * unit, out, size);
}

static void readMemory(char* out, size_t size) {
	unsigned long long total = 0, available = 0;
	char* text = readAll("/proc/meminfo", 16 * 1024);
	int got = Si_parseMeminfo(text, &total, &available);
	free(text);
#ifdef __linux__
	struct sysinfo si;
	if (got != 0 && sysinfo(&si) == 0) {
		unsigned long long unit = si.mem_unit ? si.mem_unit : 1;
		total = (unsigned long long)si.totalram * unit;
		available = ((unsigned long long)si.freeram + si.bufferram) * unit;
	}
#endif
	(void)got;
	Si_formatMemory(available, total, out, size);
}

static void readUptime(char* out, size_t size) {
	char* text = readAll("/proc/uptime", 256);
	long long seconds = Si_parseUptime(text);
	free(text);
#ifdef __linux__
	struct sysinfo si;
	if (seconds < 0 && sysinfo(&si) == 0) seconds = si.uptime;
#endif
	Si_formatUptime(seconds, out, size);
}

static void readVersion(char* out, size_t size) {
	char path[1024];
	snprintf(path, sizeof(path), "%s/.system/version.txt", host->sdcard_path);
	char* text = readAll(path, 4096);
	Si_formatVersion(text, out, size);
	free(text);
}

static void readValues(SiValues* v) {
	memset(v, 0, sizeof(*v));
	readBattery(v->text[SI_BATTERY], SI_TEXT_MAX);
	readWifi(v->text[SI_WIFI], SI_TEXT_MAX);
	readAddrs(v->text[SI_IP], SI_TEXT_MAX);
	readStorage(v->text[SI_STORAGE], SI_TEXT_MAX);
	readMemory(v->text[SI_MEMORY], SI_TEXT_MAX);
	readUptime(v->text[SI_UPTIME], SI_TEXT_MAX);
	snprintf(v->text[SI_PLATFORM], SI_TEXT_MAX, "%s", host->platform ? host->platform : "unavailable");
	readVersion(v->text[SI_VERSION], SI_TEXT_MAX);
}

static void logValues(void) {
	char line[SI_ROWS * (SI_TEXT_MAX + 16)];
	size_t used = 0;
	used += (size_t)snprintf(line, sizeof(line), "values:");
	for (int i = 0; i < SI_ROWS && used < sizeof(line); i++) {
		int n = snprintf(line + used, sizeof(line) - used, "%s %s: %s", i ? ";" : "", Si_label(i), values.text[i]);
		if (n < 0) break;
		used += (size_t)n;
	}
	say(line);
}

static void readHistory(void) {
	Si_historyInit(&history);
	if (!MOSAIC_HOST_HAS(host, call)) return;
	static char reply[MOSAIC_CALL_MAX];
	if (host->call("battery.history", NULL, reply, sizeof(reply)) < 0) return;
	char value[32];
	int count = host->event_value(reply, "count", value, sizeof(value)) > 0 ? atoi(value) : 0;
	for (int i = 0; i < count && i < SI_SESSIONS_MAX; i++) {
		char key[32], session[128];
		snprintf(key, sizeof(key), "session.%d", i);
		if (host->event_value(reply, key, session, sizeof(session)) > 0) Si_historyAdd(&history, session);
	}
}

static void refresh(void) {
	SiValues now;
	readValues(&now);
	if (!Si_changed(&now, &values)) return;
	values = now;
	logValues();
	host->request_redraw();
}

static void storageTick(void);
static void inputTick(unsigned long now);

static void tick(unsigned long now) {
	if (showing && Si_due(&next_read, now, SI_REFRESH_MS)) refresh();
	storageTick();
	inputTick(now);
}

///////////////////////////////////////
// drawing

static MosaicColor themeColor(int index, unsigned int fallback) {
	int rgb = host->theme_color(index);
	return mosaic_color(rgb < 0 ? fallback : (unsigned int)rgb, 255);
}

static void fillAround(MosaicSurface* screen, const MosaicRect* rect, const MosaicRect* hole, MosaicColor color) {
	int x1 = rect->x, y1 = rect->y, x2 = rect->x + rect->w, y2 = rect->y + rect->h;
	if (hole->w <= 0 || hole->h <= 0 || hole->x >= x2 || hole->y >= y2 || hole->x + hole->w <= x1 || hole->y + hole->h <= y1) {
		host->fill_rect(screen, rect, color);
		return;
	}
	int hx1 = hole->x > x1 ? hole->x : x1, hy1 = hole->y > y1 ? hole->y : y1;
	int hx2 = hole->x + hole->w < x2 ? hole->x + hole->w : x2, hy2 = hole->y + hole->h < y2 ? hole->y + hole->h : y2;
	MosaicRect parts[4] = {
		{ x1, y1, x2 - x1, hy1 - y1 },
		{ x1, hy2, x2 - x1, y2 - hy2 },
		{ x1, hy1, hx1 - x1, hy2 - hy1 },
		{ hx2, hy1, x2 - hx2, hy2 - hy1 },
	};
	for (int i = 0; i < 4; i++) {
		if (parts[i].w > 0 && parts[i].h > 0) host->fill_rect(screen, &parts[i], color);
	}
}

static void text(MosaicSurface* screen, int font, const char* s, int x, int y, int max_w, MosaicColor color) {
	char cut[SI_TEXT_MAX + 64];
	if (max_w <= 0 || host->text_truncate(font, s, max_w, cut, sizeof(cut)) < 0) return;
	host->draw_text(screen, font, cut, x, y, color, max_w);
}

// Lays out the info page at font, or with rows 0 the graph's own page.
// Returns 0, or -1 when nothing fits.
static int tryLayout(const MosaicScreenView* view, const MosaicRect* hole, int font, int rows, SiLayout* out) {
	int scale = host->screen_scale();
	if (scale < 1) scale = 1;
	int label_w = 0;
	for (int i = 0; i < SI_ROWS; i++) {
		int w = host->text_width(font, tr(Si_label(i)));
		if (w > label_w) label_w = w;
	}
	int row_h = host->text_height(font) + 2 * scale;
	int caption_h = host->text_height(MOSAIC_FONT_TINY);
	const char* title = rows ? INFO_TITLE : GRAPH_TITLE;
	return Si_layout(&view->area, hole, scale, host->text_width(MOSAIC_FONT_LARGE, title), rows, label_w, row_h,
		caption_h, out);
}

static int allRows(const SiLayout* l) {
	return l->rows[SI_ROWS - 1].w > 0;
}

// The info page's layout and font: the larger font with the graph under
// the rows, else the smaller, else the graph on its own page (own_page 1)
// with every row at the larger font if they fit or the smaller.
static int infoLayout(const MosaicScreenView* view, const MosaicRect* hole, SiLayout* out, int* font, int* own) {
	static const int fonts[] = { MOSAIC_FONT_SMALL, MOSAIC_FONT_TINY };
	SiLayout tried[2];
	int ok[2];
	for (int i = 0; i < 2; i++) {
		ok[i] = tryLayout(view, hole, fonts[i], SI_ROWS, &tried[i]) == 0;
		if (ok[i] && allRows(&tried[i]) && tried[i].graph.w > 0) {
			*out = tried[i];
			*font = fonts[i];
			*own = 0;
			return 0;
		}
	}
	*own = 1;
	int pick = ok[0] && allRows(&tried[0]) ? 0 : 1;
	if (!ok[pick]) return -1;
	*out = tried[pick];
	*font = fonts[pick];
	out->graph.w = out->caption.w = 0;
	return 0;
}

static void logGraph(const char* line) {
	if (strcmp(line, graph_logged) == 0) return;
	snprintf(graph_logged, sizeof(graph_logged), "%s", line);
	say(line);
}

static void drawGraph(MosaicSurface* screen, const SiLayout* layout) {
	MosaicColor ink = themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF);
	if (layout->graph.w <= 0) return;
	if (!history.count) {
		text(screen, MOSAIC_FONT_TINY, tr("No battery history: battery-logger hasn't logged a game yet"), layout->caption.x,
			layout->caption.y, layout->caption.w, ink);
		logGraph("no battery history");
		return;
	}
	char caption[320];
	Si_graphCaption(&history, caption, sizeof(caption));
	text(screen, MOSAIC_FONT_TINY, caption, layout->caption.x, layout->caption.y, layout->caption.w, ink);
	host->fill_rect(screen, &layout->graph, themeColor(MOSAIC_COLOR_ACCENT2, 0x333333));

	int scale = host->screen_scale();
	int bar = GRAPH_COLUMN * (scale > 0 ? scale : 1);
	int columns = layout->graph.w / bar;
	if (columns > GRAPH_COLUMNS_MAX) columns = GRAPH_COLUMNS_MAX;
	int n = Si_graph(&history, columns, graph_levels);
	MosaicColor fill = themeColor(MOSAIC_COLOR_ACCENT, 0x9B2257);
	int x0 = layout->graph.x + (layout->graph.w - columns * bar) / 2;
	for (int c = 0; c < n; c++) {
		int h = Si_barHeight(graph_levels[c], layout->graph.h);
		if (h <= 0) continue;
		MosaicRect r = { x0 + c * bar, layout->graph.y + layout->graph.h - h, bar, h };
		host->fill_rect(screen, &r, fill);
	}
	char line[200];
	snprintf(line, sizeof(line), "graph of %i games over %i columns, from %i%% to %i%%", history.count, n,
		n ? graph_levels[0] : 0, n ? graph_levels[n - 1] : 0);
	logGraph(line);
}

static void drawTitle(MosaicSurface* screen, const MosaicRect* rect, const char* title) {
	if (rect->w <= 0) return;
	int scale = host->screen_scale();
	int inner = 12 * (scale > 0 ? scale : 1);
	int h = host->text_height(MOSAIC_FONT_LARGE);
	host->fill_round_rect(screen, rect, rect->h / 2, themeColor(MOSAIC_COLOR_ACCENT, 0x9B2257));
	text(screen, MOSAIC_FONT_LARGE, title, rect->x + inner, rect->y + (rect->h - h + 1) / 2, rect->w - 2 * inner,
		themeColor(MOSAIC_COLOR_HINT, 0xFFFFFF));
}

static int render(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
	(void)data;
	MosaicRect hole = Si_hole(&view->status, &view->header);
	fillAround(screen, &view->area, &hole, themeColor(MOSAIC_COLOR_BACKGROUND, 0x000000));

	int font = MOSAIC_FONT_SMALL;
	SiLayout layout;
	int own = 0;
	int rc = infoLayout(view, &hole, &layout, &font, &own);
	own_page = rc == 0 && own;
	if (!own_page) on_graph = 0;
	if (on_graph) {
		if (tryLayout(view, &hole, font, 0, &layout) != 0 || layout.graph.w <= 0) {
			on_graph = 0;
			rc = infoLayout(view, &hole, &layout, &font, &own);
		}
	}
	if (rc != 0) return 0;

	const char* where = !own_page ? "the graph under the rows" : on_graph ? "the graph's page" : "the info page";
	if (strcmp(where, page_logged) != 0) {
		snprintf(page_logged, sizeof(page_logged), "%s", where);
		char message[96];
		snprintf(message, sizeof(message), "showing %s", where);
		say(message);
	}

	if (on_graph) {
		drawTitle(screen, &layout.title, GRAPH_TITLE);
	} else {
		drawTitle(screen, &layout.title, INFO_TITLE);
		MosaicColor label = themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF);
		MosaicColor value = themeColor(MOSAIC_COLOR_MAIN, 0xFFFFFF);
		int th = host->text_height(font);
		for (int i = 0; i < SI_ROWS; i++) {
			const MosaicRect* row = &layout.rows[i];
			if (row->w <= 0) continue;
			int y = row->y + (row->h - th) / 2;
			text(screen, font, tr(Si_label(i)), row->x, y, layout.label_w, label);
			text(screen, font, values.text[i], row->x + layout.label_w, y, row->w - layout.label_w, value);
		}
	}
	drawGraph(screen, &layout);

	MosaicHint left[2], right[2];
	int left_count = 0, right_count = 0;
	Si_hints(own_page, on_graph, left, &left_count, right, &right_count);
	if (left_count) host->draw_hints(screen, MOSAIC_HINTS_LEFT, left, left_count, NULL);
	if (right_count) host->draw_hints(screen, MOSAIC_HINTS_RIGHT, right, right_count, NULL);
	return 0;
}

static void openScreen(const char* id) {
	if (host->open_screen(id) != 0) {
		char message[96];
		snprintf(message, sizeof(message), "couldn't open %s", id);
		say(message);
	}
}

// B closes, or from the graph's own page goes back to the info; A moves
// between the two pages when the graph has its own; X opens the input
// tester and Y the Storage page.
static int input(const MosaicInput* in, void* data) {
	(void)data;
	if (in->pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B)) {
		if (!on_graph) return MOSAIC_SCREEN_CLOSE;
		on_graph = 0;
		host->request_redraw();
	} else if (own_page && (in->pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_A))) {
		on_graph = !on_graph;
		host->request_redraw();
	} else if (!on_graph && (in->pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_X))) {
		openScreen(INPUT_ID);
	} else if (!on_graph && (in->pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_Y))) {
		openScreen(STORAGE_ID);
	}
	return MOSAIC_SCREEN_STAY;
}

///////////////////////////////////////
// the screen

static int enter(void* data) {
	(void)data;
	showing = 1;
	next_read = 0;
	graph_logged[0] = '\0';
	page_logged[0] = '\0';
	on_graph = 0;
	memset(&values, 0, sizeof(values));
	readHistory();
	char message[64];
	snprintf(message, sizeof(message), "opened, %i games of battery history", history.count);
	say(message);
	refresh();
	return 0;
}

static void leave(void* data) {
	(void)data;
	showing = 0;
	say("closed");
}

///////////////////////////////////////
// the Storage page

static StCache cache;
static StTotals totals;       // what shows, the main thread's
static StTotals work;         // the running count's, its thread's until it's done
static pthread_t worker;
static int worker_running = 0; // the main thread's
static volatile int cancel_count = 0;
static pthread_mutex_t progress_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
	char name[ST_NAME_MAX];
	int done, total;
	unsigned long long files;
	int finished, ok;
} progress; // under progress_lock
static char progress_line[192];
static char card_path[1024];
static int storage_showing = 0;
static int storage_first = 0; // the first row shown
static int storage_rows_shown = 1;

// On the counting thread: what it's on.
static void onProgress(void* ctx, const char* name, int done, int total, unsigned long long files) {
	(void)ctx;
	pthread_mutex_lock(&progress_lock);
	snprintf(progress.name, sizeof(progress.name), "%s", name ? name : "");
	progress.done = done;
	progress.total = total;
	progress.files = files;
	pthread_mutex_unlock(&progress_lock);
}

static void* countThread(void* arg) {
	(void)arg;
	StWalk w;
	St_walkInit(&w);
	w.cancel = &cancel_count;
	w.progress = onProgress;
	int rc = St_count(card_path, &work, &w);
	St_walkFree(&w);
	pthread_mutex_lock(&progress_lock);
	progress.finished = 1;
	progress.ok = rc == 0;
	pthread_mutex_unlock(&progress_lock);
	return NULL;
}

static void startCount(void) {
	memset(&progress, 0, sizeof(progress));
	progress_line[0] = '\0';
	cancel_count = 0;
	snprintf(card_path, sizeof(card_path), "%s", host->sdcard_path);
	if (pthread_create(&worker, NULL, countThread, NULL) != 0) {
		St_cacheDone(&cache, 0);
		say("storage: couldn't start counting");
		return;
	}
	worker_running = 1;
	say("storage: counting");
}

static void logTotals(void) {
	char line[320], a[32], b[32], c[32], d[32];
	St_formatCount(totals.files, a, sizeof(a));
	Si_formatSize(St_total(&totals), b, sizeof(b));
	snprintf(line, sizeof(line), "storage: %i systems, %s files, %s", totals.count, a, b);
	say(line);
	for (int i = 0; i < totals.count; i++) {
		const StRow* r = &totals.rows[i];
		Si_formatSize(r->bytes[ST_GAMES], a, sizeof(a));
		Si_formatSize(r->bytes[ST_SAVES], b, sizeof(b));
		Si_formatSize(r->bytes[ST_ART], c, sizeof(c));
		Si_formatSize(St_rowTotal(r), d, sizeof(d));
		snprintf(line, sizeof(line), "storage: %s (%s): games %s, saves %s, art %s, total %s", r->name[0] ? r->name : r->tag,
			r->tag, a, b, c, d);
		say(line);
	}
	Si_formatSize(totals.bios, a, sizeof(a));
	Si_formatSize(totals.screenshots, b, sizeof(b));
	snprintf(line, sizeof(line), "storage: BIOS %s, Screenshots %s", a, b);
	say(line);
}

// From tick: the count's progress, and its sizes once it's done.
static void storageTick(void) {
	if (!worker_running) return;
	pthread_mutex_lock(&progress_lock);
	char name[ST_NAME_MAX];
	snprintf(name, sizeof(name), "%s", progress.name);
	int done = progress.done, total = progress.total, finished = progress.finished, ok = progress.ok;
	unsigned long long files = progress.files;
	pthread_mutex_unlock(&progress_lock);
	if (!finished) {
		char line[sizeof(progress_line)];
		St_formatProgress(name, done, total, files, line, sizeof(line));
		if (strcmp(line, progress_line) != 0) {
			snprintf(progress_line, sizeof(progress_line), "%s", line);
			if (storage_showing) host->request_redraw();
		}
		return;
	}
	pthread_join(worker, NULL);
	worker_running = 0;
	St_cacheDone(&cache, ok);
	if (ok) {
		totals = work;
		logTotals();
	}
	else say("storage: counting stopped");
	progress_line[0] = '\0';
	if (storage_showing) {
		host->request_redraw();
		// the card changed while it counted
		if (St_cacheStart(&cache)) startCount();
	}
}

static void stopCount(void) {
	if (!worker_running) return;
	cancel_count = 1;
	pthread_join(worker, NULL);
	worker_running = 0;
	St_cacheDone(&cache, 0);
}

static void onCardChanged(const char* name, const char* payload, void* data) {
	(void)payload;
	(void)data;
	St_cacheChanged(&cache);
	char message[96];
	snprintf(message, sizeof(message), "storage: %s, counting again next time", name);
	say(message);
}

// The rows the page lists: the systems, BIOS, Screenshots and the total.
static int storageRows(void) {
	return cache.have ? totals.count + 3 : 0;
}

static void storageRow(int i, const char** name, unsigned long long* parts, int* has_parts, unsigned long long* total) {
	*has_parts = 0;
	memset(parts, 0, sizeof(unsigned long long) * ST_PARTS);
	if (i < totals.count) {
		const StRow* r = &totals.rows[i];
		*name = r->name[0] ? r->name : r->tag;
		memcpy(parts, r->bytes, sizeof(r->bytes));
		*has_parts = 1;
		*total = St_rowTotal(r);
	} else if (i == totals.count) {
		*name = tr("BIOS");
		*total = totals.bios;
	} else if (i == totals.count + 1) {
		*name = tr("Screenshots");
		*total = totals.screenshots;
	} else {
		*name = tr("Everything");
		*total = St_total(&totals);
	}
}

static int storageRender(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
	(void)data;
	MosaicRect hole = Si_hole(&view->status, &view->header);
	fillAround(screen, &view->area, &hole, themeColor(MOSAIC_COLOR_BACKGROUND, 0x000000));
	int font = MOSAIC_FONT_TINY;
	int scale = host->screen_scale();
	if (scale < 1) scale = 1;
	int line_h = host->text_height(font) + 2 * scale;
	SiLayout layout;
	if (Si_layout(&view->area, &hole, scale, host->text_width(MOSAIC_FONT_LARGE, tr("Storage")), 0, 0, 0, line_h,
			&layout) != 0) return 0;
	drawTitle(screen, &layout.title, tr("Storage"));

	MosaicColor label = themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF);
	MosaicColor value = themeColor(MOSAIC_COLOR_MAIN, 0xFFFFFF);
	// the columns: games, saves (and states), art and the total
	static const char* heads[] = { TR_MARK("Games"), TR_MARK("Saves"), TR_MARK("Art"), TR_MARK("Total") };
	// each column as wide as its heading or a size, and the names the rest
	int number_w = host->text_width(font, "999.9 MB");
	int col_x[4], col_w[4], cols_w = 0;
	for (int h = 0; h < 4; h++) {
		int w = host->text_width(font, tr(heads[h]));
		col_w[h] = (w > number_w ? w : number_w) + 6 * scale;
		cols_w += col_w[h];
	}
	MosaicRect list = layout.graph;
	int name_w = list.w - cols_w;
	if (name_w < list.w / 4) name_w = list.w / 4;
	for (int h = 0, x = list.x + name_w; h < 4; h++) {
		col_x[h] = x;
		x += col_w[h];
		if (col_x[h] + col_w[h] > list.x + list.w) col_w[h] = list.x + list.w - col_x[h];
	}
	MosaicRect head = layout.caption;
	text(screen, font, tr("System"), head.x, head.y, name_w - 4 * scale, label);
	for (int h = 0; h < 4; h++) text(screen, font, tr(heads[h]), col_x[h], head.y, col_w[h], label);

	int rows = storageRows();
	int room = list.h / line_h;
	if (worker_running && room > 0) room--; // the progress line
	storage_rows_shown = room > 0 ? room : 1;
	if (storage_first > rows - storage_rows_shown) storage_first = rows - storage_rows_shown;
	if (storage_first < 0) storage_first = 0;
	int y = list.y;
	for (int i = storage_first; i < rows && i < storage_first + room; i++, y += line_h) {
		const char* name;
		unsigned long long parts[ST_PARTS], total;
		int has_parts;
		storageRow(i, &name, parts, &has_parts, &total);
		text(screen, font, name, list.x, y, name_w - 4 * scale, i == rows - 1 ? label : value);
		char size[32];
		for (int p = 0; p < ST_PARTS && has_parts; p++) {
			Si_formatSize(parts[p], size, sizeof(size));
			text(screen, font, size, col_x[p], y, col_w[p], value);
		}
		Si_formatSize(total, size, sizeof(size));
		text(screen, font, size, col_x[3], y, col_w[3], i == rows - 1 ? label : value);
	}
	if (worker_running) {
		const char* line = progress_line[0] ? progress_line : tr("Counting");
		text(screen, font, line, list.x, list.y + room * line_h, list.w, label);
	}

	MosaicHint left[1] = { { MOSAIC_BUTTON_B, "BACK" } };
	host->draw_hints(screen, MOSAIC_HINTS_LEFT, left, 1, NULL);
	return 0;
}

// B goes back; up and down scroll.
static int storageInput(const MosaicInput* in, void* data) {
	(void)data;
	if (in->pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B)) return MOSAIC_SCREEN_CLOSE;
	int move = 0;
	if (in->repeated & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_UP)) move = -1;
	if (in->repeated & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_DOWN)) move = 1;
	int last = storageRows() - storage_rows_shown;
	int to = storage_first + move;
	if (to > last) to = last;
	if (to < 0) to = 0;
	if (to != storage_first) {
		storage_first = to;
		host->request_redraw();
	}
	return MOSAIC_SCREEN_STAY;
}

static int storageEnter(void* data) {
	(void)data;
	storage_showing = 1;
	storage_first = 0;
	if (St_cacheStart(&cache)) startCount();
	else say(worker_running ? "storage: still counting" : "storage: opened with the sizes counted before");
	return 0;
}

static void storageLeave(void* data) {
	(void)data;
	storage_showing = 0;
	say("storage: closed");
}

///////////////////////////////////////
// the input tester

static int testing = 0;
static ItHistory presses;
static ItHold hold;
static unsigned int held_now = 0;
static unsigned int draining = 0; // the buttons held as it closed, whose releases it still takes
static int leaving = 0;
static unsigned long tick_now = 0;
static unsigned int fn_mask = 0, stick_mask = 0;
static int sticks[2][2];

static const char* buttonName(int button) {
	if (button >= MOSAIC_BUTTON_FN1 && MOSAIC_HOST_HAS(host, fn_name)) return host->fn_name(button);
	return It_name(button);
}

static int onTop(void) {
	return testing && strcmp(host->current_screen(), INPUT_ID) == 0;
}

// Redraws when a stick moved.
static void readSticks(void) {
	if (!MOSAIC_HOST_HAS(host, stick)) return;
	for (int s = 0; s < 2; s++) {
		if (!((stick_mask >> s) & 1)) continue;
		int x = 0, y = 0;
		if (host->stick(s, &x, &y) != 0 || (x == sticks[s][0] && y == sticks[s][1])) continue;
		sticks[s][0] = x;
		sticks[s][1] = y;
		host->request_redraw();
	}
}

static void logSticks(void) {
	char line[160], l[48] = "none", r[48] = "none";
	if (stick_mask & 1) It_formatStick(sticks[0][0], sticks[0][1], l, sizeof(l));
	if (stick_mask & 2) It_formatStick(sticks[1][0], sticks[1][1], r, sizeof(r));
	snprintf(line, sizeof(line), "input tester: left stick %s; right stick %s", l, r);
	say(line);
}

// Every button but power, while the tester is on top, and after it closes
// the releases of what was held.
static int inputFilter(const MosaicInput* in, unsigned int* consumed) {
	unsigned int frame = in->pressed | in->repeated | in->released;
	// a press of one is a new one, which gets through
	if (draining) {
		*consumed |= (in->repeated | in->released) & draining & ~in->pressed;
		draining &= in->held;
	}
	if (!onTop()) return 0;
	unsigned int power = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_POWER);
	*consumed |= frame & ~power;
	if (It_historyPress(&presses, in->pressed)) {
		for (int b = 0; b < IT_BUTTONS; b++) {
			if (!(in->pressed & MOSAIC_BUTTON_BIT(b))) continue;
			char message[64];
			snprintf(message, sizeof(message), "input tester: pressed %s", buttonName(b));
			say(message);
		}
		host->request_redraw();
	}
	if (in->held != held_now) {
		held_now = in->held;
		host->request_redraw();
	}
	if (!leaving && It_holdStep(&hold, in->held, tick_now)) {
		leaving = 1;
		draining = in->held & ~power;
		say("input tester: B and START held, leaving");
	}
	return 0;
}

static void inputTick(unsigned long now) {
	tick_now = now;
	if (onTop()) readSticks();
}

static int inputAnimating(void* data) {
	(void)data;
	return hold.holding;
}

static int testerInput(const MosaicInput* in, void* data) {
	(void)in;
	(void)data;
	return leaving ? MOSAIC_SCREEN_CLOSE : MOSAIC_SCREEN_STAY;
}

static void drawButton(MosaicSurface* screen, const MosaicRect* r, const char* name, int lit, int round) {
	if (r->w <= 0) return;
	MosaicColor fill = lit ? themeColor(MOSAIC_COLOR_ACCENT, 0x9B2257) : themeColor(MOSAIC_COLOR_ACCENT2, 0x333333);
	host->fill_round_rect(screen, r, round ? r->h / 2 : r->h / 4, fill);
	int tw = host->text_width(MOSAIC_FONT_TINY, name), th = host->text_height(MOSAIC_FONT_TINY);
	if (tw > r->w) tw = r->w;
	text(screen, MOSAIC_FONT_TINY, name, r->x + (r->w - tw) / 2, r->y + (r->h - th) / 2, r->w,
		themeColor(lit ? MOSAIC_COLOR_HINT : MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF));
}

static int testerRender(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
	(void)data;
	MosaicRect hole = Si_hole(&view->status, &view->header);
	fillAround(screen, &view->area, &hole, themeColor(MOSAIC_COLOR_BACKGROUND, 0x000000));
	int scale = host->screen_scale();
	if (scale < 1) scale = 1;
	int line_h = host->text_height(MOSAIC_FONT_TINY) + 2 * scale;
	SiLayout layout;
	if (Si_layout(&view->area, &hole, scale, host->text_width(MOSAIC_FONT_LARGE, tr("Input tester")), 0, 0, 0, line_h,
			&layout) != 0) return 0;
	drawTitle(screen, &layout.title, tr("Input tester"));
	MosaicColor label = themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF);

	// how to leave, filling up while B and START are held
	MosaicRect cap = layout.caption;
	int percent = It_holdPercent(&hold, tick_now);
	if (percent > 0) {
		MosaicRect bar = { cap.x, cap.y, cap.w * percent / 100, cap.h };
		host->fill_rect(screen, &bar, themeColor(MOSAIC_COLOR_ACCENT, 0x9B2257));
	}
	text(screen, MOSAIC_FONT_TINY, tr("Hold B and START together for a second to leave"), cap.x, cap.y, cap.w, label);

	// the drawing, with the last presses under it, down into the hints'
	// room, as the tester has none
	MosaicRect area = layout.graph;
	area.h += HINTS_H * scale - line_h;
	ItLayout it;
	if (It_layout(&area, fn_mask, stick_mask, &it) == 0) {
		for (int b = 0; b < IT_BUTTONS; b++) {
			int round = b == MOSAIC_BUTTON_A || b == MOSAIC_BUTTON_B || b == MOSAIC_BUTTON_X || b == MOSAIC_BUTTON_Y;
			const char* name = b >= MOSAIC_BUTTON_FN1 ? buttonName(b) : It_label(b);
			drawButton(screen, &it.buttons[b], name, (held_now >> b) & 1, round);
		}
		for (int s = 0; s < 2; s++) {
			const MosaicRect* box = &it.sticks[s];
			if (box->w <= 0) continue;
			host->fill_round_rect(screen, box, box->w / 2, themeColor(MOSAIC_COLOR_ACCENT2, 0x333333));
			int dot = it.unit * 3 / 2;
			int radius = (box->w - dot) / 2;
			MosaicRect d = { box->x + radius + It_stickOffset(sticks[s][0], radius),
				box->y + radius + It_stickOffset(sticks[s][1], radius), dot, dot };
			host->fill_round_rect(screen, &d, dot / 2, themeColor(MOSAIC_COLOR_ACCENT, 0x9B2257));
			char raw[48];
			It_formatStick(sticks[s][0], sticks[s][1], raw, sizeof(raw));
			const MosaicRect* t = &it.stick_text[s];
			int tw = host->text_width(MOSAIC_FONT_TINY, raw);
			if (tw > t->w) tw = t->w;
			text(screen, MOSAIC_FONT_TINY, raw, t->x + (t->w - tw) / 2, t->y, t->w, label);
		}
	}
	char last[256];
	size_t used = (size_t)snprintf(last, sizeof(last), "%s", tr("Last pressed:"));
	if (!presses.count) snprintf(last + used, sizeof(last) - used, " %s", tr("nothing yet"));
	for (int i = 0; i < presses.count && used < sizeof(last); i++) {
		int n = snprintf(last + used, sizeof(last) - used, "%s %s", i ? "," : "", buttonName(presses.buttons[i]));
		if (n < 0) break;
		used += (size_t)n;
	}
	text(screen, MOSAIC_FONT_TINY, last, area.x, area.y + area.h + scale, area.w, label);
	return 0;
}

static int testerEnter(void* data) {
	(void)data;
	testing = 1;
	leaving = 0;
	held_now = 0;
	It_historyInit(&presses);
	It_holdInit(&hold);
	fn_mask = 0;
	for (int b = MOSAIC_BUTTON_FN1; b <= MOSAIC_BUTTON_FN3 && MOSAIC_HOST_HAS(host, fn_name); b++) {
		if (host->fn_name(b)[0]) fn_mask |= 1u << (b - MOSAIC_BUTTON_FN1);
	}
	stick_mask = 0;
	memset(sticks, 0, sizeof(sticks));
	for (int s = 0; s < 2 && MOSAIC_HOST_HAS(host, stick); s++) {
		if (host->stick(s, &sticks[s][0], &sticks[s][1]) == 0) stick_mask |= 1u << s;
	}
	say("input tester: opened");
	logSticks();
	return 0;
}

static void testerLeave(void* data) {
	(void)data;
	testing = 0;
	say("input tester: closed");
}

///////////////////////////////////////
// the action

static void openInfo(const char* path, void* data) {
	(void)path;
	(void)data;
	if (host->open_screen(SCREEN_ID) != 0) say("couldn't open the system info screen");
}

static void openStorage(const char* path, void* data) {
	(void)path;
	(void)data;
	openScreen(STORAGE_ID);
}

static void openTester(const char* path, void* data) {
	(void)path;
	(void)data;
	openScreen(INPUT_ID);
}

static void quit(void) {
	stopCount();
}

static int init(const MosaicHost* h) {
	host = h;
	static const SiWords words = { tr, trPlural };
	Si_setWords(&words);
	showing = 0;
	if (!MOSAIC_HOST_HAS(host, draw_hints) || !MOSAIC_HOST_HAS(host, open_screen)) {
		say("this launcher has no plugin screens or button hints");
		return 1;
	}
	static const MosaicScreenHooks storage_hooks = {
		.size = sizeof(MosaicScreenHooks),
		.enter = storageEnter,
		.exit = storageLeave,
		.input = storageInput,
		.render = storageRender,
	};
	static const MosaicScreenHooks tester_hooks = {
		.size = sizeof(MosaicScreenHooks),
		.enter = testerEnter,
		.exit = testerLeave,
		.input = testerInput,
		.render = testerRender,
		.animating = inputAnimating,
	};
	St_cacheInit(&cache);
	St_init(&totals);
	testing = 0;
	draining = 0;
	if (host->register_screen(STORAGE_ID, &storage_hooks) != 0) say("can't register the storage screen");
	else if (host->register_action(STORAGE_ID, "Storage", 0, NULL, openStorage, NULL) != 0) say("couldn't register " STORAGE_ID);
	// the tester needs the filter to keep its buttons from everything else
	if (!MOSAIC_HOST_HAS(host, hold_switch)) say("this launcher has no input filters, so no input tester");
	else if (host->register_screen(INPUT_ID, &tester_hooks) != 0) say("can't register the input tester");
	else if (host->register_action(INPUT_ACTION_ID, "Input tester", 0, NULL, openTester, NULL) != 0) {
		say("couldn't register " INPUT_ACTION_ID);
	}
	if (MOSAIC_HOST_HAS(host, set_names) && MOSAIC_HOST_HAS(host, subscribe)) {
		host->subscribe("launcher.return", onCardChanged, NULL);
	}
	static const MosaicScreenHooks hooks = {
		.size = sizeof(MosaicScreenHooks),
		.enter = enter,
		.exit = leave,
		.input = input,
		.render = render,
	};
	if (host->register_screen(SCREEN_ID, &hooks) != 0) {
		say("can't register the system info screen");
		return 1;
	}
	if (host->register_action(ACTION_ID, "System info", 0, NULL, openInfo, NULL) != 0) {
		say("couldn't register " ACTION_ID);
		return 1;
	}
	if (strcmp(host->cfg_get("tile", "on"), "off") != 0
			&& (!MOSAIC_HOST_HAS(host, quick_tile) || host->quick_tile(ACTION_ID, MOSAIC_QUICK_TOP, NULL, NULL, NULL) != 0)) {
		say("couldn't add the System info tile");
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
	const char* name = host->cfg_get("button", "none");
	for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if (strcmp(name, buttons[i].name) != 0) continue;
		if (host->bind_button(MOSAIC_SCREEN_GAMELIST, buttons[i].button, MOSAIC_TRIGGER_PRESS, ACTION_ID) != 0) {
			say("couldn't bind the system info button");
		}
	}
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.tick = tick,
	.quit = quit,
	.input_filter = inputFilter,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
