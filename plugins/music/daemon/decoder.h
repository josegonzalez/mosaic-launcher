// The daemon's decoders: MP3, FLAC and WAV through the vendored dr_libs,
// each read as signed 16-bit frames in the machine's byte order.

#ifndef MUSIC_DECODER_H
#define MUSIC_DECODER_H

typedef struct MuDecoder {
	int kind; // an enum MuKind, MU_KIND_NONE while closed
	void* handle;
	unsigned int rate, channels;
	unsigned long long frames; // the track's length in frames, or 0 when not known
} MuDecoder;

// Opens path by its extension. Returns 0, or -1 for a file it can't read.
int MuDecoder_open(MuDecoder* decoder, const char* path);

// Reads up to count frames into out. Returns how many, 0 at the end.
unsigned long long MuDecoder_read(MuDecoder* decoder, short* out, unsigned long long count);

// Moves to a frame. Returns 0, or -1.
int MuDecoder_seek(MuDecoder* decoder, unsigned long long frame);

void MuDecoder_close(MuDecoder* decoder);

#endif
