#include "launcher.h"
#include "playhistory.h"

// The built-in list's thumbnail on its layer, sized and placed as stock
// does, where art decorators were told it is (ListArt_rect).
static void drawThumb(void) {
	MosaicRect art;
	ListArt_rect(screen->w, screen->h, CFG_getGameArtWidth(), SCALE1(BUTTON_MARGIN*3), thumbbmp->w, thumbbmp->h, &art);
	GFX_clearLayers(LAYER_THUMBNAIL);
	GFX_drawOnLayer(thumbbmp, art.x, art.y, art.w, art.h, 1.0f, 0, LAYER_THUMBNAIL);
}

// A plugin's art_replace surface in the thumbnail's place: a copy of what
// it holds now, with the corners stock would give a thumbnail that size,
// placed as one. The copy is kept for the next frame of the same size.
static SDL_Surface* swap_copy = NULL;
static void drawSwap(SDL_Surface* src) {
	MosaicRect art;
	if (!ListArt_rect(screen->w, screen->h, CFG_getGameArtWidth(), SCALE1(BUTTON_MARGIN*3), src->w, src->h, &art)) {
		GFX_clearLayers(LAYER_THUMBNAIL);
		return;
	}
	if (swap_copy && (swap_copy->w != src->w || swap_copy->h != src->h)) {
		SDL_FreeSurface(swap_copy);
		swap_copy = NULL;
	}
	// in the format drawOnLayer uploads
	if (!swap_copy) swap_copy = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!swap_copy || SDL_ConvertPixels(src->w, src->h, src->format->format, src->pixels, src->pitch,
			swap_copy->format->format, swap_copy->pixels, swap_copy->pitch) != 0) {
		GFX_clearLayers(LAYER_THUMBNAIL);
		return;
	}
	// only the corners' rows have pixels to clear
	int radius = ListArt_radius(CFG_getThumbnailRadius(), FIXED_SCALE, src->w, art.w, src->w, src->h);
	for (int i = 0; i < radius; i++) {
		int cut = ListArt_cornerCut(radius, radius - i);
		int rows[2] = { i, src->h - 1 - i };
		for (int r = 0; r < 2; r++) {
			Uint8* row = (Uint8*)swap_copy->pixels + (size_t)rows[r] * swap_copy->pitch;
			memset(row, 0, (size_t)cut * 4);
			memset(row + (size_t)(src->w - cut) * 4, 0, (size_t)cut * 4);
		}
	}
	GFX_clearLayers(LAYER_THUMBNAIL);
	GFX_drawOnLayer(swap_copy, art.x, art.y, art.w, art.h, 1.0f, 0, LAYER_THUMBNAIL);
}

// The thumbnail layer, once thumbchanged says it's out of date: a plugin's
// art_replace surface, the built-in list's thumbnail, or nothing.
static void showThumb(void) {
	SDL_Surface* swapped = Plugins_artReplaced();
	if (swapped) drawSwap(swapped);
	else if (thumbbmp) drawThumb();
	else GFX_clearLayers(LAYER_THUMBNAIL);
}

int main (int argc, char *argv[]) {
	// LOG_info("time from launch to:\n");
	// unsigned long main_begin = SDL_GetTicks();
	// unsigned long first_draw = 0;

	// the Launcher pak's screens: nextui.elf --settings [plugins]
	if (argc > 1 && !strcmp(argv[1], "--settings")) return Settings_main(argc - 2, argv + 2);

	if (autoResume()) return 0; // nothing to do

	state.simple_mode = exists(SIMPLE_MODE_PATH);

	LOG_info("NextUI\n");
	InitSettings();

	screen = GFX_init(MODE_MAIN);
// LOG_info("- graphics init: %lu\n", SDL_GetTicks() - main_begin);

	PAD_init();
	// LOG_info("- input init: %lu\n", SDL_GetTicks() - main_begin);
	VIB_init();
	PWR_init();
	if (!HAS_POWER_BUTTON && !state.simple_mode) PWR_disableSleep();
	// LOG_info("- power init: %lu\n", SDL_GetTicks() - main_begin);

	// start my threaded image loader :D
	initImageLoaderPool();
	ArtLoader_init();
	// built-in actions first, so plugins can bind buttons to them
	Screens_init();
	// before Menu_init: building the root list already runs plugin hooks
	Plugins_init();
	PakHooks_init();
	Menu_init();
	QuickMenu_setup();
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

	// a plugin's home screen in place of the root list
	Plugins_homeStart();
	// and its game switcher in place of the built-in one
	Plugins_switcherCheck();

	// make sure we have no running games logged as active anymore (we might be launching back into the UI here)
	system("gametimectl.elf stop_all");
	// and forget what plugins read before stop_all closed the last session
	PlayHistory_invalidate();

	GFX_setVsync(VSYNC_STRICT);
	PWR_setCPUSpeed(CPU_SPEED_AUTO);

	PAD_reset();
	GFX_clearLayers(LAYER_ALL);
	GFX_clear(screen);

	show_setting = 0; // 1=brightness,2=volume
	int was_online = PWR_isOnline();
    int had_bt = PLAT_btIsConnected();
	int had_sink = GetAudioSink();

	selected_row = state.top->selected - state.top->start;
	is_scrolling = 0;
	list_show_entry_names = true;
	list_show_selected_name = true;

	folderbgbmp = NULL;

	blackBG = SDL_CreateRGBSurfaceWithFormat(0,screen->w,screen->h,screen->format->BitsPerPixel,screen->format->format);
	SDL_FillRect(blackBG,NULL,SDL_MapRGBA(screen->format,0,0,0,255));

	SDL_LockMutex(animMutex);
	globalpill = SDL_CreateRGBSurfaceWithFormat(SDL_SWSURFACE, screen->w, SCALE1(PILL_SIZE), FIXED_DEPTH, screen->format->format);
	globalText = SDL_CreateRGBSurfaceWithFormat(SDL_SWSURFACE, screen->w, SCALE1(PILL_SIZE), FIXED_DEPTH, screen->format->format);
	SDL_UnlockMutex(animMutex);

	//LOG_info("Start time time %ims\n",SDL_GetTicks());
	while (!state.quit) {
		GFX_startFrame();
		now = SDL_GetTicks();

		PAD_poll();
		// before anything reads the pad, on every screen
		Plugins_inputFilter(now);

		// a plugin changed something on the card (host->reload_lists)
		if (state.reload) {
			ArtLoader_reset();
			// the theme too, before the lists' rows are counted again
			// and the background drawn again, as its wallpapers may have changed
			if (Plugins_themeRefresh()) folderBgPath[0] = '\0';
			Menu_reload();
		}
		// art decoded since the last frame, and art past the cache's cap dropped
		ArtLoader_pump(1);
		// a plugin added or hid a quick menu tile
		if (state.rebuild_quick) QuickMenu_rebuild();

		selected = state.top->selected;
		total = state.top->entries->count;

		Power_update(&state.dirty, &show_setting);
		Plugins_tick(now);
		// the lock screen, and ambient screens that want to show
		Plugins_ambientCheck(now);
		// a plugin asked to power off or reboot (host->power_off)
		if (state.power_off) {
			int reboot = state.power_off == 2;
			state.power_off = 0;
			QuickItem_powerOff(reboot);
		}
		Toast_tick(now);
		Awake_tick(now);
		StatusPill_tick(now);

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
			state.dirty = 1;
		was_online = is_online;

        int has_bt = PLAT_btIsConnected();
        if (had_bt != has_bt)
            state.dirty = 1;
        had_bt = has_bt;

		int has_sink = GetAudioSink();
		if (had_sink != has_sink)
			state.dirty = 1;
		had_sink = has_sink;

		gsanimdir = ANIM_NONE;

		// an FN button or a plugin's tick opened the game switcher: a plugin's in its place
		Plugins_switcherCheck();
		Screen_get(currentScreen)->handleInput();
		// plugins' screens close once a built-in screen opened over them
		Plugins_screensCheck();

		// so a thumbnail decoded meanwhile shows on this frame
		ArtLoader_pump(0);
		// a plugin's art in the thumbnail's place goes once the selection moves
		Plugins_artReplaceCheck();
		// once the selection settles, before it's drawn
		Plugins_selection(now);

		if(state.dirty) {
			// what's on screen now, before it's cleared, for plugins' captures
			Plugins_frameBegin();
			tmpOldScreen = NULL;
			switcherSur = NULL;
			// NOTE:22 This causes slowdown when CFG_getMenuTransitions is set to false because animationdirection turns > 0 somewhere but is never set back to 0 and so this code runs on every action, will fix later
			if(state.animationdirection != ANIM_NONE || (lastScreen==SCREEN_GAMELIST && currentScreen == SCREEN_GAMESWITCHER)) {
				if(tmpOldScreen) SDL_FreeSurface(tmpOldScreen);
				tmpOldScreen = GFX_captureRendererToSurface();
				SDL_SetSurfaceBlendMode(tmpOldScreen,SDL_BLENDMODE_BLEND);
				Transition_keepSelection();
			}
			// the launch slot's owner starts from what's on screen
			if (state.startgame && !Screen_get(currentScreen)->rendersWhileLaunching && Plugins_hasTransition(MOSAIC_TRANSITION_LAUNCH))
				Transition_captureLaunch();

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

			// a plugin screen can take the whole screen, header and all
			if (currentScreen == SCREEN_PLUGIN && (Plugins_screenFlags() & MOSAIC_SCREEN_FULL)) {
				ow = 0;
				header_w = 0;
			}
			else {
				ow = GFX_blitHardwareGroup(screen, show_setting);
				// shell paks' status pill, then plugins' header widgets, beside it
				HeaderRow header;
				HeaderRow_init(&header, screen->w, SCALE1(PADDING), SCALE1(BUTTON_MARGIN), ow, SCALE1(PILL_SIZE));
				StatusPill_place(screen, &header);
				Plugins_header(screen, &header);
				header_w = HeaderRow_width(&header);
			}
			const Screen* active = Screen_get(currentScreen);
			if (state.startgame && !active->rendersWhileLaunching) LaunchFade_render();
			else {
				if (active->update) active->update();
				active->render();
				Toast_render();
			}

			if(state.animationdirection != ANIM_NONE) {
				int kind = state.animationdirection == SLIDE_LEFT ? MOSAIC_TRANSITION_PUSH : MOSAIC_TRANSITION_POP;
				if(CFG_getMenuTransitions() || Plugins_hasTransition(kind)) Transition_slide(tmpOldScreen, state.animationdirection);
				// animation done
				state.animationdirection = ANIM_NONE;
			}

			if(lastScreen == SCREEN_QUICKMENU) {
				SDL_LockMutex(bgMutex);
				if(folderbgchanged && Background_layer()) folderbgchanged = 0;
				SDL_UnlockMutex(bgMutex);
			}
			else if(lastScreen == SCREEN_GAMELIST) {
				SDL_LockMutex(bgMutex);
				if(folderbgchanged && Background_layer()) folderbgchanged = 0;
				SDL_UnlockMutex(bgMutex);
				SDL_LockMutex(thumbMutex);
				if(list_layout_drawn) {
					// a plugin layout draws its own art
					GFX_clearLayers(LAYER_THUMBNAIL);
				} else if(thumbchanged) {
					showThumb();
				}
				SDL_UnlockMutex(thumbMutex);

				GFX_clearLayers(LAYER_TRANSITION);
				GFX_clearLayers(LAYER_SCROLLTEXT);

				SDL_LockMutex(animMutex);
				if (list_show_selected_name && !list_layout_drawn) {
					GFX_drawOnLayer(globalpill, pillRect.x, pillRect.y, globallpillW, globalpill->h, 1.0f, 0, LAYER_TRANSITION);
					GFX_drawOnLayer(globalText, SCALE1(BUTTON_MARGIN + BUTTON_PADDING), pilltargetTextY, globalText->w, globalText->h, 1.0f, 0, LAYER_SCROLLTEXT);
				}
				SDL_UnlockMutex(animMutex);
			}
			if(!state.startgame) { // dont flip if game gonna start
				Overlays_render();
				GFX_flip(screen);
				Plugins_frameShown();
			}
			Plugins_started(); // a frame is up, so plugins survived starting

			state.dirty = 0;
		} else if(getAnimationDraw() || folderbgchanged || thumbchanged || is_scrolling) {
			// honestly this whole thing is here only for the scrolling text, I set it now to run this at 30fps which is enough for scrolling text, should move this to seperate animation function eventually
			Uint32 now = SDL_GetTicks();
			Uint32 frame_start = now;
			static char cached_display_name[256] = "";
			// a plugin screen has none of the game list's layers; they wait for it
			int on_plugin = currentScreen == SCREEN_PLUGIN;
			SDL_LockMutex(bgMutex);
			if(!on_plugin && folderbgchanged && Background_layer()) folderbgchanged = 0;
			SDL_UnlockMutex(bgMutex);
			SDL_LockMutex(thumbMutex);
			if(on_plugin) {
				// kept changed for the game list
			} else if(list_layout_drawn && thumbchanged) {
				GFX_clearLayers(LAYER_THUMBNAIL);
				thumbchanged = 0;
			} else if(thumbchanged) {
				showThumb();
				thumbchanged = 0;
			}
			SDL_UnlockMutex(thumbMutex);
			SDL_LockMutex(animMutex);
			if (!on_plugin && getAnimationDraw()) {
				GFX_clearLayers(LAYER_TRANSITION);
				if (list_show_selected_name && !list_layout_drawn)
					GFX_drawOnLayer(globalpill, pillRect.x, pillRect.y, globallpillW, globalpill->h, 1.0f, 0, LAYER_TRANSITION);
				setAnimationDraw(0);
			}
			SDL_UnlockMutex(animMutex);
			if (currentScreen != SCREEN_GAMESWITCHER && currentScreen != SCREEN_QUICKMENU && currentScreen != SCREEN_MODAL && !on_plugin) {
				if(is_scrolling && pillanimdone && currentAnimQueueSize < 1) {
					int ow = GFX_blitHardwareGroup(screen, show_setting);
					Entry* entry = state.top->entries->items[state.top->selected];
					trimSortingMeta(&entry->name);
					char* entry_text = entry->name;
					if (entry->unique) {
						trimSortingMeta(&entry->unique);
						entry_text = entry->unique;
					}

					int available_width = (had_thumb ? ox + SCALE1(BUTTON_MARGIN) : screen->w - SCALE1(BUTTON_MARGIN)) - SCALE1(PADDING * 2);
					if (state.top->selected == state.top->start && !had_thumb) available_width -= ow;
					if (state.top->selected == state.top->start)
						available_width = HeaderRow_topRowWidth(available_width, screen->w, SCALE1(PADDING), SCALE1(BUTTON_MARGIN), ow, header_w);

					SDL_Color text_color = uintToColour(THEME_COLOR5_255);

					int text_width = GFX_getTextWidth(font.large, entry_text, cached_display_name, available_width, SCALE1(BUTTON_PADDING * 2));
					int max_width = MIN(available_width, text_width);
					int text_offset_y = (SCALE1(PILL_SIZE) - TTF_FontHeight(font.large) + 1) >> 1;

					GFX_clearLayers(LAYER_SCROLLTEXT);
					if (list_show_selected_name) {
						GFX_scrollTextTexture(
							font.large,
							entry_text,
							SCALE1(BUTTON_MARGIN + BUTTON_PADDING), SCALE1(PADDING + state.previous_row * PILL_SIZE) + text_offset_y,
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
					if (list_show_selected_name && !list_layout_drawn) {
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
			state.dirty = 0;
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
		assert(state.animationdirection == ANIM_NONE);

		// handle HDMI change
		static int had_hdmi = -1;
		int has_hdmi = GetHDMI();
		if (had_hdmi==-1) had_hdmi = has_hdmi;
		if (has_hdmi!=had_hdmi) {
			had_hdmi = has_hdmi;

			// an empty folder has no entry, so the folder itself
			char* last = state.top->entries->count > 0 ? ((Entry*)state.top->entries->items[state.top->selected])->path : state.top->path;
			LOG_info("restarting after HDMI change... (%s)\n", last);
			saveLast(last); // NOTE: doesn't work in Recents (by design)
			Plugins_homeLeaving(); // and back to a plugin's home screen, if it showed
			sleep(1); // cable-seating debounce; the output switch itself happens at next app start
			state.quit = 1;
		}
	}

	Plugins_started(); // a clean exit isn't a crash either
	Plugins_quit();
	Menu_quit();
	PWR_quit();
	PAD_quit();

	// Cleanup worker threads and their synchronization primitives
	cleanupImageLoaderPool();

	GFX_quit(); // Cleanup video subsystem first to stop GPU threads

	// Now safe to free surfaces after GPU threads are stopped
	if(blackBG)	SDL_FreeSurface(blackBG);
	if (folderbgbmp) SDL_FreeSurface(folderbgbmp);
	Background_quit();
	ArtLoader_quit(); // thumbbmp is the art cache's
	if (swap_copy) SDL_FreeSurface(swap_copy);
	Overlays_quit();

	QuitSettings();
}
