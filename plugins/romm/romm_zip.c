#include "romm_zip.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "miniz.h"

// the most miniz may ask for at once (its copy of the central directory),
// so a zip that claims a huge one fails instead of filling memory
#define ALLOC_MAX ((size_t)16 * 1024 * 1024)
#define PART ".romm-part" // a file being written, in its folder

static void* capAlloc(void* opaque, size_t items, size_t size) {
	(void)opaque;
	if (size && items > ALLOC_MAX / size) return NULL;
	return malloc(items * size);
}

static void capFree(void* opaque, void* p) {
	(void)opaque;
	free(p);
}

static void* capRealloc(void* opaque, void* p, size_t items, size_t size) {
	(void)opaque;
	if (size && items > ALLOC_MAX / size) return NULL;
	return realloc(p, items * size);
}

typedef struct Made {
	char* path; // in the temporary folder
	int dir;
} Made;

typedef struct Unpack {
	RZJob* job;
	int top; // the temporary folder
	Made* made;
	int made_count, made_cap;
	// the file being written
	int fd;
	unsigned long long wrote, want;
} Unpack;

static int fail(RZJob* j, const char* format, ...) __attribute__((format(printf, 2, 3)));
static int fail(RZJob* j, const char* format, ...) {
	if (j->error[0]) return -1; // the first reason stands
	va_list args;
	va_start(args, format);
	vsnprintf(j->error, sizeof(j->error), format, args);
	va_end(args);
	return -1;
}

static int cancelled(const RZJob* j) {
	return j->cancel && *j->cancel;
}

static int remember(Unpack* u, const char* path, size_t len, int dir) {
	if (u->made_count == u->made_cap) {
		int cap = u->made_cap ? u->made_cap * 2 : 64;
		Made* more = realloc(u->made, (size_t)cap * sizeof(Made));
		if (!more) return -1;
		u->made = more;
		u->made_cap = cap;
	}
	char* copy = malloc(len + 1);
	if (!copy) return -1;
	memcpy(copy, path, len);
	copy[len] = '\0';
	u->made[u->made_count++] = (Made){ copy, dir };
	return 0;
}

// Opens the folder name's first len bytes name inside the temporary
// folder, making the folders on the way that aren't there yet. Returns its
// descriptor, or -1.
static int openFolder(Unpack* u, const char* name, size_t len) {
	int fd = dup(u->top);
	char part[RM_NAME_MAX];
	for (size_t start = 0; fd >= 0 && start < len;) {
		size_t end = start;
		while (end < len && name[end] != '/') end++;
		snprintf(part, sizeof(part), "%.*s", (int)(end - start), name + start);
		if (mkdirat(fd, part, 0755) == 0) {
			if (remember(u, name, end, 1) != 0) {
				close(fd);
				return -1;
			}
		} else if (errno != EEXIST) {
			close(fd);
			return -1;
		}
		int next = openat(fd, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		close(fd);
		fd = next;
		start = end + 1;
	}
	return fd;
}

static size_t writeOut(void* opaque, mz_uint64 offset, const void* data, size_t n) {
	(void)offset;
	Unpack* u = opaque;
	if (cancelled(u->job) || u->wrote + n > u->want) return 0;
	const char* p = data;
	for (size_t left = n; left > 0;) {
		ssize_t w = write(u->fd, p, left);
		if (w < 0 && errno == EINTR) continue;
		if (w <= 0) return 0;
		p += w;
		left -= (size_t)w;
	}
	u->wrote += n;
	if (u->job->done) *u->job->done += (long long)n;
	return n;
}

// Writes one file at path (len bytes of it) in the temporary folder: from
// the zip's entry index, or else from text. Returns 0, or -1.
static int writeFile(Unpack* u, mz_zip_archive* zip, int index, const char* text, size_t text_len, const char* path,
	size_t len, unsigned long long size) {
	RZJob* j = u->job;
	const char* slash = NULL;
	for (size_t i = 0; i < len; i++) {
		if (path[i] == '/') slash = path + i;
	}
	size_t folder = slash ? (size_t)(slash - path) : 0;
	const char* leaf = slash ? slash + 1 : path;
	char name[RM_NAME_MAX];
	snprintf(name, sizeof(name), "%.*s", (int)(len - (size_t)(leaf - path)), leaf);
	int parent = openFolder(u, path, folder);
	if (parent < 0) return fail(j, "can't make a folder for %s", name);
	u->fd = openat(parent, PART, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
	if (u->fd < 0) {
		close(parent);
		return fail(j, "can't write %s", name);
	}
	u->wrote = 0;
	u->want = size;
	int ok;
	if (text) {
		ok = writeOut(u, 0, text, text_len) == text_len;
	} else {
		ok = mz_zip_reader_extract_to_callback(zip, (mz_uint)index, writeOut, u, 0);
		if (!ok && !cancelled(j)) fail(j, "can't unpack %s: %s", name, mz_zip_get_error_string(mz_zip_get_last_error(zip)));
	}
	ok = ok && u->wrote == u->want;
	ok = fsync(u->fd) == 0 && ok;
	ok = close(u->fd) == 0 && ok;
	u->fd = -1;
	struct stat st;
	if (ok && fstatat(parent, name, &st, AT_SYMLINK_NOFOLLOW) == 0) ok = 0; // never over anything
	if (ok && renameat(parent, PART, parent, name) == 0) {
		ok = remember(u, path, len, 0) == 0;
	} else {
		unlinkat(parent, PART, 0);
		ok = 0;
	}
	close(parent);
	if (!ok) {
		if (cancelled(j)) return fail(j, "cancelled");
		return fail(j, "can't write %s", name);
	}
	return 0;
}

// undoes what the unpack made, newest first
static void undo(Unpack* u) {
	for (int i = u->made_count - 1; i >= 0; i--) {
		if (u->top >= 0) unlinkat(u->top, u->made[i].path, u->made[i].dir ? AT_REMOVEDIR : 0);
	}
}

static int pathFits(const char* dir, const char* a, const char* b) {
	char path[RM_STOCK_PATH_MAX];
	int n = b ? snprintf(path, sizeof(path), "%s/%s/%s", dir, a, b) : snprintf(path, sizeof(path), "%s/%s", dir, a);
	return n >= 0 && (size_t)n < sizeof(path);
}

int RZ_unpack(RZJob* j) {
	j->launch[0] = j->error[0] = '\0';
	j->files = j->discs = 0;
	if (j->done) *j->done = 0;
	Unpack u = { .job = j, .top = -1, .fd = -1 };
	mz_zip_archive zip;
	memset(&zip, 0, sizeof(zip));
	zip.m_pAlloc = capAlloc;
	zip.m_pFree = capFree;
	zip.m_pRealloc = capRealloc;
	RMZipEntry* entries = NULL;
	char* names = NULL;
	const char** files = NULL;
	int* order = NULL;
	char* m3u = NULL;
	int made_temp = 0, rc = -1, zip_open = 0;
	size_t name_size = RM_ZIP_NAME_MAX + 2;

	FILE* f = NULL;
	int fd = open(j->zip, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	struct stat st;
	if (fd < 0 || fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || !(f = fdopen(fd, "rb"))) {
		if (fd >= 0) close(fd);
		fail(j, "can't read the download");
		goto done;
	}
	if (!mz_zip_reader_init_cfile(&zip, f, (mz_uint64)st.st_size, 0)) {
		fail(j, "the download isn't a zip RomM made: %s", mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
		goto done;
	}
	zip_open = 1;
	int count = (int)mz_zip_reader_get_num_files(&zip);
	if (count <= 0 || count > RM_ZIP_ENTRIES_MAX) {
		fail(j, count <= 0 ? "the zip is empty" : "the zip has too many entries");
		goto done;
	}
	entries = calloc((size_t)count, sizeof(RMZipEntry));
	names = calloc((size_t)count, name_size);
	files = calloc((size_t)count, sizeof(char*));
	order = calloc((size_t)count, sizeof(int));
	if (!entries || !names || !files || !order) {
		fail(j, "out of memory");
		goto done;
	}
	for (int i = 0; i < count; i++) {
		char* name = names + (size_t)i * name_size;
		mz_uint got = mz_zip_reader_get_filename(&zip, (mz_uint)i, name, (mz_uint)name_size);
		mz_zip_archive_file_stat s;
		if (got == 0 || !mz_zip_reader_file_stat(&zip, (mz_uint)i, &s)) {
			fail(j, "the zip's entries can't be read");
			goto done;
		}
		if (!s.m_is_supported) {
			fail(j, "an entry is packed in a way it can't unpack");
			goto done;
		}
		entries[i] = (RMZipEntry){ name, got - 1, s.m_method, s.m_bit_flag, s.m_external_attr, s.m_comp_size,
			s.m_uncomp_size };
	}
	long long total = 0;
	char why[160];
	if (RM_zipCheck(entries, count, j->expect, &total, why, sizeof(why)) != 0) {
		fail(j, "%s", why);
		goto done;
	}

	// the files that land, without the zip's own <game>.m3u, which the plugin writes itself
	int file_count = 0, single = -1;
	long long land = 0;
	for (int i = 0; i < count; i++) {
		if (RM_zipIsDir(&entries[i]) || RM_isGameM3u(j->game, entries[i].name)) continue;
		single = i;
		files[file_count++] = entries[i].name;
		land += (long long)entries[i].size;
	}
	if (file_count == 0) {
		fail(j, "the zip has nothing to play");
		goto done;
	}
	if (file_count != 1 || strchr(entries[single].name, '/')) single = -1;
	char target[RM_PATH_MAX];
	if (single >= 0) {
		if (!pathFits(j->dir, entries[single].name, NULL)) {
			fail(j, "its name is too long for NextUI");
			goto done;
		}
		snprintf(target, sizeof(target), "%s/%s", j->dir, entries[single].name);
	} else {
		j->discs = RM_discs(files, file_count, order);
		if (j->discs == 0) {
			fail(j, "the zip has nothing to play");
			goto done;
		}
		char launch[RM_NAME_MAX];
		RM_launchFile(j->game, files, file_count, launch, sizeof(launch));
		int fits = pathFits(j->dir, j->game, launch);
		for (int i = 0; i < j->discs && j->discs >= 2; i++) fits = fits && pathFits(j->dir, j->game, files[order[i]]);
		if (!fits) {
			fail(j, "its names are too long for NextUI");
			goto done;
		}
		snprintf(target, sizeof(target), "%s/%s", j->dir, j->game);
		snprintf(j->launch, sizeof(j->launch), "%s/%s", j->game, launch);
	}
	if (lstat(target, &st) == 0) {
		fail(j, "%s is already on the card", single >= 0 ? entries[single].name : j->game);
		goto done;
	}
	size_t m3u_len = 0;
	if (j->discs >= 2) {
		size_t cap = (size_t)j->discs * (RM_ZIP_NAME_MAX + 2) + 1;
		m3u = malloc(cap);
		int n = m3u ? RM_m3uText(files, order, j->discs, m3u, cap) : -1;
		if (n < 0) {
			fail(j, "out of memory");
			goto done;
		}
		m3u_len = (size_t)n;
	}
	struct statvfs vfs;
	if (statvfs(j->dir, &vfs) == 0
			&& (unsigned long long)vfs.f_bavail * vfs.f_frsize < (unsigned long long)land + m3u_len + 1024 * 1024) {
		fail(j, "not enough space on the card");
		goto done;
	}
	if (j->total) *j->total = land + (long long)m3u_len;

	if (mkdir(j->temp, 0755) != 0) {
		fail(j, "can't make a folder to unpack in");
		goto done;
	}
	made_temp = 1;
	u.top = open(j->temp, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (u.top < 0) {
		fail(j, "can't open the folder to unpack in");
		goto done;
	}
	for (int i = 0; i < count; i++) {
		const RMZipEntry* e = &entries[i];
		if (cancelled(j)) {
			fail(j, "cancelled");
			goto done;
		}
		if (RM_isGameM3u(j->game, e->name)) continue;
		if (RM_zipIsDir(e) && single >= 0) continue; // the one file lands on its own
		if (RM_zipIsDir(e)) {
			int folder = openFolder(&u, e->name, e->name_len - 1);
			if (folder < 0) {
				fail(j, "can't make a folder");
				goto done;
			}
			close(folder);
			continue;
		}
		if (writeFile(&u, &zip, i, NULL, 0, e->name, e->name_len, e->size) != 0) goto done;
		j->files++;
	}
	if (m3u) {
		char name[RM_NAME_MAX + 8];
		snprintf(name, sizeof(name), "%s.m3u", j->game);
		if (writeFile(&u, &zip, -1, m3u, m3u_len, name, strlen(name), m3u_len) != 0) goto done;
		j->files++;
	}

	// into place, only now that every file is in
	if (lstat(target, &st) == 0) {
		fail(j, "%s is already on the card", single >= 0 ? entries[single].name : j->game);
		goto done;
	}
	if (single >= 0) {
		char from[RM_PATH_MAX * 2];
		snprintf(from, sizeof(from), "%s/%s", j->temp, entries[single].name);
		if (rename(from, target) != 0) {
			fail(j, "can't move %s into place", entries[single].name);
			goto done;
		}
		u.made_count--; // moved out; nothing to undo
		free(u.made[u.made_count].path);
		close(u.top);
		u.top = -1;
		rmdir(j->temp);
		snprintf(j->launch, sizeof(j->launch), "%s", entries[single].name);
	} else {
		close(u.top);
		u.top = -1;
		if (rename(j->temp, target) != 0) {
			u.top = open(j->temp, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
			fail(j, "can't move %s into place", j->game);
			goto done;
		}
	}
	made_temp = 0;
	rc = 0;

done:
	if (rc != 0) {
		if (u.fd >= 0) close(u.fd);
		undo(&u);
		if (made_temp) rmdir(j->temp);
		j->launch[0] = '\0';
	}
	if (u.top >= 0) close(u.top);
	for (int i = 0; i < u.made_count; i++) free(u.made[i].path);
	free(u.made);
	if (zip_open) mz_zip_reader_end(&zip);
	if (f) fclose(f);
	free(entries);
	free(names);
	free(files);
	free(order);
	free(m3u);
	return rc;
}

static int removeIn(int fd, int depth) {
	int ok = 0;
	int copy = dup(fd);
	DIR* d = copy >= 0 ? fdopendir(copy) : NULL;
	if (!d) {
		if (copy >= 0) close(copy);
		return -1;
	}
	struct dirent* e;
	while ((e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		struct stat st;
		if (fstatat(fd, e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
			ok = -1;
			continue;
		}
		if (S_ISDIR(st.st_mode)) {
			int sub = depth < 16 ? openat(fd, e->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC) : -1;
			if (sub < 0 || removeIn(sub, depth + 1) != 0) ok = -1;
			if (sub >= 0) close(sub);
			if (unlinkat(fd, e->d_name, AT_REMOVEDIR) != 0) ok = -1;
		} else if (unlinkat(fd, e->d_name, 0) != 0) {
			ok = -1;
		}
	}
	closedir(d);
	return ok;
}

int RZ_removeTree(const char* path) {
	struct stat st;
	if (lstat(path, &st) != 0) return errno == ENOENT ? 0 : -1;
	if (!S_ISDIR(st.st_mode)) return unlink(path);
	int fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0) return -1;
	int ok = removeIn(fd, 0);
	close(fd);
	return rmdir(path) == 0 ? ok : -1;
}
