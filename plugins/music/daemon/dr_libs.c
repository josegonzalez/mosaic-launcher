// The vendored decoders' code, compiled on its own with its warnings off
// and the options in plugin.mk (no Ogg FLAC, no wide-character paths).

#define DR_MP3_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION
#define DR_WAV_IMPLEMENTATION
#include "dr_mp3.h"
#include "dr_flac.h"
#include "dr_wav.h"
