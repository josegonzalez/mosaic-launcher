// Plugins' audio sink: stock's answer, and an .asoundrc read as stock's
// Bluetooth code reads it.

#include "check.h"
#include "sinkmodel.h"
#include "mosaic_plugin.h"

// what stock's audiomon writes for a Bluetooth device and a USB DAC
static const char* bluetooth =
	"defaults.bluealsa.device \"AA:BB:CC:DD:EE:FF\"\n\n"
	"pcm.!default {\n"
	"    type plug\n"
	"    slave.pcm {\n"
	"        type bluealsa\n"
	"        device \"AA:BB:CC:DD:EE:FF\"\n"
	"        profile \"a2dp\"\n"
	"        delay 0\n"
	"    }\n"
	"}\n"
	"ctl.!default {\n"
	"    type bluealsa\n"
	"}\n";
static const char* usb =
	"pcm.!default {\n"
	"    type hw\n"
	"    card 1\n"
	"}\n"
	"ctl.!default {\n"
	"    type hw\n"
	"    card 1\n"
	"}\n";

int main(void) {
	CHECK(Sink_fromStock(0) == MOSAIC_AUDIO_SINK_NONE);
	CHECK(Sink_fromStock(1) == MOSAIC_AUDIO_SINK_BLUETOOTH);
	CHECK(Sink_fromStock(2) == MOSAIC_AUDIO_SINK_USB);
	CHECK(Sink_fromStock(-1) == MOSAIC_AUDIO_SINK_NONE);
	CHECK(Sink_fromStock(3) == MOSAIC_AUDIO_SINK_NONE);

	CHECK(Sink_parse(bluetooth) == MOSAIC_AUDIO_SINK_BLUETOOTH);
	CHECK(Sink_parse(usb) == MOSAIC_AUDIO_SINK_USB);
	CHECK(Sink_parse(NULL) == MOSAIC_AUDIO_SINK_NONE);
	CHECK(Sink_parse("") == MOSAIC_AUDIO_SINK_NONE);
	CHECK(Sink_parse("pcm.!default {\n    type plug\n}\n") == MOSAIC_AUDIO_SINK_NONE);
	// either Bluetooth line alone, with no newline at the end
	CHECK(Sink_parse("defaults.bluealsa.device \"AA:BB:CC:DD:EE:FF\"") == MOSAIC_AUDIO_SINK_BLUETOOTH);
	CHECK(Sink_parse("pcm.bt {\n type bluealsa\n}") == MOSAIC_AUDIO_SINK_BLUETOOTH);
	// the first line that says wins
	CHECK(Sink_parse("pcm.a {\n type hw\n}\npcm.b {\n type bluealsa\n}\n") == MOSAIC_AUDIO_SINK_USB);
	CHECK(Sink_parse("pcm.a {\n type bluealsa\n}\npcm.b {\n type hw\n}\n") == MOSAIC_AUDIO_SINK_BLUETOOTH);
	// a word split across lines isn't one
	CHECK(Sink_parse("type\nbluealsa\ntype\nhw\n") == MOSAIC_AUDIO_SINK_NONE);
	// CRLF lines
	CHECK(Sink_parse("pcm.!default {\r\n    type bluealsa\r\n}\r\n") == MOSAIC_AUDIO_SINK_BLUETOOTH);

	CHECK_STR(Sink_name(MOSAIC_AUDIO_SINK_NONE), "none");
	CHECK_STR(Sink_name(MOSAIC_AUDIO_SINK_BLUETOOTH), "bluetooth");
	CHECK_STR(Sink_name(MOSAIC_AUDIO_SINK_USB), "usb");
	CHECK_STR(Sink_name(7), "none");
	CHECK_DONE();
}
