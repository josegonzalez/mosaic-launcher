#ifndef BUG_REPORT_MODEL_H
#define BUG_REPORT_MODEL_H

// The bug report plugin without the host: which button its setting names,
// the flags its settings give host->bug_report, and the toast it shows.

#include <stddef.h>

// The game list button the "button" setting names, as MOSAIC_BUTTON_*, or
// -1 for none or anything else.
int BR_button(const char* name);

// host->bug_report's flags for the keep_network setting.
int BR_flags(const char* keep_network);

// The toast for bug_report's rc and the zip's path from the card, from
// the plugin's translated "Saved %s" and "Couldn't make a bug report":
// the zip's name without its folder, so the toast fits a 640 pixel wide
// screen.
void BR_toast(int rc, const char* path, const char* saved, const char* failed, char* out, size_t size);

#endif
