// The sync plugin's model: see sync_model.h.

#include "sync_model.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int endsWith(const char* s, const char* suffix) {
	size_t n = strlen(s), k = strlen(suffix);
	return n >= k && !strcmp(s + n - k, suffix);
}

int Sy_syncable(const char* name) {
	if (!name || !name[0] || name[0] == '.') return 0;
	for (const unsigned char* p = (const unsigned char*)name; *p; p++) {
		if (*p == '/' || *p < 0x20 || *p == 0x7f) return 0;
	}
	if (strlen(name) >= SY_NAME_MAX) return 0;
	return !endsWith(name, SY_CONFLICT) && !endsWith(name, SY_DELETED) && !endsWith(name, ".part") && !endsWith(name, ".partial") && !endsWith(name, ".tmp");
}

int Sy_stateDir(const char* name) {
	if (!Sy_syncable(name)) return 0;
	const char* dash = strchr(name, '-');
	return dash && dash != name && dash[1];
}

// rel's folder and file, after its top folder; 0 when it's not top/dir/file
static int splitRel(const char* rel, const char* top, char* dir, char* file) {
	size_t t = strlen(top);
	if (strncmp(rel, top, t) != 0 || rel[t] != '/') return 0;
	const char* d = rel + t + 1;
	const char* slash = strchr(d, '/');
	if (!slash || slash == d || (size_t)(slash - d) >= SY_NAME_MAX) return 0;
	const char* f = slash + 1;
	if (strchr(f, '/')) return 0;
	memcpy(dir, d, (size_t)(slash - d));
	dir[slash - d] = '\0';
	snprintf(file, SY_NAME_MAX, "%s", f);
	return Sy_syncable(dir) && Sy_syncable(f);
}

int Sy_kind(const char* rel) {
	char dir[SY_NAME_MAX], file[SY_NAME_MAX];
	if (!rel || strlen(rel) >= SY_PATH_MAX) return -1;
	if (splitRel(rel, "Saves", dir, file)) return SY_SAVE;
	if (splitRel(rel, "States", dir, file) && Sy_stateDir(dir)) return SY_STATE;
	return -1;
}

int Sy_relValid(const char* rel) {
	return Sy_kind(rel) >= 0;
}

int Sy_localPath(const char* sdcard, const char* rel, char* out, size_t size) {
	int kind = Sy_kind(rel);
	if (kind < 0) return -1;
	int n = kind == SY_SAVE ? snprintf(out, size, "%s/%s", sdcard, rel)
		: snprintf(out, size, "%s/.userdata/shared/%s", sdcard, rel + strlen("States/"));
	return n < 0 || (size_t)n >= size ? -1 : 0;
}

int Sy_relOf(const char* sdcard, const char* path, char* out, size_t size) {
	size_t s = strlen(sdcard);
	if (strncmp(path, sdcard, s) != 0 || path[s] != '/') return -1;
	const char* rest = path + s + 1;
	int n;
	if (!strncmp(rest, "Saves/", 6)) n = snprintf(out, size, "%s", rest);
	else if (!strncmp(rest, ".userdata/shared/", 17)) n = snprintf(out, size, "States/%s", rest + 17);
	else return -1;
	if (n < 0 || (size_t)n >= size) return -1;
	return Sy_relValid(out) ? 0 : -1;
}

int Sy_gameOf(const char* sdcard, const char* rom_path, char* tag, size_t tag_size, char* base, size_t base_size) {
	char roms[SY_PATH_MAX];
	int n = snprintf(roms, sizeof(roms), "%s/Roms/", sdcard);
	if (n < 0 || (size_t)n >= sizeof(roms) || strncmp(rom_path, roms, (size_t)n) != 0) return -1;
	const char* system = rom_path + n;
	const char* slash = strchr(system, '/');
	if (!slash) return -1;
	// the tag is in the system folder's last parentheses
	const char* open = NULL;
	for (const char* p = system; p < slash; p++) {
		if (*p == '(') open = p;
	}
	if (!open) return -1;
	const char* close = memchr(open, ')', (size_t)(slash - open));
	if (!close || close == open + 1 || (size_t)(close - open - 1) >= tag_size) return -1;
	memcpy(tag, open + 1, (size_t)(close - open - 1));
	tag[close - open - 1] = '\0';
	const char* file = strrchr(rom_path, '/') + 1;
	const char* dot = strrchr(file, '.');
	size_t len = dot && dot != file ? (size_t)(dot - file) : strlen(file);
	if (!len || len >= base_size) return -1;
	memcpy(base, file, len);
	base[len] = '\0';
	return 0;
}

int Sy_ofGame(const char* rel, const char* tag, const char* base) {
	char dir[SY_NAME_MAX], file[SY_NAME_MAX];
	size_t t = strlen(tag), b = strlen(base);
	if (!t || !b) return 0;
	if (splitRel(rel, "Saves", dir, file)) {
		if (strcmp(dir, tag) != 0) return 0;
	} else if (splitRel(rel, "States", dir, file) && Sy_stateDir(dir)) {
		if (strncmp(dir, tag, t) != 0 || dir[t] != '-') return 0;
	} else {
		return 0;
	}
	return !strncmp(file, base, b) && file[b] == '.';
}

int Sy_remoteJoin(const char* remote, const char* rel, char* out, size_t size) {
	size_t n = strlen(remote);
	const char* sep = n == 0 || remote[n - 1] == '/' || remote[n - 1] == ':' ? "" : "/";
	int w = snprintf(out, size, "%s%s%s", remote, sep, rel);
	return w < 0 || (size_t)w >= size ? -1 : 0;
}

long long Sy_utc(int year, int month, int day, int hour, int minute, int second) {
	if (year < 1970 || year > 9999 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0
			|| minute > 59 || second < 0 || second > 60) {
		return -1;
	}
	// days from 1970-01-01 to the date, by Howard Hinnant's days_from_civil
	int y = year - (month <= 2);
	long long era = y / 400;
	long long yoe = y - era * 400;
	long long doy = (153LL * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
	long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	long long days = era * 146097 + doe - 719468;
	return days * 86400 + hour * 3600LL + minute * 60LL + second;
}

static int digits(const char* p, int n, int* out) {
	int v = 0;
	for (int i = 0; i < n; i++) {
		if (!isdigit((unsigned char)p[i])) return -1;
		v = v * 10 + (p[i] - '0');
	}
	*out = v;
	return 0;
}

// "YYYY?MM?DD HH:MM:SS" with sep between the date's parts
static long long parseTime(const char* p, char sep) {
	int y, mo, d, h, mi, s;
	if (digits(p, 4, &y) || p[4] != sep || digits(p + 5, 2, &mo) || p[7] != sep || digits(p + 8, 2, &d) || p[10] != ' '
			|| digits(p + 11, 2, &h) || p[13] != ':' || digits(p + 14, 2, &mi) || p[16] != ':' || digits(p + 17, 2, &s)) {
		return -1;
	}
	return Sy_utc(y, mo, d, h, mi, s);
}

static int setRel(SyFile* out, const char* rel, size_t len) {
	if (!len || len >= sizeof(out->rel)) return -1;
	memcpy(out->rel, rel, len);
	out->rel[len] = '\0';
	return Sy_relValid(out->rel) ? 0 : -1;
}

static size_t lineLength(const char* line) {
	return strcspn(line, "\r\n");
}

int Sy_parseRclone(const char* line, SyFile* out) {
	memset(out, 0, sizeof(*out));
	size_t len = lineLength(line);
	const char* tab1 = memchr(line, '\t', len);
	if (!tab1) return -1;
	const char* tab2 = memchr(tab1 + 1, '\t', len - (size_t)(tab1 + 1 - line));
	if (!tab2 || tab2 == tab1 + 1) return -1;
	long long size = 0;
	for (const char* p = tab1 + 1; p < tab2; p++) {
		if (!isdigit((unsigned char)*p) || size > (1LL << 50)) return -1;
		size = size * 10 + (*p - '0');
	}
	if ((size_t)(line + len - (tab2 + 1)) < 19) return -1;
	long long mtime = parseTime(tab2 + 1, '-');
	if (mtime < 0) return -1;
	out->size = size;
	out->mtime = mtime;
	return setRel(out, line, (size_t)(tab1 - line));
}

static int number(const char** p, long long* out) {
	char* end;
	long long v = strtoll(*p, &end, 10);
	if (end == *p || *end != '\t') return -1;
	*out = v;
	*p = end + 1;
	return 0;
}

int Sy_baseParse(const char* line, SyBase* out) {
	memset(out, 0, sizeof(*out));
	const char* p = line;
	if (number(&p, &out->lsize) || number(&p, &out->lmtime) || number(&p, &out->rsize) || number(&p, &out->rmtime)) return -1;
	size_t len = lineLength(p);
	if (!len || len >= sizeof(out->rel)) return -1;
	memcpy(out->rel, p, len);
	out->rel[len] = '\0';
	return Sy_relValid(out->rel) ? 0 : -1;
}

int Sy_baseFormat(const SyBase* b, char* out, size_t size) {
	if (!Sy_relValid(b->rel)) return -1;
	int n = snprintf(out, size, "%lld\t%lld\t%lld\t%lld\t%s\n", b->lsize, b->lmtime, b->rsize, b->rmtime, b->rel);
	return n < 0 || (size_t)n >= size ? -1 : n;
}

static long long gap(long long a, long long b) {
	return a > b ? a - b : b - a;
}

int Sy_decide(const SyFile* l, const SyFile* r, const SyBase* b) {
	if (!l && !r) return SY_NONE;
	// on one side only: removed from the other since the last sync, so
	// removed here too unless it changed since; or new, so copied
	if (!r) return b && l->size == b->lsize && l->mtime == b->lmtime ? SY_DELETE_LOCAL : SY_UPLOAD;
	if (!l) return b && r->size == b->rsize && r->mtime == b->rmtime ? SY_DELETE_REMOTE : SY_DOWNLOAD;
	int alike = l->size == r->size && gap(l->mtime, r->mtime) <= SY_SLACK;
	int local_changed = !b || l->size != b->lsize || l->mtime != b->lmtime;
	int remote_changed = !b || r->size != b->rsize || r->mtime != b->rmtime;
	if (!local_changed && !remote_changed) return SY_NONE;
	if (alike) return SY_SAME;
	if (!remote_changed) return SY_UPLOAD;
	if (!local_changed) return SY_DOWNLOAD;
	// both changed: the newer wins, the local one on a tie
	return r->mtime > l->mtime + SY_SLACK ? SY_DOWNLOAD_KEEP : SY_UPLOAD_KEEP;
}

static int fileCompare(const void* a, const void* b) {
	return strcmp(((const SyFile*)a)->rel, ((const SyFile*)b)->rel);
}

static int baseCompare(const void* a, const void* b) {
	return strcmp(((const SyBase*)a)->rel, ((const SyBase*)b)->rel);
}

void Sy_sortFiles(SyFile* files, int count) {
	if (count > 1) qsort(files, (size_t)count, sizeof(*files), fileCompare);
}

void Sy_sortBase(SyBase* base, int count) {
	if (count > 1) qsort(base, (size_t)count, sizeof(*base), baseCompare);
}

const SyFile* Sy_findFile(const SyFile* files, int count, const char* rel) {
	int lo = 0, hi = count - 1;
	while (lo <= hi) {
		int mid = lo + (hi - lo) / 2;
		int c = strcmp(files[mid].rel, rel);
		if (!c) return &files[mid];
		if (c < 0) lo = mid + 1;
		else hi = mid - 1;
	}
	return NULL;
}

const SyBase* Sy_findBase(const SyBase* base, int count, const char* rel) {
	int lo = 0, hi = count - 1;
	while (lo <= hi) {
		int mid = lo + (hi - lo) / 2;
		int c = strcmp(base[mid].rel, rel);
		if (!c) return &base[mid];
		if (c < 0) lo = mid + 1;
		else hi = mid - 1;
	}
	return NULL;
}

// Walks the union of two sorted lists' rels in order: the next rel after
// *i and *j, advancing past it; NULL at the end.
static const char* nextRel(const SyFile* a, int na, int* i, const SyFile* b, int nb, int* j) {
	if (*i >= na && *j >= nb) return NULL;
	if (*i >= na) return b[(*j)++].rel;
	if (*j >= nb) return a[(*i)++].rel;
	int c = strcmp(a[*i].rel, b[*j].rel);
	if (c < 0) return a[(*i)++].rel;
	if (c > 0) return b[(*j)++].rel;
	(*j)++;
	return a[(*i)++].rel;
}

int Sy_plan(const SyFile* local, int local_count, const SyFile* remote, int remote_count, const SyBase* base, int base_count,
		int (*want)(const char* rel, void* data), void* data, SyStep* steps, int max) {
	int count = 0, i = 0, j = 0;
	const char* rel;
	while (count < max && (rel = nextRel(local, local_count, &i, remote, remote_count, &j))) {
		if (want && !want(rel, data)) continue;
		int action = Sy_decide(Sy_findFile(local, local_count, rel), Sy_findFile(remote, remote_count, rel),
			Sy_findBase(base, base_count, rel));
		if (action == SY_NONE) continue;
		snprintf(steps[count].rel, sizeof(steps[count].rel), "%s", rel);
		steps[count].action = action;
		count++;
	}
	return count;
}

int Sy_baseNext(const SyBase* old, int old_count, const SyFile* local, int local_count, const SyFile* remote, int remote_count,
		int (*want)(const char* rel, void* data), int (*failed)(const char* rel, void* data), void* data, SyBase* out, int max) {
	int count = 0;
	// what the run looked at, from the lists after it
	int i = 0, j = 0;
	const char* rel;
	while (count < max && (rel = nextRel(local, local_count, &i, remote, remote_count, &j))) {
		if (want && !want(rel, data)) continue;
		const SyFile* l = Sy_findFile(local, local_count, rel);
		const SyFile* r = Sy_findFile(remote, remote_count, rel);
		const SyBase* b = Sy_findBase(old, old_count, rel);
		if (failed && failed(rel, data)) {
			if (b) out[count++] = *b;
			continue;
		}
		if (!l || !r) continue;
		SyBase* n = &out[count++];
		snprintf(n->rel, sizeof(n->rel), "%s", rel);
		n->lsize = l->size;
		n->lmtime = l->mtime;
		n->rsize = r->size;
		n->rmtime = r->mtime;
	}
	// and the old record of files the run left out; a file it looked at
	// and found on neither side, as after a removal, is dropped
	for (int k = 0; k < old_count && count < max; k++) {
		const char* r = old[k].rel;
		if (want && !want(r, data)) out[count++] = old[k];
	}
	Sy_sortBase(out, count);
	return count;
}

int Sy_replyInt(const char* reply, const char* key, int fallback) {
	if (!reply) return fallback;
	size_t k = strlen(key);
	for (const char* line = reply; *line;) {
		if (!strncmp(line, key, k) && line[k] == '=') return atoi(line + k + 1);
		const char* next = strchr(line, '\n');
		if (!next) break;
		line = next + 1;
	}
	return fallback;
}

int Sy_rommSkips(const char* game_reply, const char* file_reply, int kind) {
	const char* any = game_reply ? game_reply : file_reply;
	if (!any || Sy_replyInt(any, "server", 0) != 1) return 0;
	if (Sy_replyInt(any, kind == SY_STATE ? "states" : "saves", 0) != 1) return 0;
	return Sy_replyInt(game_reply, "game", 0) == 1 || Sy_replyInt(file_reply, "synced", 0) == 1;
}

int Sy_deviceIdValid(const char* id) {
	if (!id) return 0;
	size_t len = strlen(id);
	int letters = 0;
	for (size_t i = 0; i < len; i++) {
		char c = (char)toupper((unsigned char)id[i]);
		if (len == 63 && i % 8 == 7) {
			if (c != '-') return 0;
			continue;
		}
		if (!((c >= 'A' && c <= 'Z') || (c >= '2' && c <= '7'))) return 0;
		letters++;
	}
	return (len == 63 || len == 56) && letters == 56;
}

int Sy_jsonValue(const char* json, const char* key, char* out, size_t size) {
	if (!json || !size) return -1;
	char quoted[128];
	int q = snprintf(quoted, sizeof(quoted), "\"%s\"", key);
	if (q < 0 || (size_t)q >= sizeof(quoted)) return -1;
	for (const char* p = strstr(json, quoted); p; p = strstr(p + 1, quoted)) {
		const char* v = p + q;
		while (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n') v++;
		if (*v != ':') continue;
		v++;
		while (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n') v++;
		const char* end;
		if (*v == '"') {
			v++;
			for (end = v; *end && *end != '"'; end++) {
				if (*end == '\\' && end[1]) end++;
			}
			if (*end != '"') return -1;
		} else {
			for (end = v; *end && *end != ',' && *end != '}' && *end != ']' && !isspace((unsigned char)*end); end++) {}
			if (end == v || *v == '{' || *v == '[') return -1;
		}
		size_t len = (size_t)(end - v);
		snprintf(out, size, "%.*s", (int)len, v);
		return (int)len;
	}
	return -1;
}

int Sy_jsonEscape(const char* text, char* out, size_t size) {
	size_t used = 0;
	for (const unsigned char* p = (const unsigned char*)text; *p; p++) {
		if (*p < 0x20) return -1;
		int esc = *p == '"' || *p == '\\';
		if (used + 1 + (size_t)esc >= size) return -1;
		if (esc) out[used++] = '\\';
		out[used++] = (char)*p;
	}
	if (used >= size) return -1;
	out[used] = '\0';
	return 0;
}

int Sy_ignoreEscape(const char* name, char* out, size_t size) {
	size_t used = 0;
	for (const char* p = name; *p; p++) {
		int esc = strchr("*?[]{}\\", *p) != NULL || (p == name && (*p == '!' || *p == '#'));
		if (used + 1 + (size_t)esc >= size) return -1;
		if (esc) out[used++] = '\\';
		out[used++] = *p;
	}
	if (used >= size) return -1;
	out[used] = '\0';
	return 0;
}
