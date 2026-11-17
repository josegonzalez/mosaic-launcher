#include <stdio.h>
#include <string.h>
#include "jump_model.h"

// The base letters of U+00C0 to U+00FF and U+0100 to U+017F, # for the
// few that aren't Latin letters (× ÷ Þ þ).
static const char latin1[] =
	"AAAAAAACEEEEIIII" "DNOOOOO#OUUUUY#S"
	"AAAAAAACEEEEIIII" "DNOOOOO#OUUUUY#Y";
static const char extended_a[] =
	"AAAAAACCCCCCCCDD" "DDEEEEEEEEEEGGGG" "GGGGHHHHIIIIIIII" "IIIIJJKKKLLLLLLL"
	"LLLNNNNNNNNNOOOO" "OOOORRRRRRSSSSSS" "SSTTTTTTUUUUUUUU" "UUUUWWYYYZZZZZZS";

void JL_letter(const char* name, char* out, size_t size) {
	if (!out || size == 0) return;
	char letter = '#';
	const unsigned char* c = (const unsigned char*)(name ? name : "");
	if (c[0] >= 'a' && c[0] <= 'z') letter = (char)(c[0] - 'a' + 'A');
	else if (c[0] >= 'A' && c[0] <= 'Z') letter = (char)c[0];
	else if (c[0] >= 0xC2 && c[0] <= 0xC5 && (c[1] & 0xC0) == 0x80) {
		// two bytes: U+0080 to U+017F
		unsigned int code = ((unsigned int)(c[0] & 0x1F) << 6) | (c[1] & 0x3F);
		if (code >= 0xC0 && code <= 0xFF) letter = latin1[code - 0xC0];
		else if (code >= 0x100 && code <= 0x17F) letter = extended_a[code - 0x100];
	}
	if (size >= 2) {
		out[0] = letter;
		out[1] = '\0';
	}
	else out[0] = '\0';
}

void JL_sections(JLSections* s, int count, JLKeyOf key_of, void* data, int at_entry, int* at_section) {
	s->count = 0;
	if (at_section) *at_section = -1;
	for (int i = 0; i < count; i++) {
		char key[JL_KEY_LEN];
		key[0] = '\0';
		key_of(i, key, sizeof(key), data);
		key[sizeof(key) - 1] = '\0';
		if (!key[0]) snprintf(key, sizeof(key), "#");
		int at = -1;
		for (int k = 0; k < s->count && at < 0; k++) {
			if (!strcmp(s->keys[k], key)) at = k;
		}
		if (at < 0 && s->count < JL_SECTIONS_MAX) {
			at = s->count++;
			snprintf(s->keys[at], sizeof(s->keys[at]), "%s", key);
			s->first[at] = i;
		}
		if (at < 0) at = s->count - 1;
		if (i == at_entry && at_section) *at_section = at;
	}
}

int JL_step(int at, int delta, int count) {
	if (count <= 0) return 0;
	at += delta;
	if (at < 0) at = 0;
	if (at >= count) at = count - 1;
	return at;
}

void JL_window(const int* widths, int count, int gap, int avail, int at, int* first, int* last) {
	*first = 0;
	*last = -1;
	if (count <= 0 || !widths) return;
	at = JL_step(at, 0, count);
	int a = at, b = at, used = widths[at];
	for (int grew = 1; grew;) {
		grew = 0;
		if (b + 1 < count && used + gap + widths[b + 1] <= avail) {
			used += gap + widths[++b];
			grew = 1;
		}
		if (a > 0 && used + gap + widths[a - 1] <= avail) {
			used += gap + widths[--a];
			grew = 1;
		}
	}
	*first = a;
	*last = b;
}

void JL_holdArm(JLHold* h, unsigned long now) {
	h->state = JL_ARMED;
	h->armed_at = now;
	h->sticky = 0;
}

void JL_holdOpenSticky(JLHold* h) {
	h->state = JL_OPEN;
	h->sticky = 1;
}

int JL_holdFrame(JLHold* h, int held, int chord, unsigned long now) {
	if (h->state == JL_ARMED) {
		// a tap, or a chord, is left to what else has the button
		if (!held || chord) h->state = JL_IDLE;
		else if (now - h->armed_at >= JL_HOLD_MS) {
			h->state = JL_OPEN;
			return 1;
		}
	}
	else if (h->state == JL_CANCELLED && !held) h->state = JL_IDLE;
	return 0;
}

int JL_holdRelease(JLHold* h) {
	int open = h->state == JL_OPEN && !h->sticky;
	if (h->state != JL_OPEN || !h->sticky) h->state = JL_IDLE;
	return open;
}

void JL_holdCancel(JLHold* h) {
	if (h->state != JL_OPEN) return;
	h->state = h->sticky ? JL_IDLE : JL_CANCELLED;
}
