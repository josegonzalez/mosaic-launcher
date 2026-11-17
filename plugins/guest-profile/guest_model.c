#include <string.h>
#include "mosaic_plugin.h"
#include "guest_model.h"

int GP_button(const char* name) {
	if (!name) return -1;
	if (!strcmp(name, "L2")) return MOSAIC_BUTTON_L2;
	if (!strcmp(name, "R2")) return MOSAIC_BUTTON_R2;
	if (!strcmp(name, "SELECT")) return MOSAIC_BUTTON_SELECT;
	return -1;
}

int GP_isGuest(const char* live) {
	return live && !strcmp(live, GP_PROFILE);
}

int GP_markWidth(int text_width, int pad, int max_width) {
	if (text_width <= 0 || pad < 0) return 0;
	int width = text_width + pad * 2;
	return width <= max_width ? width : 0;
}
