#ifndef LAUNCHER_FALLBACKTTF_H
#define LAUNCHER_FALLBACKTTF_H

// The launcher's makefile includes this header ahead of every file of
// nextui.elf (its own and the vendored ones it compiles, before SDL_ttf.h),
// so all of its text is measured and drawn through fallback.c: stock's
// api.c (truncation, wrapping, GFX_blitMessage, the button hints), the
// platform's scrolling text in generic_video.c, the launcher's own text and
// plugins' draw_text. SDL_ttf.h then declares these names with SDL_ttf's
// own signatures. fallback.c undefines them and declares SDL_ttf's own
// again, which it calls. With no fallback font, or text the UI font has every glyph of, each
// is exactly SDL_ttf's call. Only nextui.elf is built this way.

#define TTF_SizeUTF8 Fallback_sizeUTF8
#define TTF_RenderUTF8_Blended Fallback_renderUTF8
#define TTF_RenderUTF8_Blended_Wrapped Fallback_renderUTF8Wrapped

#endif
