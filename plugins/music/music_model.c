#include "music_model.h"

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

// English the plugin translates where it draws it, for make pot
#define TR_MARK(text) (text)

///////////////////////////////////////
// files

int MU_trackKind(const char* name) {
	if (!name || !name[0] || name[0] == '.') return MU_KIND_NONE;
	const char* dot = strrchr(name, '.');
	if (!dot || dot == name) return MU_KIND_NONE;
	if (!strcasecmp(dot, ".mp3")) return MU_KIND_MP3;
	if (!strcasecmp(dot, ".flac")) return MU_KIND_FLAC;
	if (!strcasecmp(dot, ".wav")) return MU_KIND_WAV;
	return MU_KIND_NONE;
}

int MU_compareNames(const char* a, const char* b) {
	const char *p = a, *q = b;
	while (*p && *q) {
		if (isdigit((unsigned char)*p) && isdigit((unsigned char)*q)) {
			while (*p == '0' && isdigit((unsigned char)p[1])) p++;
			while (*q == '0' && isdigit((unsigned char)q[1])) q++;
			const char *pe = p, *qe = q;
			while (isdigit((unsigned char)*pe)) pe++;
			while (isdigit((unsigned char)*qe)) qe++;
			if (pe - p != qe - q) return pe - p < qe - q ? -1 : 1;
			int c = strncmp(p, q, (size_t)(pe - p));
			if (c) return c < 0 ? -1 : 1;
			p = pe;
			q = qe;
			continue;
		}
		int x = tolower((unsigned char)*p), y = tolower((unsigned char)*q);
		if (x != y) return x < y ? -1 : 1;
		p++;
		q++;
	}
	if (*p || *q) return *p ? 1 : -1;
	int c = strcmp(a, b);
	return c < 0 ? -1 : c > 0;
}

void MU_trackTitle(const char* path, char* out, size_t size) {
	if (!size) return;
	out[0] = '\0';
	if (!path) return;
	const char* slash = strrchr(path, '/');
	const char* name = slash ? slash + 1 : path;
	const char* dot = strrchr(name, '.');
	size_t len = dot && dot != name ? (size_t)(dot - name) : strlen(name);
	if (len >= size) len = size - 1;
	memcpy(out, name, len);
	out[len] = '\0';
}

void MU_listInit(MuList* list) {
	list->items = NULL;
	list->count = list->dirs = 0;
}

void MU_listFree(MuList* list) {
	free(list->items);
	MU_listInit(list);
}

static int compareItems(const void* x, const void* y) {
	const MuItem *a = x, *b = y;
	if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1;
	return MU_compareNames(a->name, b->name);
}

int MU_scan(const char* dir, MuList* list, int with_dirs) {
	MU_listFree(list);
	DIR* d = opendir(dir);
	if (!d) return -1;
	int cap = 0;
	struct dirent* e;
	while ((e = readdir(d)) && list->count < MU_LIST_MAX) {
		if (e->d_name[0] == '.' || strlen(e->d_name) >= MU_NAME_MAX) continue;
		char path[MU_PATH_MAX];
		if (!MU_childPath(dir, e->d_name, path, sizeof(path))) continue;
		struct stat st;
		if (stat(path, &st) != 0) continue;
		int is_dir = S_ISDIR(st.st_mode);
		if (is_dir ? !with_dirs : !S_ISREG(st.st_mode) || MU_trackKind(e->d_name) == MU_KIND_NONE) continue;
		if (list->count == cap) {
			int grown = cap ? cap * 2 : 32;
			MuItem* items = realloc(list->items, (size_t)grown * sizeof(MuItem));
			if (!items) break;
			list->items = items;
			cap = grown;
		}
		MuItem* item = &list->items[list->count++];
		snprintf(item->name, sizeof(item->name), "%s", e->d_name);
		item->is_dir = is_dir;
		if (is_dir) list->dirs++;
	}
	closedir(d);
	if (list->count > 1) qsort(list->items, (size_t)list->count, sizeof(MuItem), compareItems);
	return list->count;
}

int MU_childPath(const char* dir, const char* name, char* out, size_t size) {
	int n = snprintf(out, size, "%s/%s", dir, name);
	return n > 0 && (size_t)n < size;
}

int MU_parentPath(const char* root, const char* dir, char* out, size_t size) {
	size_t root_len = strlen(root);
	if (strncmp(dir, root, root_len) != 0 || dir[root_len] != '/' || !dir[root_len + 1]) return 0;
	const char* slash = strrchr(dir, '/');
	size_t len = (size_t)(slash - dir);
	if (len < root_len || len >= size) return 0;
	memcpy(out, dir, len);
	out[len] = '\0';
	return 1;
}

///////////////////////////////////////
// the queue

int MU_parseRepeat(const char* text) {
	if (text && !strcmp(text, "all")) return MU_REPEAT_ALL;
	if (text && !strcmp(text, "one")) return MU_REPEAT_ONE;
	return MU_REPEAT_OFF;
}

const char* MU_repeatName(int repeat) {
	return repeat == MU_REPEAT_ALL ? TR_MARK("all") : repeat == MU_REPEAT_ONE ? TR_MARK("one") : TR_MARK("off");
}

void MuQueue_init(MuQueue* queue, unsigned int seed) {
	memset(queue, 0, sizeof(*queue));
	queue->seed = seed;
}

void MuQueue_free(MuQueue* queue) {
	free(queue->paths);
	free(queue->order);
	queue->paths = NULL;
	queue->order = NULL;
	queue->count = queue->pos = 0;
}

static unsigned int nextRandom(MuQueue* queue) {
	queue->seed = queue->seed * 1103515245u + 12345u;
	return queue->seed >> 16;
}

static void shuffleOrder(MuQueue* queue, int current) {
	for (int i = 0; i < queue->count; i++) queue->order[i] = i;
	for (int i = queue->count - 1; i > 0; i--) {
		int j = (int)(nextRandom(queue) % (unsigned int)(i + 1));
		int t = queue->order[i];
		queue->order[i] = queue->order[j];
		queue->order[j] = t;
	}
	for (int i = 0; i < queue->count; i++) {
		if (queue->order[i] != current) continue;
		queue->order[i] = queue->order[0];
		queue->order[0] = current;
		break;
	}
	queue->pos = 0;
}

int MuQueue_set(MuQueue* queue, const char* dir, const MuList* list, int start) {
	int tracks = list->count - list->dirs;
	if (tracks <= 0 || start < list->dirs || start >= list->count) return -1;
	char (*paths)[MU_PATH_MAX] = malloc((size_t)tracks * sizeof(*paths));
	int* order = malloc((size_t)tracks * sizeof(int));
	if (!paths || !order) {
		free(paths);
		free(order);
		return -1;
	}
	int count = 0;
	for (int i = list->dirs; i < list->count; i++) {
		if (!MU_childPath(dir, list->items[i].name, paths[count], MU_PATH_MAX)) {
			if (i == start) {
				free(paths);
				free(order);
				return -1;
			}
			continue;
		}
		if (i == start) start = count;
		count++;
	}
	free(queue->paths);
	free(queue->order);
	queue->paths = paths;
	queue->order = order;
	queue->count = count;
	if (queue->shuffle) shuffleOrder(queue, start);
	else {
		for (int i = 0; i < count; i++) order[i] = i;
		queue->pos = start;
	}
	return 0;
}

int MuQueue_current(const MuQueue* queue) {
	if (queue->count <= 0 || queue->pos < 0 || queue->pos >= queue->count) return -1;
	return queue->order[queue->pos];
}

int MuQueue_next(MuQueue* queue, int manual) {
	if (queue->count <= 0) return -1;
	if (!manual && queue->repeat == MU_REPEAT_ONE) return MuQueue_current(queue);
	if (queue->pos + 1 < queue->count) queue->pos++;
	else if (queue->repeat != MU_REPEAT_OFF) queue->pos = 0;
	else return -1;
	return MuQueue_current(queue);
}

int MuQueue_prev(MuQueue* queue) {
	if (queue->count <= 0) return -1;
	if (queue->pos > 0) queue->pos--;
	else if (queue->repeat != MU_REPEAT_OFF) queue->pos = queue->count - 1;
	return MuQueue_current(queue);
}

void MuQueue_setShuffle(MuQueue* queue, int on) {
	int current = MuQueue_current(queue);
	queue->shuffle = on ? 1 : 0;
	if (current < 0) return;
	if (on) shuffleOrder(queue, current);
	else {
		for (int i = 0; i < queue->count; i++) queue->order[i] = i;
		queue->pos = current;
	}
}

///////////////////////////////////////
// the protocol

static const char* const commands[] = {
	[MU_CMD_STATUS] = "status",
	[MU_CMD_PLAY] = "play",
	[MU_CMD_PAUSE] = "pause",
	[MU_CMD_RESUME] = "resume",
	[MU_CMD_TOGGLE] = "toggle",
	[MU_CMD_NEXT] = "next",
	[MU_CMD_PREV] = "prev",
	[MU_CMD_STOP] = "stop",
	[MU_CMD_SEEK] = "seek",
	[MU_CMD_SHUFFLE] = "shuffle",
	[MU_CMD_REPEAT] = "repeat",
	[MU_CMD_HOLD] = "hold",
	[MU_CMD_RELEASE] = "release",
	[MU_CMD_SLEEP] = "sleep",
	[MU_CMD_QUIT] = "quit",
	[MU_CMD_RESTORE] = "restore",
};
#define COMMAND_COUNT ((int)(sizeof(commands) / sizeof(commands[0])))

int MU_parseCommand(const char* line, char* arg, size_t size) {
	if (size) arg[0] = '\0';
	if (!line) return MU_CMD_NONE;
	size_t len = strlen(line);
	while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) len--;
	size_t name_len = 0;
	while (name_len < len && line[name_len] != ' ') name_len++;
	for (int i = 0; i < COMMAND_COUNT; i++) {
		if (strlen(commands[i]) != name_len || strncmp(line, commands[i], name_len) != 0) continue;
		if (name_len < len && size) {
			size_t n = len - name_len - 1;
			if (n >= size) return MU_CMD_NONE;
			memcpy(arg, line + name_len + 1, n);
			arg[n] = '\0';
		}
		return i;
	}
	return MU_CMD_NONE;
}

int MU_formatCommand(int command, const char* arg, char* out, size_t size) {
	if (command < 0 || command >= COMMAND_COUNT) return -1;
	if (arg && strpbrk(arg, "\r\n")) return -1;
	int n = arg && arg[0] ? snprintf(out, size, "%s %s\n", commands[command], arg)
		: snprintf(out, size, "%s\n", commands[command]);
	return n > 0 && (size_t)n < size ? n : -1;
}

const char* MU_stateName(int state) {
	return state == MU_PLAYING ? "playing" : state == MU_PAUSED ? "paused" : "stopped";
}

int MU_formatStatus(const MuStatus* s, char* out, size_t size) {
	if (strpbrk(s->track, "\r\n")) return -1;
	int n = snprintf(out, size, "ok\tstate=%s\tindex=%i\tcount=%i\tpos=%lld\tlen=%lld\tshuffle=%i\trepeat=%s\theld=%i\ttrack=%s\n",
		MU_stateName(s->state), s->index, s->count, s->pos_ms, s->len_ms, s->shuffle ? 1 : 0, MU_repeatName(s->repeat),
		s->held ? 1 : 0, s->track);
	return n > 0 && (size_t)n < size ? n : -1;
}

static int number(const char* text, long long* out) {
	if (!*text) return 0;
	long long v = 0;
	for (const char* p = text; *p; p++) {
		if (!isdigit((unsigned char)*p) || v > 1000000000000LL) return 0;
		v = v * 10 + (*p - '0');
	}
	*out = v;
	return 1;
}

int MU_parseStatus(const char* line, MuStatus* s) {
	memset(s, 0, sizeof(*s));
	if (!line || strncmp(line, "ok\t", 3) != 0) return 0;
	const char* p = line + 3;
	int seen = 0;
	while (*p) {
		if (!strncmp(p, "track=", 6)) {
			p += 6;
			size_t len = strlen(p);
			while (len && (p[len - 1] == '\n' || p[len - 1] == '\r')) len--;
			if (len >= sizeof(s->track)) return 0;
			memcpy(s->track, p, len);
			s->track[len] = '\0';
			return seen == 8;
		}
		const char* tab = strchr(p, '\t');
		if (!tab) return 0;
		char field[64];
		size_t len = (size_t)(tab - p);
		if (len >= sizeof(field)) return 0;
		memcpy(field, p, len);
		field[len] = '\0';
		p = tab + 1;
		char* eq = strchr(field, '=');
		if (!eq) return 0;
		*eq = '\0';
		const char* value = eq + 1;
		long long v = 0;
		if (!strcmp(field, "state")) {
			if (!strcmp(value, "playing")) s->state = MU_PLAYING;
			else if (!strcmp(value, "paused")) s->state = MU_PAUSED;
			else if (!strcmp(value, "stopped")) s->state = MU_STOPPED;
			else return 0;
		} else if (!strcmp(field, "repeat")) {
			s->repeat = MU_parseRepeat(value);
		} else if (number(value, &v)) {
			if (!strcmp(field, "index")) s->index = (int)v;
			else if (!strcmp(field, "count")) s->count = (int)v;
			else if (!strcmp(field, "pos")) s->pos_ms = v;
			else if (!strcmp(field, "len")) s->len_ms = v;
			else if (!strcmp(field, "shuffle")) s->shuffle = v != 0;
			else if (!strcmp(field, "held")) s->held = v != 0;
			else return 0;
		} else return 0;
		seen++;
	}
	return 0;
}

int MU_parseSeek(const char* arg, long long pos_ms, long long len_ms, long long* out) {
	if (!arg) return 0;
	int sign = 0;
	if (*arg == '+' || *arg == '-') sign = *arg++ == '+' ? 1 : -1;
	long long v;
	if (!number(arg, &v)) return 0;
	long long to = sign ? pos_ms + sign * v : v;
	if (len_ms > 0 && to > len_ms) to = len_ms;
	if (to < 0) to = 0;
	*out = to;
	return 1;
}

void MU_formatTime(long long ms, char* out, size_t size) {
	if (ms < 0) ms = 0;
	long long s = ms / 1000;
	if (s >= 3600) snprintf(out, size, "%lld:%02lld:%02lld", s / 3600, s / 60 % 60, s % 60);
	else snprintf(out, size, "%lld:%02lld", s / 60, s % 60);
}

///////////////////////////////////////
// resuming after a power-off

// a line of the order field, MU_LIST_MAX indexes of up to 4 digits and
// their commas, and its key
#define RESUME_LINE_MAX (MU_LIST_MAX * 5 + 64)

int MU_isFile(const char* path, void* data) {
	(void)data;
	struct stat st;
	return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int resumePath(const char* path) {
	return path[0] == '/' && strlen(path) < MU_PATH_MAX && !strpbrk(path, "\r\n");
}

// Syncs the folder path is in, so a rename into it lasts.
static void syncFolder(const char* path) {
	char folder[MU_PATH_MAX];
	const char* slash = strrchr(path, '/');
	if (!slash || (size_t)(slash - path) >= sizeof(folder)) return;
	size_t len = slash == path ? 1 : (size_t)(slash - path);
	memcpy(folder, path, len);
	folder[len] = '\0';
	int fd = open(folder, O_RDONLY);
	if (fd < 0) return;
	fsync(fd);
	close(fd);
}

int MU_writeResume(const char* path, const MuQueue* q, long long pos_ms, long long len_ms) {
	int current = MuQueue_current(q);
	if (!path || current < 0 || q->count > MU_LIST_MAX || pos_ms < 0 || len_ms < 0) return -1;
	for (int i = 0; i < q->count; i++) {
		if (!resumePath(q->paths[i])) return -1;
	}
	char tmp[MU_PATH_MAX + 8];
	int n = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	if (n <= 0 || (size_t)n >= sizeof(tmp)) return -1;
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return -1;
	FILE* f = fdopen(fd, "w");
	if (!f) {
		close(fd);
		unlink(tmp);
		return -1;
	}
	fprintf(f, "%s\npos=%lld\nlen=%lld\nindex=%i\nshuffle=%i\nrepeat=%s\ncount=%i\norder=", MU_RESUME_HEADER, pos_ms,
		len_ms, q->pos, q->shuffle ? 1 : 0, MU_repeatName(q->repeat), q->count);
	for (int i = 0; i < q->count; i++) fprintf(f, i ? ",%i" : "%i", q->order[i]);
	fprintf(f, "\ntrack=%s\n", q->paths[current]);
	for (int i = 0; i < q->count; i++) fprintf(f, "path=%s\n", q->paths[i]);
	fputs("end\n", f);
	int ok = fflush(f) == 0 && !ferror(f) && fsync(fileno(f)) == 0;
	if (fclose(f) != 0) ok = 0;
	if (!ok || rename(tmp, path) != 0) {
		unlink(tmp);
		return -1;
	}
	syncFolder(path);
	return 0;
}

// The next line, without its line break, into line. Returns 0 at the end
// of the file, or for a line too long for it.
static int readLine(FILE* f, char* line, size_t size) {
	if (!fgets(line, (int)size, f)) return 0;
	size_t len = strlen(line);
	if (len && line[len - 1] == '\n') line[--len] = '\0';
	else if (!feof(f)) return 0;
	return 1;
}

// count indexes, by commas, each below count and each once.
static int parseOrder(const char* text, int* order, int count) {
	char* seen = calloc((size_t)count, 1);
	if (!seen) return 0;
	const char* p = text;
	int ok = 1;
	for (int i = 0; i < count && ok; i++) {
		if (i && *p++ != ',') ok = 0;
		else if (!isdigit((unsigned char)*p)) ok = 0;
		else {
			long v = 0;
			while (isdigit((unsigned char)*p) && v <= count) v = v * 10 + (*p++ - '0');
			if (isdigit((unsigned char)*p) || v >= count || seen[v]) ok = 0;
			else {
				seen[v] = 1;
				order[i] = (int)v;
			}
		}
	}
	if (*p) ok = 0;
	free(seen);
	return ok;
}

static int parseResume(FILE* f, char* line, MuQueue* q, long long* pos_ms, long long* len_ms) {
	if (!readLine(f, line, RESUME_LINE_MAX) || strcmp(line, MU_RESUME_HEADER) != 0) return 0;
	long long pos = -1, len = -1, index = -1, shuffle = -1, count = -1;
	int repeat = -1;
	char track[MU_PATH_MAX] = "";
	char* order_text = NULL;
	char (*paths)[MU_PATH_MAX] = NULL;
	int* order = NULL;
	int ok = 0;
	// the fields, up to the first path
	for (;;) {
		if (!readLine(f, line, RESUME_LINE_MAX)) goto done;
		if (!strncmp(line, "path=", 5)) break;
		char* eq = strchr(line, '=');
		if (!eq) goto done;
		*eq = '\0';
		const char* value = eq + 1;
		long long v = 0;
		if (!strcmp(line, "track")) {
			if (track[0] || !resumePath(value)) goto done;
			snprintf(track, sizeof(track), "%s", value);
		} else if (!strcmp(line, "order")) {
			if (order_text || !(order_text = strdup(value))) goto done;
		} else if (!strcmp(line, "repeat")) {
			if (repeat >= 0) goto done;
			if (!strcmp(value, "off")) repeat = MU_REPEAT_OFF;
			else if (!strcmp(value, "all")) repeat = MU_REPEAT_ALL;
			else if (!strcmp(value, "one")) repeat = MU_REPEAT_ONE;
			else goto done;
		} else {
			long long* field = !strcmp(line, "pos") ? &pos
				: !strcmp(line, "len") ? &len
				: !strcmp(line, "index") ? &index
				: !strcmp(line, "shuffle") ? &shuffle
				: !strcmp(line, "count") ? &count
				: NULL;
			if (!field || *field >= 0 || !number(value, &v)) goto done;
			*field = v;
		}
	}
	if (pos < 0 || len < 0 || shuffle < 0 || shuffle > 1 || repeat < 0 || !track[0] || !order_text || count < 1
		|| count > MU_LIST_MAX || index < 0 || index >= count) {
		goto done;
	}
	paths = malloc((size_t)count * sizeof(*paths));
	order = malloc((size_t)count * sizeof(int));
	if (!paths || !order || !parseOrder(order_text, order, (int)count)) goto done;
	// the paths, then the end line
	for (int i = 0; i < count; i++) {
		if (i && !readLine(f, line, RESUME_LINE_MAX)) goto done;
		if (strncmp(line, "path=", 5) != 0 || !resumePath(line + 5)) goto done;
		snprintf(paths[i], MU_PATH_MAX, "%s", line + 5);
	}
	if (!readLine(f, line, RESUME_LINE_MAX) || strcmp(line, "end") != 0) goto done;
	if (strcmp(paths[order[index]], track) != 0) goto done;
	if (len > 0 && pos > len) pos = len;
	q->paths = paths;
	q->order = order;
	paths = NULL;
	order = NULL;
	q->count = (int)count;
	q->pos = (int)index;
	q->shuffle = (int)shuffle;
	q->repeat = repeat;
	*pos_ms = pos;
	*len_ms = len;
	ok = 1;
done:
	free(order_text);
	free(paths);
	free(order);
	return ok;
}

int MU_readResume(const char* path, MuQueue* q, long long* pos_ms, long long* len_ms) {
	MuQueue_free(q);
	*pos_ms = *len_ms = 0;
	FILE* f = path ? fopen(path, "r") : NULL;
	if (!f) return 0;
	char* line = malloc(RESUME_LINE_MAX);
	int ok = line && parseResume(f, line, q, pos_ms, len_ms);
	free(line);
	fclose(f);
	return ok;
}

int MuQueue_dropMissing(MuQueue* q, int (*exists)(const char* path, void* data), void* data) {
	int current = MuQueue_current(q);
	int* index = current >= 0 ? malloc((size_t)q->count * sizeof(int)) : NULL;
	if (!index || !exists(q->paths[current], data)) {
		free(index);
		MuQueue_free(q);
		return -1;
	}
	// each track's new index, or -1 for a gone one
	int count = 0;
	for (int i = 0; i < q->count; i++) {
		if (i != current && !exists(q->paths[i], data)) {
			index[i] = -1;
			continue;
		}
		if (count != i) memcpy(q->paths[count], q->paths[i], MU_PATH_MAX);
		index[i] = count++;
	}
	int n = 0;
	for (int i = 0; i < q->count; i++) {
		int t = q->order[i];
		if (index[t] < 0) continue;
		if (t == current) q->pos = n;
		q->order[n++] = index[t];
	}
	q->count = count;
	free(index);
	return count;
}

///////////////////////////////////////
// the plugin's state

int MU_parseOverlay(const char* text) {
	if (!text || !strcmp(text, "off")) return 0;
	if (!strcmp(text, "always")) return MU_OVERLAY_ALWAYS;
	long long v;
	if (!number(text, &v) || v > 3600) return 0;
	return (int)v;
}

void MuOverlay_init(MuOverlay* overlay, int seconds) {
	memset(overlay, 0, sizeof(*overlay));
	overlay->seconds = seconds;
}

int MuOverlay_update(MuOverlay* overlay, const MuStatus* status, unsigned long now) {
	int again = status->state == MU_PLAYING
		&& (overlay->state != MU_PLAYING || strcmp(overlay->track, status->track) != 0);
	overlay->state = status->state;
	snprintf(overlay->track, sizeof(overlay->track), "%s", status->track);
	if (again && overlay->seconds > 0) overlay->until = now + (unsigned long)overlay->seconds * 1000;
	return again;
}

int MuOverlay_visible(const MuOverlay* overlay, unsigned long now) {
	if (overlay->seconds == 0 || overlay->state == MU_STOPPED || !overlay->track[0]) return 0;
	if (overlay->seconds == MU_OVERLAY_ALWAYS) return 1;
	return now < overlay->until;
}

void MuOverlay_show(MuOverlay* overlay, const MuStatus* status, unsigned long now) {
	overlay->state = status->state;
	snprintf(overlay->track, sizeof(overlay->track), "%s", status->track);
	if (overlay->seconds > 0) overlay->until = now + (unsigned long)overlay->seconds * 1000;
}

int MU_move(int selected, int count, int delta, int wrap) {
	if (count <= 0) return 0;
	int to = selected + delta;
	if (to < 0) to = wrap && selected == 0 ? count - 1 : 0;
	else if (to >= count) to = wrap && selected == count - 1 ? 0 : count - 1;
	return to;
}

int MU_window(int selected, int count, int rows, int first) {
	if (rows < 1 || count <= rows) return 0;
	if (selected < first) first = selected;
	else if (selected >= first + rows) first = selected - rows + 1;
	if (first > count - rows) first = count - rows;
	return first < 0 ? 0 : first;
}

int MuUnlock_frame(MuUnlock* unlock, unsigned int pressed, unsigned int held, unsigned int select_bit,
	unsigned int start_bit, unsigned long now) {
	if (pressed & start_bit) {
		int ok = (held & select_bit) || (unlock->armed && now - unlock->select_at <= MU_UNLOCK_MS);
		unlock->armed = 0;
		if (ok) return 1;
	}
	if (pressed & select_bit) {
		unlock->armed = 1;
		unlock->select_at = now;
	} else if (pressed & ~start_bit) {
		unlock->armed = 0;
	}
	return 0;
}
