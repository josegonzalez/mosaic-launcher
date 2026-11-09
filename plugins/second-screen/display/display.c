// second-screen-display: the second screen's window. The plugin starts it
// with the frame file's path and the window's size, and its stdin is a
// socket the plugin writes a byte to each time a new frame is in the
// file. It shows the newest frame, fitted to the window, and quits when
// its stdin closes (the launcher quit or died) or its window is closed,
// removing the frame file.
//
// With MOSAIC_SECOND_SCREEN_SHOT set to a folder, each frame shown is
// also read back from the window and saved there as frame-<n>.bmp, for
// the desktop tests.
//
//   second-screen-display <frame file> <width> <height>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <SDL.h>
#include "ss_model.h"

#define TAG "second-screen-display"

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[1200];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	fprintf(stderr, TAG ": %s\n", message);
	fflush(stderr);
}

// the whole frame file, or NULL
static unsigned char* readFile(const char* path, size_t* len) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	unsigned char* data = NULL;
	if (fseek(f, 0, SEEK_END) == 0) {
		long size = ftell(f);
		if (size > 0 && fseek(f, 0, SEEK_SET) == 0 && (data = malloc((size_t)size))) {
			if (fread(data, 1, (size_t)size, f) == (size_t)size) {
				*len = (size_t)size;
			} else {
				free(data);
				data = NULL;
			}
		}
	}
	fclose(f);
	return data;
}

int main(int argc, char** argv) {
	if (argc != 4) {
		fprintf(stderr, "usage: " TAG " <frame file> <width> <height>\n");
		return 2;
	}
	const char* path = argv[1];
	int w = atoi(argv[2]), h = atoi(argv[3]);
	if (w < SS_SIDE_MIN || h < SS_SIDE_MIN || w > SS_SIDE_MAX || h > SS_SIDE_MAX) {
		say("bad window size %ix%i", w, h);
		return 2;
	}
	// nothing of the launcher's but stdin, stdout and stderr
	for (int fd = 3; fd < 1024; fd++) close(fd);
	signal(SIGPIPE, SIG_IGN);
	fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
	const char* shots = getenv("MOSAIC_SECOND_SCREEN_SHOT");

	// leave the launcher's window focused
	SDL_SetHint(SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN, "1");
	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		say("can't start SDL's video: %s", SDL_GetError());
		unlink(path);
		return 1;
	}
	SDL_Window* window = SDL_CreateWindow("Second screen", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h,
		SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
	SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, 0) : NULL;
	if (!renderer) {
		say("can't open a window: %s", SDL_GetError());
		SDL_Quit();
		unlink(path);
		return 1;
	}
	SDL_ShowWindow(window);
	say("opened a %ix%i window with %s", w, h, SDL_GetCurrentVideoDriver());

	SDL_Texture* texture = NULL;
	int tex_w = 0, tex_h = 0;
	unsigned int shown = 0, saved = 0, logged = 0;
	int have_frame = 0, dirty = 1, running = 1, load = 0;
	while (running) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT) running = 0;
			else if (event.type == SDL_WINDOWEVENT) dirty = 1;
		}
		char buf[64];
		ssize_t n;
		while ((n = read(0, buf, sizeof(buf))) > 0) load = 1;
		if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) running = 0;
		if (!running) break;

		if (load) {
			load = 0;
			size_t len = 0;
			unsigned char* data = readFile(path, &len);
			unsigned int number;
			int fw, fh;
			if (data && SS_frameParse(data, len, &number, &fw, &fh)) {
				if (!texture || fw != tex_w || fh != tex_h) {
					if (texture) SDL_DestroyTexture(texture);
					texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, fw, fh);
					tex_w = fw;
					tex_h = fh;
				}
				if (texture && SDL_UpdateTexture(texture, NULL, data + SS_FRAME_HEADER, fw * 4) == 0) {
					shown = number;
					have_frame = 1;
					dirty = 1;
				}
			} else {
				say("can't read a frame from %s", path);
			}
			free(data);
		}

		if (dirty) {
			dirty = 0;
			SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
			SDL_RenderClear(renderer);
			int out_w = w, out_h = h;
			SDL_GetRendererOutputSize(renderer, &out_w, &out_h);
			if (have_frame) {
				MosaicRect fit = SS_fit(tex_w, tex_h, (MosaicRect){ 0, 0, out_w, out_h });
				SDL_Rect dst = { fit.x, fit.y, fit.w, fit.h };
				SDL_RenderCopy(renderer, texture, NULL, &dst);
			}
			if (have_frame && shown != saved && shots && shots[0]) {
				// read back before presenting, while the frame's still drawn
				SDL_Surface* shot = SDL_CreateRGBSurfaceWithFormat(0, out_w, out_h, 24, SDL_PIXELFORMAT_BGR24);
				char file[1024];
				snprintf(file, sizeof(file), "%s/frame-%u.bmp", shots, shown);
				if (shot && SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_BGR24, shot->pixels, shot->pitch) == 0
					&& SDL_SaveBMP(shot, file) == 0) say("saved %s, %ix%i", file, out_w, out_h);
				if (shot) SDL_FreeSurface(shot);
				saved = shown;
			}
			SDL_RenderPresent(renderer);
			if (have_frame && shown != logged) say("showing frame %u, %ix%i", shown, tex_w, tex_h);
			logged = shown;
		}
		SDL_Delay(30);
	}

	if (texture) SDL_DestroyTexture(texture);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	unlink(path);
	return 0;
}
