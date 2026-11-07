#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "prefetch_model.h"

#define LINE_LEN (CP_PATH_LEN + CP_NAME_LEN + 8)

///////////////////////////////////////
// paths

// snprintf, through a va_list so the compiler doesn't warn about paths
// that might not fit. Returns 1 when it fit.
static int fmt(char* out, size_t size, const char* format, ...) {
	va_list args;
	va_start(args, format);
	int n = vsnprintf(out, size, format, args);
	va_end(args);
	return n >= 0 && (size_t)n < size;
}

static const char* baseName(const char* path) {
	const char* slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

// The folder a path is in. Returns 0 when it has none.
static int dirName(const char* path, char* out, size_t size) {
	const char* slash = strrchr(path, '/');
	if (!slash || slash == path || (size_t)(slash - path) >= size) return 0;
	memcpy(out, path, (size_t)(slash - path));
	out[slash - path] = '\0';
	return 1;
}

// True for an absolute path with no empty, "." or ".." parts and no
// trailing slash.
static int cleanPath(const char* path) {
	if (path[0] != '/' || !path[1]) return 0;
	const char* p = path;
	while (*p) {
		p++; // past a slash
		const char* end = strchr(p, '/');
		size_t len = end ? (size_t)(end - p) : strlen(p);
		if (len == 0) return 0;
		if (len == 1 && p[0] == '.') return 0;
		if (len == 2 && p[0] == '.' && p[1] == '.') return 0;
		p += len;
	}
	return 1;
}

// True when path is root itself or inside it.
static int inside(const char* path, const char* root) {
	size_t len = strlen(root);
	return !strncmp(path, root, len) && (path[len] == '/' || path[len] == '\0');
}

static int isFile(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int endsWith(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcasecmp(s + a - b, suffix);
}

// The next line of text, without its \r\n, in out. Returns the text after
// it, or NULL at the end.
static const char* nextLine(const char* text, char* out, size_t size) {
	if (!text || !*text) return NULL;
	const char* end = strchr(text, '\n');
	size_t len = end ? (size_t)(end - text) : strlen(text);
	size_t copy = len < size - 1 ? len : size - 1;
	memcpy(out, text, copy);
	out[copy] = '\0';
	if (copy && out[copy - 1] == '\r') out[copy - 1] = '\0';
	return end ? end + 1 : text + len;
}

///////////////////////////////////////
// emulator paks

int CP_emuTag(const char* sdcard, const char* path, char* tag, size_t size) {
	if (!sdcard || !path || !tag || !size) return 0;
	char roms[CP_PATH_LEN];
	if (!fmt(roms, sizeof(roms), "%s/Roms/", sdcard)) return 0;
	size_t roms_len = strlen(roms);
	if (strncmp(path, roms, roms_len) != 0) return 0;
	const char* folder = path + roms_len;
	const char* slash = strchr(folder, '/');
	if (!slash || slash == folder) return 0;
	size_t len = (size_t)(slash - folder);

	// the last parentheses in the folder's name, or the whole name
	const char* open = NULL;
	for (const char* p = folder; p < slash; p++) {
		if (*p == '(') open = p;
	}
	const char* from = folder;
	if (open) {
		from = open + 1;
		const char* close = memchr(from, ')', (size_t)(slash - from));
		len = close ? (size_t)(close - from) : (size_t)(slash - from);
	}
	if (len == 0 || len >= size) return 0;
	memcpy(tag, from, len);
	tag[len] = '\0';
	return 1;
}

int CP_validName(const char* name) {
	if (!name || !name[0] || name[0] == '.' || strlen(name) >= CP_NAME_LEN) return 0;
	for (const unsigned char* p = (const unsigned char*)name; *p; p++) {
		if (*p < 0x20 || *p == 0x7f || *p == '/' || *p == '\\') return 0;
	}
	return 1;
}

int CP_pakLaunch(const char* sdcard, const char* platform, const char* name, char* out, size_t size) {
	if (!sdcard || !platform || !platform[0] || strchr(platform, '/') || !CP_validName(name) || !out || !size) return 0;
	for (int which = 0; which < 2; which++) {
		char root[CP_PATH_LEN];
		char launch[CP_PATH_LEN];
		int ok = which == 0 ? fmt(root, sizeof(root), "%s/Emus/%s", sdcard, platform)
			: fmt(root, sizeof(root), "%s/.system/%s/paks/Emus", sdcard, platform);
		if (!ok || !fmt(launch, sizeof(launch), "%s/%s.pak/launch.sh", root, name)) return 0;
		struct stat st;
		if (stat(launch, &st) != 0) continue; // stock tries the next folder
		if (!S_ISREG(st.st_mode)) return 0;

		char card_real[PATH_MAX];
		char root_real[PATH_MAX];
		char real[PATH_MAX];
		char want[PATH_MAX];
		if (!realpath(sdcard, card_real) || !realpath(root, root_real) || !realpath(launch, real)) return 0;
		if (!inside(root_real, card_real)) return 0;
		if (!fmt(want, sizeof(want), "%s/%s.pak/launch.sh", root_real, name) || strcmp(real, want) != 0) return 0;
		return fmt(out, size, "%s", real);
	}
	return 0;
}

static int coreNameChar(int c) {
	return isalnum(c) || c == '_' || c == '-' || c == '.';
}

// The value of a NAME=value line, without quotes, or NULL when line isn't
// one for name.
static const char* assigned(const char* line, const char* name, char* out, size_t size) {
	while (*line == ' ' || *line == '\t') line++;
	if (!strncmp(line, "export ", 7)) line += 7;
	size_t len = strlen(name);
	if (strncmp(line, name, len) != 0 || line[len] != '=') return NULL;
	const char* value = line + len + 1;
	size_t n = strlen(value);
	while (n && (value[n - 1] == ' ' || value[n - 1] == '\t')) n--;
	if (n >= 2 && (value[0] == '"' || value[0] == '\'') && value[n - 1] == value[0]) {
		value++;
		n -= 2;
	}
	if (n >= size) return NULL;
	memcpy(out, value, n);
	out[n] = '\0';
	return out;
}

int CP_scriptCore(const char* script, const char* pak_dir, const char* sdcard, const char* platform, char* out, size_t size) {
	if (!script || !pak_dir || !sdcard || !platform || !out || !size) return 0;
	char exe[CP_NAME_LEN] = "";
	int cores_here = 0;
	int cores_other = 0;
	char line[LINE_LEN];
	char value[LINE_LEN];
	const char* p = script;
	while ((p = nextLine(p, line, sizeof(line)))) {
		if (assigned(line, "EMU_EXE", value, sizeof(value))) {
			int ok = value[0] && strlen(value) < sizeof(exe);
			for (const char* c = value; ok && *c; c++) ok = coreNameChar((unsigned char)*c);
			if (!ok) return 0; // set from something we can't follow
			strcpy(exe, value);
		}
		else if (assigned(line, "CORES_PATH", value, sizeof(value))) {
			if (!strcmp(value, "$(dirname \"$0\")") || !strcmp(value, "$(dirname $0)") || !strcmp(value, "`dirname \"$0\"`")) {
				cores_here = 1;
				cores_other = 0;
			}
			else {
				cores_other = 1;
			}
		}
	}
	if (!exe[0] || cores_other) return 0;
	if (!strstr(script, "$CORES_PATH/${EMU_EXE}_libretro.so") && !strstr(script, "${CORES_PATH}/${EMU_EXE}_libretro.so")) return 0;
	if (cores_here) return fmt(out, size, "%s/%s_libretro.so", pak_dir, exe);
	return fmt(out, size, "%s/.system/%s/cores/%s_libretro.so", sdcard, platform, exe);
}

///////////////////////////////////////
// emulator-override

int CP_overrideOn(const char* choices) {
	int on = 0;
	char line[LINE_LEN];
	const char* p = choices;
	while ((p = nextLine(p, line, sizeof(line)))) {
		char* s = line;
		while (isspace((unsigned char)*s)) s++;
		const char* id = "emulator-override";
		size_t len = strlen(id);
		if (strncmp(s, id, len) != 0) continue;
		s += len;
		while (*s == ' ' || *s == '\t') s++;
		if (*s != '=') continue;
		s++;
		while (*s == ' ' || *s == '\t') s++;
		size_t n = strlen(s);
		while (n && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
		if (!strcmp(s, "on")) on = 1;
		else if (!strcmp(s, "off")) on = 0;
	}
	return on;
}

int CP_overrideKey(const char* sdcard, const char* path, char* key, size_t size) {
	if (!sdcard || !path || !key || !size) return 0;
	char tag[CP_NAME_LEN];
	if (!CP_emuTag(sdcard, path, tag, sizeof(tag)) || !cleanPath(path) || strlen(path) >= CP_PATH_LEN) return 0;
	char game[CP_PATH_LEN];
	char dir[CP_PATH_LEN];
	if (!dirName(path, dir, sizeof(dir)) || !fmt(game, sizeof(game), "%s/%s.m3u", dir, baseName(dir)) || !isFile(game)) {
		strcpy(game, path);
	}
	return fmt(key, size, "%s", game + strlen(sdcard));
}

int CP_overridePak(const char* file, const char* key, long max_bytes, char* out, size_t size) {
	if (!file || !key || !key[0] || !out || !size) return 0;
	FILE* f = fopen(file, "r");
	if (!f) return 0;
	int found = 0;
	long seen = 0;
	char line[LINE_LEN];
	size_t key_len = strlen(key);
	while (seen < max_bytes && fgets(line, sizeof(line), f)) {
		size_t len = strlen(line);
		seen += (long)len;
		if (len && line[len - 1] != '\n' && !feof(f)) {
			// too long to be ours; skip the rest of it
			int c;
			while ((c = fgetc(f)) != EOF && c != '\n') seen++;
			continue;
		}
		while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
		if (strncmp(line, key, key_len) != 0 || line[key_len] != '\t') continue;
		const char* pak = line + key_len + 1;
		if (!CP_validName(pak)) continue;
		found = fmt(out, size, "%s", pak);
	}
	fclose(f);
	return found;
}

///////////////////////////////////////
// files

long CP_readText(const char* path, char* out, size_t size) {
	if (!path || !out || !size) return -1;
	out[0] = '\0';
	FILE* f = fopen(path, "r");
	if (!f) return -1;
	size_t got = fread(out, 1, size - 1, f);
	fclose(f);
	out[got] = '\0';
	return (long)got;
}

// The first file a .cue names on a FILE line, beside it.
static int cueTrack(const char* cue, char* out, size_t size) {
	char text[CP_SCRIPT_MAX];
	if (CP_readText(cue, text, sizeof(text)) < 0) return 0;
	char dir[CP_PATH_LEN];
	if (!dirName(cue, dir, sizeof(dir))) return 0;
	char line[LINE_LEN];
	const char* p = text;
	while ((p = nextLine(p, line, sizeof(line)))) {
		const char* s = line;
		while (*s == ' ' || *s == '\t') s++;
		if (strncasecmp(s, "FILE", 4) != 0 || (s[4] != ' ' && s[4] != '\t')) continue;
		s += 5;
		while (*s == ' ' || *s == '\t') s++;
		char name[CP_PATH_LEN];
		size_t n;
		if (*s == '"') {
			s++;
			const char* close = strchr(s, '"');
			if (!close) return 0;
			n = (size_t)(close - s);
		}
		else {
			n = strcspn(s, " \t");
		}
		if (n == 0 || n >= sizeof(name)) return 0;
		memcpy(name, s, n);
		name[n] = '\0';
		if (name[0] == '/') return 0; // only beside the cue
		return fmt(out, size, "%s/%s", dir, name);
	}
	return 0;
}

int CP_romFiles(const char* path, char files[][CP_PATH_LEN], int max) {
	if (!path || !files || max <= 0) return 0;
	char rom[CP_PATH_LEN];
	if (!fmt(rom, sizeof(rom), "%s", path)) return 0;
	if (endsWith(rom, ".m3u")) {
		// stock starts with the m3u's first line, beside it
		char text[CP_SCRIPT_MAX];
		char dir[CP_PATH_LEN];
		char line[LINE_LEN];
		if (CP_readText(path, text, sizeof(text)) < 0 || !dirName(path, dir, sizeof(dir))) return 0;
		const char* p = text;
		int found = 0;
		while (!found && (p = nextLine(p, line, sizeof(line)))) {
			if (!line[0]) continue;
			found = fmt(rom, sizeof(rom), "%s/%s", dir, line);
			if (!found) return 0;
		}
		if (!found) return 0;
	}
	int count = 0;
	strcpy(files[count++], rom);
	if (count < max && endsWith(rom, ".cue") && cueTrack(rom, files[count], CP_PATH_LEN)) count++;
	return count;
}

int CP_cardFile(const char* card_real, const char* path, char* real, size_t size) {
	if (!card_real || !card_real[0] || !path || !real || !size) return 0;
	char resolved[PATH_MAX];
	if (!realpath(path, resolved) || !inside(resolved, card_real) || !isFile(resolved)) return 0;
	return fmt(real, size, "%s", resolved);
}

///////////////////////////////////////
// warming

void CP_warm(char files[][CP_PATH_LEN], int count, const CPBudget* budget, CPResult* result) {
	memset(result, 0, sizeof(*result));
	if (!files || !budget || !budget->advise || !budget->now_ms || budget->chunk <= 0) return;
	for (int i = 0; i < count; i++) {
		if (result->bytes >= budget->max_bytes) {
			result->capped = 1;
			return;
		}
		if (budget->now_ms() - budget->start_ms >= budget->max_ms) {
			result->timed_out = 1;
			return;
		}
		int fd = open(files[i], O_RDONLY | O_NOFOLLOW);
		if (fd < 0) {
			result->failed++;
			continue;
		}
		struct stat st;
		if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
			close(fd);
			result->failed++;
			continue;
		}
		long long size = (long long)st.st_size;
		long long offset = 0;
		int advised = 0;
		while (offset < size) {
			if (result->bytes >= budget->max_bytes) {
				result->capped = 1;
				break;
			}
			if (budget->now_ms() - budget->start_ms >= budget->max_ms) {
				result->timed_out = 1;
				break;
			}
			long long len = size - offset;
			if (len > budget->chunk) len = budget->chunk;
			if (len > budget->max_bytes - result->bytes) len = budget->max_bytes - result->bytes;
			if (budget->advise(fd, offset, len) != 0) {
				result->failed++;
				break;
			}
			advised = 1;
			offset += len;
			result->bytes += len;
		}
		close(fd);
		if (advised) result->files++;
		if (result->capped || result->timed_out) return;
	}
}
