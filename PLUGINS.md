# Writing plugins

A plugin is a shared object the launcher loads at startup. It's written against one header, [`sdk/nextui_plugin.h`](sdk/nextui_plugin.h), which is MIT licensed and doesn't include anything from NextUI, so a plugin can use any license.

## Layout

A plugin is a directory holding a manifest and one build per CPU architecture:

```
Favorites.plugin/        or   Tools/<platform>/Launcher Favorites.pak/
    plugin.txt
    lib/aarch64/plugin.so
```

The launcher looks in two places:

- `Launcher.pak/plugins/*.plugin/`, for plugins that ship with the launcher. These are off until turned on.
- `Tools/<platform>/*.pak/` with a `plugin.txt`, for plugins installed as their own pak (for example from the Pak Store). These are on by default.

Every current NextUI device is aarch64, so one `lib/aarch64/plugin.so` covers tg5040, tg5050 and h700. Build it against the oldest toolchain sysroot (tg5040's) so it runs on all of them. Desktop builds load `lib/desktop-<os>-<arch>/plugin.so`, eg. `lib/desktop-darwin-arm64/`.

## plugin.txt

```
id=favorites
name=Favorites
api=1
slots=
requires=
conflicts=
```

- `id` must match the `id` in the plugin's `NUIPlugin`.
- `api` is the `NUI_API_VERSION` the plugin was built against. A plugin for a different version is skipped.
- `slots` lists exclusive slots the plugin takes over, such as a list layout. Only one plugin can own a slot.
- `requires` lists plugins that must also load. `conflicts` lists plugins that can't load alongside it.

## The entry point

```c
#include "nextui_plugin.h"

static const NUIHost* host;

static int init(const NUIHost* h) {
	host = h;
	host->log("hello", "hello");
	return 0; // anything else skips the plugin
}

static const NUIPlugin plugin = {
	.size = sizeof(NUIPlugin),
	.api_version = NUI_API_VERSION,
	.id = "hello",
	.init = init,
};

const NUIPlugin* nextui_plugin_get(const NUIHost* h) {
	return &plugin;
}
```

Build it as a shared object, with no references into the launcher:

```shell
gcc -shared -fPIC -I sdk hello.c -o lib/aarch64/plugin.so
```

Plugins in this repo live in `plugins/<name>/` (a `plugin.txt` plus `.c` files). `make plugins-device` builds them all for devices in the tg5040 toolchain, `make plugins-desktop` for the desktop build, and `make pak` bundles them into `Launcher.pak/plugins/`. `make run-desktop` runs the desktop build with them available; turn one on with `<id>=on` in `/var/tmp/nextui/sdcard/.userdata/desktop/launcher/plugins.txt`.

## Bundled plugins

| Plugin | What it does |
|---|---|
| `favorites` | A Favorites folder on the root. **Y** on the game list adds or removes the selected game or pak. |

Every hook in `NUIPlugin` is optional; leave out what you don't use. The launcher calls plugins only on its main thread. Both `NUIPlugin` and `NUIHost` start with their own `size`, and fields are only ever added at the end, so a plugin built against an older header keeps working with a newer launcher.

## Turning plugins on and off

On the device, open **Launcher** from Tools and choose **Plugins**. Each plugin shows as On or Off; selecting one flips it, and the change takes effect when you leave the Launcher screen.

Behind that, `.userdata/<platform>/launcher/plugins.txt` holds the user's choices:

```
favorites=on
grid=off
slot.layout=carousel
```

Plugins listed there load first, in that order. `slot.<name>=<id>` picks which plugin owns an exclusive slot. Each plugin's own settings, from `cfg_set`, live in `.userdata/<platform>/launcher/<id>.cfg`.

## Features added since the first API

Newer host fields may be missing on an older launcher. Check before calling:

```c
if (NUI_HOST_HAS(host, toast)) host->toast("Added to Favorites");
```

- `toast(message)` shows a short message at the bottom of the screen.
- `inhibit_sleep(id, on)` holds off auto-sleep until the same plugin releases it.
- `art_path` in `NUIPlugin` lets a plugin pick the thumbnail, folder background or list background for an entry.

## Without writing C

Shell paks can hook into the launcher too:

- **Before a launch:** `Tools/<platform>/<name>.pak/pre-launch.sh` runs before every game or pak launch, in pak name order. It gets `HOOK_PHASE=pre`, `HOOK_TYPE` (`rom` or `pak`), `HOOK_ROM_PATH` and `HOOK_PAK_PATH`. Exiting 1 cancels the launch; any other failure is logged and the launch goes ahead. This sits alongside stock NextUI's `.userdata/<platform>/.hooks/*.d/` folders, which still work and can't cancel.
- **Staying awake:** while `/tmp/stay_awake` exists, the launcher holds off auto-sleep. Create it during a long download or sync and remove it afterwards. The power button still sleeps the device.

## When a plugin crashes

The launcher marks plugin loading as in progress and clears the mark once its first frame is on screen. If it crashes before then, the next start pauses plugins and runs without them. The Launcher pak shows "plugins paused after a crash" and **Try again** turns them back on.

The launcher logs why each plugin did or didn't load to `.userdata/<platform>/logs/nextui.txt`, as `plugins: ...` lines.
