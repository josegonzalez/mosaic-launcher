#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "resetmodel.h"

#define RESET_FILE_MAX (1024 * 1024) // a cfg bigger than this isn't one the launcher wrote

int Reset_isSetting(const PluginManifest* m, const char* key) {
	if (!m || !key || !key[0]) return 0;
	for (int i = 0; i < m->setting_count; i++) {
		if (strcmp(m->settings[i].key, key) != 0) continue;
		return !(m->settings[i].flags & SETTING_SECRET);
	}
	return 0;
}

char* Reset_cfgText(const char* text, const PluginManifest* m, int* removed) {
	if (removed) *removed = 0;
	if (!text) text = "";
	size_t len = strlen(text);
	char* out = malloc(len + 2);
	if (!out) return NULL;
	size_t at = 0;
	const char* p = text;
	while (*p) {
		const char* end = strchr(p, '\n');
		size_t line_len = end ? (size_t)(end - p) : strlen(p);
		size_t content = line_len;
		if (content > 0 && p[content - 1] == '\r') content--;
		const char* eq = memchr(p, '=', content);
		int drop = content == 0;
		if (!drop && eq) {
			char key[sizeof(m->settings[0].key)];
			size_t key_len = (size_t)(eq - p);
			if (key_len < sizeof(key)) {
				memcpy(key, p, key_len);
				key[key_len] = '\0';
				drop = Reset_isSetting(m, key);
				if (drop && removed) (*removed)++;
			}
		}
		if (!drop) {
			memcpy(out + at, p, content);
			at += content;
			out[at++] = '\n';
		}
		p += line_len;
		if (*p == '\n') p++;
	}
	out[at] = '\0';
	return out;
}

static void say(void (*log)(const char*), const char* format, const char* a, const char* b) {
	if (!log) return;
	char message[1200];
	snprintf(message, sizeof(message), format, a, b);
	log(message);
}

// The file at path in a new string, "" for none; NULL when it can't be read.
static char* readAll(const char* path) {
	FILE* f = fopen(path, "rb");
	if (!f) return errno == ENOENT ? calloc(1, 1) : NULL;
	char* text = malloc(RESET_FILE_MAX + 1);
	size_t n = text ? fread(text, 1, RESET_FILE_MAX + 1, f) : 0;
	int failed = !text || ferror(f) || n > RESET_FILE_MAX;
	fclose(f);
	if (failed) {
		free(text);
		return NULL;
	}
	text[n] = '\0';
	return text;
}

// Puts text in place of the file at path, through a .tmp beside it.
static int writeAll(const char* path, const char* text) {
	char tmp[1100];
	if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) return -1;
	FILE* f = fopen(tmp, "w");
	if (!f) return -1;
	size_t len = strlen(text);
	int failed = fwrite(text, 1, len, f) != len;
	if (fclose(f) != 0) failed = 1;
	if (failed || rename(tmp, path) != 0) {
		remove(tmp);
		return -1;
	}
	return 0;
}

int Reset_apply(const char* dir, const PluginManifest* manifests, int count, void (*log)(const char* message)) {
	if (!dir) return -1;
	int changed = 0, failed = 0;
	char path[1024];
	snprintf(path, sizeof(path), "%s/plugins.txt", dir);
	if (remove(path) == 0) {
		changed++;
		say(log, "removed %s%s", path, "");
	}
	else if (errno != ENOENT) {
		failed = 1;
		say(log, "couldn't remove %s: %s", path, strerror(errno));
	}
	for (int i = 0; i < count; i++) {
		const PluginManifest* m = &manifests[i];
		if (!m->id[0] || strchr(m->id, '/')) continue;
		if (snprintf(path, sizeof(path), "%s/%s.cfg", dir, m->id) >= (int)sizeof(path)) continue;
		char* text = readAll(path);
		if (!text) {
			failed = 1;
			say(log, "couldn't read %s%s", path, "");
			continue;
		}
		int removed = 0;
		char* kept = Reset_cfgText(text, m, &removed);
		free(text);
		if (!kept) {
			failed = 1;
			say(log, "out of memory resetting %s%s", path, "");
			continue;
		}
		if (removed > 0) {
			int rc = kept[0] ? writeAll(path, kept) : remove(path);
			if (rc == 0) {
				changed++;
				say(log, kept[0] ? "reset the settings in %s%s" : "removed %s, which had only settings%s", path, "");
			}
			else {
				failed = 1;
				say(log, "couldn't reset %s%s", path, "");
			}
		}
		free(kept);
	}
	return failed ? -1 : changed;
}
