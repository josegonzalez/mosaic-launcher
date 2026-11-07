#include "launcher.h"

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
	{
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
		GFX_drawOnLayer(tmpOldScreen,0,0,screen->w, screen->h,1.0f,0,LAYER_ALL);
		GFX_animateSurface(tmpNewScreen,0,0-screen->h,0,0,screen->w,screen->h,CFG_getMenuTransitions() ? 100:20,255,255,LAYER_BACKGROUND);
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
		GFX_animateSurface(tmpsur,0,0-screen->h,0,0,screen->w,screen->h,CFG_getMenuTransitions() ? 100:20,255,255,LAYER_ALL);
	} else if(from == SCREEN_GAMESWITCHER) {
		GFX_flipHidden();
		if(slide == SLIDE_LEFT)
			GFX_animateSurface(tmpsur,0+screen->w,0,0,0,screen->w,screen->h,CFG_getMenuTransitions() ? 80:20,0,255,LAYER_ALL);
		else if(slide == SLIDE_RIGHT)
			GFX_animateSurface(tmpsur,0-screen->w,0,0,0,screen->w,screen->h,CFG_getMenuTransitions() ? 80:20,0,255,LAYER_ALL);
	}
}
