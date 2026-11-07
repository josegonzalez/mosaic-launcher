#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cfgfile.h"

#define CFG_LINE 512

// The key of a key=value line, compared without copying.
static int lineHasKey(const char* line, const char* key) {
	size_t len = strlen(key);
	return strncmp(line, key, len) == 0 && line[len] == '=';
}

int Cfg_get(const char* path, const char* key, const char* fallback, char* out, size_t size) {
	int found = 0;
	snprintf(out, size, "%s", fallback ? fallback : "");
	FILE* f = fopen(path, "r");
	if (!f) return 0;
	char line[CFG_LINE];
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = '\0';
		if (!lineHasKey(line, key)) continue;
		snprintf(out, size, "%s", line + strlen(key) + 1);
		found = 1;
	}
	fclose(f);
	return found;
}

int Cfg_set(const char* path, const char* key, const char* value) {
	if (!key || !key[0] || strpbrk(key, "=\r\n") || !value || strpbrk(value, "\r\n")) return -1;

	char tmp[CFG_LINE];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	FILE* out = fopen(tmp, "w");
	if (!out) return -1;

	FILE* in = fopen(path, "r");
	if (in) {
		char line[CFG_LINE];
		while (fgets(line, sizeof(line), in)) {
			line[strcspn(line, "\r\n")] = '\0';
			if (!line[0] || lineHasKey(line, key)) continue;
			fprintf(out, "%s\n", line);
		}
		fclose(in);
	}
	fprintf(out, "%s=%s\n", key, value);
	fclose(out);
	return rename(tmp, path) == 0 ? 0 : -1;
}
