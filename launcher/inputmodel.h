#ifndef INPUTMODEL_H
#define INPUTMODEL_H

// Plugins' input filters, without SDL: taking the buttons filters consumed
// off the pad, and letting a long power press through whatever they take.
// Masks are the pad's own bits, as PAD_Context has them.

// Takes the bits in taken off a frame's presses, repeats and releases.
// What's held stays held, so the next frame's presses and releases are as
// the pad has them.
void InputMask_take(int* pressed, int* repeated, int* released, int taken);

#define POWERHOLD_MS 1000 // stock powers off when power is held this long

// Follows the power button across frames while filters take some of it.
typedef struct PowerHold {
	unsigned long taken_at; // when a press the filters took went down, while it's held, else 0
	int passed;             // a press the filters left alone is held, so stock is timing it
} PowerHold;

// Each frame after the filters. power is the power button's pad bit;
// pressed and released are the frame's presses and releases before the
// filters took anything, held what's down now, and *taken what they took.
// A release whose press got through isn't taken (power is dropped from
// *taken), since stock is timing that press and, without the release,
// would power off once it had been a second. Returns
// 1 on the frame a press the filters took has been held POWERHOLD_MS, when
// stock would have powered off, so the caller can power off in its place.
int PowerHold_step(PowerHold* hold, unsigned long now, int power, int pressed, int released, int held, int* taken);

#endif
