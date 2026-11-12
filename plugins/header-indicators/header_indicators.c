// Header indicators: free storage, an SSH dot, a profile badge and the
// launcher's frames a second, in one pill beside stock's status pill.
//
// Free storage comes from statvfs on the card and the SSH dot from a
// socket listening on port 22 in /proc/net/tcp and tcp6, each looked at
// every 5 seconds from tick rather than each frame. The FPS is counted
// from the times tick is called. The widget asks for a redraw only when
// what it shows changes. Stock has no SSH server or profiles: the dot
// shows for any SSH pak listening on port 22, and the profile badge is the
// text of the profile setting. Stock already shows the clock and battery,
// so this doesn't.
//
// The SSH dot is a device check: on desktop it stays off, and the plugin
// logs that once.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include "mosaic_plugin.h"
#include "indicators_model.h"

#define ID "header-indicators"
#define FONT MOSAIC_FONT_SMALL
#define FPS_SUFFIX " FPS"

static const MosaicHost* host;
static int want[HI_COUNT];
static char profile[128];
static char storage[16];
static int ssh_on = 0;
static HiFps fps;
static unsigned long next_storage = 0, next_ssh = 0;
static int ssh_supported = 0;

// this frame's layout, from header_width for header_widget
static int widths[HI_COUNT];
static int shown[HI_COUNT];
static char profile_cut[sizeof(profile) + 4];
static char fps_text[16];

// what was last logged, so it's logged only when it changes
static MosaicRect logged_rect;
static int logged_shown[HI_COUNT];
static int fps_logged = 0;

static void say(const char* message) {
	host->log(ID, message);
}

static int scale(void) {
	int s = host->screen_scale();
	return s > 0 ? s : 1;
}

static int pad(void) {
	return 12 * scale(); // stock's BUTTON_PADDING, as its pills use
}

static int gap(void) {
	return 6 * scale();
}

static int dotSize(void) {
	int d = host->text_height(FONT) / 3;
	return d > 4 ? d : 4;
}

static void readStorage(void) {
	char text[sizeof(storage)] = "";
	struct statvfs st;
	if (statvfs(host->sdcard_path, &st) == 0) {
		Hi_formatSize((unsigned long long)st.f_bavail * (unsigned long long)st.f_frsize, text, sizeof(text));
	}
	if (strcmp(text, storage) != 0) {
		snprintf(storage, sizeof(storage), "%s", text);
		host->request_redraw();
	}
}

// up to 64 KiB of path, NUL-terminated, or "" when it can't be read
static const char* readFile(const char* path) {
	static char text[64 * 1024];
	text[0] = '\0';
	FILE* f = fopen(path, "r");
	if (!f) return text;
	size_t n = fread(text, 1, sizeof(text) - 1, f);
	fclose(f);
	text[n] = '\0';
	return text;
}

static void readSsh(void) {
	int on = Hi_listening(readFile("/proc/net/tcp"), HI_SSH_PORT)
		|| Hi_listening(readFile("/proc/net/tcp6"), HI_SSH_PORT);
	if (on != ssh_on) {
		ssh_on = on;
		host->request_redraw();
	}
}

static void tick(unsigned long now) {
	if (want[HI_STORAGE] && Hi_due(&next_storage, now, HI_REFRESH_MS)) readStorage();
	if (want[HI_SSH] && ssh_supported && Hi_due(&next_ssh, now, HI_REFRESH_MS)) readSsh();
	if (want[HI_FPS] && HiFps_frame(&fps, now)) {
		if (!fps_logged) {
			char message[64];
			snprintf(message, sizeof(message), "fps first %i", fps.value);
			say(message);
			fps_logged = 1;
		}
		host->request_redraw();
	}
}

// the storage and SSH may have changed while the device slept
static void afterResume(void) {
	next_storage = next_ssh = 0;
}

static int headerWidth(int max_width, int height) {
	(void)height;
	memset(widths, 0, sizeof(widths));
	int room = max_width - pad() * 2;
	if (room <= 0) return 0;

	if (want[HI_PROFILE]) widths[HI_PROFILE] = host->text_truncate(FONT, profile, room, profile_cut, sizeof(profile_cut));
	if (want[HI_STORAGE] && storage[0]) widths[HI_STORAGE] = host->text_width(FONT, storage);
	if (want[HI_FPS] && fps.value >= 0) {
		snprintf(fps_text, sizeof(fps_text), "%i" FPS_SUFFIX, fps.value);
		// at least two digits' room, so the pill doesn't change width every second
		int w = host->text_width(FONT, fps_text);
		int least = host->text_width(FONT, "00" FPS_SUFFIX);
		widths[HI_FPS] = w > least ? w : least;
	}
	if (want[HI_SSH] && ssh_on) widths[HI_SSH] = dotSize();
	for (int i = 0; i < HI_COUNT; i++) {
		if (widths[i] < 0) widths[i] = 0;
	}
	return Hi_fit(widths, pad(), gap(), max_width, shown);
}

static void logLayout(const MosaicRect* rect) {
	if (!memcmp(rect, &logged_rect, sizeof(*rect)) && !memcmp(shown, logged_shown, sizeof(shown))) return;
	static const char* names[HI_COUNT] = { "profile", "storage", "fps", "ssh" };
	char message[160];
	int n = snprintf(message, sizeof(message), "widget at %i,%i %ix%i:", rect->x, rect->y, rect->w, rect->h);
	for (int i = 0; i < HI_COUNT && n > 0 && (size_t)n < sizeof(message); i++) {
		if (shown[i]) n += snprintf(message + n, sizeof(message) - (size_t)n, " %s", names[i]);
	}
	say(message);
	logged_rect = *rect;
	memcpy(logged_shown, shown, sizeof(shown));
}

static int headerWidget(MosaicSurface* screen, const MosaicRect* rect) {
	int pill = host->theme_color(MOSAIC_COLOR_ACCENT);
	int ink = host->theme_color(MOSAIC_COLOR_HINT);
	if (pill < 0 || ink < 0) return -1;
	MosaicColor ink_color = mosaic_color((unsigned int)ink, 255);
	host->fill_round_rect(screen, rect, rect->h / 2, mosaic_color((unsigned int)pill, 255));

	int x = rect->x + pad();
	int text_y = rect->y + (rect->h - host->text_height(FONT)) / 2;
	int right = rect->x + rect->w - pad();
	for (int i = 0; i < HI_COUNT; i++) {
		if (!shown[i]) continue;
		int room = right - x;
		if (room <= 0) break;
		switch (i) {
		case HI_PROFILE: host->draw_text(screen, FONT, profile_cut, x, text_y, ink_color, room); break;
		case HI_STORAGE: host->draw_text(screen, FONT, storage, x, text_y, ink_color, room); break;
		case HI_FPS: host->draw_text(screen, FONT, fps_text, x, text_y, ink_color, room); break;
		case HI_SSH: {
			int d = widths[HI_SSH];
			MosaicRect dot = { x, rect->y + (rect->h - d) / 2, d, d };
			host->fill_round_rect(screen, &dot, d / 2, ink_color);
			break;
		}
		}
		x += widths[i] + gap();
	}
	logLayout(rect);
	return 0;
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, decorate_cell) || !MOSAIC_HOST_HAS(host, draw_text) || !MOSAIC_HOST_HAS(host, theme_color)) {
		say("unsupported: this launcher has no header widgets");
		return 1;
	}
	snprintf(profile, sizeof(profile), "%s", host->cfg_get("profile", ""));
	want[HI_PROFILE] = profile[0] != '\0';
	want[HI_STORAGE] = Hi_parseToggle(host->cfg_get("storage", "on"), 1);
	want[HI_FPS] = Hi_parseToggle(host->cfg_get("fps", "off"), 0);
	want[HI_SSH] = Hi_parseToggle(host->cfg_get("ssh", "off"), 0);

	storage[0] = '\0';
	ssh_on = 0;
	next_storage = next_ssh = 0;
	HiFps_init(&fps);
	fps_logged = 0;
	memset(&logged_rect, 0, sizeof(logged_rect));
	memset(logged_shown, 0, sizeof(logged_shown));

	ssh_supported = strcmp(host->platform, "desktop") != 0;
	if (want[HI_SSH] && !ssh_supported) say("unsupported on desktop: the SSH dot stays off");
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.tick = tick,
	.after_resume = afterResume,
	.header_width = headerWidth,
	.header_widget = headerWidget,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
