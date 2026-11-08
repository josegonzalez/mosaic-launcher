#include <sys/stat.h>
#include <time.h>
#include "launcher.h"
#include "statusmodel.h"
#include "statuspill.h"
#include "plugindraw.h"

#ifdef __APPLE__
#define MTIME_NS(st) ((st).st_mtimespec.tv_nsec)
#else
#define MTIME_NS(st) ((st).st_mtim.tv_nsec)
#endif

static StatusBoard board;
static char shown[STATUS_LABEL_MAX + 32];     // what the pill says, "" for nothing
static char shown_pak[STATUS_NAME_MAX];       // whose it is, for the log
static int shown_state = -1;
static int full_logged = 0;
static MosaicRect logged_rect = { 0, 0, 0, 0 };

static StatusSig sigOf(const struct stat* st) {
	StatusSig sig = { (long long)st->st_mtime, (long)MTIME_NS(*st), (long long)st->st_size,
		(unsigned long long)st->st_ino };
	return sig;
}

// The file's text, read without following a link or blocking on a FIFO,
// into data (STATUS_FILE_MAX + 1 bytes). Returns how many bytes it read,
// which is past STATUS_FILE_MAX for a file that's too big, or -1.
static long readFile(int dir, const char* name, char* data) {
	int fd = openat(dir, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
	if (fd < 0) return -1;
	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
		close(fd);
		return -1;
	}
	size_t got = 0;
	while (got < STATUS_FILE_MAX + 1) {
		ssize_t n = read(fd, data + got, STATUS_FILE_MAX + 1 - got);
		if (n <= 0) break;
		got += (size_t)n;
	}
	close(fd);
	return (long)got;
}

static void scan(unsigned long now) {
	StatusBoard_begin(&board);
	// never through a link, so a pak can't point the launcher elsewhere
	int dir = open(STATUS_DIR, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
	DIR* dh = dir >= 0 ? fdopendir(dir) : NULL;
	if (!dh) {
		if (dir >= 0) close(dir);
		StatusBoard_end(&board);
		return;
	}
	struct dirent* de;
	while ((de = readdir(dh))) {
		char pak[STATUS_NAME_MAX];
		if (!Status_fileName(de->d_name, pak, sizeof(pak))) continue;
		struct stat st;
		if (fstatat(dir, de->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) continue;
		StatusSig sig = sigOf(&st);
		StatusSlot* slot;
		int changed = StatusBoard_track(&board, pak, &sig, now, &slot);
		if (changed < 0) {
			if (!full_logged) LOG_warn("status: more than %i status files, leaving %s out\n", STATUS_SLOTS, de->d_name);
			full_logged = 1;
			continue;
		}
		if (!changed) continue;
		const char* why = NULL;
		if (!S_ISREG(st.st_mode)) {
			slot->valid = 0;
			why = "it isn't a regular file";
		}
		else {
			char data[STATUS_FILE_MAX + 1];
			long got = readFile(dir, de->d_name, data);
			StatusSlot_set(slot, got < 0 ? NULL : data, got < 0 ? 0 : (size_t)got, now, &why);
		}
		if (!slot->valid) LOG_warn("status: ignoring %s: %s\n", de->d_name, why ? why : "it can't be read");
	}
	closedir(dh); // closes dir too
	StatusBoard_end(&board);
}

void StatusPill_tick(unsigned long now) {
	if (!StatusBoard_due(&board, now)) return;
	scan(now);

	const StatusSlot* slot = StatusBoard_pick(&board, now, (long long)time(NULL));
	char text[sizeof(shown)] = "";
	if (slot) Status_text(&slot->file, slot->name, text, sizeof(text));
	if (!strcmp(text, shown)) return;
	snprintf(shown, sizeof(shown), "%s", text);
	state.dirty = 1;
	// the log follows which pak shows and its state, not every percentage
	if (!slot) {
		if (shown_pak[0]) LOG_info("status: none\n");
		shown_pak[0] = '\0';
		shown_state = -1;
	}
	else if (strcmp(slot->name, shown_pak) != 0 || slot->file.state != shown_state) {
		LOG_info("status: showing %s: %s\n", slot->name, shown);
		snprintf(shown_pak, sizeof(shown_pak), "%s", slot->name);
		shown_state = slot->file.state;
	}
}

void StatusPill_place(SDL_Surface* dst, HeaderRow* row) {
	if (!shown[0] || !dst) return;
	int pad = SCALE1(BUTTON_PADDING);
	int room = HeaderRow_room(row) - pad * 2;
	if (room <= 0) return;
	char text[sizeof(shown) + 4];
	int text_w = PluginDraw_textTruncate(MOSAIC_FONT_SMALL, shown, room, text, sizeof(text));
	if (text_w <= 0) return;
	MosaicRect rect;
	if (!HeaderRow_place(row, text_w + pad * 2, &rect)) return;
	if (memcmp(&rect, &logged_rect, sizeof(rect)) != 0) {
		LOG_info("status: pill at %i,%i %ix%i\n", rect.x, rect.y, rect.w, rect.h);
		logged_rect = rect;
	}

	GFX_blitPillColor(ASSET_WHITE_PILL, dst, &(SDL_Rect){ rect.x, rect.y, rect.w, rect.h }, THEME_COLOR2, RGB_WHITE);
	if (fontMutex) SDL_LockMutex(fontMutex);
	SDL_Surface* drawn = TTF_RenderUTF8_Blended(font.small, text, uintToColour(THEME_COLOR6_255));
	if (fontMutex) SDL_UnlockMutex(fontMutex);
	if (!drawn) return;
	SDL_BlitSurface(drawn, NULL, dst, &(SDL_Rect){ rect.x + pad, rect.y + (rect.h - drawn->h) / 2, 0, 0 });
	SDL_FreeSurface(drawn);
}
