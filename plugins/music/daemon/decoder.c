#include "decoder.h"

#include <stdlib.h>
#include <string.h>
#include "dr_flac.h"
#include "dr_mp3.h"
#include "dr_wav.h"
#include "music_model.h"

int MuDecoder_open(MuDecoder* d, const char* path) {
	memset(d, 0, sizeof(*d));
	const char* slash = strrchr(path, '/');
	int kind = MU_trackKind(slash ? slash + 1 : path);
	if (kind == MU_KIND_MP3) {
		drmp3* mp3 = malloc(sizeof(drmp3));
		if (!mp3 || !drmp3_init_file(mp3, path, NULL)) {
			free(mp3);
			return -1;
		}
		d->handle = mp3;
		d->rate = mp3->sampleRate;
		d->channels = mp3->channels;
		d->frames = drmp3_get_pcm_frame_count(mp3);
	} else if (kind == MU_KIND_FLAC) {
		drflac* flac = drflac_open_file(path, NULL);
		if (!flac) return -1;
		d->handle = flac;
		d->rate = flac->sampleRate;
		d->channels = flac->channels;
		d->frames = flac->totalPCMFrameCount;
	} else if (kind == MU_KIND_WAV) {
		drwav* wav = malloc(sizeof(drwav));
		if (!wav || !drwav_init_file(wav, path, NULL)) {
			free(wav);
			return -1;
		}
		d->handle = wav;
		d->rate = wav->sampleRate;
		d->channels = wav->channels;
		d->frames = wav->totalPCMFrameCount;
	} else return -1;
	d->kind = kind;
	if (!d->rate || !d->channels) {
		MuDecoder_close(d);
		return -1;
	}
	return 0;
}

unsigned long long MuDecoder_read(MuDecoder* d, short* out, unsigned long long count) {
	if (d->kind == MU_KIND_MP3) return drmp3_read_pcm_frames_s16(d->handle, count, out);
	if (d->kind == MU_KIND_FLAC) return drflac_read_pcm_frames_s16(d->handle, count, out);
	if (d->kind == MU_KIND_WAV) return drwav_read_pcm_frames_s16(d->handle, count, out);
	return 0;
}

int MuDecoder_seek(MuDecoder* d, unsigned long long frame) {
	int ok = 0;
	if (d->kind == MU_KIND_MP3) ok = drmp3_seek_to_pcm_frame(d->handle, frame);
	else if (d->kind == MU_KIND_FLAC) ok = drflac_seek_to_pcm_frame(d->handle, frame);
	else if (d->kind == MU_KIND_WAV) ok = drwav_seek_to_pcm_frame(d->handle, frame);
	return ok ? 0 : -1;
}

void MuDecoder_close(MuDecoder* d) {
	if (d->kind == MU_KIND_MP3) {
		drmp3_uninit(d->handle);
		free(d->handle);
	} else if (d->kind == MU_KIND_FLAC) {
		drflac_close(d->handle);
	} else if (d->kind == MU_KIND_WAV) {
		drwav_uninit(d->handle);
		free(d->handle);
	}
	memset(d, 0, sizeof(*d));
}
