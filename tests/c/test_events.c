#include "check.h"
#include "events.h"

static int calls = 0;
static void onA(const char* name, const char* payload, void* data) { (void)name, (void)payload, (void)data; calls++; }
static void onB(const char* name, const char* payload, void* data) { (void)name, (void)payload, (void)data; calls += 10; }

static void testNames(void) {
	CHECK(Events_nameValid("music.track"));
	CHECK(Events_nameValid("launcher.set_changed"));
	CHECK(!Events_nameValid(""));
	CHECK(!Events_nameValid(NULL));
	CHECK(!Events_nameValid("music.two words"));
	CHECK(!Events_nameValid("music.a\tb"));
	CHECK(!Events_nameValid("music.a\nb"));
	char name[EVENT_NAME_MAX + 1];
	memset(name, 'a', sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	CHECK(!Events_nameValid(name));
	name[EVENT_NAME_MAX - 1] = '\0';
	CHECK(Events_nameValid(name));

	// a plugin publishes only <its id>.<name>
	CHECK(Events_mayPublish("music.track", "music"));
	CHECK(Events_mayPublish("romm.download_done", "romm"));
	CHECK(Events_mayPublish("music.a.b", "music"));
	CHECK(!Events_mayPublish("music.", "music"));
	CHECK(!Events_mayPublish("music", "music"));
	CHECK(!Events_mayPublish("romm.download_done", "music"));
	CHECK(!Events_mayPublish("musicx.track", "music"));
	CHECK(!Events_mayPublish("mus.track", "music"));
	CHECK(!Events_mayPublish("launcher.launch", "music"));
	CHECK(!Events_mayPublish("music.track", ""));
	// a plugin can't take the launcher's prefix by its id
	CHECK(!Events_mayPublish("launcher.launch", "launcher"));
	// the launcher publishes only launcher.<name>
	CHECK(Events_mayPublish("launcher.launch", NULL));
	CHECK(!Events_mayPublish("launcher.", NULL));
	CHECK(!Events_mayPublish("music.track", NULL));
}

static void testPayloads(void) {
	CHECK(Events_payloadValid(NULL));
	CHECK(Events_payloadValid(""));
	CHECK(Events_payloadValid("title=Song"));
	CHECK(Events_payloadValid("title=Song\nartist=Band"));
	CHECK(Events_payloadValid("title=Song\n"));
	CHECK(Events_payloadValid("a.b-c_d=x=y"));
	CHECK(Events_payloadValid("empty="));
	CHECK(Events_payloadValid("path=/mnt/SDCARD/Roms/GB (GB)/Tetris.gb"));
	CHECK(!Events_payloadValid("title"));
	CHECK(!Events_payloadValid("=Song"));
	CHECK(!Events_payloadValid("ti tle=Song"));
	CHECK(!Events_payloadValid("title=Song\n\nartist=Band"));
	CHECK(!Events_payloadValid("title=Song\n\n"));
	CHECK(!Events_payloadValid("title=a\tb"));
	CHECK(!Events_payloadValid("title=a\rb"));
	char key[EVENT_KEY_MAX + 4];
	memset(key, 'k', EVENT_KEY_MAX);
	strcpy(key + EVENT_KEY_MAX, "=v");
	CHECK(!Events_payloadValid(key));
	strcpy(key + EVENT_KEY_MAX - 1, "=v");
	CHECK(Events_payloadValid(key));
	char big[EVENT_PAYLOAD_MAX + 1];
	memset(big, 'v', sizeof(big) - 1);
	memcpy(big, "k=", 2);
	big[sizeof(big) - 1] = '\0';
	CHECK(!Events_payloadValid(big));
	big[EVENT_PAYLOAD_MAX - 1] = '\0';
	CHECK(Events_payloadValid(big));
}

static void testValues(void) {
	char out[16];
	CHECK(Events_value("title=Song\nartist=Band", "artist", out, sizeof(out)) == 4);
	CHECK_STR(out, "Band");
	CHECK(Events_value("title=Song\nartist=Band", "title", out, sizeof(out)) == 4);
	CHECK_STR(out, "Song");
	// the first line with the key wins, and = in a value is kept
	CHECK(Events_value("k=1\nk=2", "k", out, sizeof(out)) == 1);
	CHECK_STR(out, "1");
	CHECK(Events_value("k=a=b", "k", out, sizeof(out)) == 3);
	CHECK_STR(out, "a=b");
	CHECK(Events_value("k=\n", "k", out, sizeof(out)) == 0);
	CHECK_STR(out, "");
	// keys match exactly, not by prefix
	CHECK(Events_value("title=Song", "tit", out, sizeof(out)) == -1);
	CHECK(Events_value("title=Song", "Title", out, sizeof(out)) == -1);
	CHECK(Events_value("subtitle=x\ntitle=y", "title", out, sizeof(out)) == 1);
	CHECK_STR(out, "y");
	CHECK(Events_value("", "k", out, sizeof(out)) == -1);
	CHECK(Events_value(NULL, "k", out, sizeof(out)) == -1);
	CHECK_STR(out, "");
	// cut to fit, with the whole length returned
	CHECK(Events_value("k=0123456789abcdefghij", "k", out, sizeof(out)) == 20);
	CHECK_STR(out, "0123456789abcde");
	CHECK(Events_value("k=abc", "k", NULL, 0) == 3);
}

static void testQueue(void) {
	static EventQueue q;
	EventQueue_init(&q);
	Event e;
	CHECK(!EventQueue_take(&q, &e));
	CHECK(EventQueue_publish(&q, "music.track", "title=A", "music") == EVENT_OK);
	CHECK(EventQueue_publish(&q, "romm.done", NULL, "music") == EVENT_NOT_YOURS);
	CHECK(EventQueue_publish(&q, "music track", NULL, "music") == EVENT_BAD_NAME);
	CHECK(EventQueue_publish(&q, "music.track", "bad", "music") == EVENT_BAD_PAYLOAD);
	CHECK(EventQueue_publish(&q, "launcher.launch", "path=/a", NULL) == EVENT_OK);
	CHECK(q.count == 2);
	// oldest first, copied
	CHECK(EventQueue_take(&q, &e));
	CHECK_STR(e.name, "music.track");
	CHECK_STR(e.payload, "title=A");
	CHECK(EventQueue_take(&q, &e));
	CHECK_STR(e.name, "launcher.launch");
	CHECK(!EventQueue_take(&q, &e));

	// plugins fill all but the launcher's reserve, which the launcher's own events still get
	for (int i = 0; i < EVENT_QUEUE_MAX - EVENT_HOST_RESERVE; i++) {
		if (EventQueue_publish(&q, "music.track", NULL, "music") != EVENT_OK) {
			CHECK(0);
			break;
		}
	}
	CHECK(EventQueue_publish(&q, "music.track", NULL, "music") == EVENT_FULL);
	CHECK(EventQueue_add(&q, "launcher.set_changed", "name=x", 0) == EVENT_FULL);
	for (int i = 0; i < EVENT_HOST_RESERVE; i++) CHECK(EventQueue_publish(&q, "launcher.return", NULL, NULL) == EVENT_OK);
	CHECK(EventQueue_publish(&q, "launcher.return", NULL, NULL) == EVENT_FULL);
	CHECK(q.count == EVENT_QUEUE_MAX);
	// taking one makes room, and the ring wraps in order
	CHECK(EventQueue_take(&q, &e) && !strcmp(e.name, "music.track"));
	CHECK(EventQueue_publish(&q, "launcher.theme_changed", NULL, NULL) == EVENT_OK);
	int n = 0;
	while (EventQueue_take(&q, &e)) n++;
	CHECK(n == EVENT_QUEUE_MAX);
	CHECK_STR(e.name, "launcher.theme_changed");
}

static void testSubs(void) {
	static EventSubs s;
	EventSubs_init(&s);
	CHECK(EventSubs_add(&s, "music.track", "screensaver", onA, NULL) == EVENT_OK);
	CHECK(EventSubs_add(&s, "music.track", "charging-screen", onA, &calls) == EVENT_OK);
	CHECK(EventSubs_add(&s, "launcher.launch", "screensaver", onA, NULL) == EVENT_OK);
	CHECK(EventSubs_add(&s, "bad name", "screensaver", onA, NULL) == EVENT_BAD_NAME);
	CHECK(EventSubs_add(&s, "music.track", "screensaver", NULL, NULL) == EVENT_BAD_NAME);
	CHECK(EventSubs_add(&s, "music.track", "", onA, NULL) == EVENT_BAD_NAME);
	CHECK(s.count == 3);
	// again replaces the callback, keeping its place
	CHECK(EventSubs_add(&s, "music.track", "screensaver", onB, NULL) == EVENT_OK);
	CHECK(s.count == 3);
	EventSub matched[8];
	CHECK(EventSubs_match(&s, "music.track", matched, 8) == 2);
	CHECK_STR(matched[0].owner, "screensaver");
	CHECK(matched[0].fn == onB);
	CHECK_STR(matched[1].owner, "charging-screen");
	CHECK(matched[1].data == &calls);
	CHECK(EventSubs_match(&s, "music.track", matched, 1) == 1);
	CHECK(EventSubs_match(&s, "music", matched, 8) == 0);
	CHECK(EventSubs_find(&s, "music.track", "charging-screen") != NULL);
	CHECK(EventSubs_find(&s, "music.track", "nobody") == NULL);

	// removing only the owner's
	CHECK(EventSubs_remove(&s, "music.track", "nobody") == -1);
	CHECK(EventSubs_remove(&s, "music.track", "charging-screen") == 0);
	CHECK(EventSubs_remove(&s, "music.track", "charging-screen") == -1);
	CHECK(EventSubs_match(&s, "music.track", matched, 8) == 1);

	// unloading releases all of a plugin's
	CHECK(EventSubs_add(&s, "music.track", "charging-screen", onA, NULL) == EVENT_OK);
	CHECK(EventSubs_releaseAll(&s, "screensaver") == 2);
	CHECK(s.count == 1);
	CHECK_STR(s.items[0].owner, "charging-screen");
	CHECK(EventSubs_releaseAll(&s, "screensaver") == 0);
	CHECK(EventSubs_releaseAll(&s, NULL) == 0);

	// full
	char name[32];
	for (int i = s.count; i < EVENT_SUBS_MAX; i++) {
		snprintf(name, sizeof(name), "p.e%d", i);
		if (EventSubs_add(&s, name, "p", onA, NULL) != EVENT_OK) {
			CHECK(0);
			break;
		}
	}
	CHECK(EventSubs_add(&s, "p.more", "p", onA, NULL) == EVENT_FULL);
	// one already there still replaces its callback
	CHECK(EventSubs_add(&s, "music.track", "charging-screen", onB, NULL) == EVENT_OK);
}

int main(void) {
	testNames();
	testPayloads();
	testValues();
	testQueue();
	testSubs();
	CHECK_DONE();
}
