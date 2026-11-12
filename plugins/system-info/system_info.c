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
// The graph reads battery-logger.csv (and the .old.csv before it) when the
// screen opens: the last games' levels back to back by play time, the
// time between games left out. Without rows it says so.

#include <arpa/inet.h>
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

#define ID "system-info"
#define SCREEN_ID ID ".main"
#define ACTION_ID ID ".open"
#define GRAPH_COLUMN 2 // each bar's width, in stock's units
#define GRAPH_COLUMNS_MAX 1024
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
	static const char* names[] = { "battery-logger.old.csv", "battery-logger.csv" };
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		char path[1024];
		snprintf(path, sizeof(path), "%s/launcher/%s", host->userdata_path, names[i]);
		// battery-logger's largest log is 1024 KB
		char* text = readAll(path, 1024 * 1024 + 4096);
		if (text) Si_historyAdd(&history, text);
		free(text);
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

static void tick(unsigned long now) {
	if (showing && Si_due(&next_read, now, SI_REFRESH_MS)) refresh();
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

	MosaicHint left[1], right[1];
	int left_count = 0, right_count = 0;
	Si_hints(own_page, on_graph, left, &left_count, right, &right_count);
	if (left_count) host->draw_hints(screen, MOSAIC_HINTS_LEFT, left, left_count, NULL);
	if (right_count) host->draw_hints(screen, MOSAIC_HINTS_RIGHT, right, right_count, NULL);
	return 0;
}

// B closes, or from the graph's own page goes back to the info; A moves
// between the two pages when the graph has its own.
static int input(const MosaicInput* in, void* data) {
	(void)data;
	if (in->pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B)) {
		if (!on_graph) return MOSAIC_SCREEN_CLOSE;
		on_graph = 0;
		host->request_redraw();
	} else if (own_page && (in->pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_A))) {
		on_graph = !on_graph;
		host->request_redraw();
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
// the action

static void openInfo(void* data) {
	(void)data;
	if (host->open_screen(SCREEN_ID) != 0) say("couldn't open the system info screen");
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
	if (host->register_action(ACTION_ID, "System info", NULL, openInfo, NULL) != 0) {
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
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
