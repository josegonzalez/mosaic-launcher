#include "launcher.h"

int main (int argc, char *argv[]) {
	// LOG_info("time from launch to:\n");
	// unsigned long main_begin = SDL_GetTicks();
	// unsigned long first_draw = 0;

	if (autoResume()) return 0; // nothing to do

	simple_mode = exists(SIMPLE_MODE_PATH);

	LOG_info("NextUI\n");
	InitSettings();

	screen = GFX_init(MODE_MAIN);
// LOG_info("- graphics init: %lu\n", SDL_GetTicks() - main_begin);

	PAD_init();
	// LOG_info("- input init: %lu\n", SDL_GetTicks() - main_begin);
	VIB_init();
	PWR_init();
	if (!HAS_POWER_BUTTON && !simple_mode) PWR_disableSleep();
	// LOG_info("- power init: %lu\n", SDL_GetTicks() - main_begin);

	// start my threaded image loader :D
	initImageLoaderPool();
	Menu_init();
	qm_row = 0;
	qm_col = 0;
	qm_slot = 0;
	qm_shift = 0;
	qm_slots = QUICK_SWITCHER_COUNT > quick->count ? quick->count : QUICK_SWITCHER_COUNT;
	// LOG_info("- menu init: %lu\n", SDL_GetTicks() - main_begin);

	lastScreen = SCREEN_OFF;
	currentScreen = CFG_getDefaultView();

	if(exists(GAME_SWITCHER_PERSIST_PATH)) {
		// consider this "consumed", dont bring up the switcher next time we regularly exit a game
		unlink(GAME_SWITCHER_PERSIST_PATH);
		currentScreen = SCREEN_GAMESWITCHER;
	}

	// add a nice fade into the game switcher
	if(currentScreen == SCREEN_GAMESWITCHER)
		lastScreen = SCREEN_GAME;

	// make sure we have no running games logged as active anymore (we might be launching back into the UI here)
	system("gametimectl.elf stop_all");

	GFX_setVsync(VSYNC_STRICT);
	PWR_setCPUSpeed(CPU_SPEED_AUTO);

	PAD_reset();
	GFX_clearLayers(LAYER_ALL);
	GFX_clear(screen);

	show_setting = 0; // 1=brightness,2=volume
	int was_online = PWR_isOnline();
    int had_bt = PLAT_btIsConnected();
	int had_sink = GetAudioSink();

	selected_row = top->selected - top->start;
	is_scrolling = 0;
	list_show_entry_names = true;

	folderbgbmp = NULL;

	blackBG = SDL_CreateRGBSurfaceWithFormat(0,screen->w,screen->h,screen->format->BitsPerPixel,screen->format->format);
	SDL_FillRect(blackBG,NULL,SDL_MapRGBA(screen->format,0,0,0,255));

	SDL_LockMutex(animMutex);
	globalpill = SDL_CreateRGBSurfaceWithFormat(SDL_SWSURFACE, screen->w, SCALE1(PILL_SIZE), FIXED_DEPTH, screen->format->format);
	globalText = SDL_CreateRGBSurfaceWithFormat(SDL_SWSURFACE, screen->w, SCALE1(PILL_SIZE), FIXED_DEPTH, screen->format->format);
	SDL_UnlockMutex(animMutex);

	//LOG_info("Start time time %ims\n",SDL_GetTicks());
	while (!quit) {
		GFX_startFrame();
		now = SDL_GetTicks();

		PAD_poll();

		selected = top->selected;
		total = top->entries->count;

		PWR_update(&dirty, &show_setting, NULL, NULL);

		// user-assignable buttons, handled before the per-screen input so they work
		// from the game list, the game switcher and the quick menu alike.
		// skipped while the brightness/volume overlay owns input, same as L1/R1 below.
		if (!show_setting) {
			if (PAD_justPressed(BTN_FN1)) runFnAction(0);
			else if (PAD_justPressed(BTN_FN2)) runFnAction(1);
			else if (PAD_justPressed(BTN_FN3)) runFnAction(2);
		}

		int is_online = PWR_isOnline();
		if (was_online!=is_online)
			dirty = 1;
		was_online = is_online;

        int has_bt = PLAT_btIsConnected();
        if (had_bt != has_bt)
            dirty = 1;
        had_bt = has_bt;

		int has_sink = GetAudioSink();
		if (had_sink != has_sink)
			dirty = 1;
		had_sink = has_sink;

		gsanimdir = ANIM_NONE;

		Screen_get(currentScreen)->handleInput();

		if(dirty) {
			tmpOldScreen = NULL;
			switcherSur = NULL;
			// NOTE:22 This causes slowdown when CFG_getMenuTransitions is set to false because animationdirection turns > 0 somewhere but is never set back to 0 and so this code runs on every action, will fix later
			if(animationdirection != ANIM_NONE || (lastScreen==SCREEN_GAMELIST && currentScreen == SCREEN_GAMESWITCHER)) {
				if(tmpOldScreen) SDL_FreeSurface(tmpOldScreen);
				tmpOldScreen = GFX_captureRendererToSurface();
				SDL_SetSurfaceBlendMode(tmpOldScreen,SDL_BLENDMODE_BLEND);
			}

			// clear only background layer on start
			if(lastScreen==SCREEN_GAME || lastScreen==SCREEN_OFF) {
				GFX_clearLayers(LAYER_ALL);
			}
			else {
				GFX_clearLayers(LAYER_TRANSITION);
				if(lastScreen!=SCREEN_GAMELIST)
					GFX_clearLayers(LAYER_THUMBNAIL);
				GFX_clearLayers(LAYER_SCROLLTEXT);
				GFX_clearLayers(LAYER_IDK2);
			}
			GFX_clear(screen);

			if(CFG_getGameSwitcherCurtain() > 0 && currentScreen == SCREEN_GAMESWITCHER) {
				GFX_blitTopCurtain(screen);
				GFX_blitBottomCurtain(screen);
			}

			ow = GFX_blitHardwareGroup(screen, show_setting);
			const Screen* active = Screen_get(currentScreen);
			if (startgame && !active->rendersWhileLaunching) LaunchFade_render();
			else active->render();

			if(animationdirection != ANIM_NONE) {
				if(CFG_getMenuTransitions()) {
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
						if(animationdirection == SLIDE_LEFT) GFX_animateAndFadeSurface(tmpOldScreen,0,0,-250,0,FIXED_WIDTH,FIXED_HEIGHT,_duration,tmpNewScreen,FIXED_WIDTH,0,0,0,FIXED_WIDTH,FIXED_HEIGHT,255,255,LAYER_THUMBNAIL,TRANSITION_CURVE,TRANSITION_CURVE,TRANSITION_INTENSITY);
						if(animationdirection == SLIDE_RIGHT) GFX_animateAndFadeSurface(tmpNewScreen,-250,0,0,0,FIXED_WIDTH,FIXED_HEIGHT,_duration,tmpOldScreen,0,0,FIXED_WIDTH,0,FIXED_WIDTH,FIXED_HEIGHT,255,255,LAYER_THUMBNAIL,TRANSITION_CURVE,TRANSITION_CURVE,TRANSITION_INTENSITY);
					}
					GFX_clearLayers(LAYER_THUMBNAIL);
					SDL_FreeSurface(tmpNewScreen);
				}
				// animation done
				animationdirection = ANIM_NONE;
			}

			if(lastScreen == SCREEN_QUICKMENU) {
				SDL_LockMutex(bgMutex);
				if(folderbgchanged) {
					if(folderbgbmp)
						GFX_drawOnLayer(folderbgbmp,0, 0, screen->w, screen->h,1.0f,0,LAYER_BACKGROUND);
					else {
						GFX_clearLayers(LAYER_BACKGROUND);
					}
					folderbgchanged = 0;
				}
				SDL_UnlockMutex(bgMutex);
			}
			else if(lastScreen == SCREEN_GAMELIST) {
				SDL_LockMutex(bgMutex);
				if(folderbgchanged) {
					if(folderbgbmp)
						GFX_drawOnLayer(folderbgbmp,0, 0, screen->w, screen->h,1.0f,0,LAYER_BACKGROUND);
					else {
						GFX_clearLayers(LAYER_BACKGROUND);
					}
					folderbgchanged = 0;
				}
				SDL_UnlockMutex(bgMutex);
				SDL_LockMutex(thumbMutex);
				if(thumbbmp && thumbchanged) {
					int img_w = thumbbmp->w;
					int img_h = thumbbmp->h;
					double aspect_ratio = (double)img_h / img_w;
					int max_w = (int)(screen->w * CFG_getGameArtWidth());
					int max_h = (int)(screen->h * 0.6);
					int new_w = max_w;
					int new_h = (int)(new_w * aspect_ratio);

					if (new_h > max_h) {
						new_h = max_h;
						new_w = (int)(new_h / aspect_ratio);
					}

					int target_x = screen->w-(new_w + SCALE1(BUTTON_MARGIN*3));
					int target_y = (int)(screen->h * 0.50);
					int center_y = target_y - (new_h / 2); // FIX: use new_h instead of thumbbmp->h
					GFX_clearLayers(LAYER_THUMBNAIL);
					GFX_drawOnLayer(thumbbmp,target_x,center_y,new_w,new_h,1.0f,0,LAYER_THUMBNAIL);
				} else if(thumbchanged) {
					GFX_clearLayers(LAYER_THUMBNAIL);
				}
				SDL_UnlockMutex(thumbMutex);

				GFX_clearLayers(LAYER_TRANSITION);
				GFX_clearLayers(LAYER_SCROLLTEXT);

				SDL_LockMutex(animMutex);
				if (list_show_entry_names) {
					GFX_drawOnLayer(globalpill, pillRect.x, pillRect.y, globallpillW, globalpill->h, 1.0f, 0, LAYER_TRANSITION);
					GFX_drawOnLayer(globalText, SCALE1(BUTTON_MARGIN + BUTTON_PADDING), pilltargetTextY, globalText->w, globalText->h, 1.0f, 0, LAYER_SCROLLTEXT);
				}
				SDL_UnlockMutex(animMutex);
			}
			if(!startgame) // dont flip if game gonna start
				GFX_flip(screen);

			dirty = 0;
		} else if(getAnimationDraw() || folderbgchanged || thumbchanged || is_scrolling) {
			// honestly this whole thing is here only for the scrolling text, I set it now to run this at 30fps which is enough for scrolling text, should move this to seperate animation function eventually
			Uint32 now = SDL_GetTicks();
			Uint32 frame_start = now;
			static char cached_display_name[256] = "";
			SDL_LockMutex(bgMutex);
			if(folderbgchanged) {
				if(folderbgbmp)
					GFX_drawOnLayer(folderbgbmp,0, 0, screen->w, screen->h,1.0f,0,LAYER_BACKGROUND);
				else {
					GFX_clearLayers(LAYER_BACKGROUND);
				}
				folderbgchanged = 0;
			}
			SDL_UnlockMutex(bgMutex);
			SDL_LockMutex(thumbMutex);
			if(thumbbmp && thumbchanged) {
				int img_w = thumbbmp->w;
				int img_h = thumbbmp->h;
				double aspect_ratio = (double)img_h / img_w;

				int max_w = (int)(screen->w * CFG_getGameArtWidth());
				int max_h = (int)(screen->h * 0.6);

				int new_w = max_w;
				int new_h = (int)(new_w * aspect_ratio);

				if (new_h > max_h) {
					new_h = max_h;
					new_w = (int)(new_h / aspect_ratio);
				}

				int target_x = screen->w-(new_w + SCALE1(BUTTON_MARGIN*3));
				int target_y = (int)(screen->h * 0.50);
				int center_y = target_y - (new_h / 2); // FIX: use new_h instead of thumbbmp->h
				GFX_clearLayers(LAYER_THUMBNAIL);
				GFX_drawOnLayer(thumbbmp,target_x,center_y,new_w,new_h,1.0f,0,LAYER_THUMBNAIL);
				thumbchanged = 0;
			} else if(thumbchanged) {
				GFX_clearLayers(LAYER_THUMBNAIL);
				thumbchanged = 0;
			}
			SDL_UnlockMutex(thumbMutex);
			SDL_LockMutex(animMutex);
			if (getAnimationDraw()) {
				GFX_clearLayers(LAYER_TRANSITION);
				if (list_show_entry_names)
					GFX_drawOnLayer(globalpill, pillRect.x, pillRect.y, globallpillW, globalpill->h, 1.0f, 0, LAYER_TRANSITION);
				setAnimationDraw(0);
			}
			SDL_UnlockMutex(animMutex);
			if (currentScreen != SCREEN_GAMESWITCHER && currentScreen != SCREEN_QUICKMENU) {
				if(is_scrolling && pillanimdone && currentAnimQueueSize < 1) {
					int ow = GFX_blitHardwareGroup(screen, show_setting);
					Entry* entry = top->entries->items[top->selected];
					trimSortingMeta(&entry->name);
					char* entry_text = entry->name;
					if (entry->unique) {
						trimSortingMeta(&entry->unique);
						entry_text = entry->unique;
					}

					int available_width = (had_thumb ? ox + SCALE1(BUTTON_MARGIN) : screen->w - SCALE1(BUTTON_MARGIN)) - SCALE1(PADDING * 2);
					if (top->selected == top->start && !had_thumb) available_width -= ow;

					SDL_Color text_color = uintToColour(THEME_COLOR5_255);

					int text_width = GFX_getTextWidth(font.large, entry_text, cached_display_name, available_width, SCALE1(BUTTON_PADDING * 2));
					int max_width = MIN(available_width, text_width);
					int text_offset_y = (SCALE1(PILL_SIZE) - TTF_FontHeight(font.large) + 1) >> 1;

					GFX_clearLayers(LAYER_SCROLLTEXT);
					if (list_show_entry_names) {
						GFX_scrollTextTexture(
							font.large,
							entry_text,
							SCALE1(BUTTON_MARGIN + BUTTON_PADDING), SCALE1(PADDING + previous_row * PILL_SIZE) + text_offset_y,
							max_width - SCALE1(BUTTON_PADDING * 2),
							0,
							text_color,
							1,
							fontMutex  // Thread-safe font access
						);
					}
				}
				else {
					GFX_clearLayers(LAYER_TRANSITION);
					GFX_clearLayers(LAYER_SCROLLTEXT);
					SDL_LockMutex(animMutex);
					if (list_show_entry_names) {
						GFX_drawOnLayer(globalpill, pillRect.x, pillRect.y, globallpillW, globalpill->h, 1.0f, 0, LAYER_TRANSITION);
						GFX_drawOnLayer(globalText, SCALE1(BUTTON_MARGIN + BUTTON_PADDING),pilltargetTextY, globalText->w, globalText->h, 1.0f, 0, LAYER_SCROLLTEXT);
					}
					SDL_UnlockMutex(animMutex);
					PLAT_GPU_Flip();
				}
			}
			else {
				GFX_sync();
			}
			dirty = 0;
		}
		else {
			// want to draw only if needed
			SDL_LockMutex(bgqueueMutex);
			SDL_LockMutex(thumbqueueMutex);
			SDL_LockMutex(animqueueMutex);
			if(getNeedDraw()) {
				PLAT_GPU_Flip();
				setNeedDraw(0);
			} else {
				GFX_sync();
			}
			SDL_UnlockMutex(animqueueMutex);
			SDL_UnlockMutex(thumbqueueMutex);
			SDL_UnlockMutex(bgqueueMutex);
		}

		SDL_LockMutex(frameMutex);
		frameReady = true;
		SDL_CondSignal(flipCond);
		SDL_UnlockMutex(frameMutex);

		// animation does not carry over between loops, this should only ever be set by
		// input handling and directly consumed by the following render pass
		assert(animationdirection == ANIM_NONE);

		// handle HDMI change
		static int had_hdmi = -1;
		int has_hdmi = GetHDMI();
		if (had_hdmi==-1) had_hdmi = has_hdmi;
		if (has_hdmi!=had_hdmi) {
			had_hdmi = has_hdmi;

			Entry* entry = top->entries->items[top->selected];
			LOG_info("restarting after HDMI change... (%s)\n", entry->path);
			saveLast(entry->path); // NOTE: doesn't work in Recents (by design)
			sleep(1); // cable-seating debounce; the output switch itself happens at next app start
			quit = 1;
		}
	}

	Menu_quit();
	PWR_quit();
	PAD_quit();

	// Cleanup worker threads and their synchronization primitives
	cleanupImageLoaderPool();

	GFX_quit(); // Cleanup video subsystem first to stop GPU threads

	// Now safe to free surfaces after GPU threads are stopped
	if(blackBG)	SDL_FreeSurface(blackBG);
	if (folderbgbmp) SDL_FreeSurface(folderbgbmp);
	if (thumbbmp) SDL_FreeSurface(thumbbmp);

	QuitSettings();
}
