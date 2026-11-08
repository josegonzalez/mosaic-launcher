#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "shelf_model.h"

void LS_aspect(int shape, int* w, int* h) {
	switch (shape) {
	case LS_SHAPE_GB: *w = 57; *h = 65; return;
	case LS_SHAPE_GBA: *w = 57; *h = 35; return;
	}
	*w = 5;
	*h = 7;
}

const char* LS_shapeName(int shape) {
	switch (shape) {
	case LS_SHAPE_GB: return "GB cartridges";
	case LS_SHAPE_GBA: return "GBA cartridges";
	}
	return "cases";
}

int LS_shapeForTag(const char* tag) {
	if (!tag) return LS_SHAPE_CASE;
	static const char* gb[] = { "GB", "GBC", "SGB" };
	static const char* gba[] = { "GBA", "MGBA" };
	for (size_t i = 0; i < sizeof(gb) / sizeof(gb[0]); i++) {
		if (!strcasecmp(tag, gb[i])) return LS_SHAPE_GB;
	}
	for (size_t i = 0; i < sizeof(gba) / sizeof(gba[0]); i++) {
		if (!strcasecmp(tag, gba[i])) return LS_SHAPE_GBA;
	}
	return LS_SHAPE_CASE;
}

void LS_systemTag(const char* path, int is_dir, char* out, size_t size) {
	if (!size) return;
	out[0] = '\0';
	if (!path) return;
	// the folders: all of path, or up to its last slash
	const char* end = path + strlen(path);
	if (!is_dir) {
		end = strrchr(path, '/');
		if (!end) return;
	}
	for (const char* name = path; name < end;) {
		const char* stop = memchr(name, '/', (size_t)(end - name));
		if (!stop) stop = end;
		// a name that ends in "(TAG)"
		if (stop - name >= 3 && stop[-1] == ')') {
			const char* open = NULL;
			for (const char* c = name; c < stop - 1; c++) {
				if (*c == '(') open = c;
			}
			size_t tag = open ? (size_t)(stop - 1 - open - 1) : 0;
			if (tag > 0) {
				if (tag >= size) tag = size - 1;
				memcpy(out, open + 1, tag);
				out[tag] = '\0';
				return;
			}
		}
		name = stop + 1;
	}
}

int LS_shapeOf(const char* path, int is_dir) {
	char tag[32];
	LS_systemTag(path, is_dir, tag, sizeof(tag));
	if (tag[0]) return LS_shapeForTag(tag);
	if (is_dir || !path) return LS_SHAPE_CASE;
	const char* slash = strrchr(path, '/');
	const char* dot = strrchr(slash ? slash : path, '.');
	if (!dot) return LS_SHAPE_CASE;
	dot++;
	if (!strcasecmp(dot, "gb") || !strcasecmp(dot, "gbc") || !strcasecmp(dot, "sgb")) return LS_SHAPE_GB;
	if (!strcasecmp(dot, "gba")) return LS_SHAPE_GBA;
	return LS_SHAPE_CASE;
}

int LS_commonShape(const int* shapes, int n) {
	if (n <= 0 || !shapes) return LS_SHAPE_CASE;
	for (int i = 1; i < n; i++) {
		if (shapes[i] != shapes[0]) return LS_SHAPE_CASE;
	}
	return shapes[0] >= 0 && shapes[0] < LS_SHAPE_COUNT ? shapes[0] : LS_SHAPE_CASE;
}

int LS_count(const char* value, int min, int max) {
	if (!value || !*value) return 0;
	char* end;
	long n = strtol(value, &end, 10);
	if (*end || n < min || n > max) return 0;
	return (int)n;
}

int LS_autoShelves(int h, int scale) {
	if (scale < 1) scale = 1;
	int unit = 110 * scale;
	int shelves = (h + unit / 2) / unit;
	if (shelves < 1) shelves = 1;
	if (shelves > LS_MAX_SHELVES) shelves = LS_MAX_SHELVES;
	return shelves;
}

static int bottom(const MosaicRect* r) {
	return r && r->w > 0 && r->h > 0 ? r->y + r->h : 0;
}

// the lift and cartridge height for shelves shelf_h tall
static void split(int shelf_h, int plank_h, int gap, int scale, int* lift, int* cart_h) {
	int rest = shelf_h - plank_h - gap;
	*lift = rest / 10;
	if (*lift < scale) *lift = scale;
	*cart_h = rest - *lift;
}

int LS_layout(const MosaicRect* area, const MosaicRect* status, const MosaicRect* header, int scale, int shelves,
		int name_h, int shape, int min_h, LSShelves* out) {
	memset(out, 0, sizeof(*out));
	if (scale < 1) scale = 1;
	if (name_h < 0) name_h = 0;
	if (min_h < 1) min_h = 1;
	if (shape < 0 || shape >= LS_SHAPE_COUNT) shape = LS_SHAPE_CASE;
	int gap = 4 * scale;
	int top = area->y + gap;
	if (bottom(status) + gap > top) top = bottom(status) + gap;
	if (bottom(header) + gap > top) top = bottom(header) + gap;
	MosaicRect room = { area->x + gap, top, area->w - 2 * gap, area->y + area->h - gap - top };
	if (room.w <= 0 || room.h <= 0) return -1;

	int plank_h = name_h > 4 * scale ? name_h : 4 * scale;
	if (shelves < 1) shelves = LS_autoShelves(room.h, scale);
	if (shelves > LS_MAX_SHELVES) shelves = LS_MAX_SHELVES;
	int lift = 0, cart_h = 0;
	for (;; shelves--) {
		split(room.h / shelves, plank_h, gap, scale, &lift, &cart_h);
		if (cart_h >= min_h || shelves == 1) break;
	}
	if (cart_h < min_h) return -1;

	int aw, ah;
	LS_aspect(shape, &aw, &ah);
	int cart_w = (int)((long)cart_h * aw / ah);
	int cols = (room.w + gap) / (cart_w + gap);
	if (cols < 1) cols = 1;
	if (cols > LS_MAX_COLUMNS) cols = LS_MAX_COLUMNS;

	out->room = room;
	out->shelves = shelves;
	out->cols = cols;
	out->col_w = room.w / cols;
	out->col_x = room.x + (room.w - out->col_w * cols) / 2;
	out->shelf_h = room.h / shelves;
	out->lift = lift;
	out->cart_h = cart_h;
	out->plank_h = plank_h;
	out->gap = gap;
	out->shape = shape;
	return 0;
}

// the top of shelf's plank
static int plankY(const LSShelves* s, int shelf) {
	return s->room.y + shelf * s->shelf_h + s->lift + s->cart_h;
}

static int inView(const LSShelves* s, int slot) {
	return s->cols > 0 && slot >= 0 && slot < s->cols * s->shelves;
}

MosaicRect LS_cart(const LSShelves* s, int slot, int shape, int progress) {
	if (!inView(s, slot)) return (MosaicRect){ 0, 0, 0, 0 };
	if (progress < 0) progress = 0;
	if (progress > LS_UNIT) progress = LS_UNIT;
	int aw, ah;
	LS_aspect(shape, &aw, &ah);
	int room_w = s->col_w - s->gap;
	if (room_w < 1) room_w = 1;
	int h = s->cart_h;
	int w = (int)((long)h * aw / ah);
	if (w > room_w) {
		w = room_w;
		h = (int)((long)w * ah / aw);
	}
	int col = slot % s->cols, shelf = slot / s->cols;
	int x = s->col_x + col * s->col_w + (s->col_w - w) / 2;
	int y = plankY(s, shelf) - h - (int)((long)s->lift * progress / LS_UNIT);
	return (MosaicRect){ x, y, w, h };
}

MosaicRect LS_plank(const LSShelves* s, int shelf) {
	if (shelf < 0 || shelf >= s->shelves) return (MosaicRect){ 0, 0, 0, 0 };
	return (MosaicRect){ s->room.x, plankY(s, shelf), s->room.w, s->plank_h };
}

MosaicRect LS_tag(const LSShelves* s, int slot, int name_h) {
	if (!inView(s, slot) || name_h <= 0) return (MosaicRect){ 0, 0, 0, 0 };
	int col = slot % s->cols, shelf = slot / s->cols;
	int w = s->col_w - s->gap;
	if (w < 1) w = 1;
	return (MosaicRect){ s->col_x + col * s->col_w + (s->col_w - w) / 2, plankY(s, shelf), w, s->plank_h };
}

static MosaicRect part(const MosaicRect* r, int x, int y, int w, int h) {
	// in hundredths of the cartridge
	return (MosaicRect){ r->x + r->w * x / 100, r->y + r->h * y / 100, r->w * w / 100, r->h * h / 100 };
}

MosaicRect LS_label(int shape, const MosaicRect* cart) {
	switch (shape) {
	case LS_SHAPE_GB: return part(cart, 12, 22, 76, 60);
	case LS_SHAPE_GBA: return part(cart, 10, 30, 80, 60);
	}
	return part(cart, 10, 3, 87, 94);
}

int LS_window(int first, int selected, int count, int cols, int shelves) {
	if (count <= 0 || cols < 1 || shelves < 1) return 0;
	int total = (count + cols - 1) / cols;
	if (selected < 0) selected = 0;
	if (selected >= count) selected = count - 1;
	int shelf = selected / cols;
	if (first > shelf) first = shelf;
	if (shelf >= first + shelves) first = shelf - shelves + 1;
	int last_first = total > shelves ? total - shelves : 0;
	if (first > last_first) first = last_first;
	if (first < 0) first = 0;
	return first;
}

int LS_move(int selected, int count, int cols, int dir, int fresh) {
	if (count <= 1 || cols < 1 || selected < 0 || selected >= count) return selected;
	int col = selected % cols;
	int last_shelf = (count - 1) / cols;
	switch (dir) {
	case MOSAIC_BUTTON_LEFT:
		if (selected > 0) return selected - 1;
		return fresh ? count - 1 : selected;
	case MOSAIC_BUTTON_RIGHT:
		if (selected < count - 1) return selected + 1;
		return fresh ? 0 : selected;
	case MOSAIC_BUTTON_UP: {
		if (selected >= cols) return selected - cols;
		if (!fresh) return selected;
		int target = last_shelf * cols + col;
		return target < count ? target : target - cols;
	}
	case MOSAIC_BUTTON_DOWN:
		if (selected + cols < count) return selected + cols;
		if (selected / cols < last_shelf) return count - 1;
		return fresh ? col : selected;
	}
	return selected;
}

int LS_ease(unsigned long elapsed, unsigned long duration) {
	if (!duration || elapsed >= duration) return LS_UNIT;
	long left = LS_UNIT - (long)(elapsed * LS_UNIT / duration);
	return (int)(LS_UNIT - left * left / LS_UNIT);
}

static int utf8Length(unsigned char c) {
	if (c >= 0xF0) return 4;
	if (c >= 0xE0) return 3;
	if (c >= 0xC0) return 2;
	return 1;
}

static void copyChar(const char* s, char* out, size_t size) {
	size_t n = (size_t)utf8Length((unsigned char)*s);
	for (size_t i = 0; i < n; i++) {
		// a sequence cut short by the end of the name is only its lead byte
		if (!s[i] || (i > 0 && ((unsigned char)s[i] & 0xC0) != 0x80)) {
			n = i;
			break;
		}
	}
	if (n + 1 > size) n = 0;
	memcpy(out, s, n);
	out[n] = '\0';
}

void LS_letter(const char* name, char* out, size_t size) {
	if (!size) return;
	out[0] = '\0';
	if (!name) return;
	for (const char* s = name; *s; s++) {
		unsigned char c = (unsigned char)*s;
		if (c >= 0x80 && c < 0xC0) continue; // a sequence's continuation byte
		if (c >= 0x80 || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
			copyChar(s, out, size);
			if (out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 'a' + 'A');
			return;
		}
	}
	for (const char* s = name; *s; s++) {
		if (*s != ' ' && *s != '\t') {
			copyChar(s, out, size);
			return;
		}
	}
}
