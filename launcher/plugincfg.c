#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plugincfg.h"

void PluginCfg_init(PluginCfg* cfg, const char* path) {
	memset(cfg, 0, sizeof(*cfg));
	snprintf(cfg->path, sizeof(cfg->path), "%s", path);
}

void PluginCfg_free(PluginCfg* cfg) {
	for (int i = 0; i < cfg->count; i++) {
		free(cfg->keys[i]);
		free(cfg->values[i]);
	}
	free(cfg->keys);
	free(cfg->values);
	cfg->keys = NULL;
	cfg->values = NULL;
	cfg->count = 0;
	cfg->read = 0;
}

static int find(const PluginCfg* cfg, const char* key) {
	for (int i = 0; i < cfg->count; i++) {
		if (!strcmp(cfg->keys[i], key)) return i;
	}
	return -1;
}

// Sets key in memory only. Returns -1 when out of memory.
static int put(PluginCfg* cfg, const char* key, const char* value) {
	int i = find(cfg, key);
	if (i >= 0) {
		char* copy = strdup(value);
		if (!copy) return -1;
		free(cfg->values[i]);
		cfg->values[i] = copy;
		return 0;
	}
	char** keys = realloc(cfg->keys, sizeof(char*) * (cfg->count + 1));
	if (!keys) return -1;
	cfg->keys = keys;
	char** values = realloc(cfg->values, sizeof(char*) * (cfg->count + 1));
	if (!values) return -1;
	cfg->values = values;
	char* k = strdup(key);
	char* v = strdup(value);
	if (!k || !v) {
		free(k);
		free(v);
		return -1;
	}
	cfg->keys[cfg->count] = k;
	cfg->values[cfg->count] = v;
	cfg->count++;
	return 0;
}

// Reads the file once; the last line for a key wins.
static void load(PluginCfg* cfg) {
	if (cfg->read) return;
	cfg->read = 1;
	FILE* f = fopen(cfg->path, "r");
	if (!f) return;
	char line[512];
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = '\0';
		char* eq = strchr(line, '=');
		if (!eq) continue;
		*eq = '\0';
		put(cfg, line, eq + 1);
	}
	fclose(f);
}

const char* PluginCfg_get(PluginCfg* cfg, const char* key, const char* fallback) {
	if (!key) return fallback;
	load(cfg);
	int i = find(cfg, key);
	return i >= 0 ? cfg->values[i] : fallback;
}

int PluginCfg_set(PluginCfg* cfg, const char* key, const char* value) {
	if (!key || !value || strchr(key, '=') || strpbrk(key, "\r\n") || strpbrk(value, "\r\n")) return -1;
	load(cfg);
	if (put(cfg, key, value) != 0) return -1;
	FILE* f = fopen(cfg->path, "w");
	if (!f) return -1;
	for (int i = 0; i < cfg->count; i++) fprintf(f, "%s=%s\n", cfg->keys[i], cfg->values[i]);
	return fclose(f) == 0 ? 0 : -1;
}

const char* PluginCfg_readPublic(PluginCfg* cfg, const PluginManifest* m, const char* key, const char* fallback) {
	if (!Manifest_isPublic(m, key)) return fallback;
	return PluginCfg_get(cfg, key, fallback);
}
