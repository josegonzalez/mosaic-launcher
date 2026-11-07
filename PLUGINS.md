# Writing plugins

To write a plugin of your own, start from [`template/`](template/): copy it into a new repo, and it builds the plugin and packages it as a pak.

A plugin is a shared object the launcher loads at startup. It's written against one header, [`sdk/mosaic_plugin.h`](sdk/mosaic_plugin.h), which is MIT licensed and doesn't include anything from NextUI, so a plugin can use any license.

## Layout

A plugin is a directory holding a manifest and one build per CPU architecture:

```
Favorites.plugin/        or   Tools/<platform>/Launcher Favorites.pak/
    plugin.txt
    lib/aarch64/plugin.so
```

The launcher looks in two places:

- `Launcher.pak/plugins/*.plugin/`, for plugins that ship with the launcher. These are off until turned on.
- `Tools/<platform>/*.pak/` with a `plugin.txt`, for plugins installed as their own pak. These are on by default.

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

- `id` must match the `id` in the plugin's `MosaicPlugin`.
- `setting.<key>=<type>|<label>|<default>[|<options>]` declares a setting the Launcher pak's Plugins page shows and edits. `type` is `toggle` (`on` or `off`), `choice` (one of the comma-separated `options`) or `text` (typed on the on-screen keyboard). The plugin reads the value with `host->cfg_get(id, key, default)`. Changes take effect the next time the launcher starts, which is when the user leaves the Launcher pak.
- `api` is the `MOSAIC_API_VERSION` the plugin was built against. A plugin for a different version is skipped.
- `slots` lists exclusive slots the plugin takes over, such as `root` for `root_replace`. Only one plugin can own a slot.
- `requires` lists plugins that must also load. `conflicts` lists plugins that can't load alongside it.

## The entry point

```c
#include "mosaic_plugin.h"

static const MosaicHost* host;

static int init(const MosaicHost* h) {
	host = h;
	host->log("hello", "hello");
	return 0; // anything else skips the plugin
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = "hello",
	.init = init,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
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
| `numeric-prefix` | Names starting with `1) `, `01. ` or `[1] ` sort by that number (2 before 10), and the number is left off the name shown; turn the `hide` setting off to keep it. It reads the shown name, so it works for games, folders and `map.txt` names. Numbered entries come first, then the rest in the usual order. On the root only consoles move, staying between Recently Played and Collections, and a root another plugin replaced, such as `all-games`' or `focus-mode`'s, keeps its owner's order. Recently Played and collections keep their own order. From marcelopcx and Ru_UI. |
| `focus-mode` | The root lists only your favorites, in the favorites collection's order, then an All systems folder with every console. Needs `favorites` and owns the `root` slot; other plugins' root folders stay off it. Until a favorite is on the card the root is stock's. A Focus tile on the quick menu turns it off and on (as does the `on` setting), and Recents, Collections and Tools stay on the quick menu. From MinUI-Zero and UnuOS. |
| `title-disambiguation` | When titles collide in a list, adds what tells them apart, and only then. Games from different systems, as in Recently Played, collections and Favorites, get the system's tag, eg. "Tetris (GB)" and "Tetris (GBC)". Games from one system get the region tags or extension the name leaves out, eg. "Tetris (USA)" or "Tetris.gbc", then the folder they're in, and the whole file name when nothing else differs. Unlike stock, which only marks neighbours with the same name, it finds collisions anywhere in the list. Turn the `same_system` setting off to rename only games from different systems and leave the rest to stock. From Ru_UI and casualducko. |
| `all-games` | The root is one list of every game across systems, by name, named as each system's list names them (`map.txt` aliases included). A Systems folder and other plugins' root folders come first; Recents, Collections and Tools stay on the quick menu. Subfolders are flattened two levels deep, and a folder with a `.cue` or `.m3u` named after it is one game. Games from different systems with the same name show their system's tag, eg. "Tetris (GB)" and "Tetris (GBC)"; turn the `tags` setting off to leave that to stock or `title-disambiguation`. The card is scanned once per start, and again only when a folder it read changes. Owns the `root` slot, so it can't be on with `focus-mode`. Until a game is on the card the root is stock's. From Xisrith. |
| `random-game` | Picks a game at random and launches it. **Y** on the game list picks from the system you're in, or from every system on the root. A Random game row on the root (the `root` setting, on by default) and a Random tile on the quick menu (the `tile` setting, off by default) pick from every system. Every game is as likely as any other, so bigger systems come up more often, and with the `unplayed` setting on (the default) games not in Recently Played are four times as likely. Games the `hidden` plugin hides are never picked. From casualducko and kUI. |
| `clear-recents` | Clears Recently Played. A Clear recents tile on the quick menu (the `tile` setting, on by default) shows while there are recents, and in Recently Played **Y** (the `button` setting picks another) removes the selected game or clears them all, opening a menu of the two when both apply. Clearing asks first, then lists the folders again, so the quick menu's Recents tile goes away. Outside Recently Played the button is left to other plugins. From MyMinUI, MinUI-Zero, nx-redux and Nocturne. |
| `game-actions` | Rename, Delete and Add to collection for the selected game, on **Y** (the `button` setting picks another) alongside other plugins' actions there, so with favorites or hidden on, **Y** opens a menu of them all. Rename sets the name shown for any entry on the keyboard, and an empty name restores the usual one; it never touches files, keeping names in `.userdata/<platform>/launcher/game-actions-names.txt`. Delete removes a game under `Roms/` after a confirm that says what goes with it: the rom (for a multi-disc or cue-sheet game its whole folder, and for a loose `.cue` or `.m3u` the files only it lists), its art in `.media/`, and with the `saves` setting on (the default) its saves in `Saves/<tag>/`, its states in `.userdata/shared/<tag>-<core>/` and `.userdata/shared/.minui/<tag>/`, and its own emulator settings. A save or art name another game in the system shares is kept, links are removed but never followed, and nothing outside the card is touched. The game also leaves recents, every collection (favorites too) and its renamed name. Add to collection appends the game to a collection in `Collections/`, as stock stores it, or to a new one named on the keyboard. Each action has its own setting to turn it off. From nx-redux, LodorOS, MinUit, kUI and casualducko. |
| `path-repair` | Keeps recents and collections pointing at games after they're renamed. When an entry's file is gone, it looks in the same folder for the one file whose name matches with accents, case, spaces and punctuation left out, eg. "Pokémon Red.gb" renamed to "Pokemon Red.gb", and points the entry at it; a renamed folder below the system folder, such as a multi-disc game's, is matched the same way. When nothing matches, or more than one file does, the entry is left alone. Collections, favorites included, are repaired at start (the `collections` setting), and recents as the launcher loads them (the `recents` setting); both are on by default. Only the folders a broken entry is in are listed. From UnuOS. |
| `simple-mode` | A simple mode for kids or a shared device. While it's on, the Tools folder is gone from every list (the root, Recently Played and collections), Tools paks don't launch from the game switcher or an FN button, and the quick menu drops its Tools and Pak Store tiles, plus Settings, Wifi, Bluetooth and Reboot with the `settings` setting (on by default). A Simple mode tile on the quick menu turns it on, and off again after a confirm, or after typing the `pin` setting when one is set. The PIN is kept only in `.userdata/<platform>/launcher/simple-mode.cfg`. A forgotten PIN can be cleared by deleting its `pin=` line from that file on a computer, or by setting `on=off` there or `simple-mode=off` in `plugins.txt`. It's separate from stock NextUI's own simple mode file, which it never touches. From Xisrith, MinUI-Zero, nx-redux (PIN), kUI and Nocturne. |
| `platform-jump` | **L1** and **R1** jump between systems instead of letters. Inside a system, or any folder in it, they open the previous or next system in the root's order, as if you'd backed out and opened it there, so **B** goes back to the root with that system selected. In Recently Played, collections, Favorites and other lists with games from several systems (the `lists` setting, on by default), they move to the first game of the previous or next system, taking systems in the order they first appear in the list. On the root they jump letters, unless the `root` setting (off by default) has them move between the system rows, past Recently Played, Collections, Tools and other plugins' rows. Past the last system they wrap around to the first (the `wrap` setting, on by default). Everywhere else, and where there's only one system to go to, they jump letters as stock does. Both kinds of jump repeat while the shoulder is held, at stock's pace, so holding R1 inside a system steps through the systems; on a launcher without `override_repeat` they go once per press. It takes L1 and R1 with `override_button`, so other plugins' `bind_button` bindings on them don't run and the shared-button menu never opens on them; if another plugin earlier in load order already overrides either one, platform jump doesn't load. With a root another plugin replaced, such as `all-games`', the systems come from the next list of systems shown, such as its Systems folder, and **B** from a system it opened goes back through the Roms folder. From kUI. |
| `button-remap` | Puts the game list's own actions, or plugins' actions, on other buttons. Each button's setting lists action ids tried in order, the first available one running, eg. `x=gamelist.open` or `y=release:random-game.pick,favorites.toggle`: it acts on press unless it starts with `release:`, or with `repeat:` to act on press and again while held, as stock's letter jumps do (eg. `l1=repeat:gamelist.prev_letter`), `none` leaves the button doing nothing, and an empty one leaves it as it is. The keys are `a`, `b`, `x`, `y`, `l1`, `r1`, `l2`, `r2`, `start` and `select` in `.userdata/<platform>/launcher/button-remap.cfg`, and the Plugins page shows A, X, Y, L2, R2 and START. The game list's actions are `gamelist.open`, `gamelist.back`, `gamelist.resume`, `gamelist.prev_letter`, `gamelist.next_letter`, `gamelist.switcher` and `gamelist.quick_menu`; a letter jump repeats only with `repeat:`. A, B, X, L1, R1 and SELECT are overridden, so other plugins' bindings there stop, a button another plugin overrode first stays that plugin's, and a plugin's action on one of them needs `button-remap` after that plugin in `plugins.txt`. With `platform-jump` on, L1 and R1 go to whichever of the two loads first, and only when `button-remap` has an `l1` or `r1` setting: if `button-remap` is first, `platform-jump` doesn't load, and if `platform-jump` is first, the `l1` and `r1` settings are left out and the rest still applies; either way the launcher log says so. Without those settings the two work together. Y, L2, R2 and START are shared with other plugins' bindings, so with two or more actions available the button opens a menu of them, except with `repeat:`, which overrides them like the others. The Resume with A setting swaps A and X: A resumes the selected game from its save state, or opens it when there's nothing to resume, X opens it without resuming, and the hints follow. Resume on press makes X act on press instead of release. Both are off by default, and a button's own setting replaces what they put on it. From Nocturne, Bramley, MinUit, MyMinUI, Xisrith (Resume with A) and ProClifo (Resume on press). |
| `quick-menu-plus` | Tweaks the quick menu. The `labels` setting renames the top row's built-in tiles with `name=label` pairs, eg. `Recents=Played, Games=Library`, and the `hide` setting hides tiles by name, eg. `Pak Store, Bluetooth`, or a plugin tile by its action id, such as `random-game.tile`. The `toggles` setting picks which of stock NextUI's on/off settings get a tile that flips them, as stock's Settings app does: `save`, `load`, `screenshot` and `adjustments` (the in-game notifications, the default), `clock`, `battery` (the battery percentage) and `haptics`. A Stay awake tile (the `awake_tile` setting, on by default) holds off auto-sleep while it's on, until the device reboots or powers off. A Reboot to stock tile (the `stock_tile` setting, on by default) asks first, then turns the launcher off with `manage.sh turn-off` and reboots, so the device comes back on stock NextUI's launcher; turn Launcher back on from Tools. All of these tiles go in the top row. Tile labels and hidden tiles are from Xisrith, quick toggles from nx-redux, and Reboot to stock from hp7hao. |
| `emulator-override` | Picks the emulator pak a game launches with. **Y** on a game (the `button` setting picks another), alongside other plugins' actions there, lists Default, the system's own pak, then the other emulator paks on the card known to run its games: GB, GBC, SGB and MGBA for Game Boy and Game Boy Color games, MGBA for Game Boy Advance, and SFC and SUPA for each other. The `all` setting (off by default) lists every emulator pak instead. Picking one saves it for that game, and Default clears it. A game folder, its discs and its `.m3u` share one choice. The choices are in `.userdata/<platform>/launcher/emulator-override.txt`, one card-relative game path, a tab and the pak's name per line. Before each launch the choice is checked again: the pak has to be in `Emus/<platform>/` or `.system/<platform>/paks/Emus/`, found the way stock finds a system's pak, with a `launch.sh` that's a regular file and doesn't lead out of its pak through a link. Otherwise the game launches as usual and the launcher log says why. A valid choice replaces the launch with the pak's `launch.sh` and the game's path, quoted as stock quotes its own, so the game's saves and states go under that pak's name, as when a system folder is renamed to it. Paks launch as usual. With another plugin that replaces launches, only the first of them in load order replaces a launch: when that's the other plugin, the override's `replace_launch` is refused and logged and the other's command runs, and when it's `emulator-override`, the other's is refused. From LodorOS. |
| `netplay` | Hidden until NextUI has netplay: stock minarch has none, so its `plugin.txt` has `requires=minarch-netplay`, and the loader leaves it off and logs why, so it never binds **Y**. Its name on the Plugins page says it needs NextUI netplay. Once it can load, **Y** on a game opens Host a game or Join a game. Joining asks for the host's name or IPv4 address on the keyboard, with `:port` for a port other than the `port` setting (55435 by default), and offers the last address next time. The game then launches through its emulator pak's `launch.sh` with `--host --port <port>` or `--connect <address> --port <port>` before the game's path, each part single-quoted. Those arguments, and a `launch.sh` that passes what comes before the game to minarch, are assumptions to confirm once minarch has netplay. A game without an emulator pak is cancelled rather than launched without netplay. Only launches picked with Host or Join are replaced. With another plugin that replaces launches, such as `emulator-override`, only the first of them in load order replaces a launch: when that's the other plugin, netplay's `replace_launch` is refused, so netplay cancels the launch and says so rather than letting it start without netplay; when it's `netplay`, the netplay launch uses the system's own pak and the other's `replace_launch` is refused. From nx-redux. |

Every hook in `MosaicPlugin` is optional; leave out what you don't use. The launcher calls plugins only on its main thread. Both `MosaicPlugin` and `MosaicHost` start with their own `size`, and fields are only ever added at the end, so a plugin built against an older header keeps working with a newer launcher.

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
if (MOSAIC_HOST_HAS(host, toast)) host->toast("Added to Favorites");
```

- `toast(message)` shows a short message at the bottom of the screen.
- `inhibit_sleep(id, on)` holds off auto-sleep until the same plugin releases it.
- `confirm(message, yes_label, on_yes, data)` asks a yes/no question full screen; `on_yes` runs after it closes, only if the user pressed A.
- `keyboard(title, initial, on_done, data)` lets the user type text; `on_done` gets the text, or `NULL` if they cancelled with Y. A types the highlighted key, B deletes, X toggles shift, START finishes.

Both dialogs return straight away and call back once they close, so open them from an action (a button press), not from a list hook.
- `art_path` in `MosaicPlugin` lets a plugin pick the thumbnail, folder background or list background for an entry.
- `root_replace()` in `MosaicPlugin` returns the whole root list in place of the stock one, or `NULL` to keep stock's. Only the owner of the `root` slot (`slots=root` in `plugin.txt`) is asked, and other plugins' `root_entries` aren't added to a replaced root unless it sets `root_with_entries`. Recents still load for the game switcher.
- `open_path(path, type)` opens a game, pak or folder the way selecting it in the game list would, including `pre_launch` and recents. It returns 1 when it opened or the launch is queued, and 0 for a missing file, a cancelled launch, or a call from `init`, before the lists exist.
- `recents_count()`, `recent_path(index)`, `recent_remove(index)` and `recents_clear()` read and change the recently played list, newest first. `recent_path` returns the full path, or `NULL` past the end. Changes are saved at once.
- `reload_lists()` lists the open folders again from the card on the next frame, keeping each selection on the same entry where it can. Call it after deleting or renaming a game or clearing recents. A folder other than the root that the reload empties is closed, as if the user had backed out of it.
- `sort_compare(a, b, dir_path)` in `MosaicPlugin` orders a list, such as pinned games first or by a number in the name. The launcher sorts a folder its own way first, then by every plugin's `sort_compare` in load order: for each pair, the first plugin to return non-zero decides (negative puts `a` first), and a pair no plugin has an opinion on keeps the launcher's order. Return 0 for lists you don't handle. Recents and collections are passed too, in their own order. The letter index L1 and R1 jump through follows the final order.
- `list_ready(list, dir_path)` in `MosaicPlugin` sees a whole list once it's filtered, named and sorted, before it's shown, so a plugin can rename entries based on their siblings, such as titles that collide. Plugins run in load order. It can't add, remove or reorder entries. If a name changed, the list is sorted again by name and then by `sort_compare`, as for `display_name`, except recents and collections, which keep their order.
- `entry_set_name(entry, name)` renames an entry from inside `list_ready`. The new name is what the list draws and what the letter index uses. It returns 0, or -1 anywhere else.
- `current_list()` returns the list the user is looking at, read with `list_count` and `list_get`, or `NULL` before the lists exist. Don't keep it past the call you got it in: it's freed when the user leaves the folder or the lists reload. `selected_index()` returns the selected row in it, or -1 when it's empty. `select_index(index)` moves the selection to a row, scrolling only as far as it takes to show it, and returns 0, or -1 for a row that isn't there. Call them from an action or `tick`, not from a list hook.
- `quick_tile(action_id, row, icon, icon_off, is_on)` puts an action from `register_action` on the quick menu, after the built-in tiles of `row`: `MOSAIC_QUICK_TOP` (the large labelled tiles such as Recents and Games) or `MOSAIC_QUICK_BOTTOM` (the small icons such as Wifi and Power off). The tile is labelled with the action's label, A runs it, and it shows only while the action is available. `icon` is a PNG to draw on it, or `NULL`; a bottom tile without one shows its label. With `is_on`, the tile has an on/off state, `icon_off` is drawn while it's off, and the background is `.media/quick_<label>_off.png` as for stock's toggles. If the action opens a folder, the game list shows it. It returns 0, or -1 for an unknown action or row.
- `hide_quick_tile(name, hidden)` hides a built-in tile by name (`Recents`, `Collections`, `Games`, `Tools`, `Settings`, `Pak Store`, `Wifi`, `Bluetooth`, `Sleep`, `Reboot`, `Poweroff`), or a plugin tile by its action id, while `hidden` is 1, and shows it again with 0.

The quick menu's tiles are built at start and again whenever the lists reload, which is also when tile actions' `available` is checked, so clearing recents and calling `reload_lists` removes the Recents tile straight away.
- Plugin actions bound to the same game list button and trigger share it: when only one of them is available, the button runs it, and when two or more are, it opens a menu titled with the selected entry that lists them, and runs the one picked. The game list's bottom left hints show the first plugin button with an available action, labelled with that action, or `MORE` when it opens a menu.
- `choose(title, choices, count, selected, on_pick, data)` shows a full-screen list of choices, drawn like the Launcher pak's menus, with the cursor on `selected`. `on_pick` runs after it closes, with the index picked with A, or -1 if the user backed out with B. The strings are copied. It returns 0, or -1 for no choices or more than 64. Like the other dialogs, open it from an action.
- `recent_set(index, path)` points a recent at a new full path on the card, such as a renamed game, keeping its place and name, and saves at once. It returns 0, or -1 for a bad index, a path off the card, or a path another recent already has.
- `root_with_entries` in `MosaicPlugin`, set to 1 by the `root` slot owner, adds plugins' `root_entries` to its replaced root, after the entries `root_replace` returned, as for the stock root (its own `root_entries` too, if it has one). Leaving it 0 keeps the replaced root to itself, as focus mode wants.
- `override_button(screen, button, trigger, action_id)` puts an action on a game list button in place of what the game list does with it, on press (`MOSAIC_TRIGGER_PRESS`) or release (`MOSAIC_TRIGGER_RELEASE`), or while held (`MOSAIC_TRIGGER_REPEAT`, see `override_repeat` below). The action is one from `register_action` or one of the game list's own, listed below. The override takes the whole button: the game list's own bindings on it and plugins' `bind_button` ones stop running, whatever their trigger, and a `NULL` action leaves the button doing nothing. The game list's hints follow its actions to their new buttons, so swapping A and X swaps their hints too, and an override with a plugin's action can show in the bottom left hint when no `bind_button` one does. A plugin can override a button more than once, for press and release, or for several actions on one trigger, where the first available one runs (so A can resume when there's something to resume and open otherwise). A button one plugin overrides can't be overridden by another: the first in load order keeps it and the loader logs the refusal, and it logs `bind_button` bindings an override stops. Call it from `init`; it returns 0, or -1 for an unknown action, a button another plugin has, a call from anywhere else or a bad argument. Only `MOSAIC_SCREEN_GAMELIST` takes overrides so far.

  | Action | Stock button | What it does |
  |---|---|---|
  | `gamelist.open` | A, press | opens the selected folder, game or pak |
  | `gamelist.back` | B, press | backs out of the folder |
  | `gamelist.resume` | X, release | resumes the selected game from its last save state |
  | `gamelist.prev_letter` | L1, repeats while held | jumps to the previous letter |
  | `gamelist.next_letter` | R1, repeats while held | jumps to the next letter |
  | `gamelist.switcher` | SELECT, tap | opens the game switcher |
  | `gamelist.quick_menu` | MENU, tap | opens the quick menu |

  The same ids work for FN buttons as `action:<id>`. MENU and the d-pad can't be overridden. A built-in action moved to another button fires on the override's trigger; only its stock button taps, and only `MOSAIC_TRIGGER_REPEAT` repeats it.
- `override_repeat` is 1 when `override_button` also takes `MOSAIC_TRIGGER_REPEAT`, which runs the action on the press and again while the button is held, at stock's pace for the d-pad and letter jumps (300ms after the press, then every 100ms). Check it with `MOSAIC_HOST_HAS(host, override_repeat)` and use `MOSAIC_TRIGGER_PRESS` without it; an older launcher returns -1 for the repeat trigger. When an override puts `gamelist.prev_letter` back on L1 or `gamelist.next_letter` back on R1, on any trigger, the jump keeps stock's check that ignores the shoulder while the other one is held. Put a plugin's action and the letter jump after it on the same trigger, as `platform-jump` does, so a held shoulder keeps doing whichever was available when it fires rather than switching to letters on the repeats. `bind_button` doesn't take `MOSAIC_TRIGGER_REPEAT` and returns -1 for it.
- `pre_launch` can return `MOSAIC_LAUNCH_REPLACE` after calling `replace_launch(program, args, count)`, to run another command in place of the launch, such as another emulator pak or a different core. `program` is a file on the card (checked after resolving links and `..`), such as `Emus/<platform>/<name>.pak/launch.sh` or minarch; up to 8 `args` follow it, each under 256 bytes and without newlines. For a game, the path the launcher would have launched goes last, after it has picked the disc to resume; for a pak nothing is added. Each part is single-quoted the way the launcher quotes its own commands. Paks' `pre-launch.sh` scripts still run first and can still cancel, and recents, resume, play time and `post_return` work as for any launch. If two plugins replace a launch, the first in load order wins and the second's `replace_launch` returns -1, which the loader logs. It returns 0, or -1 outside `pre_launch`, for a program that isn't a file on the card, or for arguments that don't fit. A launcher without `replace_launch` treats `MOSAIC_LAUNCH_REPLACE` as `MOSAIC_LAUNCH_CONTINUE`.
- `rename_quick_tile(name, label)` relabels a built-in tile in the quick menu's top row (`Recents`, `Collections`, `Games` or `Tools`), and `NULL` or `""` puts its own label back. Hiding and art still go by the built-in name. It returns 0, or -1 for any other name; the bottom row's built-ins are icons without labels.
- `battery_level()` returns the battery's charge from 0 to 100, and `is_charging()` returns 1 while it charges and 0 otherwise, as the status bar shows them. Both return -1 on a platform without a battery, which today is desktop, so a plugin like a low battery guard can report itself unsupported there.
- `led_count()` returns how many LEDs the device's LED settings control, and 0 on devices without controllable LEDs and on desktop. The host doesn't set them: a plugin writes the platform's own LED files in `/sys`, and the launcher's LED rules set them again when the device starts or stops charging, gets low on battery, or sleeps.
- `power_off(reboot)` powers the device off, or reboots it when `reboot` is 1, the way the quick menu's Poweroff and Reboot tiles do: on the next frame, after every plugin's `before_poweroff`, with stock's "Powering off" or "Rebooting" screen. On desktop both quit the launcher.
- `show_recents()` returns 1 when stock's root would list Recently Played: Show Recents is on in Settings and a recent's game is on the card. A `root_replace` can match stock by adding the folder itself, as a `MOSAIC_ENTRY_DIR` entry at `sdcard_path` + `/Recently Played`. Recents load as the root is built, so ask from `root_replace` or later; from `init` it returns 0.
- `stock_toggle_get(name)` and `stock_toggle_set(name, on)` read and change a few of stock NextUI's own on/off settings, by the names below. They go through the launcher's copy of the settings, as stock's Settings app does, so the change is saved to `minuisettings.txt` at once, the status bar redraws with it, and games launched afterwards see it. Don't edit `minuisettings.txt` yourself: the launcher rewrites the whole file from its own copy on its next change. `stock_toggle_get` returns 1 for on, 0 for off, or -1 for any other name. `stock_toggle_set` takes 1 or 0 and returns 0, or -1 for any other name or value, which the launcher logs. A set that changes nothing doesn't rewrite the file.

  | Name | Constant | Stock setting |
  |---|---|---|
  | `notify_manual_save` | `MOSAIC_TOGGLE_NOTIFY_MANUAL_SAVE` | Notifications: Save states |
  | `notify_load` | `MOSAIC_TOGGLE_NOTIFY_LOAD` | Notifications: Load states |
  | `notify_screenshot` | `MOSAIC_TOGGLE_NOTIFY_SCREENSHOT` | Notifications: Screenshots |
  | `notify_adjustments` | `MOSAIC_TOGGLE_NOTIFY_ADJUSTMENTS` | Notifications: Vol / Display Adjustments |
  | `show_clock` | `MOSAIC_TOGGLE_SHOW_CLOCK` | Show clock |
  | `show_battery_percent` | `MOSAIC_TOGGLE_SHOW_BATTERY_PERCENT` | Show battery percentage |
  | `haptics` | `MOSAIC_TOGGLE_HAPTICS` | Haptic feedback |

  A quick menu tile for one wires `is_on` to `stock_toggle_get` and its action to `stock_toggle_set(name, !on)`.
- `repair_path(missing, out, size)` in `MosaicPlugin` is asked about each line of `recent.txt` whose file is no longer on the card, which stock drops. It can write the full path the file moved to and return 1, or return 0; the first plugin in load order to answer wins. The new path is used only when it's on the card (a game or pak) and isn't already a recent, and recents are saved with it, keeping the line's place and name; the launcher logs both repairs and refusals. It runs whenever recents load: at start and when the lists reload. Lines whose file is there never reach it.
- When a plugin's `init` declines to start, everything it registered goes with it: its actions (and an action it re-registered goes back to the earlier one), `bind_button` bindings, overrides, quick menu tiles, hidden and renamed tiles, `inhibit_sleep` hold and a `power_off` request.
- `before_sleep`, `after_resume` and `before_poweroff` follow what the device does. Holding the power button to shut down, or pressing a power-off button, runs `before_poweroff` instead of `before_sleep`, as the quick menu's Poweroff does. When stock powers off on its own after the suspend timeout, the device is already asleep, so `before_sleep` has run, and `before_poweroff` runs as the launcher exits, after stock's "Powering off" screen and with the display already shut down. The quick menu's Sleep runs `before_sleep` and `after_resume` too. `before_poweroff` runs at most once.

## Without writing C

Shell paks can hook into the launcher too:

- **Before a launch:** `Tools/<platform>/<name>.pak/pre-launch.sh` runs before every game or pak launch, in pak name order. It gets `HOOK_PHASE=pre`, `HOOK_TYPE` (`rom` or `pak`), `HOOK_ROM_PATH` and `HOOK_PAK_PATH`. Exiting 1 cancels the launch; any other failure is logged and the launch goes ahead. This sits alongside stock NextUI's `.userdata/<platform>/.hooks/*.d/` folders, which still work and can't cancel.
- **Staying awake:** while `/tmp/stay_awake` or `/tmp/stay_alive` exists, the launcher holds off auto-sleep. Create it during a long download or sync and remove it afterwards. Stock only powers off on its own after a period asleep, so this also keeps the device from powering off. The power button still sleeps the device, and stock's power-off after the suspend timeout still applies once it's asleep.

## When a plugin crashes

The launcher marks plugin loading as in progress and clears the mark once its first frame is on screen. If it crashes before then, the next start pauses plugins and runs without them. The Launcher pak shows "plugins paused after a crash" and **Try again** turns them back on.

The launcher logs why each plugin did or didn't load to `.userdata/<platform>/logs/nextui.txt`, as `plugins: ...` lines.
