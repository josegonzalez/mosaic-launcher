#ifndef EVENTS_H
#define EVENTS_H

// The SDL-free half of plugins' events (host->subscribe and publish): the
// names a plugin may publish, payload checks and lookups, the queue events
// wait in until the launcher's thread delivers them, and who is subscribed
// to what. plugins.c locks the queue, since a plugin may publish from its
// own thread, and runs each subscriber's callback as that subscriber.

#include <stddef.h>

#define EVENT_NAME_MAX 64     // an event's name's bytes, its terminator included
#define EVENT_PAYLOAD_MAX 1024 // a payload's bytes, its terminator included
#define EVENT_KEY_MAX 32      // a payload key's bytes, its terminator included
#define EVENT_OWNER_MAX 64    // a plugin id's bytes, as LoadedPlugin's
#define EVENT_QUEUE_MAX 64    // the most events waiting at once
#define EVENT_HOST_RESERVE 8  // of those, the room only the launcher's own events can use
#define EVENT_SUBS_MAX 256    // the most subscriptions at once, across every plugin
#define EVENT_HOST_PREFIX "launcher." // the launcher's own events' names start with it

// A subscriber's callback, as MosaicHost's subscribe takes it.
typedef void (*EventFn)(const char* name, const char* payload, void* data);

typedef struct Event {
	char name[EVENT_NAME_MAX];
	char payload[EVENT_PAYLOAD_MAX];
} Event;

typedef struct EventQueue {
	Event items[EVENT_QUEUE_MAX]; // a ring, oldest at head
	int head;
	int count;
} EventQueue;

typedef struct EventSub {
	char name[EVENT_NAME_MAX];
	char owner[EVENT_OWNER_MAX]; // the plugin that subscribed
	EventFn fn;
	void* data;
} EventSub;

typedef struct EventSubs {
	EventSub items[EVENT_SUBS_MAX]; // in the order they subscribed
	int count;
} EventSubs;

// What the calls below return.
enum EventResult {
	EVENT_OK = 0,
	EVENT_BAD_NAME = -1,    // no name, an empty or too long one, or one with spaces or control characters
	EVENT_NOT_YOURS = -2,   // a name the publisher can't publish
	EVENT_BAD_PAYLOAD = -3, // a payload that isn't key=value lines, or is too long
	EVENT_FULL = -4,        // the queue, or the subscriptions, are full
};

// 1 for a name anyone may subscribe to: 1 to EVENT_NAME_MAX - 1 bytes,
// with no spaces or control characters.
int Events_nameValid(const char* name);

// 1 when publisher may publish name: <publisher>.<something> for a
// plugin, or launcher.<something> for the launcher (publisher NULL). A
// plugin whose id is "launcher" may publish nothing.
int Events_mayPublish(const char* name, const char* publisher);

// 1 for a payload of key=value lines separated by \n, as publish takes
// it: NULL or "" for none, shorter than EVENT_PAYLOAD_MAX, each key 1 to
// EVENT_KEY_MAX - 1 letters, digits, '_', '-' or '.', each value with no
// control characters, and one \n after the last line allowed.
int Events_payloadValid(const char* payload);

// The value of key in payload, the first line with it, copied into out
// (cut to fit, and terminated when size is above 0). Returns the value's
// whole length, or -1 when payload has no such key.
int Events_value(const char* payload, const char* key, char* out, size_t size);

void EventQueue_init(EventQueue* queue);

// Checks that publisher may publish name, then adds it as EventQueue_add
// does, with the reserve only for the launcher's own events (publisher
// NULL). Returns an enum EventResult.
int EventQueue_publish(EventQueue* queue, const char* name, const char* payload, const char* publisher);

// Checks name and payload and adds the event at the end of the queue,
// leaving EVENT_HOST_RESERVE places free unless reserve is 1. Returns an
// enum EventResult.
int EventQueue_add(EventQueue* queue, const char* name, const char* payload, int reserve);

// Takes the oldest event into out. Returns 1, or 0 when there's none.
int EventQueue_take(EventQueue* queue, Event* out);

void EventSubs_init(EventSubs* subs);

// Subscribes owner to name. Subscribing again to the same name replaces
// the callback and data. Returns an enum EventResult.
int EventSubs_add(EventSubs* subs, const char* name, const char* owner, EventFn fn, void* data);

// Removes owner's subscription to name. Returns 0, or -1 for none.
int EventSubs_remove(EventSubs* subs, const char* name, const char* owner);

// owner's subscription to name, or NULL. Only valid until the next add or
// remove.
const EventSub* EventSubs_find(const EventSubs* subs, const char* name, const char* owner);

// Copies up to max subscriptions to name into out, in the order they
// subscribed. Returns how many it copied.
int EventSubs_match(const EventSubs* subs, const char* name, EventSub* out, int max);

// Removes every subscription owner has, as it's unloaded. Returns how many.
int EventSubs_releaseAll(EventSubs* subs, const char* owner);

#endif
