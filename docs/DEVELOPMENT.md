# Development

Notes for working on mosaic-launcher.

## How it takes over

Stock `MinUI.pak/launch.sh` runs `$SYSTEM_PATH/bin/nextui.elf` in a loop. Turning Launcher on installs a boot hook (`.userdata/<platform>/.hooks/boot.d/00-launcher.sync.sh`) that runs `bin/boot.sh` on every boot, and runs it once right away.

`boot.sh` bind-mounts the stock binary to `/tmp/mosaic-launcher/nextui.stock.elf`, then bind-mounts a small wrapper over `nextui.elf`. Nothing in `.system/` is modified, so NextUI updates never conflict. On devices that refuse file bind mounts it falls back to backing up the stock binary as `nextui.stock.elf` and copying the wrapper in place.

Each time `launch.sh` starts `nextui.elf`, the wrapper picks a binary. It runs stock when the launcher is off, when the pak has been uninstalled, or after our launcher crashes twice in a row. Otherwise it runs this pak's launcher. Shutdown signals (SIGTERM, SIGKILL) don't count as crashes, and any clean exit resets the count. Choosing **Try again** in the Launcher screen clears the crash guard.

The pak only activates on the NextUI release it was built against. It compares the build hash in `.system/version.txt` with `supported-versions.txt`, and leaves stock in place on any other release.

State lives in `.userdata/<platform>/launcher/`, which replacing or deleting the pak never touches.

## Building

The NextUI source is a submodule pinned to a release tag:

```shell
git submodule update --init
```

Two more submodules hold the third-party code the launcher hashes games with for RetroAchievements progress, pinned to the commits NextUI's minarch builds: `vendor/rcheevos` (its `src/rhash`, MIT) and `vendor/libchdr` (BSD 3-Clause, with its bundled LZMA SDK, miniz, Zstandard and dr_flac). minarch clones them at build time, rcheevos at a fixed commit and libchdr at whatever its default branch is then; `vendor/libchdr` is pinned to the commit that was current when the pinned NextUI release was made. `launcher/vendor.mk` lists the sources and options and compiles them with flags of their own (not the launcher's, and with their warnings off) into objects the launcher links, for every platform alike; libchdr's CMake build isn't used, so no toolchain needs CMake. When NextUI moves to new commits, move the submodules with it. Their licences are in `THIRD_PARTY_NOTICES.txt`, which the pak carries as `licenses/third-party.txt`.

The bundled `romm` plugin speaks HTTPS with BearSSL, its sources copied unchanged into `vendor/BearSSL` from upstream's master at 7bea48e (2026-04-06), 39 commits of fixes after its last release, v0.6, with the upstream URL and commit in `vendor/BearSSL/VERSION.txt` (MIT). It isn't a submodule: bearssl.org serves only branch and tag tips, which CI's shallow submodule fetch can't pin. `plugins/romm/plugin.mk`, which `plugins/makefile` includes like any plugin's `plugin.mk`, compiles it with flags of its own (its symbols hidden) into `build/bearssl/<arch>/libbearssl.a` that only romm's `plugin.so` links, and copies its licence beside the plugin; `tests/c/romm.mk` builds it the same way for the unit test. It unpacks games RomM serves as a zip with miniz, copied unchanged into `vendor/miniz` from its 3.1.2 release (MIT, the release in `vendor/miniz/VERSION.txt`), compiled the same way (read-only, without zlib's names, its symbols hidden) into `build/miniz/<arch>/miniz.o`, through `plugins/romm/romm_zip.c`, which checks every entry with `romm_model.c` before writing anything (unit tested with hand-made malicious zips, and fuzzed with a throwaway libFuzzer harness while it was written).

The bundled `music` plugin's daemon, `musicd`, is a program of its own beside the plugin's `plugin.so`. `plugins/music/plugin.mk` builds it from `plugins/music/daemon/` (which the plugins' own rule doesn't compile) and the plugin's `music_model.c`, linking SDL2 from `pkg-config` as the launcher does (the toolchain's on devices), and decodes with dr_mp3, dr_flac and dr_wav, copied unchanged into `vendor/dr_libs` from upstream's master at b55a0d9 (2026-08-20), with the URL and commit in `vendor/dr_libs/VERSION.txt` (public domain or MIT No Attribution). Their code is compiled on its own with its warnings off into `build/music/<arch>/`, and their licence goes beside the daemon. The plugin starts the daemon without the launcher's `LD_PRELOAD` or `DYLD_INSERT_LIBRARIES`, so the tests' presser isn't loaded into it. Its desktop case runs it with `SDL_AUDIODRIVER=dummy` and stops any left running by the pid in `/tmp/mosaic-music.pid`. The plugin reads tags with `plugins/music/music_tags.c` (unit tested, and fuzzed with a throwaway libFuzzer harness while it was written) on a thread of its own, built with `-pthread` from its `plugin.mk`; the case makes a tagged MP3 with an embedded cover with ffmpeg when it's there, and a folder `cover.png` with Python, and removes `/tmp/mosaic-music-art`.

Plugins' helper programs, such as `musicd` and `second-screen`'s `second-screen-display`, are programs of their own that a plugin starts, for work outside the launcher's process (PLUGINS.md's "Helper programs" has the rules they follow). A plugin's `plugin.mk` builds them for the `ARCH` being built and lists them, with any licence that goes with them, in its target's `PLUGIN_SHIP`, which `plugins/makefile` copies into `<name>.plugin/lib/<arch>/` beside `plugin.so`; the plugin finds them there with `dladdr`. They can link libraries the plugin itself doesn't, as both link SDL2. `plugins/second-screen/plugin.mk` builds `second-screen-display` from `display/` only on desktop and only with SDL2 from `pkg-config`, leaving `PLUGIN_SHIP` empty otherwise. `template/` builds an example helper the same way from its `helper/` folder, when there is one, into each `lib/<arch>/`, and its `make pak` packs it beside `plugin.so`; `tests/plugins/cases/270-template.sh` builds copies of the template with and without `helper/`, checks that the helper answers, logs to its file and quits once the launcher stops, and, with `zip` and `unzip` installed, that each pak has the helper only when it was built.

Device builds run in the LoveRetro toolchain images, through Docker or podman:

```shell
make build-tg5040
make devices          # tg5040, tg5050 and h700
make pak              # build/Launcher.pak.zip from whatever is built
```

`make devices` also runs `make ffmpeg-device`, which builds the trimmed LGPL ffmpeg the `video-previews` plugin loads, in the tg5040 toolchain, before `make plugins-device` puts its three libraries and licence beside the plugin's `plugin.so`. `plugins/video-previews/ffmpeg/build.sh` downloads ffmpeg 6.1.2's release tarball into `build/ffmpeg/aarch64/`, checks it against a pinned SHA-256 and builds it with only what preview clips need (about 2.6 MB of libraries). It's stamped with the script's hash, so it runs once (a few minutes) and then does nothing until the script changes; `rm -rf build/ffmpeg` builds it again. Plugins built without it, by `make plugins-device` on its own or in CI, get a `video-previews` that says it's unavailable. On desktop the plugin builds against the system's ffmpeg through `pkg-config` (`brew install ffmpeg`, or `libavformat-dev libavcodec-dev libavutil-dev`) and loads it from that libdir, and builds without it when there's none. `make test-plugins` makes its clips with the `ffmpeg` command and skips the cases that play them without it.

The desktop build runs natively on macOS (Homebrew) or Linux:

```shell
brew install gcc make sdl2_image sdl2_ttf libsamplerate sqlite
make desktop
make run-desktop      # uses upstream's fake SD card at /var/tmp/nextui/sdcard
```

On macOS, upstream symlinks Homebrew gcc into `/usr/local/bin` with sudo. This project keeps those symlinks in `build/host-bin/` instead.

## Tests

```shell
brew install bats-core shellcheck gettext
make lint
make test
```

The bats tests run the pak scripts against a fake SD card, with a fake `mount` that copies files the way a bind mount looks from outside. The C tests in `tests/c/` cover launcher code that doesn't need a display; they compile against the desktop platform headers, so they need SDL2's headers (`brew install sdl2`, or `libsdl2-dev`) but never open a window. `tests/c/makefile` builds every `test_<name>.c`; a test that needs more than its own source and the SDK header, such as a plugin's model file, the launcher's sources or extra flags, has its rule in `tests/c/<name>.mk`, which the makefile includes by wildcard.

`make test-plugins` (`tests/plugins/run.sh`) loads test plugins into the desktop build. `run.sh` holds the helpers, the setup and the teardown, and runs the cases in `tests/plugins/cases/*.sh` in name order; each case builds the test and bundled plugins it uses. A new plugin's cases go in a file of their own, numbered after the last case and before `990-crash.sh`, which leaves plugins paused and so runs last. Each case file has a `# shard: <n>` line: with `SHARD=<i>/<n>` set, `run.sh` runs only the cases of shard i (and `010-hello.sh`, marked `all`, whose logs later cases check against), and with no `SHARD` it runs them all. Each case prints how long it took, so pick the shard with the least time. Cases that use what an earlier one left share its shard, and say so under their shard line: `190-last-path.sh` follows the last path `080-drawer.sh` saved, `300-achiever.sh` reads `290-tracker.sh`'s log, and `510-screens.sh` uses the presser `500-plugins-page.sh` builds.

Each bundled plugin's row in PLUGINS.md's "Bundled plugins" table comes from its `plugins/<name>/README.md`: a `# <name>` heading, a blank line, then the row's text. `./scripts/plugin-table.sh` rewrites the table from them in name order, and `make lint` fails when it's out of date.

`po/launcher.pot` is the gettext template of the launcher's strings, for the strings slot's owner to translate. `make pot` (`scripts/pot.sh`) writes it with xgettext from every literal passed to `TR()`, `TRC()` (with a context), `TRN()` and `TRCN()` (plurals), or marked with `TR_MARK()` (English kept in data, such as an action's label, translated where it's drawn), keeping xgettext's `c-format` flags. `make lint` fails when the committed one is out of date, comparing entries without their locations, which gettext releases wrap differently. It needs gettext's xgettext (`brew install gettext`, or `gettext` on Linux, which CI's lint job installs). New UI text goes through `TR()` where it's drawn, keyed on its English; text that's a printf format gets the same conversions in every translation the launcher uses, so it stays safe to pass to printf.

`make smoke` starts the desktop build for a few seconds, then its settings screens, and fails if either exits on its own. A run still going 5 seconds after SIGTERM gets SIGKILL and fails too, so a launcher that won't stop can't hold up the run. Its logs (`build/smoke.log` and `build/smoke-settings.log`) are also handy for comparing startup output before and after a change.

`make test-profiles` does the same with the bundled plugins together, in profiles built from their `plugin.txt` files: everything on with the first owner of each exclusive slot (`all`), then each slot given its next owner (`alternate`), and so on (`alternate-2`, ...). Profile N gives each slot its owner N mod the number of owners, and as many profiles run as the largest slot has owners, so every owner is loaded once. Each profile prints the owners it picks. Plugins whose `requires` can't be met are left off with a note. It fails if the launcher exits on its own or the loader refuses a plugin the profile didn't turn off; logs are in `build/profiles/`.

CI runs `make lint`, `make test` and `make test-profiles` in one job, and `make test-plugins` in three more, one per shard (the first also runs `make smoke`), all on Linux under `xvfb-run`. The shards install ffmpeg and sqlite3 so the video preview and play history cases run, and each keeps its logs as the `desktop-logs-<shard>` artifact when it fails; the first job keeps `build/profiles/` as `profile-logs`.

`scripts/desktop-smoke.sh` can also wait for log lines instead of a fixed time: with `SMOKE_UNTIL` set to fixed strings, one per line, the seconds are the most it waits, and it stops once the log has every line and the file named by `SMOKE_UNTIL_GONE` (if set) is gone, then `SMOKE_SETTLE` more seconds (1 by default) pass. It fails with exit 3 if that doesn't happen in time. With `SMOKE_STACKS` set too, it first writes every thread's stack to `<log>.stacks` with gdb. CI sets `SMOKE_STACKS` for smoke, the plugin tests and the profiles, with gdb installed and allowed to attach.

With `SMOKE_STACKS` set, a run still going 5 seconds after SIGTERM also has its process tree (the launcher and every process under it, with their command lines) and every thread's stack added to the end of its log, after a `--- still running 5s after SIGTERM` line, before the SIGKILL. It still fails, with no retry. `make test-profiles` and `tests/plugins/run.sh` print the process tree and the main thread's stack from there, and the profiles also say whether the first frame was up (the loading marker was gone) and print the log's last lines. A test-profiles `all` run, the check job's first launcher start, once didn't stop on SIGTERM (#260).

In `tests/plugins/run.sh`, a start that logged every line it waited for but never drew its first frame (the loading marker is still there) is started once more, with a warning, its log kept as `<log>-stalled.log` and its main thread's stack printed. That stall was seen once in CI, on a job's first start under `xvfb-run`, and not in about 2,000 starts since, locally or in CI (#233); a second stall in a row fails the case.

## Updating NextUI

The launcher in `launcher/` started as upstream's `nextui.c`, split into modules:

| File | What it holds |
|---|---|
| `main.c` | startup and the main loop, including drawing the thumbnail layer, a plugin's `art_replace` surface in place of the thumbnail included |
| `state.c` | `LauncherState`, the launcher-wide state shared between modules |
| `containers.c` | `Array`, `Hash` and `IntArray` |
| `library.c` | entries, directories, recents, and the lists built from the card |
| `launch.c` | handing off to games and paks, resume state, FN buttons |
| `nav.c` | the directory stack, restoring the last position, opening entries |
| `imgloader.c` | the background image, art cache decode and selection pill worker threads |
| `screens.c` | the screen registry `main()` dispatches through, and state shared with it |
| `screen_gamelist.c` | game list input and rendering, handing both to the layout slot's owner when there is one and running plugins' row decorators on the built-in list's rows, plus the fade shown when launching |
| `screen_switcher.c` | game switcher input and rendering |
| `screen_quickmenu.c` | quick menu input and rendering |
| `screen_plugin.c` | plugins' screens, lent the screen under the header `main()` drew, the stock button hints they draw with `draw_hints`, and plugins' overlays, drawn on the topmost layer before each redrawn frame is shown |
| `hintmodel.c` | the SDL-free half of `draw_hints`: checking a plugin screen's hints, the names stock draws for their buttons, and where stock puts the pill |
| `screenmodel.c` | the SDL-free half of plugins' screens: the screens registered, the stack of those open over a built-in screen, which screens slide and take overlays, the home slot's rules for where the launcher starts and goes back to, and the switcher slot's screen standing in for the game switcher |
| `art.c` | where each kind of image (thumbnails, backgrounds, quick menu art) is looked up |
| `quickitems.c` | the quick menu's pak shortcuts and toggles: which appear, their icons, and what they do |
| `quicktiles.c` | plugins' quick menu tiles, and the tiles plugins hide or relabel |
| `tr.c` | `TR()` and its context and plural forms, which every piece of fixed UI text passes through where it's drawn, asking the strings slot's owner once for each string and keeping its answers |
| `trmodel.c` | the SDL-free half of the strings slot: whether a translation is safe to draw and to pass to printf (UTF-8, length, lines, the same printf conversions as its English), English's plural rule, and the cache of answers |
| `text.c` | stock's text truncation and hardware hints made safe for translations: truncating a character at a time so a UTF-8 character is never cut, and the brightness and colour temperature hints through `TR()` |
| `actions.c` | named actions, the button bindings that trigger them, and the hints generated from those bindings |
| `transitions.c` | the folder slide and the game switcher's entrance animations, and handing those and the launch to the transition and launch slots' owners, drawing their frames |
| `pluginset.c` | which discovered plugins load, and in what order |
| `pluginorder.c` | the Launcher pak's Plugins page order: plugins grouped by the exclusive slot they claim, then the rest, each group A-Z |
| `plugins.c` | finding and loading plugins, the host table they call, chained hooks such as row decorators, header widgets and overlays, selection events, `art_replace` and `art_replacement`, opening and closing plugins' screens, opening the switcher slot's screen wherever the game switcher opens, and the crash guard |
| `toast.c` | short messages shown over the current screen |
| `awake.c` | stay-awake files and plugin requests that keep the device from sleeping |
| `pakhooks.c` | `pre-launch.sh` scripts next to a pak's `launch.sh` |
| `kbmodel.c` | the on-screen keyboard's layout and typing, apart from drawing |
| `modal.c` | the confirm, message, keyboard and choice list dialogs |
| `cfgfile.c` | reading and writing a plugin's `key=value` settings file |
| `settings.c` | the Launcher pak's screens, shown by `nextui.elf --settings`, and asking for the PIN before protected plugin settings |
| `pin.c` | the launcher's PIN file (`pin.txt` beside `plugins.txt`): reading, checking and saving it, and taking over simple mode's old one |
| `listsort.c` | sorting a list by plugins' chained `sort_compare` hooks |
| `launchcmd.c` | checking and quoting the command a plugin replaces a launch with |
| `stocktoggles.c` | the stock settings plugins can read and set, through `CFG_get*` and `CFG_set*` |
| `leds.c` | a plugin's LED colour, as stock's ambient LED override, and the theme colours |
| `bluetooth.c` | plugins' Bluetooth: the worker thread that looks for the connected device and runs connects, each a bounded `fork` and `execvp` |
| `wifi.c` | plugins' Wi-Fi state: the worker thread that asks stock's connection call every few seconds while plugins keep asking and keeps its last answer, and the desktop tests' stand-in file named by `MOSAIC_LAUNCHER_WIFI_FAKE` |
| `drawmodel.c` | the SDL-free half of plugins' drawing calls: which surfaces exist and which plugin owns each, with its memory budget, handing a plugin's surface to the launcher, text truncation (plugins' and, for the launcher's own text, stock's a character at a time rather than a byte), rounded corners, fitting, and clipping the pixels a plugin writes to its surface |
| `plugindraw.c` | plugins' drawing calls over SDL and stock's fonts, and lending a surface such as the screen to a render hook, clipped to the area it may draw in |
| `layoutmodel.c` | the SDL-free half of the layout slot: whether the owner's layout is still on after its init and each call, the buttons it sees and consumes, and the area it draws in; also the built-in list's row rects for row decorators and where the selection is for transitions, how header widgets are laid out beside the status pill and what they take off the top row, and whether the background slot's owner or stock's background shows, and which plugin's `art_replace` surface shows, with the rounded corners stock gives a thumbnail, and what a change to it draws again on the built-in list or under a layout |
| `artcache.c` | the SDL-free half of the art cache: decoded images by path and kind, least recently used out first past a byte cap, pins, the worker's load order with prefetches last, requests dropped by sequence number once the selection moves, and plugins' generated art put in under keys of its own |
| `selmodel.c` | the SDL-free half of selection events: seeing the selection move (the art cache uses it too), and when it has settled on an entry plugins haven't heard about |
| `artloader.c` | the art cache over SDL: decoding on the art worker, taking images in and evicting at the start of each frame, the built-in list's thumbnail and its neighbours, plugins' `art_find`, `art_get` and `art_load`, and asking plugins' `art_surface` for generated art when no file answers |
| `background.c` | the game list's background layer: stock's folder background, or what the background slot's owner draws over it before the list draws, which folder slides capture too |
| `statusmodel.c` | the SDL-free half of the status pill: parsing shell paks' status files, tracking the files in `status.d` between polls, which are stale, and the one the pill shows |
| `statuspill.c` | the status pill over the card and SDL: looking at `status.d` once a second without following links, and drawing the pill through the header row before plugins' header widgets |
| `infomodel.c` | parsing `.info` metadata sidecars and where an entry's is; `plugins.c` reads one for `entry_info` and keeps it with the entry |
| `playmodel.c` | the SDL-free half of the play history: the games read from `game_logs.sqlite` by path (and Onion's orphans by name), today's total from the sessions in local time, the whole history's list of games, most played first, and any local day's total from every session sorted by start |
| `playhistory.c` | the play history over sqlite, loaded with `dlopen` the first time a plugin asks: opening `game_logs.sqlite` read-only with a busy timeout, reading it in one go (and every session in a read of its own, the first time a plugin asks for a day), up to its caps on games and sessions, and keeping the answers until a game returns |
| `transmodel.c` | the SDL-free half of transition styles: which slot's owner is asked about each kind and when an error turns it off, checking its choice, the duration cap, easing, and stepping through frames by the clock |
| `inputmodel.c` | the SDL-free half of input filters: taking what plugins' `input_filter` consumed off the pad, and a power press they took still powering off once it's held as long as stock's, unless it came in the first second after the launcher started or woke, which stock ignores, while a release whose press got through always gets through |
| `audiomodel.c` | the SDL-free half of plugins' audio: which plugin has the device and in what format, whether an open keeps, reopens or is refused, the queue's cap, and how long a sleep waits for what's queued to play out |
| `audio.c` | plugins' audio over SDL's audio queue: starting SDL's audio on the first open and quitting it when the device closes, a mutex so plugins' threads can queue and count while the main thread opens and closes, and the launcher closing the device before a sleep, a launch, powering off and quitting |
| `theme.c` | the theme slot over stock's look: the owner's colours, font, fallback font, text step, UI sizes and wallpapers, kept in memory only, the runtime sizes `uisizes.h` asks for, and the hooks the launcher's build of `api.c` calls for the font and colours |
| `thememodel.c` | the SDL-free half of the theme slot: which of the owner's values are refused, the padding and rows worked out from its pill size the way stock's comments work out its own, the contrast clamp, checking that a path is a regular file on the card, and which wallpaper fills which spot |
| `uisizes.h` | included ahead of every file of `nextui.elf`: turns stock's `PILL_SIZE`, `BUTTON_SIZE`, `BUTTON_MARGIN`, `PADDING`, `MAIN_ROW_COUNT` and `FONT_*` into calls into `theme.c`, which answer stock's own values until a theme sets one |
| `fallback.c` | the fallback font: opening it at stock's five sizes, a cache of which glyphs each font has, and SDL_ttf's measuring and drawing done run by run (each run in the UI font or the fallback font, baselines lined up, at the UI font's height), or SDL_ttf's own call when there's no fallback font or the UI font has every glyph |
| `fallbackmodel.c` | the SDL-free half of the fallback font: decoding UTF-8, splitting text into runs by font (combining marks stay with the character before them), their widths, and where wrapped text breaks |
| `fallbackttf.h` | included ahead of every file of `nextui.elf`, before `uisizes.h`: renames `TTF_SizeUTF8`, `TTF_RenderUTF8_Blended` and `TTF_RenderUTF8_Blended_Wrapped` to `fallback.c`'s, so stock's `api.c`, the platform's scrolling text, the launcher and plugins' `draw_text` all use the fallback font |
| `smooth.c` | pill ends and middles, button circles, input prompts and the status bar's icons from stock's sprite sheets, smooth-scaled to a theme's pill or button size once each and kept until the theme changes, for the launcher's build of `api.c` |
| `scalemodel.c` | the SDL-free half of `smooth.c`: scaling ARGB pixels axis by axis, averaging the pixels covered when shrinking and bilinear when growing, with colours weighted by alpha, an exact copy at the same size |
| `patches/api.c.patch` | the launcher's changes to stock's `api.c` (see below) |

See [PLUGINS.md](../PLUGINS.md) for writing plugins. `make test-plugins` loads test plugins (one that works, one with the wrong API version, one that crashes) into the desktop build. `MOSAIC_LAUNCHER_ART_CACHE_BYTES` replaces the art cache's 32 MiB cap, which the eviction case uses to fill a small one. `MOSAIC_LAUNCHER_SQLITE` names the sqlite library the play history loads in place of the system's, which the cases without one point at a file that isn't there. Each case waits up to `PLUGIN_TEST_SECONDS` (10 by default) for the log lines it checks and its first frame, and stops as soon as they're in. Before each case it clears the crash guard's state, so one slow or failed case can't start the later ones with plugins paused; only the crash checks at the end carry it from one start to the next. A failure names the case and its log. The Launcher pak's Plugins page cases run `nextui.elf --settings plugins` with `tests/plugins/presser`, a library (not a plugin) that `desktop-smoke.sh` preloads when `SMOKE_PRELOAD` names it (`LD_PRELOAD`, or `DYLD_INSERT_LIBRARIES` on macOS): it presses the buttons in `PRESSER_KEYS` through `SDL_PollEvent` and adds each text SDL_ttf draws to `PRESSER_LOG`, so a case can check what the screen showed, such as a secret setting's value never being drawn. With `PRESSER_FRAME` set it also saves the last frame presented before it's done, as a BMP and the texts drawn for it with their colours, which the long Plugins page case reads to check which rows are on screen and that the selected one has its pill. NextUI's `MIN` and `MAX` have no outer parentheses, so use them only as a whole expression, never inside a comparison or sum. The audio cases run with `SDL_AUDIODRIVER=dummy`, SDL's driver that plays nothing but takes queued audio at the device's rate, so a queue falls as it would on a device. SDL2's own dummy driver (Linux) opens only one device at a time, while Homebrew's SDL2 is sdl2-compat over SDL3, whose dummy driver opens any number; so on Linux a plugin's reopen after a sleep is refused once stock has opened its own device, and a separate case with no sleep checks the closes before a launch and at quit. The two also blend differently: a translucent fill over an opaque pixel leaves alpha 254 with SDL2 and 255 with SDL3. Pass a call that changes state and a call that reads it back as separate statements, not as arguments to the same call: gcc evaluates arguments right to left, clang left to right.

FN buttons can be bound to a launcher action as well as a pak, with `action:<id>` (eg. `action:gamelist.back`) in place of `pak:<name>`.

Upstream changes have to be ported forward by hand. To see what changed upstream since the pinned tag:

```shell
make drift                        # pinned tag -> latest tag
make drift FROM=v6.15.0 TO=v6.16.0
```

Then check out the new tag in `vendor/NextUI`, port the launcher changes, and rebuild. `supported-versions.txt` follows the submodule automatically.

The launcher builds stock's `api.c` with a patch, `launcher/patches/api.c.patch`, which `launcher/makefile` applies to the vendored file into `build/<platform>/api.c`; the vendored tree is never changed, so minarch and the Settings pak stay stock's. With `launcher/uisizes.h`, which the makefile includes ahead of every file of `nextui.elf` (`-include`), it lets the theme slot's owner change the UI: `uisizes.h` turns stock's compile-time sizes into calls whose answers are stock's own until a theme sets one, and the patch keeps the input prompt sheet's cells at stock's button size, draws pill ends and middles, button circles and input prompts at a theme's pill or button size, and the status bar's icons (the battery with its fill and bolt, Wi-Fi, Bluetooth, audio, and the volume and brightness indicator) scaled by its pill size over stock's, with the gaps inside the battery, when it isn't stock's, from `smooth.c`'s smooth-scaled copies of stock's sprite sheets (made once per size and dropped in `GFX_themeChanged`, so a frame only looks them up; at stock's sizes they're drawn from the sheet as stock does), and reads the font path and the seven colours through `theme.c`, so a theme's are used without writing stock's settings. It also tells `fallback.c` when stock's five fonts are opened and closed, so the fallback font is opened at the same sizes. The fallback font's measuring and drawing don't need the patch: `launcher/fallbackttf.h`, included the same way, sends every file's SDL_ttf text calls through `fallback.c`, including the platform's own (`generic_video.c`'s scrolling text), which a patch to `api.c` alone wouldn't reach. It's a patch rather than a copy of `api.c` so upstream's fixes to the rest of the file come along with each bump: `make drift` shows whether it still applies to the new tag, and upstream's changes to `api.c` and `defines.h`, and a build fails when it doesn't. Port it by editing a patched copy and writing the patch again with `diff -u`, keeping it small and each change marked `mosaic`.

## The Launcher screen

The Launcher pak's screens are drawn by the launcher binary itself: `launch.sh` runs `nextui.elf --settings`, or `--settings plugins` to open the plugin list, so they look and move like the game list. Anything that mounts, unmounts or edits the boot hook goes through `bin/manage.sh` (`turn-on`, `turn-off`, `try-again`, `status`), which also works over SSH.

