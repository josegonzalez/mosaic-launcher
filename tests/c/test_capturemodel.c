#include "check.h"
#include "capturemodel.h"

// one frame begun and shown, returning what begin asked for
static int frame(CaptureFrames* f, int menu, int hold_ok) {
	int step = CaptureFrames_begin(f, menu);
	if (step == CAPTURE_HOLD) CaptureFrames_held(f, hold_ok);
	CaptureFrames_shown(f);
	return step;
}

static void testNothingShown(void) {
	CaptureFrames f;
	CaptureFrames_clear(&f);
	CHECK(CaptureFrames_source(&f) == CAPTURE_NONE);
	// a first frame that's a menu has nothing before it to hold
	CHECK(frame(&f, 1, 1) == CAPTURE_KEEP);
	CHECK(CaptureFrames_source(&f) == CAPTURE_NONE);
}

static void testLive(void) {
	CaptureFrames f;
	CaptureFrames_clear(&f);
	CHECK(frame(&f, 0, 1) == CAPTURE_KEEP);
	CHECK(CaptureFrames_source(&f) == CAPTURE_LIVE);
	CHECK(frame(&f, 0, 1) == CAPTURE_KEEP);
	CHECK(CaptureFrames_source(&f) == CAPTURE_LIVE);
}

// nothing while a frame is drawn, since its layers are half done
static void testDrawing(void) {
	CaptureFrames f;
	CaptureFrames_clear(&f);
	frame(&f, 0, 1);
	CHECK(CaptureFrames_begin(&f, 0) == CAPTURE_KEEP);
	CHECK(CaptureFrames_source(&f) == CAPTURE_NONE);
	CaptureFrames_shown(&f);
	CHECK(CaptureFrames_source(&f) == CAPTURE_LIVE);
	// shown again without a frame begun changes nothing
	CaptureFrames_shown(&f);
	CHECK(CaptureFrames_source(&f) == CAPTURE_LIVE);
}

// a menu over the list holds the list's frame until the list shows again
static void testMenuHoldsTheFrameBefore(void) {
	CaptureFrames f;
	CaptureFrames_clear(&f);
	frame(&f, 0, 1);
	CHECK(CaptureFrames_begin(&f, 1) == CAPTURE_HOLD);
	CaptureFrames_held(&f, 1);
	// while the menu is drawn, nothing
	CHECK(CaptureFrames_source(&f) == CAPTURE_NONE);
	CaptureFrames_shown(&f);
	CHECK(CaptureFrames_source(&f) == CAPTURE_HELD);
	// the menu redrawn, and a dialog over it, keep it
	CHECK(frame(&f, 1, 1) == CAPTURE_KEEP);
	CHECK(CaptureFrames_source(&f) == CAPTURE_HELD);
	CHECK(frame(&f, 1, 1) == CAPTURE_KEEP);
	CHECK(CaptureFrames_source(&f) == CAPTURE_HELD);
	// the dialog closed, its callback runs before the list is drawn: the
	// menu is still what's shown, so it's still the frame before it
	CHECK(CaptureFrames_source(&f) == CAPTURE_HELD);
	CHECK(CaptureFrames_begin(&f, 0) == CAPTURE_DROP);
	CHECK(CaptureFrames_source(&f) == CAPTURE_NONE);
	CaptureFrames_shown(&f);
	CHECK(CaptureFrames_source(&f) == CAPTURE_LIVE);
	// and the next menu holds again
	CHECK(frame(&f, 1, 1) == CAPTURE_HOLD);
	CHECK(CaptureFrames_source(&f) == CAPTURE_HELD);
}

// a hold that failed gives nothing while the menu shows, never the menu
static void testHoldFailed(void) {
	CaptureFrames f;
	CaptureFrames_clear(&f);
	frame(&f, 0, 1);
	CHECK(frame(&f, 1, 0) == CAPTURE_HOLD);
	CHECK(CaptureFrames_source(&f) == CAPTURE_NONE);
	CHECK(frame(&f, 1, 1) == CAPTURE_KEEP);
	CHECK(CaptureFrames_source(&f) == CAPTURE_NONE);
	CHECK(frame(&f, 0, 1) == CAPTURE_KEEP);
	CHECK(CaptureFrames_source(&f) == CAPTURE_LIVE);
}

// a menu frame begun but never shown, such as before a launch, then the
// list: the held frame is dropped
static void testMenuNotShown(void) {
	CaptureFrames f;
	CaptureFrames_clear(&f);
	frame(&f, 0, 1);
	CHECK(CaptureFrames_begin(&f, 1) == CAPTURE_HOLD);
	CaptureFrames_held(&f, 1);
	CHECK(CaptureFrames_begin(&f, 0) == CAPTURE_DROP);
	CaptureFrames_shown(&f);
	CHECK(CaptureFrames_source(&f) == CAPTURE_LIVE);
}

static void testName(void) {
	struct tm when;
	memset(&when, 0, sizeof(when));
	when.tm_year = 2026 - 1900;
	when.tm_mon = 10;
	when.tm_mday = 2;
	when.tm_hour = 7;
	when.tm_min = 5;
	when.tm_sec = 9;
	char name[64];
	CHECK(Capture_name(&when, name, sizeof(name)) == 0);
	CHECK_STR(name, "Launcher.2026-11-02-07-05-09.png");
	char small[16];
	CHECK(Capture_name(&when, small, sizeof(small)) == -1);
	CHECK(Capture_name(NULL, name, sizeof(name)) == -1);

	char path[256];
	CHECK(Capture_path("/mnt/SDCARD", &when, path, sizeof(path)) == 0);
	CHECK_STR(path, "/mnt/SDCARD/Screenshots/Launcher.2026-11-02-07-05-09.png");
	CHECK(Capture_path("/mnt/SDCARD", &when, path, 20) == -1);
	CHECK(Capture_dir("/mnt/SDCARD", path, sizeof(path)) == 0);
	CHECK_STR(path, "/mnt/SDCARD/Screenshots");
	CHECK(Capture_dir(NULL, path, sizeof(path)) == -1);
}

int main(void) {
	testNothingShown();
	testLive();
	testDrawing();
	testMenuHoldsTheFrameBefore();
	testHoldFailed();
	testMenuNotShown();
	testName();
	CHECK_DONE();
}
