#include "launcher.h"
#include "transmodel.h"

static SDL_Surface* launchOldScreen = NULL; // the frame a launch transition starts from

// An opaque black w by h frame, or NULL.
static SDL_Surface* blackFrame(int w, int h) {
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (s) SDL_FillRect(s, NULL, SDL_MapRGBA(s->format, 0, 0, 0, 255));
	return s;
}

// The transition or launch slot's owner's style for kind, between two
// frames. Returns 1 when it drew the transition or turned it off, or 0 to
// draw stock's, as with no owner, a choice of stock's, or an error. A
// custom transition's frames go on the topmost layer, cleared afterwards.
static int byPlugin(int kind, SDL_Surface* old_frame, SDL_Surface* new_frame, int stock_ms) {
	if (!Plugins_hasTransition(kind)) return 0;
	MosaicTransition t;
	int style = Plugins_transitionBegin(kind, old_frame, new_frame, stock_ms, &t);
	if (style == MOSAIC_STYLE_OFF) return 1;
	if (style != MOSAIC_STYLE_CUSTOM) return 0;
	SDL_Surface* frame = SDL_CreateRGBSurfaceWithFormat(0, screen->w, screen->h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!frame) {
		Plugins_transitionEnd();
		return 0;
	}
	Uint32 black = SDL_MapRGBA(frame->format, 0, 0, 0, 255);
	TransitionRun run;
	TransitionRun_start(&run, SDL_GetTicks(), t.duration_ms);
	float progress;
	int drawn = 1;
	while (drawn && TransitionRun_next(&run, SDL_GetTicks(), &progress)) {
		Uint32 at = SDL_GetTicks();
		SDL_FillRect(frame, NULL, black);
		if (Plugins_transitionFrame(frame, Transition_ease(t.ease, progress)) != 0) {
			drawn = 0;
			break;
		}
		GFX_clearLayers(LAYER_IDK2);
		GFX_drawOnLayer(frame, 0, 0, screen->w, screen->h, 1.0f, 0, LAYER_IDK2);
		PLAT_GPU_Flip();
		Uint32 spent = SDL_GetTicks() - at;
		if (!run.done && spent < TRANSITION_FRAME_MS) SDL_Delay(TRANSITION_FRAME_MS - spent);
	}
	GFX_clearLayers(LAYER_IDK2);
	Plugins_transitionEnd();
	SDL_FreeSurface(frame);
	return drawn;
}

void Transition_captureLaunch(void) {
	if (launchOldScreen) SDL_FreeSurface(launchOldScreen);
	launchOldScreen = GFX_captureRendererToSurface();
}

int Transition_launch(void) {
	SDL_Surface* old_frame = launchOldScreen;
	launchOldScreen = NULL;
	if (!old_frame) return 0;
	SDL_Surface* black = blackFrame(old_frame->w, old_frame->h);
	// stock shows nothing here: its fade starts from a frame it never kept
	int drawn = black && byPlugin(MOSAIC_TRANSITION_LAUNCH, old_frame, black, 0);
	if (black) SDL_FreeSurface(black);
	SDL_FreeSurface(old_frame);
	return drawn;
}

void Transition_slide(SDL_Surface* oldScreen, int direction) {
	SDL_LockMutex(bgMutex);
	if(folderbgbmp) {
		GFX_drawOnLayer(folderbgbmp, 0, 0, screen->w, screen->h, 1.0f, 0, LAYER_BACKGROUND);
	} else {
		GFX_clearLayers(LAYER_BACKGROUND);
	}
	SDL_UnlockMutex(bgMutex);
	folderbgchanged = 1;
	GFX_clearLayers(LAYER_TRANSITION);
	// Draw the selection pill at its target position before capturing the new
	// screen, so the cursor is present in the incoming slide surface rather than
	// blinking in after the transition animation ends.
	if (currentScreen == SCREEN_GAMELIST && total > 0 && globalpill && list_show_entry_names) {
		SDL_LockMutex(animMutex);
		GFX_drawOnLayer(globalpill, SCALE1(BUTTON_MARGIN), (int)SCALE1(targetY + PADDING), globallpillW, globalpill->h, 1.0f, 0, LAYER_TRANSITION);
		if (globalText) {
			GFX_drawOnLayer(globalText, SCALE1(BUTTON_MARGIN + BUTTON_PADDING), pilltargetTextY, globalText->w, globalText->h, 1.0f, 0, LAYER_SCROLLTEXT);
		}
		SDL_UnlockMutex(animMutex);
	}
	GFX_flipHidden();
	SDL_Surface *tmpNewScreen = GFX_captureRendererToSurface();
	SDL_SetSurfaceBlendMode(tmpNewScreen,SDL_BLENDMODE_BLEND);
	GFX_clearLayers(LAYER_SCROLLTEXT); // baked into tmpNewScreen; must clear or it floats above the animation
	GFX_clearLayers(LAYER_THUMBNAIL);
	int kind = direction == SLIDE_LEFT ? MOSAIC_TRANSITION_PUSH : MOSAIC_TRANSITION_POP;
	int stock_ms = !CFG_getMenuTransitions() ? 0
		: CFG_getMenuTransitions() == TRANSITION_COMFY ? TRANSITION_COMFY_DURATION : TRANSITION_SNAPPY_DURATION;
	if (!byPlugin(kind, oldScreen, tmpNewScreen, stock_ms) && CFG_getMenuTransitions()) {
		int _duration = (CFG_getMenuTransitions() == TRANSITION_COMFY)
			? TRANSITION_COMFY_DURATION
			: TRANSITION_SNAPPY_DURATION;
		if(direction == SLIDE_LEFT) GFX_animateAndFadeSurface(oldScreen,0,0,-250,0,FIXED_WIDTH,FIXED_HEIGHT,_duration,tmpNewScreen,FIXED_WIDTH,0,0,0,FIXED_WIDTH,FIXED_HEIGHT,255,255,LAYER_THUMBNAIL,TRANSITION_CURVE,TRANSITION_CURVE,TRANSITION_INTENSITY);
		if(direction == SLIDE_RIGHT) GFX_animateAndFadeSurface(tmpNewScreen,-250,0,0,0,FIXED_WIDTH,FIXED_HEIGHT,_duration,oldScreen,0,0,FIXED_WIDTH,0,FIXED_WIDTH,FIXED_HEIGHT,255,255,LAYER_THUMBNAIL,TRANSITION_CURVE,TRANSITION_CURVE,TRANSITION_INTENSITY);
	}
	GFX_clearLayers(LAYER_THUMBNAIL);
	SDL_FreeSurface(tmpNewScreen);
}

void Transition_showPreview(SDL_Surface* bmp, SDL_Rect dst, int from, int slide) {
	int ax = dst.x;
	int ay = dst.y;
	int aw = dst.w;
	int ah = dst.h;

	if(from == SCREEN_GAME) {
		// need to flip once so streaming_texture1 is updated
		GFX_flipHidden();
		GFX_animateSurfaceOpacity(bmp,0,0,screen->w,screen->h,0,255,CFG_getMenuTransitions() ? 150:20,LAYER_ALL);
	} else if(from == SCREEN_GAMELIST) {

		GFX_drawOnLayer(blackBG,0,0,screen->w,screen->h,1.0f,0,LAYER_BACKGROUND);
		GFX_drawOnLayer(bmp,ax,ay,aw, ah,1.0f,0,LAYER_BACKGROUND);
		GFX_flipHidden();
		SDL_Surface *tmpNewScreen = GFX_captureRendererToSurface();
		GFX_clearLayers(LAYER_ALL);
		folderbgchanged=1;
		int drawn = byPlugin(MOSAIC_TRANSITION_SCREEN, tmpOldScreen, tmpNewScreen, CFG_getMenuTransitions() ? 100:20);
		GFX_drawOnLayer(tmpOldScreen,0,0,screen->w, screen->h,1.0f,0,LAYER_ALL);
		// where stock's slide ends: the frame on the layer it slides on
		if (drawn) GFX_drawOnLayer(tmpNewScreen,0,0,screen->w,screen->h,1.0f,0,LAYER_SCROLLTEXT);
		else GFX_animateSurface(tmpNewScreen,0,0-screen->h,0,0,screen->w,screen->h,CFG_getMenuTransitions() ? 100:20,255,255,LAYER_BACKGROUND);
		SDL_FreeSurface(tmpNewScreen);

	} else if(from == SCREEN_GAMESWITCHER) {
		GFX_flipHidden();
		GFX_drawOnLayer(blackBG,0,0,screen->w, screen->h,1.0f,0,LAYER_BACKGROUND);
		if(slide == SLIDE_LEFT)
			GFX_animateSurface(bmp,ax+screen->w,ay,ax,ay,aw,ah,CFG_getMenuTransitions() ? 80:20,0,255,LAYER_ALL);
		else if(slide == SLIDE_RIGHT)
			GFX_animateSurface(bmp,ax-screen->w,ay,ax,ay,aw,ah,CFG_getMenuTransitions() ? 80:20,0,255,LAYER_ALL);

		GFX_drawOnLayer(bmp,ax,ay,aw,ah,1.0f,0,LAYER_BACKGROUND);
	} else if(from == SCREEN_QUICKMENU) {
		GFX_flipHidden();
		GFX_drawOnLayer(blackBG,0,0,screen->w, screen->h,1.0f,0,LAYER_BACKGROUND);
		GFX_drawOnLayer(bmp,ax,ay,aw,ah,1.0f,0,LAYER_BACKGROUND);
	}
}

void Transition_showPlaceholder(SDL_Surface* tmpsur, int from, int slide) {
	if(from == SCREEN_GAME) {
		GFX_animateSurfaceOpacity(tmpsur,0,0,screen->w,screen->h,255,0,CFG_getMenuTransitions() ? 150:20,LAYER_BACKGROUND);
	} else if(from == SCREEN_GAMELIST) {
		// a custom one ends where stock's does, on the layer it slides on
		if (byPlugin(MOSAIC_TRANSITION_SCREEN, tmpOldScreen, tmpsur, CFG_getMenuTransitions() ? 100:20))
			GFX_drawOnLayer(tmpsur,0,0,screen->w,screen->h,1.0f,0,LAYER_TRANSITION);
		else GFX_animateSurface(tmpsur,0,0-screen->h,0,0,screen->w,screen->h,CFG_getMenuTransitions() ? 100:20,255,255,LAYER_ALL);
	} else if(from == SCREEN_GAMESWITCHER) {
		GFX_flipHidden();
		if(slide == SLIDE_LEFT)
			GFX_animateSurface(tmpsur,0+screen->w,0,0,0,screen->w,screen->h,CFG_getMenuTransitions() ? 80:20,0,255,LAYER_ALL);
		else if(slide == SLIDE_RIGHT)
			GFX_animateSurface(tmpsur,0-screen->w,0,0,0,screen->w,screen->h,CFG_getMenuTransitions() ? 80:20,0,255,LAYER_ALL);
	}
}
