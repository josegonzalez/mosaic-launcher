// The desktop platform, with the SD card moved to a test fixture.
//
// Every card path in defines.h (ROMS_PATH, COLLECTIONS_PATH, ...) is built
// from SDCARD_PATH when it's used, so repointing it here moves them all.

#ifndef TEST_PLATFORM_H
#define TEST_PLATFORM_H

#include_next "platform.h"

#ifndef TEST_SDCARD_PATH
#error "build with -DTEST_SDCARD_PATH=\"...\""
#endif

#undef SDCARD_PATH
#define SDCARD_PATH TEST_SDCARD_PATH

#endif
