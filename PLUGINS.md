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
| `favorites` | A Favorites folder on the root. **Y** on the game list adds or removes the selected game or pak. The Pin to top setting (off by default) also sorts favorites first in whichever folder they're in, except Recently Played, collections and Favorites itself. Pin to top is from kUI and nx-redux. |
| `smart-collections` | A Smart Collections folder on the root with one collection per franchise (Mario, Zelda, Sonic and so on), matched by game name across every system. **Y** on a collection dismisses it, and **Y** on the folder brings dismissed ones back. Settings add franchises, turn off the built-in list, and set the fewest games a collection needs. From kUI. |
| `hidden` | Drops the games you hide from every list. **Y** on the game list hides the selected game or pak (the `button` setting picks another), and a Hidden folder on the root, shown while something is hidden, lists them so the same button unhides them. The list is `.userdata/<platform>/launcher/hidden.txt`, one full path per line. From MyMinUI. |
| `numeric-prefix` | Names starting with `1) `, `01. ` or `[1] ` sort by that number (2 before 10), and the number is left off the name shown; turn the `hide` setting off to keep it. It reads the shown name, so it works for games, folders and `map.txt` names. Numbered entries come first, then the rest in the usual order. On the root only consoles move, staying between Recently Played and Collections. Recently Played and collections keep their own order. From marcelopcx and Ru_UI. |
| `focus-mode` | The root lists only your favorites, in the favorites collection's order, then an All systems folder with every console. Needs `favorites` and owns the `root` slot; other plugins' root folders stay off it. Until a favorite is on the card the root is stock's. A Focus tile on the quick menu turns it off and on (as does the `on` setting), and Recents, Collections and Tools stay on the quick menu. From MinUI-Zero and UnuOS. |

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
- `root_replace()` in `NUIPlugin` returns the whole root list in place of the stock one, or `NULL` to keep stock's. Only the owner of the `root` slot (`slots=root` in `plugin.txt`) is asked, and other plugins' `root_entries` aren't added to a replaced root unless it sets `root_with_entries`. Recents still load for the game switcher.
- `open_path(path, type)` opens a game, pak or folder the way selecting it in the game list would, including `pre_launch` and recents. It returns 1 when it opened or the launch is queued, and 0 for a missing file, a cancelled launch, or a call from `init`, before the lists exist.
- `recents_count()`, `recent_path(index)`, `recent_remove(index)` and `recents_clear()` read and change the recently played list, newest first. `recent_path` returns the full path, or `NULL` past the end. Changes are saved at once.
- `reload_lists()` lists the open folders again from the card on the next frame, keeping each selection on the same entry where it can. Call it after deleting or renaming a game or clearing recents. A folder other than the root that the reload empties is closed, as if the user had backed out of it.
- `sort_compare(a, b, dir_path)` in `NUIPlugin` orders a list, such as pinned games first or by a number in the name. The launcher sorts a folder its own way first, then by every plugin's `sort_compare` in load order: for each pair, the first plugin to return non-zero decides (negative puts `a` first), and a pair no plugin has an opinion on keeps the launcher's order. Return 0 for lists you don't handle. Recents and collections are passed too, in their own order. The letter index L1 and R1 jump through follows the final order.
- `list_ready(list, dir_path)` in `NUIPlugin` sees a whole list once it's filtered, named and sorted, before it's shown, so a plugin can rename entries based on their siblings, such as titles that collide. Plugins run in load order. It can't add, remove or reorder entries. If a name changed, the list is sorted again by name and then by `sort_compare`, as for `display_name`, except recents and collections, which keep their order.
- `entry_set_name(entry, name)` renames an entry from inside `list_ready`. The new name is what the list draws and what the letter index uses. It returns 0, or -1 anywhere else.
- `current_list()` returns the list the user is looking at, read with `list_count` and `list_get`, or `NULL` before the lists exist. Don't keep it past the call you got it in: it's freed when the user leaves the folder or the lists reload. `selected_index()` returns the selected row in it, or -1 when it's empty. `select_index(index)` moves the selection to a row, scrolling only as far as it takes to show it, and returns 0, or -1 for a row that isn't there. Call them from an action or `tick`, not from a list hook.
- `quick_tile(action_id, row, icon, icon_off, is_on)` puts an action from `register_action` on the quick menu, after the built-in tiles of `row`: `NUI_QUICK_TOP` (the large labelled tiles such as Recents and Games) or `NUI_QUICK_BOTTOM` (the small icons such as Wifi and Power off). The tile is labelled with the action's label, A runs it, and it shows only while the action is available. `icon` is a PNG to draw on it, or `NULL`; a bottom tile without one shows its label. With `is_on`, the tile has an on/off state, `icon_off` is drawn while it's off, and the background is `.media/quick_<label>_off.png` as for stock's toggles. If the action opens a folder, the game list shows it. It returns 0, or -1 for an unknown action or row.
- `hide_quick_tile(name, hidden)` hides a built-in tile by name (`Recents`, `Collections`, `Games`, `Tools`, `Settings`, `Pak Store`, `Wifi`, `Bluetooth`, `Sleep`, `Reboot`, `Poweroff`), or a plugin tile by its action id, while `hidden` is 1, and shows it again with 0.

The quick menu's tiles are built at start and again whenever the lists reload, which is also when tile actions' `available` is checked, so clearing recents and calling `reload_lists` removes the Recents tile straight away.
- Plugin actions bound to the same game list button and trigger share it: when only one of them is available, the button runs it, and when two or more are, it opens a menu titled with the selected entry that lists them, and runs the one picked. The game list's bottom left hints show the first plugin button with an available action, labelled with that action, or `MORE` when it opens a menu.
- `choose(title, choices, count, selected, on_pick, data)` shows a full-screen list of choices, drawn like the Launcher pak's menus, with the cursor on `selected`. `on_pick` runs after it closes, with the index picked with A, or -1 if the user backed out with B. The strings are copied. It returns 0, or -1 for no choices or more than 64. Like the other dialogs, open it from an action.
- `recent_set(index, path)` points a recent at a new full path on the card, such as a renamed game, keeping its place and name, and saves at once. It returns 0, or -1 for a bad index, a path off the card, or a path another recent already has.
- `root_with_entries` in `NUIPlugin`, set to 1 by the `root` slot owner, adds plugins' `root_entries` to its replaced root, after the entries `root_replace` returned, as for the stock root (its own `root_entries` too, if it has one). Leaving it 0 keeps the replaced root to itself, as focus mode wants.

## Without writing C

Shell paks can hook into the launcher too:

- **Before a launch:** `Tools/<platform>/<name>.pak/pre-launch.sh` runs before every game or pak launch, in pak name order. It gets `HOOK_PHASE=pre`, `HOOK_TYPE` (`rom` or `pak`), `HOOK_ROM_PATH` and `HOOK_PAK_PATH`. Exiting 1 cancels the launch; any other failure is logged and the launch goes ahead. This sits alongside stock NextUI's `.userdata/<platform>/.hooks/*.d/` folders, which still work and can't cancel.
- **Staying awake:** while `/tmp/stay_awake` exists, the launcher holds off auto-sleep. Create it during a long download or sync and remove it afterwards. The power button still sleeps the device.

## When a plugin crashes

The launcher marks plugin loading as in progress and clears the mark once its first frame is on screen. If it crashes before then, the next start pauses plugins and runs without them. The Launcher pak shows "plugins paused after a crash" and **Try again** turns them back on.

The launcher logs why each plugin did or didn't load to `.userdata/<platform>/logs/nextui.txt`, as `plugins: ...` lines.
