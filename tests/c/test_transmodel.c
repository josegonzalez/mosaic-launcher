// transmodel.c: who draws each transition, checking a plugin's choice, the
// timing caps, easing and stepping through frames by the clock.

#include <math.h>
#include "check.h"
#include "transmodel.h"

#define NEAR(a, b) ((a) - (b) < 0.0001f && (b) - (a) < 0.0001f)

static void testSlots(void) {
	TransitionSlots s;
	TransitionSlots_clear(&s);
	for (int kind = MOSAIC_TRANSITION_PUSH; kind <= MOSAIC_TRANSITION_LAUNCH; kind++) {
		CHECK(TransitionSlots_owner(&s, kind) == -1);
	}

	// the transition slot has pushes, pops and screen changes; launch has launches
	s.transition = 2;
	CHECK(TransitionSlots_owner(&s, MOSAIC_TRANSITION_PUSH) == 2);
	CHECK(TransitionSlots_owner(&s, MOSAIC_TRANSITION_POP) == 2);
	CHECK(TransitionSlots_owner(&s, MOSAIC_TRANSITION_SCREEN) == 2);
	CHECK(TransitionSlots_owner(&s, MOSAIC_TRANSITION_LAUNCH) == -1);
	s.launch = 4;
	CHECK(TransitionSlots_owner(&s, MOSAIC_TRANSITION_LAUNCH) == 4);
	CHECK(TransitionSlots_owner(&s, -1) == -1);
	CHECK(TransitionSlots_owner(&s, MOSAIC_TRANSITION_LAUNCH + 1) == -1);

	// a failing owner is off for its own kinds only
	TransitionSlots_fail(&s, 4);
	CHECK(TransitionSlots_owner(&s, MOSAIC_TRANSITION_LAUNCH) == -1);
	CHECK(TransitionSlots_owner(&s, MOSAIC_TRANSITION_PUSH) == 2);
	TransitionSlots_fail(&s, -1);
	TransitionSlots_fail(&s, 9);
	CHECK(TransitionSlots_owner(&s, MOSAIC_TRANSITION_POP) == 2);

	// one plugin owning both is off for both
	TransitionSlots_clear(&s);
	s.transition = 1;
	s.launch = 1;
	TransitionSlots_fail(&s, 1);
	CHECK(TransitionSlots_owner(&s, MOSAIC_TRANSITION_PUSH) == -1);
	CHECK(TransitionSlots_owner(&s, MOSAIC_TRANSITION_LAUNCH) == -1);
	TransitionSlots_clear(&s);
	CHECK(s.transition == -1 && s.launch == -1 && !s.transition_off && !s.launch_off);
}

static void testStyle(void) {
	CHECK(Transition_checkStyle(MOSAIC_STYLE_STOCK, 0) == MOSAIC_STYLE_STOCK);
	CHECK(Transition_checkStyle(MOSAIC_STYLE_OFF, 0) == MOSAIC_STYLE_OFF);
	CHECK(Transition_checkStyle(MOSAIC_STYLE_CUSTOM, 1) == MOSAIC_STYLE_CUSTOM);
	// custom needs a transition_frame to draw it
	CHECK(Transition_checkStyle(MOSAIC_STYLE_CUSTOM, 0) == -1);
	CHECK(Transition_checkStyle(-1, 1) == -1);
	CHECK(Transition_checkStyle(3, 1) == -1);
	CHECK(Transition_checkStyle(7, 1) == -1);
}

static void testCap(void) {
	CHECK(Transition_capDuration(250, TRANSITION_MAX_MS) == 250);
	CHECK(Transition_capDuration(0, TRANSITION_MAX_MS) == 0);
	CHECK(Transition_capDuration(-5, TRANSITION_MAX_MS) == 0);
	CHECK(Transition_capDuration(TRANSITION_MAX_MS, TRANSITION_MAX_MS) == TRANSITION_MAX_MS);
	CHECK(Transition_capDuration(5000, TRANSITION_MAX_MS) == TRANSITION_MAX_MS);
	CHECK(Transition_capDuration(5000, -1) == 0);
}

static void testEase(void) {
	int eases[] = { MOSAIC_EASE_LINEAR, MOSAIC_EASE_IN, MOSAIC_EASE_OUT, MOSAIC_EASE_IN_OUT, 99 };
	for (int i = 0; i < 5; i++) {
		// every curve runs from 0 to 1, and never backwards
		CHECK(NEAR(Transition_ease(eases[i], 0.0f), 0.0f));
		CHECK(NEAR(Transition_ease(eases[i], 1.0f), 1.0f));
		CHECK(NEAR(Transition_ease(eases[i], -1.0f), 0.0f));
		CHECK(NEAR(Transition_ease(eases[i], 2.0f), 1.0f));
		CHECK(NEAR(Transition_ease(eases[i], NAN), 0.0f));
		float last = 0.0f;
		for (int step = 1; step <= 100; step++) {
			float v = Transition_ease(eases[i], step / 100.0f);
			CHECK(v >= last && v <= 1.0f);
			last = v;
		}
	}
	CHECK(NEAR(Transition_ease(MOSAIC_EASE_LINEAR, 0.25f), 0.25f));
	CHECK(NEAR(Transition_ease(MOSAIC_EASE_IN, 0.5f), 0.125f));
	CHECK(NEAR(Transition_ease(MOSAIC_EASE_OUT, 0.5f), 0.875f));
	CHECK(NEAR(Transition_ease(MOSAIC_EASE_IN_OUT, 0.5f), 0.5f));
	CHECK(NEAR(Transition_ease(MOSAIC_EASE_IN_OUT, 0.25f), 0.0625f));
	CHECK(NEAR(Transition_ease(MOSAIC_EASE_IN_OUT, 0.75f), 0.9375f));
	// an unknown curve is linear
	CHECK(NEAR(Transition_ease(99, 0.3f), 0.3f));
}

static void testProgress(void) {
	CHECK(NEAR(Transition_progress(0, 200), 0.0f));
	CHECK(NEAR(Transition_progress(50, 200), 0.25f));
	CHECK(NEAR(Transition_progress(200, 200), 1.0f));
	CHECK(NEAR(Transition_progress(900, 200), 1.0f));
	CHECK(NEAR(Transition_progress(0, 0), 1.0f));
	CHECK(NEAR(Transition_progress(10, -3), 1.0f));
}

static void testRun(void) {
	TransitionRun run;
	float p = -1.0f;

	// frames follow the clock, and the last is exactly 1
	TransitionRun_start(&run, 1000, 100);
	CHECK(TransitionRun_next(&run, 1000, &p) == 1 && NEAR(p, 0.0f));
	CHECK(TransitionRun_next(&run, 1016, &p) == 1 && NEAR(p, 0.16f));
	CHECK(TransitionRun_next(&run, 1050, &p) == 1 && NEAR(p, 0.5f));
	CHECK(!run.done);
	// a slow frame skips ahead rather than stretching the transition
	CHECK(TransitionRun_next(&run, 1400, &p) == 1 && p == 1.0f);
	CHECK(run.done && run.frames == 4);
	CHECK(TransitionRun_next(&run, 1416, &p) == 0);

	// no time: one frame, at 1
	TransitionRun_start(&run, 500, 0);
	CHECK(TransitionRun_next(&run, 500, &p) == 1 && p == 1.0f);
	CHECK(TransitionRun_next(&run, 500, &p) == 0);
	TransitionRun_start(&run, 500, -20);
	CHECK(run.duration == 0);
	CHECK(TransitionRun_next(&run, 500, &p) == 1 && p == 1.0f);

	// a clock that stalls or goes backwards still ends
	TransitionRun_start(&run, 2000, TRANSITION_MAX_MS);
	int frames = 0;
	while (TransitionRun_next(&run, 1500, &p)) {
		frames++;
		if (frames > TRANSITION_MAX_FRAMES + 5) break;
	}
	CHECK(frames == TRANSITION_MAX_FRAMES + 1);
	CHECK(p == 1.0f);

	// at 60 frames a second a capped transition takes about max / 16 frames
	TransitionRun_start(&run, 0, Transition_capDuration(5000, TRANSITION_MAX_MS));
	frames = 0;
	unsigned long now = 0;
	while (TransitionRun_next(&run, now, &p)) {
		frames++;
		now += TRANSITION_FRAME_MS;
	}
	CHECK(frames == TRANSITION_MAX_MS / TRANSITION_FRAME_MS + 2);
	CHECK(now <= TRANSITION_MAX_MS + 2 * TRANSITION_FRAME_MS);
}

int main(void) {
	testSlots();
	testStyle();
	testCap();
	testEase();
	testProgress();
	testRun();
	CHECK_DONE();
}
