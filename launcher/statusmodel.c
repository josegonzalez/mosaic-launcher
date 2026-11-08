#include <stdio.h>
#include <string.h>
#include "statusmodel.h"

static int isSpace(char c) {
	return c == ' ' || c == '\t';
}

// [*start, *end) without the spaces and tabs around it
static void trim(const char** start, const char** end) {
	while (*start < *end && isSpace(**start)) (*start)++;
	while (*end > *start && isSpace((*end)[-1])) (*end)--;
}

static int is(const char* start, const char* end, const char* word) {
	size_t n = strlen(word);
	return (size_t)(end - start) == n && !memcmp(start, word, n);
}

// 1 to 9 digits, no sign
static int number(const char* start, const char* end, long* out) {
	size_t n = (size_t)(end - start);
	if (n == 0 || n > 9) return 0;
	long value = 0;
	for (const char* c = start; c < end; c++) {
		if (*c < '0' || *c > '9') return 0;
		value = value * 10 + (*c - '0');
	}
	*out = value;
	return 1;
}

// How many bytes the UTF-8 character at text takes, or 0 when it isn't one.
static size_t utf8Length(const unsigned char* text, size_t left) {
	unsigned char c = text[0];
	size_t n;
	if (c >= 0xC2 && c <= 0xDF) n = 2;
	else if (c >= 0xE0 && c <= 0xEF) n = 3;
	else if (c >= 0xF0 && c <= 0xF4) n = 4;
	else return 0;
	if (n > left) return 0;
	for (size_t i = 1; i < n; i++) {
		if ((text[i] & 0xC0) != 0x80) return 0;
	}
	return n;
}

void Status_sanitise(const char* text, size_t len, int max_chars, char* out, size_t size) {
	if (!size) return;
	out[0] = '\0';
	if (!text) return;
	const unsigned char* p = (const unsigned char*)text;
	const unsigned char* end = p + len;
	while (p < end && isSpace((char)*p)) p++;
	size_t n = 0;
	int chars = 0;
	while (p < end && chars < max_chars && n + 1 < size) {
		char c;
		if (*p >= 0x20 && *p <= 0x7E) {
			c = (char)*p;
			p++;
		}
		else if (*p == '\t') {
			c = ' ';
			p++;
		}
		else {
			size_t skip = *p >= 0x80 ? utf8Length(p, (size_t)(end - p)) : 0;
			c = '?';
			p += skip ? skip : 1;
		}
		out[n++] = c;
		chars++;
	}
	while (n > 0 && out[n - 1] == ' ') n--;
	out[n] = '\0';
}

int Status_parse(const char* data, size_t len, StatusFile* out, const char** why) {
	const char* ignored;
	if (!why) why = &ignored;
	*why = NULL;
	memset(out, 0, sizeof(*out));
	out->state = -1;
	out->done = out->total = -1;
	if (!data) {
		*why = "it can't be read";
		return -1;
	}
	if (len > STATUS_FILE_MAX) {
		*why = "it's over 1024 bytes";
		return -1;
	}
	if (memchr(data, '\0', len)) {
		*why = "it isn't text";
		return -1;
	}

	const char* p = data;
	const char* end = data + len;
	while (p < end) {
		const char* eol = memchr(p, '\n', (size_t)(end - p));
		if (!eol) eol = end;
		const char* line = p;
		const char* line_end = eol;
		p = eol < end ? eol + 1 : end;
		if (line_end > line && line_end[-1] == '\r') line_end--;
		trim(&line, &line_end);
		if (line == line_end || *line == '#') continue;

		const char* eq = memchr(line, '=', (size_t)(line_end - line));
		if (!eq) {
			*why = "a line has no =";
			return -1;
		}
		const char* key = line;
		const char* key_end = eq;
		const char* value = eq + 1;
		const char* value_end = line_end;
		trim(&key, &key_end);
		trim(&value, &value_end);
		if (key == key_end) {
			*why = "a line has no key";
			return -1;
		}

		if (is(key, key_end, "state")) {
			if (is(value, value_end, "running")) out->state = STATUS_RUNNING;
			else if (is(value, value_end, "done")) out->state = STATUS_DONE;
			else if (is(value, value_end, "error")) out->state = STATUS_ERROR;
			else {
				*why = "state isn't running, done or error";
				return -1;
			}
		}
		else if (is(key, key_end, "done")) {
			if (!number(value, value_end, &out->done)) {
				*why = "done isn't a number from 0 to 999999999";
				return -1;
			}
		}
		else if (is(key, key_end, "total")) {
			if (!number(value, value_end, &out->total)) {
				*why = "total isn't a number from 0 to 999999999";
				return -1;
			}
		}
		else if (is(key, key_end, "label")) {
			Status_sanitise(value, (size_t)(value_end - value), STATUS_LABEL_MAX, out->label, sizeof(out->label));
		}
		// other keys are left for newer launchers
	}

	if (out->state < 0) {
		*why = "it has no state";
		return -1;
	}
	if (out->total >= 0 && out->done > out->total) out->done = out->total;
	return 0;
}

int Status_fileName(const char* file, char* out, size_t size) {
	static const char suffix[] = ".status";
	if (size) out[0] = '\0';
	if (!file || file[0] == '.' || strchr(file, '/')) return 0;
	size_t n = strlen(file);
	size_t s = sizeof(suffix) - 1;
	if (n <= s || strcmp(file + n - s, suffix) != 0) return 0;
	size_t name = n - s;
	if (name >= size || name >= STATUS_NAME_MAX) return 0;
	memcpy(out, file, name);
	out[name] = '\0';
	return 1;
}

void Status_text(const StatusFile* file, const char* pak, char* out, size_t size) {
	if (!size) return;
	char name[STATUS_LABEL_MAX + 1];
	if (file->label[0]) snprintf(name, sizeof(name), "%s", file->label);
	else Status_sanitise(pak, pak ? strlen(pak) : 0, STATUS_LABEL_MAX, name, sizeof(name));

	if (file->state == STATUS_DONE) snprintf(out, size, "%s done", name);
	else if (file->state == STATUS_ERROR) snprintf(out, size, "%s failed", name);
	else if (file->total > 0) {
		long done = file->done > 0 ? file->done : 0;
		snprintf(out, size, "%s %lld%%", name, (long long)done * 100 / file->total);
	}
	else snprintf(out, size, "%s", name);
}

///////////////////////////////////////
// the board

int StatusBoard_due(StatusBoard* board, unsigned long now) {
	if (now < board->next_poll) return 0;
	board->next_poll = now + STATUS_POLL_MS;
	return 1;
}

void StatusBoard_begin(StatusBoard* board) {
	for (int i = 0; i < STATUS_SLOTS; i++) board->slots[i].seen = 0;
}

static int sameSig(const StatusSig* a, const StatusSig* b) {
	return a->mtime == b->mtime && a->mtime_ns == b->mtime_ns && a->size == b->size && a->ino == b->ino;
}

int StatusBoard_track(StatusBoard* board, const char* pak, const StatusSig* sig, unsigned long now,
		StatusSlot** out) {
	StatusSlot* free_slot = NULL;
	for (int i = 0; i < STATUS_SLOTS; i++) {
		StatusSlot* slot = &board->slots[i];
		if (!slot->name[0]) {
			if (!free_slot) free_slot = slot;
			continue;
		}
		if (strcmp(slot->name, pak) != 0) continue;
		slot->seen = 1;
		*out = slot;
		if (sameSig(&slot->sig, sig)) return 0;
		slot->sig = *sig;
		slot->changed_ms = now;
		return 1;
	}
	if (!free_slot) return -1;
	memset(free_slot, 0, sizeof(*free_slot));
	snprintf(free_slot->name, sizeof(free_slot->name), "%s", pak);
	free_slot->sig = *sig;
	free_slot->changed_ms = now;
	free_slot->seen = 1;
	*out = free_slot;
	return 1;
}

int StatusSlot_set(StatusSlot* slot, const char* data, size_t len, unsigned long now, const char** why) {
	slot->valid = Status_parse(data, len, &slot->file, why) == 0;
	if (!slot->valid || slot->file.state != STATUS_DONE) slot->done_seen = 0;
	else if (!slot->done_seen) {
		slot->done_seen = 1;
		slot->done_ms = now;
	}
	return slot->valid ? 0 : -1;
}

void StatusBoard_end(StatusBoard* board) {
	for (int i = 0; i < STATUS_SLOTS; i++) {
		if (board->slots[i].name[0] && !board->slots[i].seen) memset(&board->slots[i], 0, sizeof(board->slots[i]));
	}
}

int StatusSlot_visible(const StatusSlot* slot, unsigned long now, long long wall_now) {
	if (!slot->name[0] || !slot->valid) return 0;
	// a file from the future has no age by the clock, only by the launcher's
	long long age = wall_now - slot->sig.mtime;
	if (age > 0 && age * 1000 > STATUS_STALE_MS) return 0;
	if (slot->file.state == STATUS_DONE) return slot->done_seen && now - slot->done_ms < STATUS_DONE_MS;
	return now - slot->changed_ms < STATUS_STALE_MS;
}

static int newer(const StatusSlot* a, const StatusSlot* b) {
	if (a->sig.mtime != b->sig.mtime) return a->sig.mtime > b->sig.mtime;
	if (a->sig.mtime_ns != b->sig.mtime_ns) return a->sig.mtime_ns > b->sig.mtime_ns;
	return strcmp(a->name, b->name) < 0;
}

const StatusSlot* StatusBoard_pick(const StatusBoard* board, unsigned long now, long long wall_now) {
	const StatusSlot* best = NULL;
	for (int i = 0; i < STATUS_SLOTS; i++) {
		const StatusSlot* slot = &board->slots[i];
		if (!StatusSlot_visible(slot, now, wall_now)) continue;
		if (!best || newer(slot, best)) best = slot;
	}
	return best;
}
