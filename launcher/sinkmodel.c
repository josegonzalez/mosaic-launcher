#include "sinkmodel.h"

#include <string.h>
#include "mosaic_plugin.h"

// stock's msettings.h
#define STOCK_SINK_BLUETOOTH 1
#define STOCK_SINK_USBDAC 2

int Sink_fromStock(int stock) {
	if (stock == STOCK_SINK_BLUETOOTH) return MOSAIC_AUDIO_SINK_BLUETOOTH;
	if (stock == STOCK_SINK_USBDAC) return MOSAIC_AUDIO_SINK_USB;
	return MOSAIC_AUDIO_SINK_NONE;
}

static int lineHas(const char* line, size_t len, const char* word) {
	size_t n = strlen(word);
	for (size_t i = 0; i + n <= len; i++) {
		if (!memcmp(line + i, word, n)) return 1;
	}
	return 0;
}

int Sink_parse(const char* asoundrc) {
	const char* line = asoundrc ? asoundrc : "";
	while (*line) {
		const char* end = strchr(line, '\n');
		size_t len = end ? (size_t)(end - line) : strlen(line);
		if (lineHas(line, len, "type bluealsa") || lineHas(line, len, "defaults.bluealsa.device")) {
			return MOSAIC_AUDIO_SINK_BLUETOOTH;
		}
		if (lineHas(line, len, "type hw")) return MOSAIC_AUDIO_SINK_USB;
		if (!end) break;
		line = end + 1;
	}
	return MOSAIC_AUDIO_SINK_NONE;
}

const char* Sink_name(int sink) {
	switch (sink) {
		case MOSAIC_AUDIO_SINK_BLUETOOTH: return "bluetooth";
		case MOSAIC_AUDIO_SINK_USB: return "usb";
		default: return "none";
	}
}
