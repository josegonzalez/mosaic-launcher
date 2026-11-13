#include "actionlist.h"

#include <stdio.h>
#include <string.h>

static int overridden(const ActionBind* binds, int count, const ActionBind* b) {
	if (b->by != MOSAIC_BIND_SCREEN && b->by != MOSAIC_BIND_BUTTON) return 0;
	for (int i = 0; i < count; i++) {
		if (binds[i].by == MOSAIC_BIND_OVERRIDE && binds[i].screen == b->screen && binds[i].button == b->button) return 1;
	}
	return 0;
}

int ActionList_dropOverridden(ActionBind* binds, int count) {
	// decide every bind before moving any, as the overrides move too
	char drop[256];
	if (count > (int)sizeof(drop)) count = (int)sizeof(drop);
	for (int i = 0; i < count; i++) drop[i] = (char)overridden(binds, count, &binds[i]);
	int kept = 0;
	for (int i = 0; i < count; i++) {
		if (!drop[i]) binds[kept++] = binds[i];
	}
	return kept;
}

void ActionList_fill(MosaicActionInfo* info, const char* id, const char* label, const char* plugin_id,
		int takes_path, const ActionBind* binds, int count) {
	memset(info, 0, sizeof(*info));
	info->size = sizeof(*info);
	snprintf(info->id, sizeof(info->id), "%s", id ? id : "");
	snprintf(info->label, sizeof(info->label), "%s", label ? label : "");
	snprintf(info->plugin_id, sizeof(info->plugin_id), "%s", plugin_id ? plugin_id : "");
	info->takes_path = takes_path ? 1 : 0;
	for (int i = 0; i < count; i++) {
		const ActionBind* b = &binds[i];
		if (!id || !b->action_id || strcmp(b->action_id, id) != 0) continue;
		if (info->binding_count < MOSAIC_ACTION_BINDINGS) {
			MosaicActionBinding* out = &info->bindings[info->binding_count];
			out->by = b->by;
			out->screen = b->screen;
			out->button = b->button;
			out->trigger = b->trigger;
			snprintf(out->plugin_id, sizeof(out->plugin_id), "%s", b->plugin_id ? b->plugin_id : "");
			snprintf(out->setting, sizeof(out->setting), "%s", b->setting ? b->setting : "");
		}
		info->binding_count++;
	}
}

int ActionList_copy(const MosaicActionInfo* info, MosaicActionInfo* out) {
	if (!out || out->size < sizeof(out->size)) return -1;
	size_t size = out->size;
	memcpy(out, info, size < sizeof(*info) ? size : sizeof(*info));
	out->size = size;
	return 0;
}
