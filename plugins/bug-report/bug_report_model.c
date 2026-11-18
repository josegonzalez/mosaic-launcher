#include "bug_report_model.h"

#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"

int BR_button(const char* name) {
	static const struct {
		const char* name;
		int button;
	} buttons[] = {
		{ "Y", MOSAIC_BUTTON_Y },
		{ "L2", MOSAIC_BUTTON_L2 },
		{ "R2", MOSAIC_BUTTON_R2 },
		{ "SELECT", MOSAIC_BUTTON_SELECT },
	};
	for (size_t i = 0; name && i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if (!strcmp(name, buttons[i].name)) return buttons[i].button;
	}
	return -1;
}

int BR_flags(const char* keep_network) {
	return keep_network && !strcmp(keep_network, "on") ? MOSAIC_REPORT_KEEP_NETWORK : 0;
}

void BR_toast(int rc, const char* path, const char* saved, const char* failed, char* out, size_t size) {
	if (rc != 0 || !path || !path[0]) {
		snprintf(out, size, "%s", failed);
		return;
	}
	const char* name = strrchr(path, '/');
	snprintf(out, size, saved, name ? name + 1 : path);
}
