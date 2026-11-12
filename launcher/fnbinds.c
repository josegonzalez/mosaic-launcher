#include <stdio.h>
#include <string.h>
#include "fnbinds.h"

#define ACTION_PREFIX "action:"
#define PAK_PREFIX "pak:"

static int validIndex(int index) {
	return index >= 0 && index < FN_COUNT;
}

static int assigned(const char* user_action) {
	return user_action && user_action[0];
}

void FnBinds_clear(FnBinds* binds) {
	if (binds) memset(binds, 0, sizeof(*binds));
}

static int hasPrefix(const char* text, const char* prefix) {
	size_t len = strlen(prefix);
	return !strncmp(text, prefix, len) && text[len];
}

int FnBinds_validAction(const char* action) {
	if (!action || strlen(action) >= FN_ACTION_MAX) return 0;
	return hasPrefix(action, ACTION_PREFIX) || hasPrefix(action, PAK_PREFIX);
}

int FnBinds_bind(FnBinds* binds, int index, const char* owner, const char* action, int has_button,
		const char* user_action) {
	if (!binds || !validIndex(index) || !owner || !owner[0] || strlen(owner) >= FN_OWNER_MAX) return FN_BIND_BAD;
	if (!FnBinds_validAction(action)) return FN_BIND_BAD;
	if (!has_button) return FN_BIND_MISSING;
	if (assigned(user_action)) return FN_BIND_USER;
	if (binds->owner[index][0] && strcmp(binds->owner[index], owner) != 0) return FN_BIND_TAKEN;
	snprintf(binds->owner[index], FN_OWNER_MAX, "%s", owner);
	snprintf(binds->action[index], FN_ACTION_MAX, "%s", action);
	return FN_BIND_OK;
}

static void drop(FnBinds* binds, int index) {
	binds->owner[index][0] = '\0';
	binds->action[index][0] = '\0';
}

int FnBinds_release(FnBinds* binds, int index, const char* owner) {
	if (!binds || !validIndex(index) || !owner || !owner[0] || strcmp(binds->owner[index], owner) != 0) return -1;
	drop(binds, index);
	return 0;
}

int FnBinds_releaseAll(FnBinds* binds, const char* owner) {
	int n = 0;
	for (int i = 0; i < FN_COUNT; i++) {
		if (FnBinds_release(binds, i, owner) == 0) n++;
	}
	return n;
}

int FnBinds_yield(FnBinds* binds, int index, const char* user_action, char* dropped, size_t size) {
	if (!binds || !validIndex(index) || !assigned(user_action) || !binds->owner[index][0]) return 0;
	if (dropped && size) snprintf(dropped, size, "%s", binds->owner[index]);
	drop(binds, index);
	return 1;
}

const char* FnBinds_effective(const FnBinds* binds, int index, const char* user_action, const char** owner) {
	if (owner) *owner = NULL;
	if (!validIndex(index)) return "";
	if (assigned(user_action)) return user_action;
	if (!binds || !binds->owner[index][0]) return "";
	if (owner) *owner = binds->owner[index];
	return binds->action[index];
}

void FnBinds_parseNames(const char* csv, char names[FN_COUNT][FN_NAME_MAX]) {
	for (int i = 0; i < FN_COUNT; i++) names[i][0] = '\0';
	if (!csv) return;
	const char* field = csv;
	for (int i = 0; i < FN_COUNT; i++) {
		size_t len = strcspn(field, ",");
		if (len < FN_NAME_MAX) {
			memcpy(names[i], field, len);
			names[i][len] = '\0';
		}
		if (!field[len]) break;
		field += len + 1;
	}
}
