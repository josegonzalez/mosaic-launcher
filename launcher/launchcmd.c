#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "launchcmd.h"

// True when path is root itself or inside it.
static int inside(const char* path, const char* root) {
	size_t len = strlen(root);
	if (len > 0 && root[len - 1] == '/') len--;
	return strncmp(path, root, len) == 0 && (path[len] == '/' || path[len] == '\0');
}

int LaunchCmd_set(LaunchCmd* cmd, const char* sdcard, const char* program, const char* const* args, int count) {
	if (!cmd || !sdcard || !program || !program[0] || count < 0 || count > LAUNCH_ARGS_MAX) return -1;
	if (count > 0 && !args) return -1;
	for (int i = 0; i < count; i++) {
		if (!args[i] || strlen(args[i]) >= LAUNCH_ARG_SIZE || strchr(args[i], '\n')) return -1;
	}

	char card[PATH_MAX];
	char resolved[PATH_MAX];
	if (!realpath(sdcard, card) || !realpath(program, resolved)) return -1;
	if (!inside(resolved, card) || strlen(resolved) >= sizeof(cmd->program) || strchr(resolved, '\n')) return -1;
	struct stat st;
	if (stat(resolved, &st) != 0 || !S_ISREG(st.st_mode)) return -1;

	strcpy(cmd->program, resolved);
	for (int i = 0; i < count; i++) strcpy(cmd->args[i], args[i]);
	cmd->count = count;
	return 0;
}

// Appends ' s ' with a leading space unless it's the first, escaping quotes.
static int quote(char* out, size_t size, size_t* at, const char* s) {
	const char* open = *at ? " '" : "'";
	for (const char* p = open; *p; p++) {
		if (*at + 1 >= size) return -1;
		out[(*at)++] = *p;
	}
	for (const char* p = s; *p; p++) {
		const char* piece = *p == '\'' ? "'\\''" : NULL;
		size_t len = piece ? 4 : 1;
		if (*at + len >= size) return -1;
		if (piece) memcpy(out + *at, piece, len);
		else out[*at] = *p;
		*at += len;
	}
	if (*at + 1 >= size) return -1;
	out[(*at)++] = '\'';
	out[*at] = '\0';
	return 0;
}

int LaunchCmd_format(const LaunchCmd* cmd, const char* target, char* out, size_t size) {
	if (!cmd || !out || size == 0) return -1;
	size_t at = 0;
	out[0] = '\0';
	int failed = quote(out, size, &at, cmd->program);
	for (int i = 0; i < cmd->count && !failed; i++) failed = quote(out, size, &at, cmd->args[i]);
	if (target && !failed) failed = quote(out, size, &at, target);
	if (failed) out[0] = '\0';
	return failed ? -1 : 0;
}
