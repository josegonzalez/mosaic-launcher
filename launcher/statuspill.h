#ifndef STATUSPILL_H
#define STATUSPILL_H

// The status pill: a shell pak's progress, from the status files it writes
// in STATUS_DIR (see statusmodel.h for what's in one), shown in the header
// beside stock's status pill, left of it and right of plugins' header
// widgets. Only one shows at a time: the visible file changed most
// recently. With no status files nothing is drawn and the header is
// stock's.

#include "layoutmodel.h"

#define STATUS_DIR USERDATA_PATH "/launcher/status.d"

// Call every frame. Looks at STATUS_DIR at most once every STATUS_POLL_MS,
// reading only files that changed, and asks for a redraw when what the
// pill says changes.
void StatusPill_tick(unsigned long now);

// Places the pill in row, through HeaderRow_place, and draws it on dst.
// Call after stock's status pill and before plugins' header widgets.
struct SDL_Surface;
void StatusPill_place(struct SDL_Surface* dst, HeaderRow* row);

#endif
