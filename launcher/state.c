#include "launcher.h"

// Anything not listed starts at zero or NULL, as the separate globals did.
LauncherState state = {
	.restore_depth = -1,
	.restore_relative = -1,
	.dirty = 1,
};
