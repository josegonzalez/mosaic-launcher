# Writing plugins

To publish a plugin of your own, start from [`template/`](template/): copy it into a new repo, and it builds the plugin, packages it as a pak and releases it for the Pak Store.

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
- `setting.<key>=<type>|<label>|<default>[|<options>]` declares a setting the Launcher pak's Plugins page shows and edits. `type` is `toggle` (`on` or `off`), `choice` (one of the comma-separated `options`) or `text` (typed on the on-screen keyboard). The plugin reads the value with `host->cfg_get(id, key, default)`. Changes take effect the next time the launcher starts, which is when the user leaves the Launcher pak.
- `api` is the `NUI_API_VERSION` the plugin was built against. A plugin for a different version is skipped.
- `slots` lists exclusive slots the plugin takes over, such as `root` for `root_replace`. Only one plugin can own a slot.
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
- `confirm(message, yes_label, on_yes, data)` asks a yes/no question full screen; `on_yes` runs after it closes, only if the user pressed A.
- `keyboard(title, initial, on_done, data)` lets the user type text; `on_done` gets the text, or `NULL` if they cancelled with Y. A types the highlighted key, B deletes, X toggles shift, START finishes.

Both dialogs return straight away and call back once they close, so open them from an action (a button press), not from a list hook.
- `art_path` in `NUIPlugin` lets a plugin pick the thumbnail, folder background or list background for an entry.
- `root_replace()` in `NUIPlugin` returns the whole root list in place of the stock one, or `NULL` to keep stock's. Only the owner of the `root` slot (`slots=root` in `plugin.txt`) is asked, and other plugins' `root_entries` aren't added to a replaced root. Recents still load for the game switcher.
- `open_path(path, type)` opens a game, pak or folder the way selecting it in the game list would, including `pre_launch` and recents. It returns 1 when it opened or the launch is queued, and 0 for a missing file, a cancelled launch, or a call from `init`, before the lists exist.
- `recents_count()`, `recent_path(index)`, `recent_remove(index)` and `recents_clear()` read and change the recently played list, newest first. `recent_path` returns the full path, or `NULL` past the end. Changes are saved at once.
- `reload_lists()` lists the open folders again from the card on the next frame, keeping each selection on the same entry where it can. Call it after deleting or renaming a game or clearing recents. A folder other than the root that the reload empties is closed, as if the user had backed out of it.

## Without writing C

Shell paks can hook into the launcher too:

- **Before a launch:** `Tools/<platform>/<name>.pak/pre-launch.sh` runs before every game or pak launch, in pak name order. It gets `HOOK_PHASE=pre`, `HOOK_TYPE` (`rom` or `pak`), `HOOK_ROM_PATH` and `HOOK_PAK_PATH`. Exiting 1 cancels the launch; any other failure is logged and the launch goes ahead. This sits alongside stock NextUI's `.userdata/<platform>/.hooks/*.d/` folders, which still work and can't cancel.
- **Staying awake:** while `/tmp/stay_awake` exists, the launcher holds off auto-sleep. Create it during a long download or sync and remove it afterwards. The power button still sleeps the device.

## When a plugin crashes

The launcher marks plugin loading as in progress and clears the mark once its first frame is on screen. If it crashes before then, the next start pauses plugins and runs without them. The Launcher pak shows "plugins paused after a crash" and **Try again** turns them back on.

The launcher logs why each plugin did or didn't load to `.userdata/<platform>/logs/nextui.txt`, as `plugins: ...` lines.
