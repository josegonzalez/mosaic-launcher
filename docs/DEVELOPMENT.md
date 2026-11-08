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

Device builds run in the LoveRetro toolchain images, through Docker or podman:

```shell
make build-tg5040
make devices          # tg5040, tg5050 and h700
make pak              # build/Launcher.pak.zip from whatever is built
```

The desktop build runs natively on macOS (Homebrew) or Linux:

```shell
brew install gcc make sdl2_image sdl2_ttf libsamplerate sqlite
make desktop
make run-desktop      # uses upstream's fake SD card at /var/tmp/nextui/sdcard
```

On macOS, upstream symlinks Homebrew gcc into `/usr/local/bin` with sudo. This project keeps those symlinks in `build/host-bin/` instead.

## Tests

```shell
brew install bats-core shellcheck
make lint
make test
```

The bats tests run the pak scripts against a fake SD card, with a fake `mount` that copies files the way a bind mount looks from outside. The C tests in `tests/c/` cover launcher code that doesn't need a display; they compile against the desktop platform headers, so they need SDL2's headers (`brew install sdl2`, or `libsdl2-dev`) but never open a window.

`make smoke` starts the desktop build for a few seconds, then its settings screens, and fails if either exits on its own. A run still going 5 seconds after SIGTERM gets SIGKILL and fails too, so a launcher that won't stop can't hold up the run. Its logs (`build/smoke.log` and `build/smoke-settings.log`) are also handy for comparing startup output before and after a change.

`make test-profiles` does the same with the bundled plugins together, in profiles built from their `plugin.txt` files: everything on with the first owner of each exclusive slot (`all`), then each slot given its next owner (`alternate`), and so on (`alternate-2`, ...). Profile N gives each slot its owner N mod the number of owners, and as many profiles run as the largest slot has owners, so every owner is loaded once. Each profile prints the owners it picks. Plugins whose `requires` can't be met are left off with a note. It fails if the launcher exits on its own or the loader refuses a plugin the profile didn't turn off; logs are in `build/profiles/`.

`scripts/desktop-smoke.sh` can also wait for log lines instead of a fixed time: with `SMOKE_UNTIL` set to fixed strings, one per line, the seconds are the most it waits, and it stops once the log has every line and the file named by `SMOKE_UNTIL_GONE` (if set) is gone, then `SMOKE_SETTLE` more seconds (1 by default) pass. It fails with exit 3 if that doesn't happen in time.

## Updating NextUI

The launcher in `launcher/` started as upstream's `nextui.c`, split into modules:

| File | What it holds |
|---|---|
| `main.c` | startup and the main loop |
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
| `art.c` | where each kind of image (thumbnails, backgrounds, quick menu art) is looked up |
| `quickitems.c` | the quick menu's pak shortcuts and toggles: which appear, their icons, and what they do |
| `quicktiles.c` | plugins' quick menu tiles, and the tiles plugins hide or relabel |
| `tr.c` | `TR()`, which every piece of fixed UI text passes through where it's drawn |
| `actions.c` | named actions, the button bindings that trigger them, and the hints generated from those bindings |
| `transitions.c` | the folder slide and the game switcher's entrance animations, and handing those and the launch to the transition and launch slots' owners, drawing their frames |
| `pluginset.c` | which discovered plugins load, and in what order |
| `plugins.c` | finding and loading plugins, the host table they call, chained hooks such as row decorators and header widgets, selection events, and the crash guard |
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
| `drawmodel.c` | the SDL-free half of plugins' drawing calls: which surfaces exist and which plugin owns each, with its memory budget, handing a plugin's surface to the launcher, text truncation, rounded corners and fitting |
| `plugindraw.c` | plugins' drawing calls over SDL and stock's fonts, and lending a surface such as the screen to a render hook, clipped to the area it may draw in |
| `layoutmodel.c` | the SDL-free half of the layout slot: whether the owner's layout is still on after its init and each call, the buttons it sees and consumes, and the area it draws in; also the built-in list's row rects for row decorators and where the selection is for transitions, how header widgets are laid out beside the status pill and what they take off the top row, and whether the background slot's owner or stock's background shows |
| `artcache.c` | the SDL-free half of the art cache: decoded images by path and kind, least recently used out first past a byte cap, pins, the worker's load order with prefetches last, requests dropped by sequence number once the selection moves, and plugins' generated art put in under keys of its own |
| `selmodel.c` | the SDL-free half of selection events: seeing the selection move (the art cache uses it too), and when it has settled on an entry plugins haven't heard about |
| `artloader.c` | the art cache over SDL: decoding on the art worker, taking images in and evicting at the start of each frame, the built-in list's thumbnail and its neighbours, plugins' `art_find`, `art_get` and `art_load`, and asking plugins' `art_surface` for generated art when no file answers |
| `background.c` | the game list's background layer: stock's folder background, or what the background slot's owner draws over it before the list draws, which folder slides capture too |
| `statusmodel.c` | the SDL-free half of the status pill: parsing shell paks' status files, tracking the files in `status.d` between polls, which are stale, and the one the pill shows |
| `statuspill.c` | the status pill over the card and SDL: looking at `status.d` once a second without following links, and drawing the pill through the header row before plugins' header widgets |
| `infomodel.c` | parsing `.info` metadata sidecars and where an entry's is; `plugins.c` reads one for `entry_info` and keeps it with the entry |
| `playmodel.c` | the SDL-free half of the play history: the games read from `game_logs.sqlite` by path (and Onion's orphans by name), and today's total from the sessions in local time |
| `playhistory.c` | the play history over sqlite, loaded with `dlopen` the first time a plugin asks: opening `game_logs.sqlite` read-only with a busy timeout, reading it in one go, and keeping the answers until a game returns |
| `transmodel.c` | the SDL-free half of transition styles: which slot's owner is asked about each kind and when an error turns it off, checking its choice, the duration cap, easing, and stepping through frames by the clock |
| `inputmodel.c` | the SDL-free half of input filters: taking what plugins' `input_filter` consumed off the pad, and a power press they took still powering off once it's held as long as stock's, while a release whose press got through always gets through |

See [PLUGINS.md](../PLUGINS.md) for writing plugins. `make test-plugins` loads test plugins (one that works, one with the wrong API version, one that crashes) into the desktop build. `MOSAIC_LAUNCHER_ART_CACHE_BYTES` replaces the art cache's 32 MiB cap, which the eviction case uses to fill a small one. `MOSAIC_LAUNCHER_SQLITE` names the sqlite library the play history loads in place of the system's, which the cases without one point at a file that isn't there. Each case waits up to `PLUGIN_TEST_SECONDS` (10 by default) for the log lines it checks and its first frame, and stops as soon as they're in. Before each case it clears the crash guard's state, so one slow or failed case can't start the later ones with plugins paused; only the crash checks at the end carry it from one start to the next. A failure names the case and its log. The Launcher pak's Plugins page cases run `nextui.elf --settings plugins` with `tests/plugins/presser`, a library (not a plugin) that `desktop-smoke.sh` preloads when `SMOKE_PRELOAD` names it (`LD_PRELOAD`, or `DYLD_INSERT_LIBRARIES` on macOS): it presses the buttons in `PRESSER_KEYS` through `SDL_PollEvent` and adds each text SDL_ttf draws to `PRESSER_LOG`, so a case can check what the screen showed, such as a secret setting's value never being drawn.

FN buttons can be bound to a launcher action as well as a pak, with `action:<id>` (eg. `action:gamelist.back`) in place of `pak:<name>`.

Upstream changes have to be ported forward by hand. To see what changed upstream since the pinned tag:

```shell
make drift                        # pinned tag -> latest tag
make drift FROM=v6.15.0 TO=v6.16.0
```

Then check out the new tag in `vendor/NextUI`, port the launcher changes, and rebuild. `supported-versions.txt` follows the submodule automatically.

## The Launcher screen

The Launcher pak's screens are drawn by the launcher binary itself: `launch.sh` runs `nextui.elf --settings`, or `--settings plugins` to open the plugin list, so they look and move like the game list. Anything that mounts, unmounts or edits the boot hook goes through `bin/manage.sh` (`turn-on`, `turn-off`, `try-again`, `status`), which also works over SSH.

