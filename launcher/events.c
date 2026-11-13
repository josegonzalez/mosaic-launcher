#include <string.h>
#include "events.h"

int Events_nameValid(const char* name) {
	if (!name) return 0;
	size_t len = strlen(name);
	if (!len || len >= EVENT_NAME_MAX) return 0;
	for (size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)name[i];
		if (c <= 0x20 || c == 0x7f) return 0;
	}
	return 1;
}

int Events_mayPublish(const char* name, const char* publisher) {
	if (!Events_nameValid(name)) return 0;
	if (!publisher) {
		size_t len = strlen(EVENT_HOST_PREFIX);
		return !strncmp(name, EVENT_HOST_PREFIX, len) && name[len];
	}
	if (!publisher[0] || !strcmp(publisher, "launcher")) return 0;
	size_t own = strlen(publisher);
	return !strncmp(name, publisher, own) && name[own] == '.' && name[own + 1];
}

static int keyChar(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
}

int Events_payloadValid(const char* payload) {
	if (!payload || !payload[0]) return 1;
	if (strlen(payload) >= EVENT_PAYLOAD_MAX) return 0;
	const char* line = payload;
	while (*line) {
		size_t key = 0;
		while (keyChar(line[key])) key++;
		if (!key || key >= EVENT_KEY_MAX || line[key] != '=') return 0;
		const char* value = line + key + 1;
		while (*value && *value != '\n') {
			unsigned char c = (unsigned char)*value;
			if (c < 0x20 || c == 0x7f) return 0;
			value++;
		}
		if (!*value) break;
		line = value + 1; // past the \n; one at the very end ends the loop
	}
	return 1;
}

int Events_value(const char* payload, const char* key, char* out, size_t size) {
	if (out && size) out[0] = '\0';
	if (!payload || !key || !key[0]) return -1;
	size_t want = strlen(key);
	const char* line = payload;
	while (*line) {
		const char* end = strchr(line, '\n');
		if (!end) end = line + strlen(line);
		if ((size_t)(end - line) > want && !strncmp(line, key, want) && line[want] == '=') {
			const char* value = line + want + 1;
			size_t len = (size_t)(end - value);
			if (out && size) {
				size_t copy = len < size - 1 ? len : size - 1;
				memcpy(out, value, copy);
				out[copy] = '\0';
			}
			return (int)len;
		}
		if (!*end) break;
		line = end + 1;
	}
	return -1;
}

void EventQueue_init(EventQueue* queue) {
	queue->head = 0;
	queue->count = 0;
}

int EventQueue_publish(EventQueue* queue, const char* name, const char* payload, const char* publisher) {
	if (!Events_nameValid(name)) return EVENT_BAD_NAME;
	if (!Events_mayPublish(name, publisher)) return EVENT_NOT_YOURS;
	return EventQueue_add(queue, name, payload, !publisher);
}

int EventQueue_add(EventQueue* queue, const char* name, const char* payload, int reserve) {
	if (!Events_nameValid(name)) return EVENT_BAD_NAME;
	if (!Events_payloadValid(payload)) return EVENT_BAD_PAYLOAD;
	int room = reserve ? EVENT_QUEUE_MAX : EVENT_QUEUE_MAX - EVENT_HOST_RESERVE;
	if (queue->count >= room) return EVENT_FULL;
	Event* event = &queue->items[(queue->head + queue->count) % EVENT_QUEUE_MAX];
	strcpy(event->name, name);
	strcpy(event->payload, payload ? payload : "");
	queue->count++;
	return EVENT_OK;
}

int EventQueue_take(EventQueue* queue, Event* out) {
	if (!queue->count) return 0;
	*out = queue->items[queue->head];
	queue->head = (queue->head + 1) % EVENT_QUEUE_MAX;
	queue->count--;
	return 1;
}

void EventSubs_init(EventSubs* subs) {
	subs->count = 0;
}

static int findIndex(const EventSubs* subs, const char* name, const char* owner) {
	for (int i = 0; i < subs->count; i++) {
		if (!strcmp(subs->items[i].name, name) && !strcmp(subs->items[i].owner, owner)) return i;
	}
	return -1;
}

int EventSubs_add(EventSubs* subs, const char* name, const char* owner, EventFn fn, void* data) {
	if (!Events_nameValid(name) || !owner || !owner[0] || strlen(owner) >= EVENT_OWNER_MAX || !fn) return EVENT_BAD_NAME;
	int i = findIndex(subs, name, owner);
	if (i < 0) {
		if (subs->count >= EVENT_SUBS_MAX) return EVENT_FULL;
		i = subs->count++;
		strcpy(subs->items[i].name, name);
		strcpy(subs->items[i].owner, owner);
	}
	subs->items[i].fn = fn;
	subs->items[i].data = data;
	return EVENT_OK;
}

static void removeAt(EventSubs* subs, int i) {
	memmove(&subs->items[i], &subs->items[i + 1], (size_t)(subs->count - i - 1) * sizeof(EventSub));
	subs->count--;
}

int EventSubs_remove(EventSubs* subs, const char* name, const char* owner) {
	if (!name || !owner) return -1;
	int i = findIndex(subs, name, owner);
	if (i < 0) return -1;
	removeAt(subs, i);
	return 0;
}

const EventSub* EventSubs_find(const EventSubs* subs, const char* name, const char* owner) {
	if (!name || !owner) return NULL;
	int i = findIndex(subs, name, owner);
	return i < 0 ? NULL : &subs->items[i];
}

int EventSubs_match(const EventSubs* subs, const char* name, EventSub* out, int max) {
	int n = 0;
	for (int i = 0; i < subs->count && n < max; i++) {
		if (!strcmp(subs->items[i].name, name)) out[n++] = subs->items[i];
	}
	return n;
}

int EventSubs_releaseAll(EventSubs* subs, const char* owner) {
	if (!owner) return 0;
	int released = 0;
	for (int i = subs->count - 1; i >= 0; i--) {
		if (strcmp(subs->items[i].owner, owner) != 0) continue;
		removeAt(subs, i);
		released++;
	}
	return released;
}
