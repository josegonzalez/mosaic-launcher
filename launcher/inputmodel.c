#include "inputmodel.h"

void InputMask_take(int* pressed, int* repeated, int* released, int taken) {
	*pressed &= ~taken;
	*repeated &= ~taken;
	*released &= ~taken;
}

int PowerHold_step(PowerHold* hold, unsigned long now, int power, int pressed, int released, int held, int* taken) {
	if (pressed & power) {
		if (*taken & power) {
			hold->taken_at = now ? now : 1;
			hold->passed = 0;
		}
		else {
			hold->taken_at = 0;
			hold->passed = 1;
		}
	}
	if (released & power) {
		if (hold->passed) *taken &= ~power;
		hold->taken_at = 0;
		hold->passed = 0;
		return 0;
	}
	// up without a release, such as after the pad was reset for a sleep
	if (!(held & power)) {
		hold->taken_at = 0;
		hold->passed = 0;
		return 0;
	}
	if (hold->taken_at && now >= hold->taken_at && now - hold->taken_at >= POWERHOLD_MS) {
		hold->taken_at = 0; // once a press
		return 1;
	}
	return 0;
}
