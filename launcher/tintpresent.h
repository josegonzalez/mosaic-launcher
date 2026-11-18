#ifndef LAUNCHER_TINTPRESENT_H
#define LAUNCHER_TINTPRESENT_H

// The launcher's makefile includes this header ahead of every file of
// nextui.elf (its own and the vendored ones it compiles, before SDL.h), so
// every frame the platform's generic_video.c shows goes through tint.c,
// which multiplies it by plugins' screen tint, if any, as the last step
// before it's shown. SDL_render.h then declares this name with
// SDL_RenderPresent's own signature. tint.c undefines it and declares
// SDL's own again, which it calls. With no tint it's exactly SDL's call.
// Only nextui.elf is built this way.

#define SDL_RenderPresent Tint_present

#endif
