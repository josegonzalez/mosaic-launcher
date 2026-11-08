#include "transmodel.h"

void TransitionSlots_clear(TransitionSlots* slots) {
	slots->transition = -1;
	slots->launch = -1;
	slots->transition_off = 0;
	slots->launch_off = 0;
}

int TransitionSlots_owner(const TransitionSlots* slots, int kind) {
	switch (kind) {
		case MOSAIC_TRANSITION_PUSH:
		case MOSAIC_TRANSITION_POP:
		case MOSAIC_TRANSITION_SCREEN:
			return slots->transition_off ? -1 : slots->transition;
		case MOSAIC_TRANSITION_LAUNCH:
			return slots->launch_off ? -1 : slots->launch;
		default:
			return -1;
	}
}

int TransitionSlots_asked(const TransitionSlots* slots, int kind, int menu_transitions) {
	if (!menu_transitions && kind != MOSAIC_TRANSITION_LAUNCH) return -1;
	return TransitionSlots_owner(slots, kind);
}

void TransitionSlots_fail(TransitionSlots* slots, int owner) {
	if (owner < 0) return;
	if (slots->transition == owner) slots->transition_off = 1;
	if (slots->launch == owner) slots->launch_off = 1;
}

int Transition_checkStyle(int rc, int has_frame) {
	switch (rc) {
		case MOSAIC_STYLE_STOCK:
		case MOSAIC_STYLE_OFF:
			return rc;
		case MOSAIC_STYLE_CUSTOM:
			return has_frame ? rc : -1;
		default:
			return -1;
	}
}

int Transition_capDuration(int ms, int max) {
	if (max < 0) max = 0;
	if (ms < 0) return 0;
	return ms > max ? max : ms;
}

float Transition_ease(int ease, float t) {
	if (!(t > 0.0f)) t = 0.0f; // NaN too
	if (t > 1.0f) t = 1.0f;
	float u = 1.0f - t;
	switch (ease) {
		case MOSAIC_EASE_IN:
			return t * t * t;
		case MOSAIC_EASE_OUT:
			return 1.0f - u * u * u;
		case MOSAIC_EASE_IN_OUT:
			return t < 0.5f ? 4.0f * t * t * t : 1.0f - 4.0f * u * u * u;
		default:
			return t;
	}
}

float Transition_progress(unsigned long elapsed, int duration) {
	if (duration <= 0 || elapsed >= (unsigned long)duration) return 1.0f;
	return (float)elapsed / (float)duration;
}

void TransitionRun_start(TransitionRun* run, unsigned long now, int duration) {
	run->start = now;
	run->duration = duration < 0 ? 0 : duration;
	run->frames = 0;
	run->done = 0;
}

int TransitionRun_next(TransitionRun* run, unsigned long now, float* progress) {
	if (run->done) return 0;
	// a clock that went backwards counts as no time passing
	unsigned long elapsed = now >= run->start ? now - run->start : 0;
	float p = Transition_progress(elapsed, run->duration);
	if (run->frames >= TRANSITION_MAX_FRAMES) p = 1.0f;
	if (p >= 1.0f) run->done = 1;
	run->frames++;
	*progress = p;
	return 1;
}
