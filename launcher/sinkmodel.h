#ifndef SINKMODEL_H
#define SINKMODEL_H

// The SDL-free half of plugins' audio sink (audio_sink in mosaic_plugin.h):
// turning stock's answer, or a desktop test's stand-in, into an enum
// MosaicAudioSink.
//
// On a device, stock's audiomon sets the sink in its shared settings
// (GetAudioSink) when a Bluetooth audio device or a USB DAC connects, and
// writes an .asoundrc that routes ALSA's default device to it.

// Stock's AUDIO_SINK_* (default, Bluetooth, USB DAC) as an enum
// MosaicAudioSink; anything else is MOSAIC_AUDIO_SINK_NONE.
int Sink_fromStock(int stock);

// The sink an .asoundrc in audiomon's form routes to, read as stock's
// Bluetooth code reads it: Bluetooth for a line with "type bluealsa" or
// "defaults.bluealsa.device", else USB for one with "type hw", whichever
// line comes first, else MOSAIC_AUDIO_SINK_NONE, as with NULL or "".
// Desktop tests' MOSAIC_LAUNCHER_AUDIO_SINK_FAKE file is read this way.
int Sink_parse(const char* asoundrc);

// "none", "bluetooth" or "usb", for the log.
const char* Sink_name(int sink);

#endif
