#ifndef UI_SOUNDS_MODEL_H
#define UI_SOUNDS_MODEL_H

// ui-sounds' model, with no SDL: which sound a button or a launcher event
// plays, where each sound's file is (the theme's sounds/ folder over the
// bundled set), reading a WAV file into 16-bit frames, and the plugin's
// volume.

#include <stddef.h>

#define UIS_MAX_MS 2000          // the longest sound the launcher plays (host->sound_play)
#define UIS_FILE_MAX (1 << 20)   // the largest WAV file read
#define UIS_RATE_MIN 8000        // the rates and channels the launcher plays
#define UIS_RATE_MAX 96000
#define UIS_VOLUME_DEFAULT 50
#define UIS_PATH_MAX 512

enum UISound {
	UIS_NONE = -1,
	UIS_MOVE,
	UIS_SELECT,
	UIS_BACK,
	UIS_ERROR,
	UIS_LAUNCH,
	UIS_MENU_OPEN,
	UIS_MENU_CLOSE,
	UIS_TOAST,
	UIS_COUNT
};

// Where a sound's file came from.
enum UISFrom {
	UIS_FROM_NONE,
	UIS_FROM_THEME,
	UIS_FROM_BUNDLED,
};

// A sound read from a WAV file: count frames of channels interleaved
// signed 16-bit samples at rate.
typedef struct UISClip {
	short* frames;
	int count;
	int rate;
	int channels;
	int cut; // the file was longer than UIS_MAX_MS, and was cut to it
} UISClip;

// The sound's name, as its file is named (<name>.wav) and the
// ui-sounds.play service takes it, or "" for UIS_NONE and others.
const char* UIS_name(int sound);

// The sound with that name, or UIS_NONE.
int UIS_byName(const char* name);

// The sound a frame's buttons play (as masks of MOSAIC_BUTTON_BIT): A
// pressed selects, B pressed goes back, and the d-pad, L1 or R1 pressed
// or repeating moves, in that order. UIS_NONE for anything else.
int UIS_forInput(unsigned int pressed, unsigned int repeated);

// The sound a launcher event plays: launcher.launch, launcher.error,
// launcher.menu_open, launcher.menu_close and launcher.toast. UIS_NONE for
// any other.
int UIS_forEvent(const char* name);

// The volume setting, 0 to 100, or UIS_VOLUME_DEFAULT for anything else.
int UIS_volume(const char* setting);

// 1 when a sound is heard at the plugin's volume and the system's, as
// host->system_volume gives it (-1 for none, which doesn't mute).
int UIS_audible(int volume, int system_volume);

// The file for sound into out: <theme_dir>/<name>.wav when theme_dir is
// set and is_file says it's there, else <bundled_dir>/<name>.wav when that
// is. Returns an enum UISFrom, with out "" for UIS_FROM_NONE.
int UIS_pickFile(const char* theme_dir, const char* bundled_dir, int sound, int (*is_file)(const char* path), char* out,
	size_t size);

// 1 for a theme's ui-sounds.dir the plugin uses: a full path.
int UIS_themeDirOk(const char* dir);

// Reads a WAV file of size bytes into clip: PCM of 8, 16, 24 or 32 bits or
// 32-bit floats, mono or stereo, at UIS_RATE_MIN to UIS_RATE_MAX Hz, cut
// to UIS_MAX_MS. Returns 0, or -1 with why saying what's wrong with it,
// leaving clip empty.
int UIS_parseWav(const unsigned char* data, size_t size, UISClip* clip, char* why, size_t why_size);

// Reads the WAV file at path as UIS_parseWav does, refusing one over
// UIS_FILE_MAX bytes.
int UIS_loadWav(const char* path, UISClip* clip, char* why, size_t why_size);

// Scales the clip's samples by volume (0 to 100).
void UIS_scale(UISClip* clip, int volume);

void UIS_freeClip(UISClip* clip);

#endif
