// Lists stock's palettes through the host and logs the theme colours in
// use each time a folder is listed, for the palette case:
//
//   palettes <n> selected <i>    host->palettes_count and palette_selected
//   palette <i> <builtin> <colors> <name>
//                                 each, its seven colours as RRGGBB,RRGGBB,...
//   refused <rc> <rc> <rc>        palette_info past the end, before the
//                                 start and with a NULL out
//   small <name> <untouched>      a MosaicPalette from before colors: its
//                                 name, and 1 when the bytes past it are kept
//   colors <c1> ... <c7>          host->theme_color, each list

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "paletter"

static const MosaicHost* host;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[700];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, palette_selected)) {
		say("no palette calls");
		return 1;
	}
	int count = host->palettes_count();
	say("palettes %d selected %d", count, host->palette_selected());
	for (int i = 0; i < count; i++) {
		MosaicPalette p = {.size = sizeof(p)};
		if (host->palette_info(i, &p) != 0) {
			say("palette %d failed", i);
			continue;
		}
		char colors[80];
		int at = 0;
		for (int c = 0; c < MOSAIC_THEME_COLORS; c++) {
			at += snprintf(colors + at, sizeof(colors) - (size_t)at, "%s%06X", c ? "," : "", (unsigned)p.colors[c]);
		}
		say("palette %d %d %s %s", i, p.builtin, colors, p.name);
	}
	MosaicPalette p = {.size = sizeof(p)};
	int past = host->palette_info(count, &p);
	int before = host->palette_info(-1, &p);
	int none = host->palette_info(0, NULL);
	say("refused %d %d %d", past, before, none);
	struct {
		MosaicPalette head;
	} small;
	memset(&small, 0x5A, sizeof(small));
	small.head.size = offsetof(MosaicPalette, colors);
	if (count > 0 && host->palette_info(0, &small.head) == 0) {
		say("small %s %d", small.head.name, ((unsigned char*)&small.head)[offsetof(MosaicPalette, colors)] == 0x5A);
	}
	return 0;
}

static void listReady(const MosaicList* list, const char* dir_path) {
	(void)list, (void)dir_path;
	say("colors %06X %06X %06X %06X %06X %06X %06X", (unsigned)host->theme_color(1), (unsigned)host->theme_color(2),
		(unsigned)host->theme_color(3), (unsigned)host->theme_color(4), (unsigned)host->theme_color(5),
		(unsigned)host->theme_color(6), (unsigned)host->theme_color(7));
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.list_ready = listReady,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
