# nextui-launcher

A drop-in replacement for the NextUI launcher (`nextui.elf`) that installs from the Pak Store and runs on a stock NextUI card. The goal is a launcher with a plugin API, so features that today live in separate MinUI and NextUI forks can be installed and combined as plugins.

This first release runs an unmodified copy of the stock launcher from the pinned NextUI release, behind a switch and a crash guard. Plugins come in later releases.

## Installing

Install **Launcher** from the Pak Store (Tools > Pak Store), open it from Tools, and choose **Turn on**. The new launcher starts when you leave the Launcher screen.

To go back to stock, open Launcher and choose **Turn off**. Uninstalling the pak from the Pak Store also returns to stock after the next reboot.

## How it takes over

Stock `MinUI.pak/launch.sh` runs `$SYSTEM_PATH/bin/nextui.elf` in a loop. Turning Launcher on installs a boot hook (`.userdata/<platform>/.hooks/boot.d/00-launcher.sync.sh`) that runs `bin/boot.sh` on every boot, and runs it once right away.

`boot.sh` bind-mounts the stock binary to `/tmp/nextui-launcher/nextui.stock.elf`, then bind-mounts a small wrapper over `nextui.elf`. Nothing in `.system/` is modified, so NextUI updates never conflict. On devices that refuse file bind mounts it falls back to backing up the stock binary as `nextui.stock.elf` and copying the wrapper in place.

Each time `launch.sh` starts `nextui.elf`, the wrapper picks a binary. It runs stock when the launcher is off, when the pak has been uninstalled, or after our launcher crashes twice in a row. Otherwise it runs this pak's launcher. Shutdown signals (SIGTERM, SIGKILL) don't count as crashes, and any clean exit resets the count. Choosing **Try again** in the Launcher screen clears the crash guard.

The pak only activates on the NextUI release it was built against. It compares the build hash in `.system/version.txt` with `supported-versions.txt`, and leaves stock in place on any other release.

State lives in `.userdata/<platform>/launcher/`, which Pak Store updates and uninstalls never touch.

## Building

The NextUI source is a submodule pinned to a release tag:

```shell
git submodule update --init
```

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

`make smoke` starts the desktop build for a few seconds and fails if it exits on its own. Its log (`build/smoke.log`) is also handy for comparing startup output before and after a change.

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
| `imgloader.c` | background image, thumbnail and selection pill worker threads |
| `screens.c` | the screen registry `main()` dispatches through, and state shared with it |
| `screen_gamelist.c` | game list input and rendering, plus the fade shown when launching |
| `screen_switcher.c` | game switcher input and rendering |
| `screen_quickmenu.c` | quick menu input and rendering |
| `art.c` | where each kind of image (thumbnails, backgrounds, quick menu art) is looked up |
| `quickitems.c` | the quick menu's pak shortcuts and toggles: which appear, their icons, and what they do |
| `tr.c` | `TR()`, which every piece of fixed UI text passes through where it's drawn |
| `actions.c` | named actions, the button bindings that trigger them, and the hints generated from those bindings |
| `transitions.c` | the folder slide and the game switcher's entrance animations |
| `pluginset.c` | which discovered plugins load, and in what order |
| `plugins.c` | finding and loading plugins, the host table they call, and the crash guard |

See [PLUGINS.md](PLUGINS.md) for writing plugins. `make test-plugins` loads test plugins (one that works, one with the wrong API version, one that crashes) into the desktop build.

FN buttons can be bound to a launcher action as well as a pak, with `action:<id>` (eg. `action:gamelist.back`) in place of `pak:<name>`.

Upstream changes have to be ported forward by hand. To see what changed upstream since the pinned tag:

```shell
make drift                        # pinned tag -> latest tag
make drift FROM=v6.15.0 TO=v6.16.0
```

Then check out the new tag in `vendor/NextUI`, port the launcher changes, and rebuild. `supported-versions.txt` follows the submodule automatically.

## Bundled tools

The Launcher screen uses [minui-list](https://github.com/josegonzalez/minui-list) and [minui-presenter](https://github.com/josegonzalez/minui-presenter). `pak/minui.lock` pins their versions and the SHA-256 of every file, and packaging refuses anything that doesn't match. Run `./scripts/pin-minui.sh <presenter version> <list version>` to upgrade.

## License

The launcher is derived from NextUI's `nextui.c`, so this project is under the [PolyForm Noncommercial License 1.0.0](LICENSE), the same as NextUI. The plugin SDK in `sdk/` is written from scratch and [MIT licensed](sdk/LICENSE), so plugins can choose their own license.
