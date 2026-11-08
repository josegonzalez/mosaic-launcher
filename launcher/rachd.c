#include "rachd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <libchdr/cdrom.h>
#include <libchdr/chd.h>

typedef struct ChdTrack {
	int type;
	int frames;
	int pregap;
	int start; // its first frame in the image, after the tracks before it and their padding
} ChdTrack;

typedef struct ChdHandle {
	chd_file* chd;
	ChdTrack tracks[CD_MAX_TRACKS];
	int count;
	const ChdTrack* track;
	int header; // bytes before a sector's data
	int data;   // and the data's size
	uint32_t frame_size;
	uint32_t frames_per_hunk;
	uint32_t hunk_bytes;
	uint8_t* hunk;
	uint32_t hunk_index; // which hunk is in hunk, or UINT32_MAX for none
} ChdHandle;

// One of these is what rcheevos holds: a CHD track, or one from its own
// reader.
typedef struct Handle {
	int is_chd;
	void* inner;
} Handle;

static rc_hash_cdreader_t fallback;

static int typeOf(const char* name) {
	static const struct {
		const char* name;
		int type;
	} types[] = {
		{ "MODE1", CD_TRACK_MODE1 },
		{ "MODE1_RAW", CD_TRACK_MODE1_RAW },
		{ "MODE1/2352", CD_TRACK_MODE1_RAW },
		{ "MODE2", CD_TRACK_MODE2 },
		{ "MODE2_FORM1", CD_TRACK_MODE2_FORM1 },
		{ "MODE2_FORM2", CD_TRACK_MODE2_FORM2 },
		{ "MODE2_FORM_MIX", CD_TRACK_MODE2_FORM_MIX },
		{ "MODE2_RAW", CD_TRACK_MODE2_RAW },
		{ "MODE2/2352", CD_TRACK_MODE2_RAW },
		{ "AUDIO", CD_TRACK_AUDIO },
	};
	for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++)
		if (!strcmp(name, types[i].name)) return types[i].type;
	return CD_TRACK_MODE1;
}

// A track's metadata, from the newest form there is for index.
static int metadata(chd_file* chd, int index, char* out, size_t size) {
	static const uint32_t tags[] = { CDROM_TRACK_METADATA2_TAG, CDROM_TRACK_METADATA_TAG, GDROM_TRACK_METADATA_TAG };
	for (size_t t = 0; t < sizeof(tags) / sizeof(tags[0]); t++) {
		uint32_t length = 0;
		if (chd_get_metadata(chd, tags[t], (uint32_t)index, out, (uint32_t)size - 1, &length, NULL, NULL) != CHDERR_NONE)
			continue;
		if (length > size - 1) length = (uint32_t)size - 1;
		out[length] = '\0';
		return 1;
	}
	return 0;
}

static int readTracks(ChdHandle* h) {
	int frame = 0;
	for (int i = 0; i < CD_MAX_TRACKS; i++) {
		char text[256];
		if (!metadata(h->chd, i, text, sizeof(text))) break;
		int number, frames, pregap = 0, postgap = 0;
		char type[32] = "", subtype[32] = "", pgtype[32] = "", pgsub[32] = "";
		int parsed = sscanf(text, "TRACK:%d TYPE:%31s SUBTYPE:%31s FRAMES:%d PREGAP:%d PGTYPE:%31s PGSUB:%31s POSTGAP:%d",
			&number, type, subtype, &frames, &pregap, pgtype, pgsub, &postgap);
		if (parsed < 4) {
			pregap = 0;
			parsed = sscanf(text, "TRACK:%d TYPE:%31s SUBTYPE:%31s FRAMES:%d", &number, type, subtype, &frames);
		}
		if (parsed < 4) continue;
		if (frames < 0) frames = 0;
		ChdTrack* track = &h->tracks[h->count++];
		track->type = typeOf(type);
		track->frames = frames;
		track->pregap = pregap;
		track->start = frame;
		// each track is padded out to a multiple of 4 frames
		frame += (frames + CD_TRACK_PADDING - 1) / CD_TRACK_PADDING * CD_TRACK_PADDING;
	}
	return h->count > 0;
}

static const ChdTrack* pick(const ChdHandle* h, uint32_t want) {
	if (want == RC_HASH_CDTRACK_FIRST_DATA) {
		for (int i = 0; i < h->count; i++)
			if (h->tracks[i].type != CD_TRACK_AUDIO) return &h->tracks[i];
		return NULL;
	}
	if (want == RC_HASH_CDTRACK_LAST) return &h->tracks[h->count - 1];
	if (want == RC_HASH_CDTRACK_LARGEST) {
		int best = 0;
		for (int i = 1; i < h->count; i++)
			if (h->tracks[i].frames > h->tracks[best].frames) best = i;
		return &h->tracks[best];
	}
	if (want >= 1 && want <= (uint32_t)h->count) return &h->tracks[want - 1];
	return NULL;
}

// Where a track's sectors keep their 2048 data bytes, going by its type.
static void layout(int type, uint32_t frame_size, int* header, int* data) {
	*header = 0;
	*data = 2048;
	switch (type) {
		case CD_TRACK_MODE1_RAW:
			if (frame_size >= 2352) *header = 16;
			break;
		case CD_TRACK_MODE2_RAW:
		case CD_TRACK_MODE2_FORM_MIX:
			if (frame_size >= 2352) *header = 24;
			break;
		case CD_TRACK_MODE2_FORM1:
		case CD_TRACK_MODE2_FORM2:
			if (frame_size == 2336) *header = 8;
			break;
		case CD_TRACK_AUDIO:
			*data = 2352;
			break;
		case CD_TRACK_MODE1:
		case CD_TRACK_MODE2:
			break;
		default:
			if (frame_size >= 2352) *header = 16;
			break;
	}
}

static void closeChd(ChdHandle* h) {
	if (!h) return;
	free(h->hunk);
	if (h->chd) chd_close(h->chd);
	free(h);
}

static int isChd(const char* path) {
	const char* ext = path ? strrchr(path, '.') : NULL;
	return ext && !strcasecmp(ext, ".chd");
}

static ChdHandle* openChd(const char* path, uint32_t want) {
	if (!isChd(path)) return NULL;
	ChdHandle* h = calloc(1, sizeof(ChdHandle));
	if (!h) return NULL;
	if (chd_open(path, CHD_OPEN_READ, NULL, &h->chd) != CHDERR_NONE) {
		h->chd = NULL;
		closeChd(h);
		return NULL;
	}
	const chd_header* header = chd_get_header(h->chd);
	h->hunk_bytes = header->hunkbytes;
	h->frame_size = header->unitbytes ? header->unitbytes : CD_FRAME_SIZE;
	h->frames_per_hunk = h->frame_size ? h->hunk_bytes / h->frame_size : 0;
	h->hunk_index = UINT32_MAX;
	if (!h->frames_per_hunk || h->frame_size < 2048 || !(h->hunk = malloc(h->hunk_bytes)) || !readTracks(h) ||
		!(h->track = pick(h, want))) {
		closeChd(h);
		return NULL;
	}
	layout(h->track->type, h->frame_size, &h->header, &h->data);
	return h;
}

// sector counts from the start of the track's data, past its pregap.
static size_t readChd(ChdHandle* h, uint32_t sector, void* buffer, size_t requested) {
	uint64_t frame = (uint64_t)h->track->start + sector;
	if (h->track->type != CD_TRACK_AUDIO && h->track->pregap > 0) frame += (uint64_t)h->track->pregap;
	uint64_t hunk = frame / h->frames_per_hunk;
	if (hunk >= UINT32_MAX) return 0;
	if ((uint32_t)hunk != h->hunk_index) {
		if (chd_read(h->chd, (uint32_t)hunk, h->hunk) != CHDERR_NONE) {
			h->hunk_index = UINT32_MAX;
			return 0;
		}
		h->hunk_index = (uint32_t)hunk;
	}
	const uint8_t* src = h->hunk + (frame % h->frames_per_hunk) * h->frame_size;
	size_t header = (size_t)h->header, data = (size_t)h->data;
	// a raw sector says its own mode, sometimes after 96 bytes of subcode
	if (h->frame_size >= 2352 && header > 0) {
		const uint8_t* sync = NULL;
		if (src[0] == 0x00 && src[1] == 0xFF && src[11] == 0x00) sync = src;
		else if (h->frame_size == 2448 && src[96] == 0x00 && src[97] == 0xFF && src[107] == 0x00) sync = src + 96;
		if (sync) {
			src = sync;
			if (sync[15] == 1) header = 16, data = 2048;
			else if (sync[15] == 2) header = 24, data = 2048;
		}
	}
	size_t n = requested < data ? requested : data;
	if (src + header + n > h->hunk + h->hunk_bytes) return 0;
	memcpy(buffer, src + header, n);
	return n;
}

static void* wrap(int is_chd, void* inner) {
	if (!inner) return NULL;
	Handle* handle = malloc(sizeof(Handle));
	if (!handle) {
		if (is_chd) closeChd(inner);
		else if (fallback.close_track) fallback.close_track(inner);
		return NULL;
	}
	handle->is_chd = is_chd;
	handle->inner = inner;
	return handle;
}

static void* openTrackIterator(const char* path, uint32_t track, const rc_hash_iterator_t* iterator) {
	ChdHandle* chd = openChd(path, track);
	if (chd) return wrap(1, chd);
	void* inner = NULL;
	if (fallback.open_track_iterator) inner = fallback.open_track_iterator(path, track, iterator);
	if (!inner && fallback.open_track) inner = fallback.open_track(path, track);
	return wrap(0, inner);
}

static void* openTrack(const char* path, uint32_t track) {
	return openTrackIterator(path, track, NULL);
}

static size_t readSector(void* track_handle, uint32_t sector, void* buffer, size_t requested) {
	Handle* handle = track_handle;
	if (!handle) return 0;
	if (handle->is_chd) return readChd(handle->inner, sector, buffer, requested);
	return fallback.read_sector ? fallback.read_sector(handle->inner, sector, buffer, requested) : 0;
}

static void closeTrack(void* track_handle) {
	Handle* handle = track_handle;
	if (!handle) return;
	if (handle->is_chd) closeChd(handle->inner);
	else if (fallback.close_track) fallback.close_track(handle->inner);
	free(handle);
}

// A CHD track's sectors are numbered from 0; rcheevos' own reader numbers
// them from the start of the disc.
static uint32_t firstTrackSector(void* track_handle) {
	Handle* handle = track_handle;
	if (!handle || handle->is_chd) return 0;
	return fallback.first_track_sector ? fallback.first_track_sector(handle->inner) : 0;
}

void RAChd_reader(rc_hash_cdreader_t* out) {
	rc_hash_get_default_cdreader(&fallback);
	out->open_track = openTrack;
	out->open_track_iterator = openTrackIterator;
	out->read_sector = readSector;
	out->close_track = closeTrack;
	out->first_track_sector = firstTrackSector;
}
