# Writing plugins

To write a plugin of your own, start from [`template/`](../template/): copy it into a new repo, and it builds the plugin and packages it as a pak.

A plugin is a shared object the launcher loads at startup. It's written against one header, [`sdk/mosaic_plugin.h`](../sdk/mosaic_plugin.h), which is MIT licensed and doesn't include anything from NextUI, so a plugin can use any license.

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
optional=
provides=sets.favorites
ambient=
description=Keeps a list of favorite games
```

- `id` must match the `id` in the plugin's `MosaicPlugin`.
- `setting.<key>=<type>|<label>|<default>[|<options>[|<flags>]]` declares a setting the Launcher pak's Plugins page shows and edits. `type` is `toggle` (`on` or `off`), `choice` (one of the comma-separated `options`) or `text` (typed on the on-screen keyboard). The plugin reads the value with `host->cfg_get(key, default)`. Changes take effect the next time the launcher starts, which is when the user leaves the Launcher pak. The plugin's `name`, each `label` and a choice's `options` are translated on the Plugins page by the `strings` slot's owner as the plugin's own text, with its id and no context, as `host->tr` translates it, so they go in the plugin's own language packs (see [Translations](#translations)). A choice is still saved as its option in English, and a text setting's value shows as typed.
- `flags`, after the options (leave the options empty for a toggle or text setting, as in `text|Code|||protected,secret`), is a comma-separated list of markers. `protected` makes the Plugins page ask for the launcher's PIN before it shows or changes that setting, and before it turns the plugin off (or turns the whole launcher off while the plugin is on); with no PIN set, a protected setting behaves like any other. `secret`, for `text` settings, makes the value write-only: once it's set, the page shows only Set or Not set, and its keyboard starts empty, so the value is replaced rather than edited. `public` lets other plugins read the setting with `cfg_read` (see [Plugins' settings](#plugins-settings)); it's ignored on a protected or secret setting, which stays private, and the Plugins page shows a public setting like any other. `button:<action id>` says the setting picks the button the plugin puts that action on, such as `button:romm.flashback` on romm's `button` setting, so `action_info` (see [Features added since the first API](#features-added-since-the-first-api)) can name the setting a binding follows; a setting can carry more than one, one per action, and it changes nothing else. Words the launcher doesn't know are ignored, and a setting without flags behaves as before. An older launcher ignores the field, showing the setting as an ordinary one.
- `api` is the `MOSAIC_API_VERSION` the plugin was built against. A plugin for a different version is skipped.
- `slots` lists exclusive slots the plugin takes over, such as `root` for `root_replace`, `layout` for the `layout_*` hooks, `transition` and `launch` for the `transition_*` hooks, `background` for `background_render`, `home` for a screen in place of the root list, `switcher` for a screen in place of the game switcher, `strings` for `translate`, or `theme` for `theme`. Only one plugin can own a slot.
- `requires` lists plugins that must also load. `conflicts` lists plugins that can't load alongside it.
- `optional` lists plugins it works with when they're there: when one of them loads, this plugin loads after it, so it's already started by this plugin's `init` (see `plugin_loaded` in [Features added since the first API](#features-added-since-the-first-api)), and when one is off, missing or refused, this plugin loads anyway. It changes only the order, never which plugins load: the launcher first decides which plugins load from `plugins.txt`, `slots`, `conflicts` and `requires`, in the usual order (so the later of two conflicting plugins still gives way, wherever `optional` would put it), and then moves each plugin that loads after the loading plugins its `optional` names, the rest keeping their order. `requires` doesn't order anything, so list a plugin in both to require it and load after it. Plugins that name each other, directly or through others, load in their usual order, and the launcher logs which of them loads before a plugin it names. A plugin naming itself is ignored.
- `provides` lists the capabilities it offers other plugins, which they check with `has_capability`. Names are `<kind>.<name>`: `sets.<name>` for an entry set it registers, with `sets.tags` and `sets.smart` for the `tags:<tag>` and `smart:<collection>` families, `events.<event>` for an event it publishes, such as `events.music.track`, and a service's own name, such as `captions.text`, for a service it provides with `provide`, which refuses a name not listed here (see `provide` in [Features added since the first API](#features-added-since-the-first-api)). Other kinds are for later host features. Names are matched exactly, case included. The launcher's own features aren't listed here; plugins check those with `MOSAIC_HOST_HAS`.
- `ambient` lists the screens it makes ambient screens with `register_ambient`, such as `screensaver.main`, so the Launcher page can list them in the ambient screens' order without loading the plugin (see `register_ambient` in [Features added since the first API](#features-added-since-the-first-api)). `register_ambient` refuses a screen it doesn't list.
- `description` is one line about the plugin, which other plugins read with `plugin_info`, up to 255 bytes.
- `category` and `summary` are for the docs only, and the launcher ignores them. In a bundled plugin, `category` is the group `docs/plugins.md` lists it under (`Looks`, `Library`, `Play time`, `Power`, `Network` and the others `scripts/plugin-table.sh` names) and `summary` is its one-sentence plain description there. `make lint` checks that the index matches them.

## Translations

A plugin ships its own translations as gettext `.po` language packs in its `res/lang/`, one `<code>.po` for each language, such as `res/lang/es.po` or `res/lang/pt_BR.po`. `res/` goes wherever the plugin goes, `Launcher.pak/plugins/<name>.plugin/res/` for a bundled plugin and `Tools/<platform>/<name>.pak/res/` for one installed as its own pak, and the `strings` slot's owner finds it through `host->plugin_info`. A pack holds the plugin's name, setting labels and choice options from its `plugin.txt` and the fixed text it passes to `host->tr` and `host->tr_plural` or gives `register_action` as a label, each `msgid` keyed on the English. There's no `msgctxt` for the plugin's id, since the file is already the plugin's: `msgctxt` is only a context the plugin passes to `host->tr` itself, to tell the same English apart. The template the packs are made from is `po/<id>.pot`, kept in the plugin's source and not shipped. The bundled `localization` reads a plugin's pack for the language it's set to (`pt_BR` falling back to `pt`), and before it the user's own `Languages/<id>/<code>.po` on the card, string by string, so a user can change or add to a plugin's strings; a plugin with no pack for the language shows its English. A pack that doesn't parse is refused whole, which leaves only that plugin's strings in English, and the launcher log names the file and the line.

```
plugins/hello/plugin.txt
plugins/hello/po/hello.pot      the template, from make pot
plugins/hello/res/lang/es.po    shipped in res/
```

For the bundled plugins, `make pot` writes each `plugins/<name>/po/<id>.pot` and `make lint` checks they're current. The template in `template/` has an example `po/example.pot` and `res/lang/es.po`, and its `make pak` packs `res/`.

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

Plugins in this repo live in `plugins/<name>/` (a `plugin.txt` plus `.c` files). They include only `sdk/mosaic_plugin.h` and libc's headers, with these exceptions: `video-previews` also includes ffmpeg's headers, for its types, but never links ffmpeg, loading it with `dlopen` instead and checking each library's major version before using it; `romm` links BearSSL, from `vendor/BearSSL`, into its own `plugin.so` for HTTPS; and a plugin's helper programs (see "Helper programs" below) are programs of their own, not part of `plugin.so`, so they can use other libraries, such as SDL2 for `second-screen-display`'s window and `musicd`'s audio, and dr_libs, from `vendor/dr_libs`, for `musicd`'s decoding. A plugin that needs more than its own `.c` files has a `plugin.mk` beside its `plugin.txt` with its rules. `make plugins-device` builds them all for devices in the tg5040 toolchain, `make plugins-desktop` for the desktop build, and `make pak` bundles them into `Launcher.pak/plugins/`. `make run-desktop` runs the desktop build with them available; turn one on with `<id>=on` in `/var/tmp/nextui/sdcard/.userdata/desktop/launcher/plugins.txt`.

## Helper programs

A plugin can ship helper programs beside its `plugin.so`, for work that shouldn't run inside the launcher's process: a daemon that outlives the launcher, such as music that plays on while a game runs; a window of its own; or work that would take the launcher down with it if it crashed. A helper is an ordinary program, built for each architecture like `plugin.so` and put in the same `lib/<arch>/` folder:

```
Example.plugin/
    plugin.txt
    lib/aarch64/plugin.so
    lib/aarch64/example-helper
```

Two bundled plugins have one. `music`'s `musicd` is a daemon that plays the music, started the first time something is played and left running while the launcher exits for a game; the plugin talks to it over `/tmp/mosaic-music.sock`, it writes its pid to `/tmp/mosaic-music.pid`, and it logs to `.userdata/<platform>/logs/music.txt`. `second-screen`'s `second-screen-display` is an SDL2 program that shows the second window, started with a socket as its stdin, and quits when that socket closes, as it does when the launcher quits or is killed. In this repo, a plugin's `plugin.mk` builds its helpers and names them in `PLUGIN_SHIP`, the files `plugins/makefile` copies beside the plugin's `plugin.so` (eg. `music: PLUGIN_SHIP = $(MUSICD) $(MUSIC_LICENSE)`). For a plugin in a pak of its own, [`template/`](template/) builds an example helper from its `helper/` folder beside `plugin.so` and packs it into the pak; its README says how to go without it.

A helper follows these rules:

- It's started by its path, found from the folder `plugin.so` is in with `dladdr` on one of the plugin's own functions, never through a shell command line or `PATH`.
- It's started with `fork` and `exec` (or `posix_spawn`), with none of the launcher's open files left open in it, and with `LD_PRELOAD` and `DYLD_INSERT_LIBRARIES` taken out of its environment, so libraries preloaded into the launcher aren't loaded into it.
- Any socket, pid file or other file it keeps under `/tmp` is named for the plugin, eg. `/tmp/mosaic-<id>.sock`.
- It's stopped by its own pid or through its own socket, never by name (no `pkill` or `killall`).
- It quits when its socket closes, so one isn't left running once the launcher is gone. A daemon that outlives the launcher on purpose quits when it's told to through its socket or sent `SIGTERM` by its pid, as `musicd` does before a power-off, and removes its socket and pid file as it goes.
- It logs to `.userdata/<platform>/logs/<name>.txt`, from `host->userdata_path`, eg. by the plugin opening that file as its stdout and stderr.

## Hooks and threads

The bundled plugins, with a page for each, are listed in [plugins.md](plugins.md).

Every hook in `MosaicPlugin` is optional; leave out what you don't use. The launcher calls plugins only on its main thread, and plugins call the host only from it too, with one exception: `audio_queue` and `audio_queued` are safe from a plugin's own threads. Both `MosaicPlugin` and `MosaicHost` start with their own `size`, and fields are only ever added at the end, so a plugin built against an older header keeps working with a newer launcher.

## Turning plugins on and off

On the device, open **Launcher** from Tools and choose **Plugins**. Each plugin shows as On or Off; selecting one flips it, and the change takes effect when you leave the Launcher screen. Plugins that claim an exclusive slot are grouped under a header for it, in the order `slots` lists them above (a plugin with more than one is under the first), and the rest come last, under Other plugins when there's a group before them. Each group is A-Z by name, ignoring case and accents. This is only the page's order: plugins still load in the order below.

Behind that, `.userdata/<platform>/launcher/plugins.txt` holds the user's choices:

```
favorites=on
grid=off
slot.layout=carousel
```

Plugins listed there load first, in that order, and then the rest, each moved after the plugins its `optional` names (see [plugin.txt](#plugintxt)). `slot.<name>=<id>` picks which plugin owns an exclusive slot. Each plugin's own settings, from `cfg_set`, live in `.userdata/<platform>/launcher/<id>.cfg`.

### The PIN

The PIN row at the end of the Plugins page sets the launcher's one PIN, which simple mode asks for to leave and the page asks for before protected settings. Changing it needs the current PIN first; typing nothing removes it. The PIN is kept in `.userdata/<platform>/launcher/pin.txt`, a file of the launcher's own rather than a plugin's cfg, so no plugin can read it with `cfg_get`; plugins only ask the launcher to check what the user types (`has_pin` and `ask_pin`). It's never logged, and while it's typed to check it the keyboard shows a `*` for each character. The right PIN unlocks every protected setting until the user leaves the Launcher pak. A wrong one only says so, and there's no lockout: the PIN keeps a child or another user out of the settings, not someone with the card. A PIN simple mode kept in its own cfg (its old `pin` setting) becomes the launcher's PIN the next time the launcher or the Launcher pak starts, unless one is already set, and is blanked there.

A forgotten PIN is cleared off the device: put the card in a computer and delete `.userdata/<platform>/launcher/pin.txt`. Protected settings then behave like any other, and simple mode leaves after a confirm. Simple mode can also be left by setting `on=off` in `simple-mode.cfg` or `simple-mode=off` in `plugins.txt`.

### Plugins' settings

`host->cfg_get(key, fallback)` and `host->cfg_set(key, value)` act only on the calling plugin's own settings, its `<id>.cfg`; they take no plugin id. The launcher knows which plugin is calling from the hook or callback it's running for it (`init`, every `MosaicPlugin` hook, screens' hooks, actions, quick menu tiles and the dialogs' callbacks), on the launcher's thread. A call from a thread of the plugin's own, or anywhere else the launcher can't tell who's calling, is refused rather than guessed at: `cfg_get` returns the fallback, `cfg_set` saves nothing, and the launcher logs the first one. Read what a thread needs before starting it, and hand it over.

Another plugin's settings are read, never written, with `host->cfg_read(other_id, key, fallback)`, and only those that plugin's `plugin.txt` marks `public`, while it's loaded. Every other setting, its protected and secret ones always, as well as keys its `plugin.txt` doesn't list, is private: `cfg_read` returns the fallback, as for a key that isn't set. A plugin's own `cfg_get` still sees all of its settings. The bundled plugins that read each other's settings mark those public: row-badges' `unplayed` and `tiers` (playtime-captions), playtime-captions' parts and `format` (switcher-plus) and charging-screen's `idle` (screensaver).

These rules keep plugins honest, but they aren't a sandbox. Plugins run in the launcher's own process, so a plugin can read the card's files, including every plugin's `.cfg` and the launcher's PIN file, and the launcher's memory directly. Only install plugins you trust. Secret settings, such as a password, aren't encrypted on the card: secret only keeps them off the Plugins page and out of other plugins' `cfg_read`.

## Features added since the first API

Newer host fields may be missing on an older launcher. Check before calling:

```c
if (MOSAIC_HOST_HAS(host, toast)) host->toast("Added to Favorites");
```

- `toast(message)` shows a short message at the bottom of the screen.
- `inhibit_sleep(id, on)` holds off auto-sleep until the same plugin releases it.
- `confirm(message, yes_label, on_yes, data)` asks a yes/no question full screen; `on_yes` runs after it closes, only if the user pressed A.
- `keyboard(title, initial, on_done, data)` lets the user type text; `on_done` gets the text, or `NULL` if they cancelled with Y. A types the highlighted key, B deletes, X toggles shift, START finishes.
- `has_pin()` and `ask_pin(title, on_done, data)` use the launcher's PIN (see [The PIN](#the-pin)) without handing it to the plugin. `has_pin` returns 1 when one is set. `ask_pin` opens the keyboard under `title`, starting empty and showing a `*` for each character typed, and checks what's typed; `on_done(result, data)` runs after it closes with `MOSAIC_PIN_RIGHT`, `MOSAIC_PIN_WRONG` or `MOSAIC_PIN_CANCELLED`. With no PIN set, `on_done` runs right away, from inside `ask_pin`, with `MOSAIC_PIN_RIGHT`. Neither logs what's typed, and there's no lockout after wrong tries. Check for them with `MOSAIC_HOST_HAS(host, ask_pin)`.

Both dialogs return straight away and call back once they close, so open them from an action (a button press), not from a list hook.
- `art_path` in `MosaicPlugin` lets a plugin pick the thumbnail, folder background or list background for an entry.
- `root_replace()` in `MosaicPlugin` returns the whole root list in place of the stock one, or `NULL` to keep stock's. Only the owner of the `root` slot (`slots=root` in `plugin.txt`) is asked, and other plugins' `root_entries` aren't added to a replaced root unless it sets `root_with_entries`. Recents still load for the game switcher.
- `open_path(path, type)` opens a game, pak or folder the way selecting it in the game list would, including `pre_launch` and recents, and a folder slides in as it does from the list. It returns 1 when it opened or the launch is queued, and 0 for a missing file, a cancelled launch, or a call from `init`, before the lists exist.
- `recents_count()`, `recent_path(index)`, `recent_remove(index)` and `recents_clear()` read and change the recently played list, newest first. `recent_path` returns the full path, or `NULL` past the end. Changes are saved at once.
- `reload_lists()` lists the open folders again from the card on the next frame, keeping each selection on the same entry where it can. Call it after deleting or renaming a game or clearing recents. A folder other than the root that the reload empties is closed, as if the user had backed out of it.
- `sort_compare(a, b, dir_path)` in `MosaicPlugin` orders a list, such as pinned games first or by a number in the name. The launcher sorts a folder its own way first, then by every plugin's `sort_compare` in load order: for each pair, the first plugin to return non-zero decides (negative puts `a` first), and a pair no plugin has an opinion on keeps the launcher's order. Return 0 for lists you don't handle. Recents and collections are passed too, in their own order. The letter index L1 and R1 jump through follows the final order.
- `list_ready(list, dir_path)` in `MosaicPlugin` sees a whole list once it's filtered, named and sorted, before it's shown, so a plugin can rename entries based on their siblings, such as titles that collide. Plugins run in load order. It can't add, remove or reorder entries. If a name changed, the list is sorted again by name and then by `sort_compare`, as for `display_name`, except recents and collections, which keep their order.
- `entry_set_name(entry, name)` renames an entry from inside `list_ready`. The new name is what the list draws and what the letter index uses. It returns 0, or -1 anywhere else.
- `current_list()` returns the list the user is looking at, read with `list_count` and `list_get`, or `NULL` before the lists exist. Don't keep it past the call you got it in: it's freed when the user leaves the folder or the lists reload. `selected_index()` returns the selected row in it, or -1 when it's empty. `select_index(index)` moves the selection to a row, scrolling only as far as it takes to show it, and returns 0, or -1 for a row that isn't there or during `layout_render`, the row decorators, header widgets or `selection_changed`. Call them from an action, `tick` or `layout_input`, not from a list hook.
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
- `led_count()` returns how many LEDs the device's LED settings control, and 0 on devices without controllable LEDs and on desktop. Set them with `led_set_color`, below, rather than through the platform's files in `/sys`: the launcher's LED rules set those again whenever the device starts or stops charging, gets low on battery, mutes, or sleeps.
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
- `led_set_color(id, rgb)` lights every LED in one static colour, `rgb` as `0xRRGGBB`, at the brightness the user's LED settings give each LED, and calling it again changes the colour. It's stock's own LED override, the one a game's ambient mode uses, starting from the user's LED settings, so stock's charging, critical battery, mute, low battery and sleep lights still take priority, and the plugin's colour comes back once they end. `led_clear(id)` puts the user's own LED settings back. The launcher also clears it after the plugin's `quit`, which runs whenever the launcher exits, as for every launch (the Launcher pak, where the plugin is turned off, included), and after every plugin's `before_poweroff`, so turning the plugin off always gives the user's colour back. A game sets the LEDs its own way, and plugins start again from `init` when the launcher comes back, so a colour set from `init` returns with it. Nothing is saved: the user's LED settings files are never written. Only one plugin's colour is on at a time: the first to set one keeps the LEDs until it clears them or quits, and until then another plugin's `led_set_color` returns -1, which the launcher logs. `led_set_color` returns 0, or -1 when there are no LEDs (`led_count()` is 0, as on desktop), while another plugin's colour is on, or for `rgb` past `0xFFFFFF`. `led_clear` returns 0, or -1 when the plugin's colour isn't on.
- `theme_color(index)` returns one of stock's theme colours from Settings as `0xRRGGBB`, or -1 for any other index. Each is what stock draws with now, without its alpha.

  | Index | Constant | Stock setting |
  |---|---|---|
  | 1 | `MOSAIC_COLOR_MAIN` | Main Color |
  | 2 | `MOSAIC_COLOR_ACCENT` | Primary Accent Color |
  | 3 | `MOSAIC_COLOR_ACCENT2` | Secondary Accent Color |
  | 4 | `MOSAIC_COLOR_LIST_TEXT` | List Text |
  | 5 | `MOSAIC_COLOR_LIST_TEXT_SELECTED` | List Text Selected |
  | 6 | `MOSAIC_COLOR_HINT` | Hint info Color |
  | 7 | `MOSAIC_COLOR_BACKGROUND` | Background Color |

  The three calls came in together, so an LED plugin checks for one of them and for LEDs, and reports itself unsupported without them:

  ```c
  if (!MOSAIC_HOST_HAS(host, led_set_color) || host->led_count() == 0) return 1;
  int accent = host->theme_color(MOSAIC_COLOR_ACCENT);
  if (accent >= 0) host->led_set_color("led-status", (unsigned)accent);
  ```
- `bt_supported()`, `bt_enabled()`, `bt_connected_address(out, size)` and `bt_connect(address)` let a plugin see and reconnect Bluetooth devices. `bt_supported()` returns 1 on a device with Bluetooth and 0 elsewhere, which today is desktop, where `bt_enabled` and `bt_connected_address` report nothing and `bt_connect` refuses. `bt_enabled()` returns the user's Bluetooth toggle from Settings or the quick menu, 1 or 0. It's read only: plugins can't turn Bluetooth on or off.

  `bt_connected_address(out, size)` writes the address of the device connected now, as `XX:XX:XX:XX:XX:XX` in upper case, and returns 1, or writes `""` and returns 0 when none is, while Bluetooth is off, or before the launcher's first look. `size` must be at least 18. The launcher looks on a thread of its own, with `hcitool con` (or `bluetoothctl devices Connected` where there's no `hcitool`), every 5 seconds while Bluetooth is on and the device is awake, and once right after it wakes, so the call only copies the last answer and is cheap enough for `tick`. When more than one device is connected it's the first one listed. Nothing runs until a plugin first calls it or `bt_connect` with Bluetooth on, so a launcher without such a plugin behaves as stock.

  `bt_connect(address)` asks a paired device to connect, as `bluetoothctl connect <address>`, and returns at once: 0 when the connect is queued, or -1 for an address not in `XX:XX:XX:XX:XX:XX` form (hex, either case), while Bluetooth is off or unsupported, or while another connect is queued or running, from any plugin. The launcher logs each refusal. The connect runs on the launcher's thread, never through a shell, and is stopped after 15 seconds; the launcher looks for the connected device again as soon as it ends, so watch `bt_connected_address` to see whether it took. When the launcher exits, as it does for every launch, a connect or look still running is stopped first, and plugins start again from `init` when it comes back. Stock never records which device was last connected, so a reconnect plugin saves the address itself, only when it changes:

  ```c
  if (!MOSAIC_HOST_HAS(host, bt_connect) || !host->bt_supported()) return 1;

  // in tick: remember the device while it's connected
  char address[18];
  if (host->bt_connected_address(address, sizeof(address))
      && strcmp(address, host->cfg_get("last", "")) != 0) {
      host->cfg_set("last", address);
  }

  // in init and after_resume: ask for it back
  const char* last = host->cfg_get("last", "");
  if (host->bt_enabled() && last[0]) host->bt_connect(last);
  ```
- Drawing calls let a plugin draw with the launcher's own fonts, theme colours and scale, without linking SDL or launcher code. Check for them with `MOSAIC_HOST_HAS(host, draw_text)`. Nothing here draws on the screen by itself: plugins draw on surfaces of their own, and the render hooks that come later hand a plugin the surface to draw on, such as the screen, with the area it may draw in, so a plugin can draw a whole layout or a row's badge with the same calls. Plugins call them on the main thread, from any hook. Every call checks its arguments: a `NULL`, a surface the launcher doesn't know (such as one already freed, or one a hook handed over after the hook returned), or a size or rect out of range is refused, with -1 or `NULL`, rather than drawn or crashed on.

  `screen_width()` and `screen_height()` return the screen's size in pixels, and `screen_scale()` returns stock's `FIXED_SCALE`, how many pixels one unit of stock's layout is (2 or 3 on today's devices), so a plugin can size things as stock does, eg. a pill `30 * screen_scale()` tall.

  A `MosaicSurface` is something to draw on or draw from, and plugins only ever hold a pointer to it. `surface_new(id, w, h)` makes a transparent one, `image_load(id, path)` reads a PNG or JPEG, and `surface_scale(id, src, w, h, fit)` makes a `w` by `h` copy of `src`, fitted by `MOSAIC_FIT_STRETCH` (filling it, changing the shape), `MOSAIC_FIT_CONTAIN` (keeping the shape, centred, with the rest transparent) or `MOSAIC_FIT_COVER` (keeping the shape and filling it, cropping the edges). Scaling picks the nearest pixel, so scale art once with `surface_scale` rather than stretching it with `blit` every frame. `id` is the plugin's own id, which must be a loaded plugin's. Each surface is up to 4096 pixels a side, and one plugin's surfaces together up to 32 MiB; past that the call returns `NULL` and the launcher logs it. `image_load` reads only a regular file of up to 16 MiB (not a folder, FIFO or device, and never through a shell), opened without blocking, and returns `NULL`, logging why, for anything else or a file it can't decode.

  A plugin owns the surfaces it makes. `surface_free(id, surface)` frees one and returns 0, or -1 for a surface that plugin didn't make, including one a hook handed over, which the launcher keeps. The launcher frees whatever a plugin still has after its `quit`, which runs whenever the launcher exits, and when its `init` declines, logging how many, so a plugin that keeps its surfaces for its whole run doesn't need to free them.

  `surface_size(surface, &w, &h)` writes a surface's size, and `read_pixel(surface, x, y, &color)` writes the colour of one pixel. Both return 0, or -1 for a bad argument or a point outside the surface.

  `MosaicRect` is `{ x, y, w, h }` in a surface's pixels from its top left, and `MosaicColor` is `{ r, g, b, a }`, with `a` 255 for opaque. `mosaic_color(rgb, a)` builds one from `0xRRGGBB`, such as `theme_color` returns, so a plugin uses stock's theme through `theme_color` above. `fill_rect(target, rect, color)` fills a rect, and `fill_round_rect(target, rect, radius, color)` fills it with its corners rounded by `radius`, at most half its shorter side, so a radius of half its height draws a pill. `blit(target, src, src_rect, dst_rect)` draws `src_rect` of `src` (`NULL` for all of it) into `dst_rect` (`NULL` for the same size at 0,0), stretching it when the sizes differ. A `NULL` rect is the whole target. All three blend by alpha (a fill at 255 replaces what's there), clip to the target, and to the area a hook allows on a surface it hands over, and return 0, or -1 for a bad argument, a rect with a negative size, or drawing a surface onto itself.

  Text is drawn in the launcher's font, at the sizes stock uses: `MOSAIC_FONT_LARGE` (list rows and menus), `MOSAIC_FONT_MEDIUM`, `MOSAIC_FONT_SMALL` (button hints), `MOSAIC_FONT_TINY` and `MOSAIC_FONT_MICRO`. Each call takes one line of UTF-8: anything after a newline is left out. `text_width(font, text)` returns its width in pixels and `text_height(font)` the height of a line. `text_truncate(font, text, max_width, out, size)` writes `text` to `out`, shortened with `...` when it's wider than `max_width` or longer than `size` allows, cutting only between characters, and returns its width; when not even `...` fits it writes `""` and returns 0. `draw_text(target, font, text, x, y, color, max_width)` draws it with its top left at `x`, `y`, truncated the same way when `max_width` is above 0, and returns the width it drew. Each returns -1 for a bad argument.

  ```c
  if (!MOSAIC_HOST_HAS(host, draw_text)) return 1;
  int scale = host->screen_scale();
  MosaicSurface* badge = host->surface_new("row-badges", 40 * scale, 12 * scale);
  MosaicRect pill = { 0, 0, 40 * scale, 12 * scale };
  host->fill_round_rect(badge, &pill, pill.h / 2, mosaic_color(host->theme_color(MOSAIC_COLOR_ACCENT), 255));
  host->draw_text(badge, MOSAIC_FONT_TINY, "NEW", 4 * scale, 1 * scale,
      mosaic_color(host->theme_color(MOSAIC_COLOR_LIST_TEXT_SELECTED), 255), 32 * scale);
  ```
- `repair_path(missing, out, size)` in `MosaicPlugin` is asked about each line of `recent.txt` whose file is no longer on the card, which stock drops. It can write the full path the file moved to and return 1, or return 0; the first plugin in load order to answer wins. The new path is used only when it's on the card (a game or pak) and isn't already a recent, and recents are saved with it, keeping the line's place and name; the launcher logs both repairs and refusals. It runs whenever recents load: at start and when the lists reload. Lines whose file is there never reach it.
- When a plugin's `init` declines to start, everything it registered goes with it: its actions (and an action it re-registered goes back to the earlier one), `bind_button` bindings, overrides, quick menu tiles, hidden and renamed tiles, `inhibit_sleep` hold, LED colour, `power_off` request and the surfaces it made.
- `before_sleep`, `after_resume` and `before_poweroff` follow what the device does. Holding the power button to shut down, or pressing a power-off button, runs `before_poweroff` instead of `before_sleep`, as the quick menu's Poweroff does. When stock powers off on its own after the suspend timeout, the device is already asleep, so `before_sleep` has run, and `before_poweroff` runs as the launcher exits, after stock's "Powering off" screen and with the display already shut down. The quick menu's Sleep runs `before_sleep` and `after_resume` too. `before_poweroff` runs at most once.
- The `layout` slot lets a plugin draw the game list in place of the built-in list, such as a grid or a carousel. A plugin with `slots=layout` in its `plugin.txt` sets `layout_init`, `layout_input` and `layout_render` in `MosaicPlugin`; only the slot's owner is called (`slot.layout=<id>` in `plugins.txt` picks it), and a plugin with them but no `slots=layout` is logged and never called. They're called only for a folder with entries: an empty folder shows the launcher's own message. The launcher keeps the selection, the scroll window and the letter index, its button hints, the status pill and the background, and every button the layout leaves alone, so opening, backing out, resume, letter jumps, the quick menu, the game switcher and plugins' bindings all work as with the built-in list. Under a layout the launcher doesn't draw its selection pill, scrolling name or thumbnail.

  Each hook gets a `MosaicListView` of the list as the launcher has it this frame: `list` (read with `list_count` and `list_get`), `count`, `selected`, the built-in list's window `start` to `end - 1` of `rows` rows, `depth` (1 on the root), `path`, `letter` (the selected entry's group in the letter index L1 and R1 jump between) of `letter_count`, `show_names` (0 when stock would show the folder's art in place of names), `show_art` (Show game art is on), `art` (the selected entry's thumbnail, as chained `art_path` and stock's `.media` folder resolve it, or its box, 3D box or screenshot when it has no thumbnail (see the art kinds below), when `show_art` is on and the file exists, or `""`), `area` (where the layout draws: the whole screen above the button hints), `status` (the status pill's rect in `area`'s top right, already drawn, to leave clear) and `header` (plugins' header widgets and a shell pak's status pill left of `status`, already drawn, to leave clear too, or empty with none; see below). Its `size` says which fields the launcher has; fields are only added at the end.

  `layout_init(view)` runs once, before the layout's first input or render. It returns 0 to take the game list, or anything else to leave it to the built-in list until the launcher restarts; the plugin's other hooks keep working either way. `layout_input(input, view, &consumed)` runs every frame before the game list's own input. `MosaicInput` has the buttons as masks of `MOSAIC_BUTTON_BIT(button)`: `pressed` and `released` this frame, `repeated` (pressed, or repeating while held, at the pace stock moves through the list) and `held`. The layout sees `MOSAIC_BUTTON_UP`, `DOWN`, `LEFT` and `RIGHT` as well as the buttons bindings use; MENU, the power button and the FN buttons stay with the launcher (input filters, below, see them). It moves the selection with `select_index` rather than keeping a selection of its own, and sets `consumed` (0 on entry) to the buttons it handled, which the game list then ignores for that frame. A grid takes the d-pad and leaves A, B and the shoulders, so the hints stay true; a layout that takes a button the hints name should take its release too. `layout_render(screen, view)` draws the list on `screen`, a surface lent for the call with the drawing calls above, over the background and status pill and clipped to `view->area`, so the hints below stay as they are. It runs on frames the launcher redraws; call `request_redraw` from `tick` to animate (a call from `layout_render` itself is lost, as that frame is already being drawn). It only draws: `select_index` returns -1 during it, and the screen can't be freed or drawn on after it returns.

  Each hook returns 0, or anything else for an error. After an error the launcher logs it and draws the built-in list from that frame on, until it restarts, so a layout that can't draw never leaves the user without a list. A crash while drawing the first frame pauses plugins on the next start, as any crash while starting does (see below).

  ```c
  // a grid of 4 columns: down moves a row, through the launcher
  static int layoutInput(const MosaicInput* in, const MosaicListView* view, unsigned int* consumed) {
      if (in->repeated & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_DOWN)) {
          if (view->selected + 4 < view->count) host->select_index(view->selected + 4);
          *consumed |= MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_DOWN);
      }
      return 0;
  }
  ```
- Art kinds and the art cache. `art_path` can now also answer `MOSAIC_ART_BOX` (the front of a game's box), `MOSAIC_ART_BOX_3D` (its box drawn in 3D), `MOSAIC_ART_LOGO` (its wheel or logo), `MOSAIC_ART_SCREENSHOT` and `MOSAIC_ART_BACKDROP` (wide art to draw behind the list). The launcher has no place of its own for these, so only plugins supply them, and an `art_path` hook should return 0 for kinds it doesn't know, since newer launchers ask for more. When a game has no thumbnail, the built-in list shows the first of its box, 3D box or screenshot that a plugin supplies and that exists, and `MosaicListView`'s `art` names the same file. With no plugin answering, the list shows exactly what stock does.

  Check for the calls with `MOSAIC_HOST_HAS(host, art_get)`. `art_find(entry, kind, out, size)` writes where an entry's image of a kind is, as plugins' `art_path` hooks in load order and then the launcher's own places answer it (stock's `.media` thumbnail and backgrounds), and returns 1 when the file exists, or 0 when it doesn't or nothing answers (writing `""`). Called from an `art_path` hook it returns 0.

  `art_get(entry, kind, &image)` gives that image decoded, from the art cache the launcher's own thumbnails use too. It returns `MOSAIC_ART_READY` with the image, `MOSAIC_ART_LOADING` while it's decoded off the main thread (the launcher redraws once it's in, so a layout just asks again on the next frame it draws), or `MOSAIC_ART_MISSING` when `art_find` finds no file or it can't be decoded. `art_load(path, &image)` does the same for an image by its full path, such as a save state's preview. Images are keyed by path and kind, and each is a PNG, JPEG or BMP regular file of up to 16 MiB and 4096 pixels a side, read as `image_load` reads one. Asking for the selected entry's art also loads that kind for the entries either side of it, so they're in when the selection moves by one. Only the latest asks count: when the selection moves, loads asked for before it that haven't finished are dropped, so ask each frame you draw rather than once.

  The images are the launcher's, and read-only: draw them with `blit`, read them with `read_pixel` and `surface_size`, or make a copy of your own with `surface_scale`, which counts against your 32 MiB. `surface_free` and drawing onto one return -1. An image stays valid until the next frame starts; after that it may have been dropped from the cache, and calls refuse it, so don't keep it. The cache holds up to 32 MiB of decoded images for the launcher and every plugin, apart from each plugin's own 32 MiB, and drops the least recently used first; the thumbnail on screen is never dropped.

  ```c
  // a grid cell's art, drawn once it's in
  const MosaicSurface* art = NULL;
  if (host->art_get(entry, MOSAIC_ART_BOX, &art) == MOSAIC_ART_READY) host->blit(screen, art, NULL, &cell);
  ```

- Row decorators draw badges and captions on the game list's rows, such as an unplayed dot or a playtime, under the built-in list or any plugin layout. Every plugin's `decorate_row(screen, entry, row)` in `MosaicPlugin` runs, in load order, for each row the built-in list draws, after the row's name. `MosaicRow` has `index` (the entry's index in the list), `selected` (1 on the selected row), `rect` (the row) and `name` (the part of `rect` the name takes, from its left, with `w` 0 when no name shows), and begins with its `size`. The hook draws on `screen`, lent for the call and clipped to `rect`; on the built-in list a row is as wide as its pill can be, so the top row stops short of the status pill and header widgets, and a row beside the thumbnail stops short of it. The selected row's pill is drawn under the screen and its scrolling name over it, so a badge on that row stays in view outside `name`. A layout runs the decorators on its own cells: it calls `host->decorate_cell(screen, index, rect, name)` from `layout_render`, with the screen it was lent, after drawing each cell, and `name` can be `NULL` when it doesn't say. That draws them clipped to the cell and to `view->area`, and returns 0, or -1 outside `layout_render`, from inside a decorator, for another surface, an index not in the list or a cell outside `view->area`. A layout that never calls it shows no decorations.
- Header widgets add items beside stock's status pill (clock, battery, wifi): on the game list, the game switcher, the quick menu, the dialogs and plugin screens without `MOSAIC_SCREEN_FULL`, though not the Launcher pak's own screens. A plugin sets both `header_width(max_width, height)` and `header_widget(screen, rect)` in `MosaicPlugin`; one with only one of them is logged and never asked. Each frame the launcher redraws, it asks each in load order how wide its widget is, given the room left and the pill's height, and lays them out right to left from the pill, the first in load order next to it, with stock's margin between them. While a shell pak's status file shows (see "Without writing C" below), its pill comes first, next to stock's, and the widgets go left of it. A widget wider than `max_width` is cut to it, and together the widgets take at most half the header left of the pill, so stock's clock and battery never move and the top row, the game switcher's title and the dialogs' titles keep room, shortened by what the widgets take. `header_width` returns the width, 0 for no widget this frame, or below 0 for an error; `header_widget` then draws on `screen`, lent and clipped to `rect` (that wide, and as tall and high as the pill), and returns 0. Call `request_redraw` from `tick` to update one. With no widgets, the header and the rows under it are stock's.

  Check for both with `MOSAIC_HOST_HAS(host, decorate_cell)`. The hooks only draw: `select_index` returns -1 during them. Each returns 0, or anything else for an error, after which the launcher logs it and stops calling that plugin's decorators, or leaves out its widget, until it restarts; its other hooks keep working.

  ```c
  // an unplayed dot at the end of each game's row
  static int decorateRow(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRow* row) {
      if (host->entry_type(entry) != MOSAIC_ENTRY_ROM || played(entry)) return 0;
      int size = row->rect.h / 4;
      MosaicRect dot = { row->rect.x + row->rect.w - size * 2, row->rect.y + (row->rect.h - size) / 2, size, size };
      return host->fill_round_rect(screen, &dot, size / 2, mosaic_color(host->theme_color(MOSAIC_COLOR_ACCENT), 255)) == 0 ? 0 : -1;
  }
  ```
- Selection events tell plugins which entry the user settled on, for companion screens and previews. `selection_changed(entry)` in `MosaicPlugin` runs, in load order, once each time the game list's selection settles on another entry, rather than every frame while the user scrolls. The selection has settled once it has gone `host->selection_settle_ms` (250) without moving and no d-pad or shoulder button (L1, R1, L2, R2) is held, so holding a direction through a list, or tapping faster than that, gives one call at the end. It's about the folder and the entry together, by path: the selection the launcher starts on counts once it settles, and so does a folder's when it's opened or backed out to, even on the same game in another folder such as Recently Played; a reload that keeps the same entry selected doesn't, and neither does moving away and back before it settles. Moves a layout or a plugin makes with `select_index` count like the user's. `entry` is the selected entry, or `NULL` in an empty folder; don't keep it, as it goes with its list, so copy its path if you need it later.

  It runs on the main thread, after the frame's input and plugins' `tick`, and before the frame is drawn, never from inside a drawing hook. Anything `tick` can call works from it, except `select_index`, which returns -1 during it: a plugin moving the selection from its own event would hear of it again and could loop. Move it from `tick` or an action instead. Check for it with `MOSAIC_HOST_HAS(host, selection_settle_ms)`. It returns 0, or anything else for an error, after which the launcher logs it and stops calling that plugin's `selection_changed` until it restarts; its other hooks keep working. A preview that should wait longer, such as one that starts a clip after 2 seconds, starts its own timer here and checks it in `tick`.

  ```c
  // start a preview 2 seconds after the selection settles on a game
  static char pending[512];
  static unsigned long due = 0, frame = 0;

  static void tick(unsigned long now) {
      frame = now;
      if (due && now >= due) {
          due = 0;
          startPreview(pending);
      }
  }

  static int selectionChanged(const MosaicEntry* entry) {
      stopPreview();
      due = 0;
      if (!entry || host->entry_type(entry) != MOSAIC_ENTRY_ROM) return 0;
      snprintf(pending, sizeof(pending), "%s", host->entry_path(entry));
      due = frame + 2000 - host->selection_settle_ms;
      return 0;
  }
  ```
- `entry_info(entry, key, out, size)` reads an entry's `.info` metadata sidecar (see "Without writing C" below), for display hooks such as `display_name`, `decorate_row` and `layout_render`, eg. a band with a game's year, publisher and genre. It writes the value of `key`, matched without case, to `out`, cut between characters to fit `size`, and returns 1, or writes `""` and returns 0 when the entry has no sidecar or the sidecar has no such key, and returns -1 for a bad argument. The sidecar is read the first time a plugin asks about the entry, and kept with the entry until its list is freed, when the user leaves the folder or the lists reload, so asking each frame doesn't read the card again. A list read again, or an entry from `entry_new`, reads it again. Nothing reads sidecars until a plugin asks. Check for it with `MOSAIC_HOST_HAS(host, entry_info)`.

  ```c
  // names a game by its sidecar
  static int displayName(const MosaicEntry* entry, char* out, size_t size) {
      return host->entry_info(entry, "name", out, size) == 1 && out[0];
  }
  ```
- `blit_alpha(target, src, src_rect, dst_rect, alpha)` draws as `blit` does, with `src`'s alpha scaled by `alpha`, from 255 (the same as `blit`) to 0 (nothing), so a plugin can fade one surface over another, even an opaque one such as a transition's frame. It returns 0, or -1 for a bad argument or an `alpha` outside 0 to 255. Check for it with `MOSAIC_HOST_HAS(host, blit_alpha)`.
- Transition styles let a plugin pick how the launcher moves from one frame to the next: its own animation and timing, stock's, or none. They're two exclusive slots, so only one plugin draws a given transition, picked like the layout (`slot.transition=<id>` or `slot.launch=<id>` in `plugins.txt`): a plugin with `slots=transition` is asked about pushes (`MOSAIC_TRANSITION_PUSH`, opening a folder, where stock slides the new list in), pops (`MOSAIC_TRANSITION_POP`, backing out of one) and screen changes (`MOSAIC_TRANSITION_SCREEN`, the game list to the game switcher, where stock slides the switcher down over the list), and one with `slots=launch` about launches (`MOSAIC_TRANSITION_LAUNCH`, a game or pak starting from the game list or game switcher, from the last frame into black, where stock shows nothing). One plugin can own both. The user's Menu transitions setting wins over the transition slot: with it off there are no menu transitions, so its owner isn't asked about pushes, pops or screen changes, which run as stock's do with the setting off. The launch slot's owner is still asked. A plugin with the hooks but neither slot is logged and never asked. The game switcher's slide between recents and the fades in after a game and after waking stay stock's. With no plugin owning a slot, its transitions are exactly stock's.

  Each time one of its kinds runs, the owner's `transition_begin(transition)` in `MosaicPlugin` gets a `MosaicTransition`: `kind`, `old_frame` (what was on screen) and `new_frame` (what replaces it), both lent read-only until the transition ends, so they can be drawn and read but not drawn on or freed, `stock_ms` (how long stock's runs at the user's Menu transitions setting, 0 for a launch, where it shows none), and `duration_ms` and `ease`, which start as `stock_ms` and `MOSAIC_EASE_OUT`. It returns `MOSAIC_STYLE_STOCK` for stock's transition, `MOSAIC_STYLE_OFF` to show the new frame at once, or `MOSAIC_STYLE_CUSTOM` to draw it, after setting `duration_ms` and `ease` (`MOSAIC_EASE_LINEAR`, `_IN`, `_OUT` or `_IN_OUT`, cubic; one the launcher doesn't know runs linear) if it wants other timing. For a custom transition, `transition_frame(screen, transition, progress)` draws each frame on `screen`, a screen-sized surface lent for the call and filled with opaque black before it, at `progress` from 0 to 1 along the ease. The launcher keeps the time: frames come about 60 a second until `duration_ms` has passed by the clock, however long each takes to draw, and the last has `progress` exactly 1. `duration_ms` is cut to 0 to `host->transition_max_ms` (1000), logged the first time, so a transition can't hold up input for longer; 0 draws one frame, at 1. Input waits until a transition ends, as with stock's. Both hooks only draw: `select_index` returns -1 during them. Check for transitions with `MOSAIC_HOST_HAS(host, transition_max_ms)`.

  `MosaicTransition` also says where the game list's selection is in each frame, so a transition can move it, such as gliding the pill to the centre as a game starts: `old_row` and `old_pill` in `old_frame`, and `new_row` and `new_pill` in `new_frame`, in the frame's pixels. On the built-in list, the row is the selected row as row decorators get it (as wide as its pill can be, stopping short of the status pill and header widgets on the top row), and the pill is the selection pill on it, the same as the row's `name`, where it comes to rest (a selection that only just moved can show it still on its way there); with names hidden there's no pill. Under a plugin layout, the row is the last cell the layout passed to `decorate_cell` for the selected entry in that frame, cut to `view->area`, and there's no pill, as the layout draws its own highlight. Launches, pushes, pops and screen changes fill the old frame's, when it's the game list; pushes and pops fill the new frame's too, and a launch's or screen change's new frame has none. Each rect is empty (`w` 0) where it isn't known: a frame that isn't the game list, such as a launch from the game switcher, an empty folder, a layout that didn't pass the selected entry to `decorate_cell`, or no pill, so check `w` before using one. They were added after `MosaicTransition` first shipped: check for them with `MOSAIC_TRANSITION_HAS(transition, new_pill)`.

  Any other value from `transition_begin`, `MOSAIC_STYLE_CUSTOM` without a `transition_frame`, or a `transition_frame` that returns anything but 0 is an error: the launcher logs it, draws stock's transition in its place, and stops asking the plugin, for every kind it owns, until it restarts. A plugin that only turns transitions off or picks stock's for some kinds needs no `transition_frame`.

  ```c
  // a crossfade, a little slower than stock's slides
  static int transitionBegin(MosaicTransition* t) {
      if (t->kind == MOSAIC_TRANSITION_LAUNCH) return MOSAIC_STYLE_STOCK;
      t->duration_ms = 300;
      t->ease = MOSAIC_EASE_IN_OUT;
      return MOSAIC_STYLE_CUSTOM;
  }

  static int transitionFrame(MosaicSurface* screen, const MosaicTransition* t, float progress) {
      int w = host->screen_width(), h = host->screen_height();
      MosaicRect all = { 0, 0, w, h };
      host->blit(screen, t->old_frame, NULL, &all);
      return host->blit_alpha(screen, t->new_frame, NULL, &all, (int)(progress * 255));
  }
  ```
- Generated art lets a plugin supply an image it made, such as a title card for a game with no art, where `art_path` can only name a file. `art_surface(entry, kind)` in `MosaicPlugin` is asked only when no file answers for that entry and kind (an enum `MosaicArtKind`): no plugin's `art_path` names a file that exists and the launcher's own places have none. So real art always comes first, whatever the load order, and generated art never hides it; load order only picks which generating plugin wins. The launcher asks each plugin with `art_surface` in load order, and the first to return a surface wins. Return `NULL` for no art, and for kinds you don't know.

  The surface must be one the plugin made with `surface_new`, `image_load` or `surface_scale`, and the launcher takes it: it moves into the art cache, off the plugin's 32 MiB and into the cache's 32 MiB, and is read-only from then on, like loaded art. The plugin must not draw on it, free it or return it again (`surface_free` and drawing onto it return -1), and the launcher frees it when the cache drops it. `art_get` hands it out like loaded art, `MOSAIC_ART_READY` at once, to the plugin itself, other plugins and layouts, and the built-in list shows the first of a thumbnail, box, 3D box or screenshot one when a game has no art file, as it does with files (`MosaicListView`'s `art` stays `""`, as there's no file). `art_find` still reports files only, so a plugin can check whether real art exists. Folder backgrounds still come only from files. The answer, an image or `NULL`, is kept until the cache drops it, such as when it's full or the lists reload, and only then is the plugin asked again, so don't count on being asked each frame.

  It runs on the main thread while a frame is drawn, so keep it quick. It only draws: `select_index` returns -1, and `art_get` gives no generated art, during it. Returning a surface the plugin didn't make, or one already handed over, is an error: the launcher logs it and stops asking that plugin until it restarts; its other hooks keep working. Check for it, and for the background slot, with `MOSAIC_HOST_HAS(host, plugin_art)`.

  ```c
  // a plain card for games with no box art
  static MosaicSurface* artSurface(const MosaicEntry* entry, int kind) {
      if (kind != MOSAIC_ART_BOX || host->entry_type(entry) != MOSAIC_ENTRY_ROM) return NULL;
      int scale = host->screen_scale();
      MosaicSurface* card = host->surface_new("art-placeholder", 100 * scale, 140 * scale);
      if (!card) return NULL;
      host->fill_rect(card, NULL, mosaic_color(host->theme_color(MOSAIC_COLOR_ACCENT), 255));
      host->draw_text(card, MOSAIC_FONT_SMALL, host->entry_name(entry), 4 * scale, 4 * scale,
          mosaic_color(host->theme_color(MOSAIC_COLOR_LIST_TEXT_SELECTED), 255), 92 * scale);
      return card;
  }
  ```
- The `background` slot lets a plugin draw what's behind the game list, such as the selected game's art dimmed or full-bleed. It's an exclusive slot, since one background covers the whole screen, picked like the layout (`slot.background=<id>` in `plugins.txt`): only a plugin with `slots=background` in its `plugin.txt` has its `background_render(screen, selected)` in `MosaicPlugin` called, and one with the hook but not the slot is logged and never called. Each frame the game list redraws, before the list draws, it gets `screen`, a screen-sized surface lent for the call and filled with what stock shows there (the folder's background stretched to the screen, and clear where stock shows the theme's background colour), and the selected entry, or `NULL` in an empty folder. What it draws goes on the screen's background layer in place of stock's, under the list, the selection pill, the thumbnail and a plugin layout, and transitions start from frames with it on. When stock's background changes under it, such as a folder's `bg.png` finishing loading, the launcher redraws and asks again. Call `request_redraw` from `tick` to animate it; art it asks for with `art_get` redraws the list once it's in. The game switcher, quick menu and dialogs keep stock's background. With no plugin owning the slot, the background is exactly stock's.

  It only draws: `select_index` returns -1 during it, and the surface can't be freed or drawn on after it returns. It returns 0, or anything else for an error, after which the launcher logs it and shows stock's background until it restarts.

  ```c
  // the selected game's backdrop, dimmed, behind the list
  static int backgroundRender(MosaicSurface* screen, const MosaicEntry* selected) {
      const MosaicSurface* art = NULL;
      if (!selected || host->art_get(selected, MOSAIC_ART_BACKDROP, &art) != MOSAIC_ART_READY) return 0;
      MosaicRect all = { 0, 0, host->screen_width(), host->screen_height() };
      host->blit(screen, art, NULL, &all);
      return host->fill_rect(screen, NULL, mosaic_color(0x000000, 160));
  }
  ```
- Hiding names lets a plugin show an entry as its art alone, such as a folder shown as its cover. `hide_name(entry, dir_path)` in `MosaicPlugin` returns 1 to hide the entry's name, or 0. It's asked once for each entry each time a folder is listed, after `list_ready`, when the names and order are final, so the answers are kept with the list rather than asked each frame; plugins are asked in load order, and the first to return 1 hides it. It's a list hook like `filter`: it can't rename entries or move the selection.

  The built-in list leaves a hidden name out while Show game art is on: the row is blank, and when it's selected there's no pill or scrolling name, only the entry's art beside the list. A selected entry with no art to show (no thumbnail, box, 3D box or screenshot, from a file or a plugin) keeps its name and pill, so the selection can always be found, and with Show game art off every name shows. Row decorators still run on hidden rows, with `name` empty. Layouts get the answers in `MosaicListView`'s `names_hidden`: one byte for each of `count` entries, in list order, 1 where a name is hidden, or `NULL` when none in the folder is. A layout decides how a hidden name looks, such as drawing the entry's art in its place, and should still show the name where it has no art. Decorators get `MosaicRow`'s `name_hidden`, 1 on a hidden entry's row or cell. With no plugin hiding anything, the list is exactly stock's. Check for the hook with `MOSAIC_HOST_HAS(host, hide_names)`, and for the new fields with `MOSAIC_VIEW_HAS(view, names_hidden)` and `MOSAIC_ROW_HAS(row, name_hidden)`.

  ```c
  // folders show as their cover
  static int hideName(const MosaicEntry* entry, const char* dir_path) {
      (void)dir_path;
      char path[512];
      return host->entry_type(entry) == MOSAIC_ENTRY_DIR && host->art_find(entry, MOSAIC_ART_BOX, path, sizeof(path));
  }
  ```
- Play history: `play_time(path)`, `last_played(path)`, `play_count(path)` and `play_time_today()` read stock's play tracker, which is what the Game Tracker tool shows: `gametimectl` keeps it in `.userdata/shared/game_logs.sqlite`, adding a session each time a game launches (and each time the device wakes from sleep in one) and closing it when the launcher starts again. `path` is a game's full path on the card, as `entry_path` and `recent_path` return it, so it works for rows, layouts and the game switcher's recents alike. `play_time` returns the seconds the game has been played in all, `last_played` when its latest session started, as a Unix time, and `play_count` how many sessions it has. A game the tracker hasn't seen, or a path outside `Roms`, gets 0 from each. Games carried over from Onion as orphans are matched by name the way `gametimectl` matches them. `play_time_today()` returns the seconds played across every game since local midnight, counting the part of a session from before midnight that ran past it, for things like a daily limit. Each returns `MOSAIC_PLAY_UNKNOWN` (-1) when the history can't be read: there's no database yet (`gametimectl` makes it at the first launch of a game), no sqlite library, or the database can't be opened, isn't one, or stays busy past a short timeout; a NULL `path` gets -1 too. The launcher never writes the database. Nothing reads it, or loads sqlite, until a plugin first asks; then the library is loaded with `dlopen`, the database is opened read-only, everything the calls answer is read in one go, and it's closed again. The answers are kept until a game or pak returns, just before `post_return` (and once more after the launcher's `gametimectl stop_all` closes the last session), so rows and header widgets can ask every frame without touching the file; today's total moves on by itself at midnight. A session `gametimectl` hasn't closed yet counts up to the time it's read. Check for them with `MOSAIC_HOST_HAS(host, play_time)`.

  ```c
  // "1h 05m" after a game's name
  long long seconds = host->play_time(host->entry_path(entry));
  if (seconds > 0) snprintf(caption, sizeof(caption), "%lldh %02lldm", seconds / 3600, seconds / 60 % 60);
  ```
- Art decorators draw details under the game's art, such as its year, publisher and genre. Every plugin's `decorate_art(screen, entry, art, below)` in `MosaicPlugin` runs, in load order, each frame the built-in list draws the selected game's thumbnail beside the list, after the rows. `art` is where the thumbnail is, as the list sizes and places it, and `below` is the free space under it: the art's column, as wide as the widest thumbnail the Game art width setting allows (as wide as the art for a wide image, wider for a tall one) and stopping where the rows do, from the art's bottom down to the button hints. The hook draws on `screen`, lent for the call and clipped to `below`. The thumbnail is on a layer of its own over the screen, and the background on one under it, so what the hook draws shows over the background, and as `below` never overlaps `art`, the thumbnail never covers it, nor it the thumbnail. Transitions start from the frame with both.

  It only runs while a thumbnail shows: not with Show game art off, for an entry with no art (no thumbnail, box, 3D box or screenshot, from a file or a plugin), in an empty folder, or when there's no room under the art. While the selected game's image is still loading, the list keeps showing the last game's art and the hook isn't called; once the image is in, the list draws again with it, so the art and what's under it are always the same game's. A layout runs the decorators on its own art: it calls `host->decorate_art(screen, index, art)` from `layout_render`, with the screen it was lent, after drawing the art of the entry at `index`, such as the selected game's. `below` is then the part of `view->area` under `art`, as wide as it. It returns 0, also when there's no room under the art, or -1 outside `layout_render`, from inside a decorator, for another surface, an index not in the list or art outside `view->area`. A layout that never calls it shows no art decorations. Of the bundled layouts, `layout-fancy` calls it with the rect around the art its panel drew, and `layout-carousel` with the selected cover down to the bottom of its name strip, so decorations go under the name.

  Check for both with `MOSAIC_HOST_HAS(host, decorate_art)`. The hook only draws: `select_index` returns -1 during it. It returns 0, or anything else for an error, after which the launcher logs it and stops calling that plugin's `decorate_art` until it restarts; its other hooks keep working. With no art decorators, nothing is lent and the list is exactly stock's.

  ```c
  // the year under the art, centred on it
  static int decorateArt(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRect* art, const MosaicRect* below) {
      char year[16];
      if (host->entry_info(entry, "year", year, sizeof(year)) != 1) return 0;
      int x = art->x + (art->w - host->text_width(MOSAIC_FONT_SMALL, year)) / 2;
      host->draw_text(screen, MOSAIC_FONT_SMALL, year, x > below->x ? x : below->x, below->y,
          mosaic_color(host->theme_color(MOSAIC_COLOR_HINT), 255), below->w);
      return 0;
  }
  ```

- Input filters see the buttons before anything else does but the launcher's lock (see `lock` below), which takes every button before them while it's locked. Every plugin's `input_filter(input, &consumed)` in `MosaicPlugin` runs, in load order, each frame on every screen: the game list, the game switcher, the quick menu, the dialogs (`confirm`, `keyboard` and `choose`) and plugin screens. It runs right after the launcher reads the pad, before `tick`, the power button, the FN buttons' actions and the screen's own input, a layout's `layout_input` included. `input` is a `MosaicInput` with every button in `enum MosaicButton`, including `MOSAIC_BUTTON_MENU`, `MOSAIC_BUTTON_POWER` and `MOSAIC_BUTTON_FN1` to `FN3`, which were added at the end of the enum for it, less what filters earlier in load order took. It sets `consumed` (0 on entry) to the buttons to take, as masks of `MOSAIC_BUTTON_BIT(button)`; they're taken off `pressed`, `repeated` and `released` for the rest of the frame, as with `layout_input`, and `held` stays as the pad has it. The FN buttons are the ones stock's Settings lets the user assign: L3 and R3 on the Brick, L4, R4 and HOME on the Brick Pro and HOME on the tg5050, while a device without one never presses it. `bind_button`, `override_button` and `layout_input` don't take MENU, POWER or the FN buttons: binding or overriding one returns -1, as for the d-pad. `fn_bind` (below) binds an FN button the way stock's Settings does.

  A long power press always gets through. A power press the filters take doesn't sleep, but held for a second it still powers off, as stock's does, running `before_poweroff` first, whatever the filters take. As with stock, a press in the first second after the launcher starts or wakes is ignored, so holding it doesn't power off. Take the power button on its press: once a press has got through, its release does too, or stock would power off a second after the press. The volume keys aren't in the enum and can't be taken: stock's keymon changes the volume, and the brightness with MENU held, outside the launcher, so the change and its overlay happen whatever a filter takes. The Launcher pak's settings screen is a screen of its own that loads no plugins, so no filter runs there.

  `hold_switch()` in `MosaicHost` returns the position of the switch on the side of the tg5040 and tg5050, 1 or 0, or -1 on a device without one, such as the h700, and on desktop. Stock's keymon reads the switch five times a second and keeps it as msettings' mute state, the one stock's Mute settings in Settings act on, so `hold_switch` reads that state as stock's own code does, and 1 is muted. A change can take a fifth of a second to show, and the switch keeps doing what the user's Mute settings say whatever a plugin does with it. It's cheap enough to ask every frame.

  Check for `input_filter`, `hold_switch` and the new buttons with `MOSAIC_HOST_HAS(host, hold_switch)`; an older launcher never calls `input_filter`. A filter returns 0, or anything else for an error, after which the launcher logs it, takes nothing for that call and stops calling that plugin's `input_filter` until it restarts; its other hooks keep working. With no filters the pad is exactly stock's.

  ```c
  // takes every button while the switch is on, except a long power press,
  // which the launcher lets through
  static int inputFilter(const MosaicInput* input, unsigned int* consumed) {
      (void)input;
      if (host->hold_switch() == 1) *consumed = ~0u;
      return 0;
  }
  ```

- RetroAchievements progress: `achievements(path, &progress)` and `achievement_hash(path, out, size)` read, read-only, what minarch keeps on the card under `.userdata/shared/.ra/` for a game it has loaded with RetroAchievements on: the achievement set and unlocks it fetched (`offline/cache/achievementsets_<hash>.bin` and `startsession_<hash>.bin`, each `[u32 length][JSON][SHA-256 of the JSON]`; an older `patch_<hash>.bin` stands in for the set), the unlocks it made offline (`offline/ledger.bin`) and the achievements muted in its menu (`muted/<hash>.txt`). Nothing is fetched over the network. `path` is a game's full path on the card, as `entry_path` and `recent_path` return it. `achievements` fills in a `MosaicAchievements`: `total` (the core achievements of the sets in the cache, unofficial ones left out), `unlocked` (of them, in either mode, from the session and the ledger), `unlocked_hardcore`, `points` and `points_unlocked`, `muted` and the game's RetroAchievements `game_id`. Set `progress.size = sizeof(MosaicAchievements)` first: the launcher fills in only the fields that fit, so fields are added at the end. It returns 1, 0 when there's nothing on the card for the game (no cached set, a system rcheevos can't hash, such as the Atari 5200, a path that isn't a file, or a cache file that's damaged: one whose length or SHA-256 doesn't match is ignored, as minarch ignores it), or `MOSAIC_ACHIEVEMENTS_UNKNOWN` (-1) until the game's hash is worked out; a NULL `path` gets -1. `achievement_hash` writes the hash, 32 lower-case hex digits, with the same answers.

  minarch names those files by the game's rcheevos hash, so the launcher works it out as minarch does, with rcheevos' own hashing code (the same commit minarch builds) and libchdr for CHD images: the console from minarch's table of folder tags, Mega Drive and PC Engine games with a disc image extension (`chd`, `cue`, `ccd`, `toc`, `m3u`) moved to Sega CD and PC Engine CD, a folder or its own m3u as its first disc, the way the launcher starts it, and a zip's first file unzipped first, as minarch unzips for cores that can't open zips (arcade games hash by name, zipped). For a tag minarch doesn't map, rcheevos guesses from the file, and the launcher takes the guess the card has a set for, as minarch takes the one the server knows. Two cases can differ from minarch: a core that opens zips itself, where minarch hashes the zip as it is, and a core that's handed the file in memory for a C64, MSX or Amstrad CPC m3u, where minarch hashes the m3u's text. Hashing happens on a thread of its own at low priority, one game at a time, only for games a plugin asks about; rcheevos reads at most 64 MiB of a file, and a zip's first file over 64 MiB isn't hashed. The hashes are kept on the card in `.userdata/shared/launcher/ra-hashes.txt` with each file's path (relative to the card), size and modification time, so a game is only hashed again when its file changes. Nothing runs, no thread starts and no file is written until a plugin first asks. The first ask about a path queues it and returns -1; the list is drawn again when answers come in, so rows can ask every frame. An answer stays the same until the launcher restarts, which it does after every game. Check for them with `MOSAIC_HOST_HAS(host, achievements)`.

  ```c
  // "3/12" after a game's name, once the launcher has its hash
  MosaicAchievements progress = { .size = sizeof(MosaicAchievements) };
  if (host->achievements(host->entry_path(entry), &progress) == 1 && progress.total > 0)
      snprintf(caption, sizeof(caption), "%i/%i", progress.unlocked, progress.total);
  ```

- `surface_write(id, surface, rect, pixels, pitch)` copies pixels into `rect` of a surface the plugin made (`NULL` for all of it), for pictures a plugin works out itself, such as decoded video frames. The pixels are RGBA8888: four bytes a pixel in memory, `r`, `g`, `b` then `a`, whatever the machine's byte order, with `a` 255 for opaque and not premultiplied (ffmpeg's `AV_PIX_FMT_RGBA`, SDL's `SDL_PIXELFORMAT_RGBA32`). They start at `rect`'s top left, and each row starts `pitch` bytes after the one before, so `pitch` is at least `rect.w * 4`. The copy replaces what's there, alpha included, rather than blending, and leaves out any part of `rect` past the surface's edges, reading only the pixels that land on it. It returns 0, also when none of `rect` is on the surface, or -1 for a `NULL` (id, surface or pixels), a rect with a negative size, a `pitch` under `rect.w * 4`, or a surface the plugin didn't make or no longer owns: one a hook lent it (such as the screen), an art cache image, one it handed the launcher from `art_surface`, another plugin's, or one already freed. `id` must be the plugin's own, the one that made the surface. It's for the main thread only, like every drawing call, so do any decoding and colour conversion on a thread of your own and hand the finished pixels over to copy; the launcher only copies them, converting into the surface's own format when it differs. Copying a 720x480 frame takes about 0.03 ms on an Apple silicon Mac, about as long as a plain `memcpy` of it. Check for it with `MOSAIC_HOST_HAS(host, surface_write)`.

  ```c
  // in tick, on the main thread: the newest frame the decoder thread finished,
  // shown in place of the selected game's thumbnail (art_replace, below)
  if (frame_ready) {
      host->surface_write(ID, video, NULL, frame_rgba, FRAME_W * 4);
      frame_ready = 0;
      host->art_replace(ID, video);
  }
  ```

- `art_replace(id, surface)` shows a surface the plugin made, and still owns, in place of the selected entry's thumbnail on the built-in list, such as a video preview. It's sized and placed as a thumbnail of the surface's size would be (as wide as the Game art width setting allows and at most 0.6 of the screen tall, keeping its shape, at the right of the screen and centred on its middle), with the rounded corners stock gives a thumbnail that size from the Thumbnail radius setting, on the thumbnail's own layer. Art decorators still run under it, handed its rect. It shows for an entry with no art of its own too, and the rows then make room for it as they do for a thumbnail. `NULL` takes the plugin's surface away again, and the entry's own thumbnail, if it has one, shows. Under a plugin layout the layout draws it, in its own place for the selected entry's art, when it asks for it with `art_replacement` (below).

  The launcher doesn't take the surface, unlike art from `art_surface`: it stays the plugin's, to draw into or write into with `surface_write` while it shows. On the built-in list, calling `art_replace` again with the same surface, at the same size, shows what it holds now, redrawing only the thumbnail layer, not the list (under a layout it redraws the layout, see `art_replacement` below): that's how to show each new frame of a video. Never call `request_redraw` for each frame, since that redraws the whole list, its rows, decorators and background included. A first call, another surface or a new size redraws the list once when it has to, for an entry with no art of its own (the rows make room) or when art decorators are on (they're placed under the new art), and the surface going away, or a `NULL`, redraws it once. On an Apple silicon Mac, showing a new 720x480 frame takes about 2.2 ms of the frame, against about 3.9 ms for redrawing a short list: about 0.15 ms to copy the surface and round its corners, about 1.6 ms for stock's own call that puts an image on a layer, and the rest for the selection pill's layers, which the launcher redraws on these frames too.

  The surface goes away by itself when the selection moves, the game list is left (for the game switcher, the quick menu, a dialog or a game), the folder empties, Show game art goes off, the surface is freed or is no longer the plugin's (such as one handed to the launcher from `art_surface`), or the plugin quits. The next call after that shows it afresh, for whatever is selected then, so check `selected_entry` before showing a frame that belongs to a game, or start from `selection_changed`. Only one plugin's surface shows at a time: the first keeps the place until it goes, and other plugins' calls return -1 meanwhile.

  It returns 0, also for `NULL`, or -1 for a `NULL` id, a surface the plugin didn't make or no longer owns (one a hook lent it, an art cache image, another plugin's or one already freed), another plugin's surface showing, a call from inside a hook that only draws (such as `decorate_art`, `decorate_row` or `layout_render`) or from `init`, or where no art for the selected entry shows: off the game list, in an empty folder or with Show game art off. It's for the main thread only, like every drawing call. Check for it with `MOSAIC_HOST_HAS(host, art_replace)`.

  ```c
  // in selection_changed: a still of the game until its video starts
  if (host->art_replace(ID, still) != 0) {
      // another plugin's art shows, or no art shows here
  }
  ```

- `art_replacement(index, &surface)` lets a plugin layout draw a plugin's `art_replace` surface in its own place for the selected entry's art. Called from `layout_render`, it writes the surface showing for the entry at `index` to `surface` and returns 1, or returns 0 when there's none for it: only the selected entry has one, and only while a plugin shows one. The layout draws it where it draws the selected entry's art, in place of that art, with `blit` or `blit_alpha`, fitting it however it fits its own art. The surface stays the other plugin's, with the same ownership and lifetime as on the built-in list: it's read-only to the layout, which can draw it, size it with `surface_size` or read it with `read_pixel`, but mustn't draw on it, free it or keep it, since it's only good until `layout_render` returns (it goes when the selection moves, and the plugin can free it between frames). Draw it straight from the surface each frame rather than copying it with `surface_scale`, since each new frame is new pixels. It returns -1 outside `layout_render`, from inside a decorator, for a `NULL` `surface` or an index not in the list. Check for it with `MOSAIC_HOST_HAS(host, art_replacement)`.

  A layout owns the whole of its area, so the cheap thumbnail-layer path doesn't apply under one: each new frame `art_replace` shows (the same surface at the same size) draws the whole game list again, the background, header, hints and the layout's `layout_render` included, but only while the layout's last `layout_render` got the surface from `art_replacement`. A layout that never asks for it, such as one from before it, isn't drawn again for frames it doesn't show. A first call, another surface or size, a `NULL` and the surface going away each draw the list again once. On an Apple silicon Mac, drawing the list again for a new 720x480 frame takes about 2.1 ms of the frame under `layout-grid` (about 0.4 ms of it in its `layout_render`, drawing the frame scaled into the tile included) and about 2.3 ms under `layout-fancy` (about 0.9 ms in its `layout_render`), as long as drawing those layouts again with no replacement, and not counting the flip that puts the frame on screen. Every bundled layout that shows the selected game's art asks for it: `layout-grid` draws it in the selected tile, `layout-carousel` in the selected cover and `layout-shelf` on the selected cartridge's label, each fitted as that layout fits its own art, `layout-hero-rail` as the hero, cover-fitted with its tint and fade drawn over it, and `layout-fancy` in its panel in the box's place.

  ```c
  // in layout_render: a plugin's video in the selected tile, or its own art
  const MosaicSurface* replaced = NULL;
  if (MOSAIC_HOST_HAS(host, art_replacement) && host->art_replacement(index, &replaced) == 1)
      host->blit(screen, replaced, NULL, &tile);
  else
      drawArt(screen, entry, &tile);
  ```

- Audio calls let a plugin play sound, such as a video preview's, through SDL's audio queue. Check for them with `MOSAIC_HOST_HAS(host, audio_open)`. One plugin at a time has the device: the first to open it keeps it until it closes it or the launcher does, and other plugins' calls return -1 meanwhile. Nothing starts until a plugin asks: the first `audio_open` starts SDL's audio, and closing the device quits it again, as minarch does, so with no plugin playing the launcher opens no device, as stock's doesn't. (Stock's launcher does open a device of its own after waking from a sleep, which stays as it is.) Volume and mute are the system's, as for stock's own sounds, so there's nothing to set.

  `audio_open(id, rate, channels)` opens the device for frames of signed 16-bit samples in the machine's byte order (`AUDIO_S16SYS`, ffmpeg's `AV_SAMPLE_FMT_S16`), `channels` (1 or 2) to a frame, interleaved, at `rate` frames a second (8000 to 96000). SDL converts them to whatever the device plays, so open at the clip's own rate and leave resampling to it. The device starts playing at once, silence while nothing is queued. Opening again in the same format keeps the device and what's queued and returns 0; in another format it closes the device and opens it again, dropping what was queued, so open once in the format you'll keep if you can. It returns 0, or -1 for a `NULL` or unknown id, a rate or channel count out of range, while another plugin has the device, or when SDL can't open it (the launcher logs why).

  `audio_queue(id, frames, count)` adds `count` frames after what's queued (copying them, so the buffer is yours again when it returns), and returns 0, also for a `count` of 0, or -1 when the plugin doesn't have the device, for a `NULL` `frames` or a negative `count`, or when the queue would then hold more than `audio_queue_max_ms` (1000) of frames, so a stuck plugin can't fill memory; the launcher logs the first refusal past the cap after each open. `audio_queued(id)` returns how many frames are queued and not yet handed to the device, or -1 when the plugin doesn't have the device. The device's own buffer, 1024 frames, plays after them, so what's heard now is about `audio_queued + 1024` frames behind the last frame queued: that's a video's clock, to show each picture when its time comes. Keep a few hundred milliseconds queued, not the whole clip.

  `audio_pause(id, paused)` stops playing with 1, keeping the queue, and plays on with 0; `audio_queued` stays put while paused. `audio_close(id)` closes the device, dropping what's queued. Both return 0, or -1 when the plugin doesn't have the device. Closing the device power-cycles the codec on some devices, which clicks, so don't close it at the end of each clip: keep it open, on silence, while another may start soon (the next selection's clip), and close it after a short linger with none.

  The launcher closes the device itself, whoever has it, before a sleep, once what's queued has played out (it waits at most for the queue and the device's buffer), before a game or pak launches, before it powers off, and when it quits, after plugins' `before_sleep` and `pre_launch` and before their `quit`. After that `audio_queue` and `audio_queued` return -1, and the plugin opens it again when it wants to play, such as after `after_resume`. A plugin that declines to start has its device closed too.

  `audio_queue` and `audio_queued` are the one exception to host calls being for the main thread only: call them from a decoder thread of your own. The launcher holds a lock while either uses the device, so a close, by the plugin or the launcher, waits for one in progress on another thread, and the device is never closed under it; calls after a close are refused with -1. Everything else, `audio_open`, `audio_pause` and `audio_close` included, is for the main thread only, and the launcher refuses those three from another thread with -1, logging it. Stop your threads in `quit`: the launcher has closed the device by then, so they're refused, but the plugin's code is unloaded after `quit` returns.

  ```c
  // init or selection_changed, on the main thread: a 44.1 kHz stereo clip
  if (host->audio_open(ID, 44100, 2) != 0) {
      // another plugin is playing: show the clip without sound
  }

  // the decoder thread: keep about a quarter of a second queued
  while (playing) {
      int queued = host->audio_queued(ID);
      if (queued < 0) break; // closed, by the launcher or the plugin
      if (queued < 44100 / 4) host->audio_queue(ID, pcm, frames);
      else sleep_ms(10);
  }

  // tick, after the clip ends: linger, then close
  if (!playing && now - ended_at > 3000) host->audio_close(ID);
  ```

- Screens let a plugin add screens of its own, such as a search, a stats page or a screensaver, over the game list, the quick menu, the game switcher or another plugin screen. Check for them, and for overlays below, with `MOSAIC_HOST_HAS(host, open_screen)`.

  `register_screen(id, hooks)` adds one, from `init` only. The id is `<plugin id>.<name>` by convention; it returns 0, or -1 outside `init`, for an id that's `NULL`, empty, 64 characters or more, another screen's, or a built-in screen's name (`gamelist`, `switcher`, `quickmenu`, `dialog`, `lock`), for hooks without `render`, or past 32 screens across every plugin. `hooks` is a `MosaicScreenHooks` the plugin fills in with `size` set to `sizeof(MosaicScreenHooks)`, copied when it's registered, as far as its size goes: `data`, passed to each hook; `flags`; and the hooks `enter(data)`, `exit(data)`, `input(input, data)`, `render(screen, view, data)` and `animating(data)`, of which only `render` is required.

  `open_screen(id)` puts the screen on top of what shows and returns 0. Call it from an action's `run`, so a button, an FN button set to `action:<id>` or a quick menu tile from `quick_tile` opens it, or from `tick`, a dialog's callback, or another screen's `input`. It runs the screen's `enter` first, which returns 0 to open or anything else to stay closed. It returns -1 for an unknown screen, one already open (each screen is open at most once), one turned off after an error, past 8 open at once, an `enter` that declined, the switcher slot's screen (below), while a dialog shows or a game or pak is starting, and from `init`, `enter`, `exit` or any hook that only draws. `close_screen(id)` closes that screen and any opened over it, top first, running each one's `exit`, and returns 0, or -1 for a screen that isn't open, while a dialog shows, and from the hooks `open_screen` refuses. Closing goes back to the screen under it: the plugin screen it opened over, or the game list, quick menu or game switcher the first one opened over. `current_screen()` returns what's showing: the top plugin screen's id, `lock` for the launcher's lock screen (see `lock` below), or `gamelist`, `switcher`, `quickmenu` or `dialog`.

  While the screen is on top, its `input` gets the buttons each frame as a `MosaicInput`, the same buttons `layout_input` sees plus `MOSAIC_BUTTON_MENU`, after `input_filter`, `tick` and the FN buttons' actions, and returns `MOSAIC_SCREEN_STAY` or `MOSAIC_SCREEN_CLOSE`. By convention B goes back, so return `MOSAIC_SCREEN_CLOSE` on a B press; a screen without `input` closes on B by itself. The power button, sleep, the volume keys and the FN buttons stay the launcher's. `confirm`, `keyboard`, `choose` and `ask_pin` work from `input`: the dialog shows over the screen and goes back to it once it closes, and `MOSAIC_SCREEN_CLOSE` from the same call is ignored, so close it from the dialog's callback instead. `toast` shows over a screen as anywhere. `open_path` works too: a game or pak launches, with every open screen's `exit` run before it starts, and the launcher comes back to the game list afterwards as usual (or to the home screen, below, when it launched from there); a folder opens on the game list under the screen, so close the screen to show it. If the user opens the quick menu or the game switcher some other way while a screen shows, such as with an FN button, every open screen closes.

  `render` draws on `screen`, the whole screen lent for the call, on frames the launcher redraws: when it opens, after `request_redraw`, and every frame while `animating` returns 1 (it's asked each frame while the screen is on top). The launcher clears what the screen under it left, such as the game list's background, thumbnail and selection pill, as the dialogs do, then draws the header as on its own screens: the status pill, with the shell paks' pill and plugins' header widgets beside it. `view` is a `MosaicScreenView` with the `area` (the whole screen), `status` and `header` rects, which are already drawn and should be left clear, as a layout leaves them, and `screen`, the screen's id. With `MOSAIC_SCREEN_FULL` in `flags` the launcher draws no header (so no brightness or volume bar either) and no overlays, and `status` and `header` are empty, for screens such as a screensaver. The screen draws its own button hints, in stock's pills with `draw_hints` (below); the drawing calls from "Drawing" above all work on it, and `select_index` returns -1 during `render`. Toasts show over it.

  Opening a plugin screen slides in, as opening a folder does, and closing one slides out, at the user's Menu transitions setting, except to or from the game switcher, which brings itself in, or a dialog, which shows at once. A screen with `MOSAIC_SCREEN_INSTANT` in its `flags` opens and closes at once too, with no slide, for a screen that takes over from the one under it in place, such as `video-previews`' clip over the screensaver; closing screens slides only when the lowest of them slides. Check for it with `MOSAIC_HOST_HAS(host, register_ambient)`: a launcher without that slides every plugin screen. The transition slot's owner gets them as `MOSAIC_TRANSITION_PUSH` and `MOSAIC_TRANSITION_POP`, with empty selection rects for the screen's frame.

  An `input` that returns anything but `MOSAIC_SCREEN_STAY` or `MOSAIC_SCREEN_CLOSE`, or a `render` that returns anything but 0, is an error: the launcher logs it, closes the screen on the next frame (running its `exit`), back to the screen under it, and refuses to open it again until it restarts. A crash inside a screen after the launcher's first frame isn't caught by the crash guard (see "When a plugin crashes"): the launcher dies and NextUI starts it again, on the game list with plugins loaded, and the Launcher pak's wrapper hands the device back to stock NextUI after two crashes in a row. Every open screen's `exit` runs when it closes, before a launch and when the launcher quits, before plugins' `quit`.

  ```c
  static int searchRender(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
      MosaicColor text = mosaic_color(host->theme_color(MOSAIC_COLOR_LIST_TEXT), 255);
      host->draw_text(screen, MOSAIC_FONT_LARGE, "Search", 16, view->status.y, text, view->status.x - 32);
      return 0;
  }
  static int searchInput(const MosaicInput* input, void* data) {
      if (input->pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B)) return MOSAIC_SCREEN_CLOSE;
      return MOSAIC_SCREEN_STAY;
  }
  static void openSearch(const char* path, void* data) { host->open_screen("search.main"); }

  // in init
  MosaicScreenHooks hooks = { .size = sizeof(hooks), .input = searchInput, .render = searchRender };
  host->register_screen("search.main", &hooks);
  host->register_action("search.open", "Search", 0, NULL, openSearch, NULL);
  host->quick_tile("search.open", MOSAIC_QUICK_TOP, NULL, NULL, NULL);
  ```

- Overlays draw over every screen, such as a now playing pill. Every plugin's `overlay_render(overlay, view)` in `MosaicPlugin` runs, in load order, each frame the launcher redraws the game list, the quick menu, the game switcher or a plugin screen, after the screen and its toast, on `overlay`, a clear surface the size of the screen lent for the call. What they draw goes on the topmost layer, over the list's thumbnail and scrolling name too, later plugins' overlays over earlier ones'. `view` is the same `MosaicScreenView` a screen gets, so `view->screen` says what's under it and `status` and `header` where the header is. Overlays aren't drawn over dialogs, whose questions and keyboards they could hide, over a screen with `MOSAIC_SCREEN_FULL`, during transitions or while a game or pak starts. They get no input of their own: a plugin that wants buttons for its overlay takes them with `input_filter`. Call `request_redraw` from `tick` to change one; overlays draw only on frames the launcher redraws. It only draws, as `decorate_row` does: `select_index`, `open_screen` and `close_screen` return -1 during it. It returns 0, or anything else for an error, after which the launcher logs it and leaves that plugin's overlay out until it restarts. With no overlays, the screen is stock's.

- The `home` slot lets a plugin's screen stand in for the root list, such as a dashboard: what the launcher opens first and goes back to. It's an exclusive slot, picked like the layout (`slot.home=<id>` in `plugins.txt`). The owner, with `slots=home` in its `plugin.txt`, registers its home screen from `init` with `register_screen` like any other, with `MOSAIC_SCREEN_HOME` in its `flags`; the first one it registers with the flag is the home screen. The flag on another plugin's screen is logged and means nothing there, and an owner with no screen marked home is logged and leaves the root list as it is. Check for it with `MOSAIC_HOST_HAS(host, run_action)`; an older launcher treats the home screen as an ordinary one.

  The home screen opens over the game list as the launcher starts, with no slide, and works as any plugin screen does. Closing it, with `MOSAIC_SCREEN_CLOSE` or `close_screen`, goes on to the game list, sliding in as a folder opens (a `MOSAIC_TRANSITION_PUSH`), so a home screen should close on the button it labels for the games rather than on B; one without `input` closes on B. B at the root of the game list goes back to it, sliding as from a folder (a `MOSAIC_TRANSITION_POP`), and the list shows a B hint there for it; `gamelist.back`, on whatever button it's on, does the same. The game list under it is kept as it was left, so it opens at the last game launched, as stock's list does; `open_path` with a folder opens it there. Other screens can open over the home screen and close back to it.

  Where the launcher goes back to:
  - Back from a game or pak, to where it was launched from: the home screen when it was open, anywhere on the stack, or under a quick menu or game switcher it launched from, and the game list, at the saved last position, otherwise. The launcher keeps which in `/tmp/mosaic-launcher-home` until the next start. Resume and auto-resume are stock's; after the device powers off in a game, auto-resume runs it and the launcher then starts on the home screen, as stock starts on the root.
  - When the start screen is the quick menu or game switcher (Settings' start screen, or the switcher after a game launched from it), B goes back to the home screen in the game list's place, unless the last launch was from the game list.
  - When the quick menu or game switcher opens over the home screen, from `run_action` (below) or an FN button, every open screen closes, as over any plugin screen, and B on it opens the home screen again (running its `enter`) rather than going to the game list. A quick menu tile that opens a folder shows the list, with the home screen behind the root as usual.
  - After sleep the same screen shows, as with stock.
  - After an HDMI change restarts the launcher, the home screen shows again if it was showing.

  A home screen whose `input` or `render` fails closes on to the game list as any screen does, and the root list stays the launcher's own for the rest of the run: B at the root does nothing and nothing opens the home screen again until the launcher restarts. An `enter` that declines leaves the game list showing; B at the root asks it again. With no plugin owning the slot, or one with no home screen, the launcher starts and goes back exactly as stock's does.

- `run_action(action_id, path)` runs an action as a button bound to it would: one from `register_action`, or one of the game list's own listed under `override_button`, such as `gamelist.quick_menu` and `gamelist.switcher`, so a home screen (or any plugin screen) can open the quick menu on MENU and the game switcher on SELECT as the root list does. `path` is the full path of the entry an action that takes one acts on (see `MOSAIC_ACTION_PATH` below), such as a game for `romm.flashback`; with `NULL` or `""` it gets what a button would give it, the game list's selected entry while the game list shows. Every other action gets `NULL` whatever `path` is, so pass `NULL` for them. It returns 0, or -1 for an unknown action, one whose `available` says no (such as `gamelist.back` at the root with nowhere to go back to), one that takes a path given none with none selected, which the launcher logs, and wherever `open_screen` is refused: from `init`, a screen's `enter` or `exit`, any hook that only draws, while a dialog shows or a game or pak is starting. The quick menu or game switcher it opens from a plugin screen closes every open screen, as an FN button does, so whatever a screen's `input` returns with it is ignored. The game list's other actions act on the list under a screen, as they do from an FN button. Check for it with `MOSAIC_HOST_HAS(host, run_action)`.

- The `switcher` slot lets a plugin's screen stand in for the built-in game switcher. It's an exclusive slot, picked like the layout (`slot.switcher=<id>` in `plugins.txt`). The owner, with `slots=switcher` in its `plugin.txt`, registers its switcher from `init` with `register_screen` like any other screen, with `MOSAIC_SCREEN_GAME_SWITCHER` in its `flags`; the first one it registers with the flag, other than its home screen, is the switcher screen. The flag on another plugin's screen is logged and means nothing there, and an owner with no screen marked switcher is logged and leaves the built-in switcher as it is. Check for it with `MOSAIC_HOST_HAS(host, recent_open)`; an older launcher treats the screen as an ordinary one.

  Every way into the game switcher opens the switcher screen in its place: SELECT on the game list, `gamelist.switcher` on whatever button it's on, an FN button set to `action:gamelist.switcher`, `run_action("gamelist.switcher")`, Settings' start screen, and coming back from a game launched from the switcher. It opens as the built-in one does: over the game list, closing every open plugin screen first (running their `exit`), with no slide, and the transition slot's owner isn't asked. `open_screen` refuses it (and logs why), so open it with `run_action("gamelist.switcher")`. Closing it, with `MOSAIC_SCREEN_CLOSE` (or B without `input`) or `close_screen`, goes back at once to the game list, or to the home screen when it opened over that, as the built-in switcher's B does. Other screens can open over it and close back to it, with no slide to or from it. `current_screen()` returns its id while it's on top. The quick menu or an FN button's built-in screen over it closes it, as over any plugin screen. It draws its own button hints, such as stock's with `draw_hints` (below), and gets the overlays plugin screens get.

  The recents and their save states come through the host, alongside `recents_count`, `recent_path` and `recent_remove`:
  - `recent_info(index, out)` fills `out`, a `MosaicRecent` the plugin sets `size` on, for the recent at `index`, newest first: `name`, the name it was launched under as the built-in switcher's title shows it; `type`, `MOSAIC_ENTRY_ROM` or `MOSAIC_ENTRY_PAK`; `available`, 1 when its emulator is installed; `can_resume`, 1 when it has a save state to resume, the last one minarch saved; and `preview`, that state's preview image for `art_load`, or `""` when there's none. It returns 0, or -1 for an index past the end or a `NULL` out. It looks on the card each time, as the built-in switcher does for each frame it draws.
  - `recent_open(index, resume)` opens a recent as the built-in switcher's A does: with `resume` 1 a game resumes that save state when it has one, and with 0 it starts fresh; a pak just launches. Launched while the switcher screen is open, the launcher starts on the switcher screen again after the game, as stock comes back to its switcher. It returns 1 when the launch is queued, or 0 for a bad index, a recent whose emulator isn't installed, a launch `pre_launch` cancelled, and wherever `open_screen` is refused.
  - `recent_remove(index)` removes one, as the built-in switcher's Y does.

  A switcher screen whose `input` or `render` fails closes, and the built-in switcher shows in its place for the rest of the run; its B still goes back to the home screen when the switcher opened over it. An `enter` that declines shows the built-in switcher that time, and the next way into the switcher asks it again. With no plugin owning the slot, or one with no switcher screen, the game switcher is exactly stock's.

  ```c
  static int switcherInput(const MosaicInput* input, void* data) {
      if (input->pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B)) return MOSAIC_SCREEN_CLOSE;
      if (input->released & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_A)) host->recent_open(selected, 1);
      return MOSAIC_SCREEN_STAY;
  }

  // in init
  MosaicScreenHooks hooks = { .size = sizeof(hooks), .flags = MOSAIC_SCREEN_GAME_SWITCHER,
      .input = switcherInput, .render = switcherRender };
  host->register_screen("switcher-plus.main", &hooks);
  ```

- `draw_hints(screen, group, hints, count, drawn)` draws button hints on a plugin screen the way the built-in screens draw theirs, with stock's own drawing: the same pill in the accent colour, button and label fonts, theme colours and place at the bottom of the screen. `group` is `MOSAIC_HINTS_LEFT` for the bottom left, where stock puts the secondary hints (the game switcher's "B BACK"), or `MOSAIC_HINTS_RIGHT` for the bottom right and the primary ones (its "Y REMOVE  A RESUME", or "A OPEN" with nothing to resume). `hints` is one or two `MosaicHint`s, left to right, each a `button` and a `label`: the button is any `enum MosaicButton` but the d-pad, drawn with the name stock gives it (A, B, X, Y, L1, R1, L2, R2, START, SELECT, MENU, POWER, and A, B, X, Y and POWER as stock's button glyphs where the hint style picks them), and `MOSAIC_BUTTON_FN1` to `FN3` by the device's name for them (see `fn_name` below), such as HOME, on a device that has them, and on a launcher with `draw_button` (below) the d-pad and the `enum MosaicGlyph` d-pad glyphs too; the label is one line, which goes through the launcher's translation as stock's labels do, so write it in upper case as stock does (`"BACK"`). Call it once per group, from `render` and only on the `screen` it was lent, on screens with or without `MOSAIC_SCREEN_FULL`. When `drawn` isn't `NULL` it gets the pill's rect, to keep the rest of the screen clear of it. It returns 0, or -1 for any other surface (a plugin's own, an overlay's, a layout's), outside `render`, for a bad `group`, a `count` other than 1 or 2, a button without a name, a `NULL` or empty label, or a pill that wouldn't fit on the screen, would cover the status pill or header, or would go over the other group's pill (or the same group's, drawn twice) this frame. Check for it with `MOSAIC_HOST_HAS(host, draw_hints)`. The built-in screens' own hints are stock's as before.

  ```c
  // the stock switcher's hints
  static int switcherRender(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
      MosaicHint back[] = { { MOSAIC_BUTTON_B, "BACK" } };
      MosaicHint resume[] = { { MOSAIC_BUTTON_Y, "REMOVE" }, { MOSAIC_BUTTON_A, "RESUME" } };
      MosaicHint open[] = { { MOSAIC_BUTTON_A, "OPEN" } };
      MosaicRect hints;
      if (count == 0) host->draw_hints(screen, MOSAIC_HINTS_RIGHT, back, 1, &hints);
      else {
          host->draw_hints(screen, MOSAIC_HINTS_LEFT, back, 1, NULL);
          host->draw_hints(screen, MOSAIC_HINTS_RIGHT, can_resume ? resume : open, can_resume ? 2 : 1, &hints);
      }
      // ... the rest above hints.y
      return 0;
  }
  ```

- The whole play history, for stats across every game rather than one path at a time: `play_games_count()`, `play_game_info(index, out)` and `play_time_on(days_ago)` read the same database as `play_time` above. `play_games_count` returns how many games the history lists: every game with at least one session, each path once (a second row `gametimectl` has for the same path is left out, as `play_time` answers from the first), and games carried over from Onion as orphans, which have no path. `play_game_info` fills `out`, a `MosaicPlayGame` whose `size` the plugin sets to `sizeof(MosaicPlayGame)` (the launcher fills only the fields that fit), for the game at `index`, most played first, then the latest played: `path`, its full path on the card as `entry_path` returns it (the game may no longer be there), or `""` for an orphan; `name`, the name the tracker keeps for it; `seconds`, `last_played` and `count`, as `play_time`, `last_played` and `play_count` return them; `first_played`, when its first session started; and `orphan`, 1 for an orphan. It returns 0, or -1 for an index past the end, a `NULL` `out` or a history that can't be read. `play_time_on` returns the seconds played across every game on the local day `days_ago` days before today, 0 being today (as `play_time_today`), counting the part of each session that falls in that day, so a session over midnight counts on both. Each returns `MOSAIC_PLAY_UNKNOWN` when the history can't be read, as `play_time` does, and `play_time_on` for a negative `days_ago` too. The list comes from the read `play_time` makes; at most the 10000 most played games are read (the rest aren't listed, and `play_time` answers 0 for them), which the log notes. Every session is read only when a plugin first asks for a day, in a read of its own, at most the newest 100000 of them; past that, a day from before the oldest one read gets `MOSAIC_PLAY_UNKNOWN`, and the log says so. Both are kept until a game or pak returns, like the rest, and days move on by themselves at midnight, so a chart can ask for every day each frame. On a desktop history of 5000 games and 100000 sessions over a year the first read takes about 60 ms, the sessions about 40 ms and a year of days under 2 ms the first time, and then microseconds. Check for them with `MOSAIC_HOST_HAS(host, play_time_on)`.

  ```c
  // the five most played games, and this week's total
  MosaicPlayGame game = { .size = sizeof(game) };
  for (int i = 0; i < 5 && host->play_game_info(i, &game) == 0; i++) show(game.path[0] ? game.path : game.name, game.seconds);
  long long week = 0;
  for (int d = 0; d < 7; d++) {
      long long seconds = host->play_time_on(d);
      if (seconds > 0) week += seconds;
  }
  ```

- `wifi_supported()`, `wifi_enabled()` and `wifi_connection(out)` let a plugin show the Wi-Fi state. They're read only: plugins can't turn Wi-Fi on or off or join a network. `wifi_supported()` returns 1 on a device with Wi-Fi and 0 elsewhere, which today is desktop (its stock platform code reports a connection it doesn't have, so the launcher doesn't ask it). `wifi_enabled()` returns the user's Wi-Fi toggle from Settings or the quick menu, 1 or 0, or `MOSAIC_WIFI_UNAVAILABLE` (-1) where there's no Wi-Fi.

  `wifi_connection(out)` fills `out`, a `MosaicWifi` whose `size` the plugin sets to `sizeof(MosaicWifi)` (the launcher fills only the fields that fit), and returns 1 while connected: `ssid`, the network's name as wpa_supplicant reports it; `ip`, the device's address on it, or `""` before it has one; `rssi`, the signal in dBm, below 0, or 0 when it isn't known; `bars`, 1 to 3 as stock's status pill shows that signal (3 from -60 dBm, 2 from -70, 1 below), or 0 when it isn't known; `freq_mhz`, the channel's frequency; and `link_mbps`, the link speed, each 0 when it isn't known. Where `iw` isn't there stock reports -60 dBm for any connection, so `rssi` and `bars` say so too. It clears `out` and returns 0 when Wi-Fi is on but not connected, or off, and `MOSAIC_WIFI_UNAVAILABLE` where there's no Wi-Fi, for a `NULL` `out`, or before the launcher's first look since Wi-Fi came on, so show nothing for -1 and ask again on a later tick.

  Stock's connection call runs `wpa_cli`, `ip` and `iw`, so the launcher makes it on a thread of its own and the call only copies the last answer, cheap enough for `tick`. It looks every 5 seconds while Wi-Fi is on, the device is awake and a plugin has asked in the last 30 seconds, and at once on the first ask, when Wi-Fi comes on, after the device wakes and on the first ask after a quiet spell (which gets the answer from before it until that look is in). Turning Wi-Fi off answers 0 at once. Nothing runs until a plugin first asks with Wi-Fi on, so a launcher without such a plugin behaves as stock. When the launcher exits, as it does for every launch, it waits up to a second for a look still running and then leaves it to finish. Check for them with `MOSAIC_HOST_HAS(host, wifi_connection)`.

  ```c
  MosaicWifi wifi = { .size = sizeof(wifi) };
  int rc = MOSAIC_HOST_HAS(host, wifi_connection) ? host->wifi_connection(&wifi) : MOSAIC_WIFI_UNAVAILABLE;
  if (rc == 1) show("%s, %s", wifi.ssid, wifi.ip[0] ? wifi.ip : "no address");
  else if (rc == 0) show(host->wifi_enabled() == 1 ? "not connected" : "off");
  ```

- `home_screen()` returns the home slot's screen's id (see the `home` slot above), so a plugin can tell when the launcher is on home: `current_screen()` returns the same id while it's on top. It returns the id whether or not the home screen is open, and `NULL` with no plugin owning the home slot, when the owner registered no screen marked `MOSAIC_SCREEN_HOME`, once the home screen failed and the root list took its place for the rest of the run, and from `init`, as the slot is settled only once every plugin has loaded. Check for it with `MOSAIC_HOST_HAS(host, home_screen)`.

  ```c
  const char* home = MOSAIC_HOST_HAS(host, home_screen) ? host->home_screen() : NULL;
  const char* now = host->current_screen();
  int idle_spot = !strcmp(now, "gamelist") || (home && !strcmp(now, home));
  ```

- The `strings` slot lets a plugin translate the launcher, such as from gettext `.po` language packs. It's an exclusive slot, picked like the layout (`slot.strings=<id>` in `plugins.txt`): only a plugin with `slots=strings` in its `plugin.txt` has its `translate(plugin_id, context, text)` and `translate_plural(plugin_id, context, singular, plural, n)` in `MosaicPlugin` asked, and one with them but not the slot is logged and never asked. Every piece of fixed text the launcher draws (button hints, the quick menu's tile names, dialogs, the status pill's "done" and "failed") is keyed on its English, as gettext's `msgid`; `po/launcher.pot` lists them all, with `c-format` on the ones that are printf formats, and `make pot` writes it again. `plugin_id` is `NULL` for the launcher's own strings and the id of the plugin whose text it is for a plugin's (see `tr` below). `context` is a `msgctxt` telling the same English apart where it means different things: the launcher's, such as `systems`, or the one a plugin passed to `tr`, or `NULL` for none. `translate_plural` gets both English forms and the count, and returns the form the language's plural rules pick for `n`; without it, `translate` is asked for the form English picks (`singular` when `n` is 1). Return `NULL` for anything you have no translation of, and the English shows.

  The launcher asks once for each string (and each count), on the main thread, as the string is first drawn, and copies the answer, so what's returned only has to last until the call returns. It asks only once every plugin has loaded, so plugins' `init` sees English. A translation is refused, logged once, and its English shown when it's empty, over 1023 bytes, has a line over 255 bytes or more than 8 lines, or has different printf conversions from its English (for a plural, from either English form): the same conversions in the same order, where `%i` and `%d` count as one, with no `%n` and no positions such as `%1$s`. A translation of plain English (no conversions) can have a `%` in it, but not one that starts a conversion. One that isn't valid UTF-8 is an error: the launcher logs it and shows English everywhere until it restarts. With no plugin owning the slot, every string is the launcher's English, exactly as stock's. The Launcher pak's settings pages load only the `strings` and `theme` slots' owners (not what they `require`), running their `init`, `translate`, `translate_plural`, `fallback_font` and `theme` and, when the pages close, their `quit`, and no other hook; so the pages, and plugins' names, setting labels and options from `plugin.txt` (see `plugin.txt` above), are translated there too.

  The root list's own rows (Recently Played, Collections and Tools) and the system folders, on the root and in the Roms folder, show translated too: a row's whole name with no context, and a system folder's name in the `systems` context, keeping stock's sorting prefix (`001) `) and anything from its tag on (` (GB)`) as they are, so "Game Boy" is asked for `Game Boy (GB)`. `po/launcher.pot` lists the systems NextUI ships. Only the shown name changes: the entry's path, which art, sorting and the selection go by, stays English, and the order is stock's, by the English names. A name from `map.txt` is the user's and isn't translated. The names change after plugins' `filter` hooks and before their `display_name` hooks, which see the translated name.

  `tr(plugin_id, context, text)` and `tr_plural(plugin_id, context, singular, plural, n)` in `MosaicHost` give plugins the owner's translations of their own text the same way. The owner gets `plugin_id`, so each plugin's strings are kept apart from the launcher's and other plugins', and the context as given: `NULL` for none, or one of your own to tell the same English apart. The bundled `localization` looks a plugin's strings up in that plugin's own language packs (see [Translations](#translations)), in the plugin's own context when it gives one. Labels a plugin gives `register_action` are its own strings: the game list's hints, the shared button's menu and the quick menu's tiles show them translated with the plugin's id and no context. A `NULL` `plugin_id` is the launcher's own, so `tr` returns the English for it. With no owner, from `init`, for a string with no translation and for one the launcher refused, they return the English passed in; otherwise the launcher's copy, kept until it quits. Ask with fixed text and put names and numbers in afterwards with printf. Check for them with `MOSAIC_HOST_HAS(host, tr)`. Other text plugins hand the launcher, such as `toast` messages, `confirm` and `keyboard` text, `choose` titles and choices, `rename_quick_tile` labels, entries' names and `draw_text`, is drawn as it's given, so a plugin passes its fixed text through `tr` first, and never game names or what the user typed. The bundled plugins do it with a `tr()` (and `trPlural()`) helper over `host->tr`, and `TR_MARK()`, which does nothing, around English they keep in data and translate where it's drawn; `make pot` finds those, and `register_action` labels, for each bundled plugin's `po/<id>.pot`.

  Text a plugin's SDL-free model builds, such as durations, relative days and counts, stays translatable when the model takes its words from the plugin instead of calling the host: the bundled models keep a small words struct of two function pointers, `tr(text)` and `tr_plural(singular, plural, n)`, set once from `init` with the plugin's own `tr()` and `trPlural()` helpers, and their own `tr()` and `trPlural()` over it, so `make pot` finds the model's strings too. Unset, the model's text is English, which is how its unit tests check the English is unchanged. Ask for a whole format with its count's plural form (`"%i day ago"`, `"%i days ago"`) rather than gluing an English "s" on, and where English reads the same for one as for more, give the same English for both forms so a translation can still have its own. Keep separators such as " · " and the joins between parts out of the translated text, or give the join its own format (`"%s and %s"`). The launcher refuses a translation whose printf conversions differ from the English, including positional ones (`%1$s`), so keep the order of what's put in a format the same in every language.

  The launcher's own text truncation, where it shortens text with "..." (dialog titles and choices, the Launcher pak's rows, the game switcher's title and the quick menu's small tiles), cuts a whole character at a time, so it never leaves half of a UTF-8 character; ASCII comes out as stock's does.

  ```c
  // French from a language pack: plural rule n <= 1
  static const char* translate(const char* plugin_id, const char* context, const char* text) {
      return pack_find(plugin_id, context, text, 0);
  }
  static const char* translatePlural(const char* plugin_id, const char* context, const char* singular, const char* plural,
          unsigned long n) {
      return pack_find(plugin_id, context, singular, n <= 1 ? 0 : 1);
  }

  // in any plugin
  char line[64];
  snprintf(line, sizeof(line), host->tr_plural(MY_ID, NULL, "%d game", "%d games", count), count);
  ```

  `plugins_count()` and `plugin_info(index, out)` in `MosaicHost` list the plugins loaded this run, so the owner can find each plugin's own files, such as a language pack it ships. `plugin_info` fills a `MosaicPluginInfo` (set its `size` first) with the plugin's `id`, its `description` from its `plugin.txt` (`""` without one) and its resource folder, `res`, which is `<its folder>/res` (`Launcher.pak/plugins/<name>.plugin/res` for a bundled plugin, `Tools/<platform>/<name>.pak/res` for one installed as its own pak) and may not be there, and returns 0, or -1 for an index past the end or a `NULL` `out`. They list, in load order, every plugin that's on and loaded, the owner too, and none that's off, was skipped or declined to start. In the Launcher pak's settings pages, which load only the `strings` and `theme` slots' owners, they list every plugin the Plugins page shows instead, loaded or not, so their names, setting labels and options can be translated from their own files. From `init`, before every plugin has loaded, `plugins_count` returns 0 and `plugin_info` -1. Only the fields that fit in the `size` the plugin set are filled, so a plugin built before `description` was added gets only `id` and `res`, and a launcher from before it leaves `description` as the plugin set it (`""` when it starts from `{ .size = sizeof(info) }`). Beyond the description, they only say where the files are: reading them, such as parsing a `.po`, is the owner's. The listing is ready by the time the owner's `translate` is first asked, so the bundled `localization` reads it then, and each plugin's packs on the first ask for one of its strings. Check for them with `MOSAIC_HOST_HAS(host, plugin_info)`.

  ```c
  for (int i = 0; i < host->plugins_count(); i++) {
      MosaicPluginInfo info = { .size = sizeof(info) };
      if (host->plugin_info(i, &info) != 0) break;
      char pack[700];
      snprintf(pack, sizeof(pack), "%s/lang/%s.po", info.res, code);
      load_pack(info.id, pack);
  }
  ```

- The `theme` slot lets a plugin supply the launcher's look: colours, the background colour, a font, a text size, UI sizes and margins, wallpapers and the button hint style, and keys for other plugins to read. It's an exclusive slot, picked like the layout (`slot.theme=<id>` in `plugins.txt`): only a plugin with `slots=theme` in its `plugin.txt` has its `theme(theme)` in `MosaicPlugin` asked, and one with it but not the slot is logged and never asked. The launcher hands it a `MosaicTheme` with `size` set and every field at its "stock's" value (`MOSAIC_THEME_UNSET` colours, empty paths, zero numbers); it sets the ones it changes, checking a field added later with `MOSAIC_THEME_HAS(theme, field)`, and returns 0. It's asked once every plugin has loaded, before the first list is built (so `init` sees stock's look), and again each time a plugin calls `reload_lists`, which applies a changed theme without a restart: colours, font, sizes, the rows the list shows and the background. Nothing it supplies is written to stock's settings, so stock's Settings app and minarch keep the user's choices. The Launcher pak's own pages load the owner too (see the `strings` slot above) and take its colours, font, fallback font, text size and UI sizes, but not its wallpapers, which they never showed.

  - `colors` are stock's seven, numbered as its palettes number them (`colors[0]` is `color1`, the main colour, through `colors[6]`, `color7`, the background colour, which the screen is also cleared with), each `0xRRGGBB`; one left unset is the user's own. `theme_color` returns what's used.
  - `font` is a TrueType or OpenType file, used for every size of stock's text; `text_step` adds points to each of stock's text sizes (16, 14, 12, 10 and 7 before scaling), from -2 to 8.
  - `fallback_font` is a TrueType or OpenType file for the characters the UI font has no glyphs for (see the fallback font below). It wins over the `strings` slot owner's.
  - `contrast`, from 10 to 210, is the least contrast ratio (WCAG's, in tenths: 45 is 4.5:1) text keeps against what it's drawn on: list text (`color4`) on the background (`color7`), selected text (`color5`) on the selected row's pill (`color1`) and hints (`color6`) on the hint and status pills (`color2`). A text colour short of it is moved towards white or black, whichever its backdrop allows more of, until it's met (or it's white or black). It applies to the colours in use, the theme's and the user's alike, so a theme of `contrast` alone makes the user's own palette readable.
  - `pill_size` is the height of a row and of every pill (stock's `PILL_SIZE`, 30), from 18 to 60, `button_size` the circle of a button in the hints (`BUTTON_SIZE`, 20), from 12 to 4 less than the pill, and `padding` the screen's margins (`PADDING`), from 1 to 30, all before scaling. With a pill size and no padding, the padding is worked out as stock's comments in `defines.h` put it, a third of the pill when it divides the screen's height and otherwise a third of what's left over (or a third of the pill when that's under an eighth of it). The rows the built-in list shows follow, as `(height - 2 * padding - pill) / pill` in layout units, which gives every stock device's `MAIN_ROW_COUNT`. With neither set, padding and rows stay stock's own. Stock's sprite sheet is drawn at stock's sizes, so pill ends and button circles at other sizes are scaled from it, and so are the status bar's icons, by the pill size over stock's, so they keep their place in the pill.
  - `wallpaper` is the background where the card has no `bg.png` and the selected folder no `.media/bg.png`; `wallpaper_list` a game folder's where it has no `.media/bglist.png` and the card no `bg.png` (`wallpaper` without it); `wallpaper_quick` the quick menu's where the card has no `.media/quick_<name>.png` or `quick.png`. A stock background placed by hand always wins, and a plugin's `art_path` answer for a folder's background comes before them too.
  - `hint_style` is the style the launcher draws button hints in, an `enum MosaicHintStyle` (see `hint_style` below), over the user's from stock's Settings; `MOSAIC_HINT_STYLE_USER` (0) leaves the user's. It applies to every hint the launcher draws, its own and plugins', but only in the launcher: stock's Settings app and minarch's menus keep the user's style, which is never written. A theme that stops setting it on a reload goes back to the user's.
  - `extra` is keys the theme passes on to other plugins, which read them with `theme_value` (see below), as `key=value` lines separated by `\n`, at most `MOSAIC_THEME_EXTRA_MAX` (4096) bytes with its terminator. Each key is named after the plugin that reads it, as `<its id>.<name>`, such as `led-status.colour`: 3 to 63 bytes of letters, digits, `_`, `-` and `.`, with a `.` that isn't its first or last byte. A value is at most `MOSAIC_THEME_EXTRA_VALUE_MAX` (255) bytes with no control characters. The launcher keeps up to `MOSAIC_THEME_EXTRA_KEYS` (64) keys; a line that isn't such a `key=value`, or a new key past them, is refused and logged on its own, and of a key given twice the last line wins. Empty lines are skipped.
  - Paths are full paths to regular files anywhere on the card, so a theme can be a folder of its own such as `<sd>/Themes/<name>/`; a symlink may point anywhere on the card but not off it.

  A value out of range, a path that isn't a regular file on the card, a font that won't open, and sizes that leave the list fewer than 3 rows or stock's text too big for its pill or button, are refused one at a time: the launcher logs each and uses stock's in its place, and the rest of the theme stays. A non-zero return is an error: the launcher logs it, puts stock's whole theme back and doesn't ask again until it restarts. With no plugin owning the slot, every size, colour and font is stock's, byte for byte.

  `ui_size(which)` in `MosaicHost` returns the size an `enum MosaicUISize` names, before scaling (`MOSAIC_UI_PILL`, `MOSAIC_UI_BUTTON`, `MOSAIC_UI_PADDING`, `MOSAIC_UI_ROWS` and `MOSAIC_UI_TEXT_STEP`), stock's or the theme's, so a layout can follow the theme; check for it, and for the theme slot, with `MOSAIC_HOST_HAS(host, ui_size)`.

  ```c
  static int theme(MosaicTheme* t) {
      t->colors[MOSAIC_COLOR_BACKGROUND - 1] = 0x102030;
      t->pill_size = 36;
      snprintf(t->font, sizeof(t->font), "%s/Themes/Night/font.ttf", host->sdcard_path);
      snprintf(t->wallpaper, sizeof(t->wallpaper), "%s/Themes/Night/bg.png", host->sdcard_path);
      return 0;
  }
  ```

- A fallback font draws the characters the UI font has no glyphs for, such as CJK game names or a translated UI, which stock shows as boxes. The launcher measures and draws text a stretch at a time: each stretch goes to the UI font when it has the glyphs, otherwise to the fallback font at the same size, with their baselines lined up. Combining marks and variation selectors stay with the character before them. Everything the launcher draws goes through it: stock's text (button hints, truncation, wrapping, messages and the scrolling selected row), the launcher's own, and plugins' `draw_text`, `text_width` and `text_truncate`. A line keeps the UI font's height, so a fallback glyph that reaches past it is cut off there. The fallback font comes from the `theme` slot's owner, as its `MosaicTheme`'s `fallback_font`, or else from the `strings` slot's owner, as `fallback_font()` in `MosaicPlugin`, which returns a full path or `NULL` for none, so a language pack can bring a font for its language. Only the `strings` slot's owner is asked; one with it but not the slot is logged and never asked. Both are asked once every plugin has loaded and again each time a plugin calls `reload_lists`. The path is checked as the theme's are, a regular file on the card that opens as a font; one that doesn't is refused and logged, and none is used (a refused theme's leaves the `strings` owner's). With no fallback font, and for text the UI font has every glyph of, text is measured and drawn exactly as stock's.

  ```c
  // in the strings slot's owner
  static const char* fallbackFont(void) {
      return language_needs_cjk ? cjk_font_path : NULL;
  }
  ```
- `cfg_read(other_id, key, fallback)` reads another plugin's setting when that plugin's `plugin.txt` marks it `public`, and returns the fallback otherwise (see [Plugins' settings](#plugins-settings)). Check for it with `MOSAIC_HOST_HAS(host, cfg_read)`. With it, `cfg_get` and `cfg_set` stopped taking a plugin id: they always act on the calling plugin's own settings.
- `fn_name(button)`, `fn_binding(button, out)`, `fn_bind(button, action)` and `fn_release(button)` let a plugin read and bind the FN buttons, `MOSAIC_BUTTON_FN1` to `FN3`, the buttons stock's Settings lets the user assign to a Tools pak or an action. `fn_name` returns the device's name for one (L3 and R3 on the Brick, L4, R4 and HOME on the Brick Pro, HOME on the tg5050), or `""` when the device lacks it, as the h700 and desktop lack all three, and for any other button. `fn_binding` fills a `MosaicFnBinding` (set its `size` first) with what the button runs when pressed, `action`, and whose binding it is, `by`: `MOSAIC_FN_NONE` when it's unassigned, `MOSAIC_FN_USER` for the user's own from Settings, or `MOSAIC_FN_PLUGIN` for a plugin's, with that plugin's id in `plugin_id`. It returns 0, or -1 for a button the device lacks, a `NULL` `out`, or where `cfg_get` is refused, such as from a plugin's own thread.

  `fn_bind` binds a button for the calling plugin, which the launcher knows from the hook or callback it's running, as for `cfg_get`, so there's no id to pass. `action` is `action:<id>` for an action from `register_action` or one of the game list's own (see `override_button`), or `pak:<folder>` for a Tools pak, the folder relative to `Tools/<platform>` as stock's Settings writes it, such as `pak:Clock.pak`. A press runs it the way the user's own binding would, from the game list, the game switcher and the quick menu, and does nothing while the action is unknown or unavailable or the pak isn't there. The binding is held in memory and never written to stock's settings, so it's gone when the launcher quits and the plugin binds it again from `init` on the next start. The user's choice always wins: a plugin can only bind a button the user left unassigned in Settings, and once the user assigns it, the plugin's binding is dropped and `fn_binding` shows `MOSAIC_FN_USER`, so the plugin can see it lost the button. The first plugin to bind a button keeps it, and can bind it again to another action; it holds it until it calls `fn_release(button)`, or until it's unloaded, as when it's turned off or declines to start, which frees the button for another plugin. `fn_bind` returns 0, or -1 for a button the device lacks, a bad action, a button the user assigned or another plugin holds, and where `cfg_get` is refused, such as from a plugin's own thread; the launcher logs each refusal. `fn_release` returns 0, or -1 when the calling plugin doesn't hold the button. There's no settings page for plugins' bindings: a plugin shows what it bound with its own hints, as `draw_hints` draws FN buttons by the device's names. With no plugin binding them, the FN buttons do what stock's Settings says. Check for these with `MOSAIC_HOST_HAS(host, fn_release)`.

  ```c
  // in init: HOME opens this plugin's screen, unless the user put something there
  host->register_action("remap.open", "REMAP", 0, NULL, openRemap, NULL);
  if (MOSAIC_HOST_HAS(host, fn_release) && host->fn_name(MOSAIC_BUTTON_FN3)[0])
      host->fn_bind(MOSAIC_BUTTON_FN3, "action:remap.open");
  ```
- `capture_screen()` and `save_screenshot(path, size)` take the last frame shown: the whole screen as stock's `PLAT_captureScreenshot` composites it from every layer, overlays, pills, the status pill and a toast included. While the launcher's dialogs (`confirm`, `keyboard`, `choose`) or the quick menu show, and from a dialog's callback, which runs as it closes, it's the last frame shown before the first of them opened, so an action run from a menu captures what the menu opened over, such as the game list, and not the menu; to do that the launcher reads the screen back once each time a dialog or the quick menu opens over another screen, and only with plugins loaded. The game switcher and plugin screens aren't menus here: they're captured as they show. `capture_screen` returns the frame as a `screen_width` by `screen_height` surface of the calling plugin's own, which the launcher knows as for `cfg_get`, counted against its 32 MiB and freed with `surface_free`. `save_screenshot` saves it as a PNG in `Screenshots` on the card, making the folder as stock does, named as stock names a game's screenshot with `Launcher` for the game, `Launcher.<YYYY-MM-DD-HH-MM-SS>.png` (a second save in the same second replaces the first, as stock's does), copies its full path to `path` (`size` bytes) when `path` isn't `NULL`, and shows "Screenshot saved" when stock's screenshot notification setting (Settings' Notifications, Screenshots) is on. It saves before it returns, so the file is there once it returns 0. Both work only on the launcher's thread, from a plugin's hooks and callbacks, but not from its `init`, before the first frame is shown, or from a hook that draws (`render`, `overlay_render`, the layout, row, art, header and background hooks, `art_surface` and transitions), whose frame isn't finished; they return `NULL` and -1 then, as for a surface over the budget or a file that can't be written, and the launcher logs each refusal. There's no built-in button for them: a plugin registers an action, such as `screenshot-viewer`'s `screenshot-viewer.capture`, and the user binds it. Check for them with `MOSAIC_HOST_HAS(host, save_screenshot)`.

  ```c
  static void capture(const char* path, void* data) {
      char saved[512];
      if (host->save_screenshot(saved, sizeof(saved)) == 0) host->log(ID, saved);
  }
  // in init
  if (MOSAIC_HOST_HAS(host, save_screenshot)) host->register_action("snap.capture", "Take screenshot", 0, NULL, capture, NULL);
  ```
- `hint_style()` returns the style button hints are drawn in, an `enum MosaicHintStyle`: `MOSAIC_HINT_STYLE_TEXT` (the button's name, such as A or START), `MOSAIC_HINT_STYLE_ABXY`, `MOSAIC_HINT_STYLE_CARDINALS` or `MOSAIC_HINT_STYLE_SHAPES` (icons), as stock's Settings app names them under "Input prompt style". It's the `theme` slot owner's when its theme sets one (`hint_style` in `MosaicTheme`, above), otherwise the user's, and changes only when the lists reload. `draw_button(surface, button, x, y, drawn)` draws one button the way stock's hints draw it, without a label, for a plugin that lays out its own: in that style, `ui_size(MOSAIC_UI_BUTTON)` high (scaled), in the theme's colours, its top left at `x`, `y` on `surface`, which is any the plugin can draw on (its own, or one lent to a hook, such as a screen's in `render`), clipped as that surface is. `button` is any a `MosaicHint` takes: a letter, or an icon in the icon styles, is a circle as wide as it's high, and a name such as START, or a d-pad's word in the text style, a wider pill. The d-pad (`MOSAIC_BUTTON_UP` to `MOSAIC_BUTTON_RIGHT`) and the `enum MosaicGlyph` glyphs (`MOSAIC_GLYPH_UP_DOWN`, `MOSAIC_GLYPH_LEFT_RIGHT` and `MOSAIC_GLYPH_DPAD`, the whole d-pad) are icons from stock's input prompt sheet in each icon style, which stock has but doesn't draw, and the words UP, DOWN, LEFT, RIGHT, U/D, L/R and DPAD in the text style; `draw_hints` takes them too. With `surface` `NULL` it draws nothing, to measure. When `drawn` isn't `NULL` it gets the button's rect. It returns 0, or -1 for a surface the plugin can't draw on, a button with no hint, such as an FN button the device lacks, or `x` or `y` out of range. With no plugins on, the launcher's own hints are drawn in the user's style, as stock's are. Check for these with `MOSAIC_HOST_HAS(host, draw_button)`.

  ```c
  // "<up/down icon> MOVE" at x, y, in whatever style is on
  MosaicRect b;
  if (host->draw_button(screen, MOSAIC_GLYPH_UP_DOWN, x, y, &b) == 0)
      host->draw_text(screen, MOSAIC_FONT_SMALL, "MOVE", b.x + b.w + 4, y, mosaic_color(host->theme_color(MOSAIC_COLOR_HINT), 255), 0);
  ```
- `palettes_count()`, `palette_info(index, out)` and `palette_selected()` list stock's colour palettes, read only, exactly as stock's Settings app lists them under Appearance's "Color Palette": stock's own, in `.system/res/palettes`, first, then the card's `Palettes`, each folder in the order it's read from the card (not sorted), up to the first 64, leaving out a file for a newer palette format, since the launcher lists them with stock's own code. `palettes_count` reads them again and returns how many there are. `palette_info` fills a `MosaicPalette` (set its `size` first) for the one at `index` in that list, reading them the first time it's asked when `palettes_count` hasn't been, and returns 0, or -1 for an index past the end or a `NULL` `out`: its `name`, as Settings shows it (the file's `name=`, else its file name without `.txt` and with underscores as spaces), its seven `colors`, `0xRRGGBB` as `MosaicTheme` takes them (stock's alpha is dropped) and stock's default for any the file leaves out, `builtin`, 1 for one of stock's own, and its file's `path`. `palette_selected` returns the index of the palette the user picked in Settings, the first in the list with its name, or -1 for Custom: none picked, colours edited since, or a palette that's no longer there, which Settings shows as Custom too. The launcher never applies a palette or writes stock's settings. The `theme` slot's owner can use a palette's colours in its `MosaicTheme`, which then show only in the launcher, while stock's Settings app and other apps keep the user's palette. Check for these with `MOSAIC_HOST_HAS(host, palette_selected)`.

  ```c
  // the user's Settings palette's background, or -1 for Custom
  int background = -1;
  int selected = host->palette_selected();
  MosaicPalette p = {.size = sizeof(p)};
  if (selected >= 0 && host->palette_info(selected, &p) == 0) background = p.colors[MOSAIC_COLOR_BACKGROUND - 1];
  ```
- `set_register(name, has, list)`, `set_unregister(name)`, `set_has(name, path)`, `set_list(name, each, data)` and `is_filtered(path)` are entry sets: named sets of entries that one plugin owns and others read, in place of reading each other's files. Paths are full paths on the card, as `entry_path` returns them. The first plugin to register a name owns it, and others can only read it, as with `cfg_read`. A plugin can register `favorites`, `hidden`, `tags:<tag>`, `smart:<collection>` or `<its own id>.<anything>`, and any other name is refused. Names are matched exactly, case included. The owner registers a set with two callbacks, which the launcher calls on its thread, as the owner, each time a plugin asks, so a set is always current and the launcher keeps no copy of it: `has(name, path)` returns non-zero when `path` is in the set, and `list(name, add, ctx)` calls `add(path, ctx)` for each path in it, only before it returns. Both get the set's name, so one pair can answer for several sets. Registering its own set again replaces the callbacks. `set_register` returns 0, or -1 for a name the plugin may not have, one another plugin registered first, or a missing callback, and the launcher logs each refusal. `set_unregister` removes the calling plugin's own set and returns 0, or -1 when it has none by that name; unloading a plugin, or its `init` declining, removes all of its sets. `set_has` returns 1 when `path` is in the set, else 0, a set nobody registered too. `set_list` calls `each(path, data)`, as the calling plugin, for each path in the set before it returns, and returns how many there were, or -1 for a set nobody registered; with `each` `NULL` it only counts. A set whose callback is running isn't asked again until it returns (`set_has` returns 0 and `set_list` -1, and the launcher logs it), so a callback can ask other sets but not its own. `is_filtered(path)` returns 1 when a loaded plugin's `filter` hides the entry at `path` from its folder right now, without saying which plugin, else 0: the entry's type is worked out from the card (a folder, a `.pak` folder or a file), the folder is `path`'s parent, and every plugin's filter is asked, the calling plugin's too; a filter that calls `is_filtered` while `is_filtered` is asking it gets 0. They take no plugin id: the launcher knows the caller as for `cfg_get`, and refuses them where `cfg_get` is refused, such as from a plugin's own thread (`set_has` and `is_filtered` return 0, the others -1). From `init`, plugins that load later haven't registered their sets yet. An owner announces a change with `set_changed`, and `set_names` lists the registered sets (see the events below). The bundled `favorites` registers `favorites` (the games and paks in its collection), `hidden` registers `hidden` (the paths in its list, and `has` is also true for anything in a hidden folder, as its filter hides), `tags` registers `tags:<name>` for each tag (the games it has, following the tags as they're added, deleted or edited in its file), and `smart-collections` registers `smart:<name>` for each franchise as the launcher starts (the games its collection lists, scanning the card the first time one is asked, and none while it has fewer than its `min` games or is dismissed). A set holds what its owner records, so a path in it may no longer be on the card. The bundled readers treat a set nobody registered, as when its owner is off, as one with nothing in it: `search` (its `fav:` and `tag:<name>` filters), `random-game` (its `from` setting), `focus-mode` (its `set` setting), `row-badges` (its favorite and tag marks), and `home-dashboard` and `screensaver` (favorites). `random-game`, `home-dashboard`, `switcher-plus`, `screensaver` and `activity-stats` leave out what `is_filtered` hides, and `search` gets that from the launcher's filters on its folder. Check for them with `MOSAIC_HOST_HAS(host, is_filtered)`.

  ```c
  // a favorite that no filter hides
  static void count(const char* path, void* data) {
      if (!host->is_filtered(path)) (*(int*)data)++;
  }
  // in a hook or callback
  int shown = 0;
  if (MOSAIC_HOST_HAS(host, is_filtered) && host->set_list("favorites", count, &shown) < 0) host->log(ID, "favorites isn't on");
  ```
- `subscribe(name, on_event, data)`, `unsubscribe(name)`, `publish(name, payload)` and `event_value(payload, key, out, size)` are events, for plugins to hear about each other, and about the launcher, in place of reading each other's files. A plugin publishes only `<its own id>.<name>`, such as `music.track` or `romm.download_done`, and the launcher's own events are `launcher.*`:
  - `launcher.launch`, with `path=<path>` and `type=rom` or `type=pak`, once a launch goes ahead past every `pre_launch`;
  - `launcher.return`, with the same payload, as the launcher starts again after the game or pak, after `post_return`;
  - `launcher.set_changed`, with `name=<set>`, when a set's owner calls `set_changed`;
  - `launcher.theme_changed`, with no payload, when the `theme` slot's owner puts a different theme in place (from `reload_lists`, as theme-plus's picker does) or its theme fails and stock's is back; the theme in place as the launcher starts isn't a change.

  The bundled `romm` publishes `romm.download_done`, with `path=<the file the download launches>` and `name=<RomM's name for it>` (the path alone when both don't fit), as each game it downloads finishes, before the game launches; `gamification` counts it towards its RomM quest.

  Published events wait in a queue, and the launcher delivers them on its thread before its next frame: oldest first, each to its subscribers in the order they subscribed, with `on_event(name, payload, data)` run as the subscriber, as for `cfg_get`. So `publish` works from a plugin's own thread too (the launcher tells which plugin by the library the call came from), and no callback runs inside another: an event published from a callback, or from anywhere after the queue was emptied for this frame, waits for the next frame. What's still waiting when the launcher quits is delivered before plugins' `quit` hooks; that's how `launcher.launch` reaches its subscribers when the launcher quits for the launch before another frame, so it's delivered like the rest, with no exception. Subscriptions are matched as an event is delivered, so one published from `init` reaches plugins that load later. A name is 1 to 63 bytes with no spaces or control characters, and names are matched exactly, case included. A payload is `NULL` or `""` for none, or `key=value` lines separated by `\n` (one more after the last is allowed), under 1024 bytes in all, with keys of 1 to 31 letters, digits, `_`, `-` or `.`, and values with no control characters; a subscriber gets `""` for none. `publish` returns 0, or -1, dropping the event, for a name that isn't the plugin's, a bad payload, or a full queue: 64 events can wait at once, the last 8 places only for the launcher's own, so a plugin publishing every frame can't crowd out `launcher.launch`. The launcher logs the first refusal of each kind. `subscribe` returns 0, or -1 for a bad name, no callback, or 256 subscriptions across every plugin; subscribing to a name again replaces the callback and data. `unsubscribe` removes the calling plugin's subscription to `name` and returns 0, or -1 for none; unloading a plugin, or its `init` declining, removes all of them, while what it published stays queued. `event_value` copies the value of `key` in a payload into `out`, cut to fit, and returns the value's whole length, or -1 when the payload has no such key; the first line with the key wins, and keys are matched exactly.
- `set_changed(name)` and `set_names(each, data)` go with entry sets. `set_changed` is for a set's owner: it publishes `launcher.set_changed` with `name=<name>` for a set the calling plugin owns, or one it may register that no plugin has, so a set it just unregistered can be announced too, and returns 0, or -1 for another plugin's set or a full queue. It counts against plugins' room in the queue. `set_names` calls `each(name, data)` for each registered set's name, in the order they were registered, before it returns, and returns how many there were; with `each` `NULL` it only counts. The bundled `favorites`, `hidden`, `tags` and `smart-collections` call `set_changed` each time they change a set themselves: a favorite added or removed, something hidden or unhidden, a game tagged or untagged or a tag deleted, and a collection dismissed or brought back. A hand edit of their files isn't announced. `subscribe`, `unsubscribe`, `set_changed` and `set_names` work where `cfg_get` does and return -1 elsewhere, such as from a plugin's own thread; `publish` and `event_value` work from any thread. Check for all of them with `MOSAIC_HOST_HAS(host, set_names)`.

  ```c
  static void onTrack(const char* name, const char* payload, void* data) {
      char title[128];
      if (host->event_value(payload, "title", title, sizeof(title)) >= 0) host->log(ID, title);
  }
  // in init
  if (MOSAIC_HOST_HAS(host, set_names)) host->subscribe("music.track", onTrack, NULL);
  // from any thread, in the music plugin
  host->publish("music.track", "title=Song\nartist=Band");
  ```
- `plugin_loaded(id)` and `has_capability(name)` say which other plugins are there, in place of reading `plugins.txt`. `plugin_loaded` returns 1 when the plugin with that id is on and its `init` succeeded, and 0 for one that's off, not installed, refused (by its slot, a conflict or a missing requirement), skipped (such as for another API version), declined to start, or paused after a crash, when none load. So a plugin that `plugins.txt` turns on but that declined counts as not there. `has_capability` returns 1 when such a plugin lists `name` in its `plugin.txt`'s `provides` (see [plugin.txt](#plugintxt) for the names), and 0 otherwise, for an empty name too. From `init`, they answer for the plugins that loaded before it: the plugin itself and the ones after it give 0 until their `init` has returned, so a plugin that asks from `init` lists the plugins it asks about in `optional`, which loads it after them; from its hooks and callbacks, once every plugin has loaded, they answer for all of them. In the Launcher pak's settings pages they answer for the `strings` and `theme` slots' owners, the only plugins loaded there. The bundled `home-dashboard` and `screensaver` ask from `init` whether `favorites` is loaded (and the screensaver whether `charging-screen` is), with both in their `optional`, `first-run` asks for its suggested plugins as it opens, and `core-prefetch` asks for `emulator-override` before each launch. The bundled `favorites`, `hidden`, `tags` and `smart-collections` provide `sets.favorites`, `sets.hidden`, `sets.tags` and `sets.smart`, and `playtime-captions`, `row-badges`, `save-state-manager`, `emulator-override`, `battery-logger`, `metadata-band`, `tags`, `playtime-limit` and `romm` the services below, `captions.text`, `games.played`, `states.last`, `emulator.choice`, `battery.history`, `info.format`, `tags.copy`, `limit.left`, `romm.synced` and `romm.downloads`, and `romm` provides `events.romm.download_done` too. Check for them with `MOSAIC_HOST_HAS(host, has_capability)`.

  ```c
  // in init, with optional=favorites in plugin.txt
  int pinned = MOSAIC_HOST_HAS(host, has_capability) && host->has_capability("sets.favorites");
  ```
- `provide(name, fn)` and `call(name, args, out, size)` are providers: services one plugin answers for and others call, in place of copying each other's logic or reading each other's files. A service's name is `<kind>.<name>`, the kind letters, digits, `_` or `-`, 1 to 63 bytes with no spaces or control characters, matched exactly, case included; the name is also the capability the provider lists in its `plugin.txt`'s `provides`, so `has_capability(name)` says whether a loaded plugin offers it, and `provide` refuses a name its `provides` doesn't list, rather than adding it, so the manifest always says what a plugin offers. `sets.` and `events.` names are entry sets' and events' capabilities, never services. `provide` makes the calling plugin, which the launcher knows as for `cfg_get`, the provider of `name`, with `fn(name, args, out, size)`, which the launcher calls on its thread, as the provider (as for `cfg_get`), each time a plugin calls the name. The first plugin to provide a name keeps it, as with sets and FN bindings, and can provide it again to replace `fn`; later plugins are refused, and unloading the provider, or its `init` declining, releases all of its names. `provide` returns 0, or -1 for a name its `provides` doesn't list, a `sets.` or `events.` name, a name another plugin provided first, a bad name or no `fn`, and the launcher logs each refusal. `call` takes no plugin id: `args` is `NULL` or `key=value` lines as for `publish`'s payload, but up to `MOSAIC_CALL_MAX` (4096) bytes with the terminator, and the reply, `key=value` lines too, is copied into `out`, where `event_value` reads each key; each service documents its keys. `fn` gets the name, `args` (`""` for none) and an empty `out` `MOSAIC_CALL_MAX` bytes long, writes its reply there and returns 0, or non-zero when it can't answer. `call` returns the reply's length, or `MOSAIC_CALL_UNKNOWN` (-1) when nobody provides the name, `MOSAIC_CALL_TOO_SMALL` (-2) when the reply doesn't fit `size` (an `out` of `MOSAIC_CALL_MAX` bytes always fits), `MOSAIC_CALL_REFUSED` (-3) for bad `args`, a `NULL` `out` or a `size` of 0, a name whose `fn` is running (a call into a service from inside its own `fn`, directly or through other services), calls nested `MOSAIC_CALL_DEPTH` (8) deep, or where `cfg_get` is refused, such as a plugin's own thread, and `MOSAIC_CALL_FAILED` (-4) when `fn` returned non-zero or its reply isn't `key=value` lines; `out` is `""` for all of them. A call into another service from inside a `fn` is allowed. The launcher logs the first of each refusal and failure. From `init`, plugins that load later haven't provided their names yet, so a plugin that calls from `init` lists the provider in `optional`. Check for them with `MOSAIC_HOST_HAS(host, call)`. The bundled providers, each with `path=<full path>` of a game (a file, or for `games.played`, `states.last` and `emulator.choice` a game folder too) where it takes one:
  - `captions.text`, from `playtime-captions`: `path` and optionally `width=<pixels>` in the small font give `text=<the caption>`, empty for none, in its settings' parts and format, fitted to the width as a row's caption is (the compact format, then fewer parts), and `width=<its width>`. `switcher-plus` shows it in the game switcher and `home-dashboard` on its continue card.
  - `games.played`, from `row-badges`, the one definition of a played game: `path` gives `played=1` when the game, a disc's `.m3u` or a game folder's `.m3u` or `.cue` is in recents, has a save, state or resume slot on the card (by its system's tag and file name, as stock files them) or a session in the play history, and `played=0` otherwise, for anything that isn't a game in a system folder too. It answers whether or not row-badges' dot is on, which follows the same definition. `search`'s `unplayed` filter, `random-game`'s unplayed preference and Unplayed games only, and `gamification`'s never played quest call it.
  - `states.last`, from `save-state-manager`: `path` gives `count=<slots with a state>` and, with any, the most recently saved one as `slot=<0 to 9>`, `label=<"Slot 1" to "Slot 8", "Default" or "Auto">`, `saved=<Unix time>` and `preview=<its preview's path, or empty>`.
  - `emulator.choice`, from `emulator-override`: `path` gives `pak=<the pak chosen for it>`, empty for none, and `launch=<its launch.sh>`, empty when there's no choice or the pak fails the check `pre_launch` makes, so the game launches with its system's own.
  - `battery.history`, from `battery-logger`, on any platform: optionally `count=<n>` gives the last `n` of the last 64 sessions it logged (every one without it) as `count=<n>`, `seconds=<their play time>` and `session.<i>=<start>,<seconds>,<start_percent>,<end_percent>,<charged>` for each, oldest first from 0, with the start a Unix time and charged 1 or 0.
  - `info.format`, from `metadata-band`: `path`, optionally `fields=<keys>`, as its `fields` setting lists them, in place of that setting's, and `width=<pixels>` in `font=small`, `tiny` or `micro` (small by default) give `count=<fields with a value>`, `<key>=<value>` for each of those, tidied as the band tidies them (trimmed, and a year cut to its four digits), and `line=<them joined with " · ">`, which with a width has the last ones left out until it fits, keeping the first, as the band does. The sidecar is read from the card as `entry_info` reads it.
  - `tags.copy`, from `tags`: `tag=<name>` and `set=<set name>` make a new tag holding every game the set lists now, once, so the tag doesn't follow the set afterwards, and give `tag=<the name as saved>` and `games=<how many>`, or `error=exists` (a tag already has the name, ignoring case), `name` (not a name a tag can have), `set` (nobody registered the set), `empty` (no games in it), `full` (too many tags or games) or `save` (its file couldn't be written). A game folder in the set is tagged by its `.m3u` or `.cue`, as the checklist tags it. `smart-collections`' Save as tag calls it.
  - `limit.left`, from `playtime-limit`, with no arguments: where today stands against its daily limit, as `state=<off, unknown, ok, warn or up>` (`off` with no limit set, `unknown` when the play history can't be read, `warn` at or under its warning threshold and `up` with no time left), `limit=<the daily limit in minutes, 0 while it's off>`, `granted=<the extra minutes granted today>`, `left=<the seconds left today, the grant included, -1 while it's off or unknown>` and `text=<the header widget's text, such as "45m left", empty while it's off or unknown>`. `home-dashboard`'s Stats tab shows it, and `gamification` leaves out play time quests that need more than it.
  - `romm.synced`, from `romm`, from the files it keeps rather than the network: with no arguments, `server=<1 while a server is set, else 0>`; with `path=<a file>`, also `synced=1` when it's the file `romm` last uploaded to or restored from the server, its size and modification time unchanged since, with `how=<upload or download>` and `at=<Unix time>`, or `synced=0`, and `game=<1 when path is a game from RomM, which romm.flashback opens for, else 0>`. It never gives the server's address, user or password. `save-state-manager` marks synced states and offers Restore from server from it, and `gamification` offers its RomM quest while `server=1`.
  - `romm.downloads`, from `romm`: optionally `count=<n>` (5 by default, up to 20) gives the games it last downloaded that are still on the card, newest first, as `count=<n>` and `path.<i>=<full path>`, `name.<i>=<RomM's name>` and `at.<i>=<Unix time>` for each from 0, as many as fit. `home-dashboard`'s Stats tab lists them under Recently downloaded.
  - `music.now`, from `music`, with no arguments: what plays, from what the player last heard from its daemon, as `state=<playing, paused or stopped>`, `title=<the track's title tag, or its file name>`, `artist=<its artist, empty for none>` and `line=<"Playing: <title> - <artist>" or "Paused: ...", as the now playing pill shows it, empty when stopped>`, in the user's language. A track paused to play on after a power-off counts as paused. `screensaver`, `charging-screen` and `video-previews` show its `line`.
  - `music.toggle`, from `music`, with no arguments: plays or pauses while there's a track, as X on the player does, and gives `state=<playing, paused or stopped>` after. `screensaver` and `charging-screen` run it on X.
  - `music.pause` and `music.play`, from `music`, with no arguments: `music.pause` pauses what plays, giving `paused=1` when it was playing and is now paused, else `paused=0`, and `state`; `music.play` plays on what's paused and gives `state`. `video-previews` pauses music while a screensaver clip plays and plays it on after.
  - `video.play`, from `video-previews`: `path=<a game>` plays the game's clip, `<rom dir>/.videos/<rom name>.mp4`, looping on video-previews' own full screen, `video-previews.saver`, which it opens over the calling screen in place, with no slide (`MOSAIC_SCREEN_INSTANT`), with `clock=<off, 24h or 12h>` and `music.now`'s `line` drawn over it, as the screensaver draws them, and gives `playing=1`; a game without a clip, or no `path`, closes that screen and gives `playing=0`. The caller's own buttons, such as the screensaver's through `input_filter`, still work, and closing the caller's screen closes it too. It's there only where ffmpeg is. `screensaver`'s `videos` source calls it for each image's game.

  ```c
  // in a hook, with optional=playtime-captions in plugin.txt when it's called from init
  char reply[MOSAIC_CALL_MAX], text[160];
  char args[1100];
  snprintf(args, sizeof(args), "path=%s\nwidth=%i", path, width);
  if (MOSAIC_HOST_HAS(host, call) && host->call("captions.text", args, reply, sizeof(reply)) >= 0
      && host->event_value(reply, "text", text, sizeof(text)) > 0) host->log(ID, text);
  ```
- Actions that take a path: `register_action(id, label, flags, available, run, data)` takes `flags`, 0 or `enum MosaicActionFlag` bits, after the label, and `available` and `run` take the path an action acts on before `data`. An action registered with `MOSAIC_ACTION_PATH` acts on an entry, such as a game: its `available` and `run` get the entry's full path, and it's refused without one. A button gives it the game list's selected entry while the game list shows; anywhere else, such as the quick menu or the game switcher, or with nothing selected, it isn't available, so its hint and quick menu tile don't show, and an FN button or `run_action` without a path does nothing. `run_action` with a path runs it on that entry from anywhere `run_action` works, so one plugin can run another's action on a game it picked. Every other action gets `NULL`. The launcher copies the path before `run` gets it, so `run` can keep using it while it changes the lists. Of the bundled plugins only `romm.flashback` takes a path.

  ```c
  static int canShow(const char* path, void* data) { return path != NULL; }
  static void show(const char* path, void* data) { host->log(ID, path); }
  // in init
  host->register_action("info.show", "INFO", MOSAIC_ACTION_PATH, canShow, show, NULL);
  // in another plugin, on a game it picked
  if (MOSAIC_HOST_HAS(host, run_action)) host->run_action("info.show", game_path);
  ```
- `actions_count()` and `action_info(index, out)` list every registered action, the launcher's own (such as `gamelist.back`) and plugins', in the order they were registered, with the buttons each is on. `action_info` fills `out`, a `MosaicActionInfo` the plugin sets `size` on, for the action at `index`: its `id`, its `label` translated as the game list's hints show it, the `plugin_id` that registered it (empty for the launcher's own), `takes_path` (1 for `MOSAIC_ACTION_PATH`), and `binding_count` buttons, the first `MOSAIC_ACTION_BINDINGS` of them in `bindings`. Each `MosaicActionBinding` has where it comes from (`by`, an `enum MosaicBindBy`: the screen's own, `bind_button`, `override_button`, an FN button the user assigned in stock's Settings or one a plugin bound with `fn_bind`), the `screen` it's on (-1 for an FN button), the `button` and `trigger`, the `plugin_id` that bound it, and the `setting`, the key of that plugin's setting its `plugin.txt` marks `button:<the action's id>` (see [plugin.txt](#plugintxt)), empty when none is marked. The bindings are the ones in effect, the game list's own first, then overrides, `bind_button`'s and the FN buttons': a game list or `bind_button` binding on a button an override took is left out, and an FN button shows what a press runs, the user's binding or a plugin's, when it's an action. `action_info` returns 0, or -1 for an index past the end or a `NULL` `out`. Both work where `cfg_get` does, on the launcher's thread; elsewhere `actions_count` returns 0 and `action_info` -1. Check for them with `MOSAIC_HOST_HAS(host, action_info)`.

  ```c
  for (int i = 0; i < host->actions_count(); i++) {
      MosaicActionInfo info = { .size = sizeof(info) };
      if (host->action_info(i, &info) == 0 && info.binding_count > 0) host->log(ID, info.label);
  }
  ```
- `theme_value(key, fallback)` reads a key the `theme` slot's owner passes on with its theme, in its `MosaicTheme`'s `extra` (see the `theme` slot above), so a theme can reach other plugins: its value, or `fallback` when the theme doesn't set it, no plugin owns the slot or its theme failed. There's no plugin id to pass: keys are named after the plugin that reads them, as `<its id>.<name>`, and a plugin reads its own, though it may read any. The theme is asked for once every plugin has loaded, so from `init` it returns `fallback`: read keys where they're used, and again on `launcher.theme_changed` (see `subscribe` above), which is published when they change, as when the user picks another theme. The returned string stays valid until the lists next reload, so copy what's kept. It works where `cfg_get` does, on the launcher's thread, and returns `fallback` elsewhere. With no plugin owning the slot it always returns `fallback`. Check for it with `MOSAIC_HOST_HAS(host, theme_value)`.

  A key is a default, never an override: each bundled reader has a setting the key stands in for, and the user's own setting wins. A choice setting takes the theme's key at its `theme` value, its default, and the reader's old default when the theme doesn't set the key or sets one it doesn't take; any other value the user picks is used whatever the theme says. The bundled plugins read these keys:

  - `led-status.colour`, a colour (`0xRRGGBB` or `#RRGGBB`), a theme colour's name as its `theme_color` setting takes it, or `battery`, in its `mode` setting's `auto`, the default (the battery's colours without the key). Its `battery` and `theme` modes are the user's and ignore the key. It reads the key again on `launcher.theme_changed` and changes the LEDs at once.
  - `art-placeholder.style`, `title` or `pattern`, at its `style` setting's `theme`.
  - `art-backdrop.dim`, a percentage from 0 to 100, at its `dim` setting's `theme` (50 without the key).
  - `layout-grid.columns` and `layout-grid.rows`, `auto` or a number, at its `columns` and `rows` settings' `theme`; `layout-shelf.shelves`, `auto` or 1 to 4, at its `shelves` setting's `theme`; and `layout-hero-rail.cards`, `small`, `medium` or `large`, at its `cards` setting's `theme`.

  This is how a theme chooses the layout: the plugin that owns the `layout` slot reads its own keys to pick among its own modes, such as a grid of one row. A theme can't turn a layout plugin on or pick which one owns the slot. That's the user's choice, in `plugins.txt` (which plugins are on and `slot.layout`), and it's made before any plugin loads, so before there's a theme to ask.

  ```c
  // where the setting is used, and again on launcher.theme_changed
  const char* style = host->cfg_get("style", "theme");
  if (!strcmp(style, "theme") && MOSAIC_HOST_HAS(host, theme_value)) style = host->theme_value("art-placeholder.style", "title");
  ```

- The launcher's built-in lock. The user turns it on with **Lock chord** on the Launcher page (SELECT and START, L1 and R1, or L2 and R2, off by default), with **Hold the chord for** (1 to 3 seconds) and **Lock with the hold switch** (the switch on the side of the tg5040 and tg5050, its stock mute switch, which still mutes as stock's Mute settings say). Holding the chord locks, and holding it again unlocks; with the switch setting on, the switch locks while it's on, and the chord can't unlock it until it's switched off. While it's locked the launcher takes every button before `input_filter` sees it, on every screen, and keeps taking them after an unlock until they're all let go, so the chord's own releases don't reach the screen. A long power press still powers off, but a short one doesn't sleep; the volume keys, and the brightness with MENU held, are stock's keymon's and can't be locked. A padlock shows in the header, next to the status pill, and over the game list or home the lock screen opens, as the ambient screen `lock` (below): the time, plugins' lines, how to unlock and plugins' buttons. It closes once it's unlocked. The lock isn't kept when the launcher restarts. With the chord off and the switch setting off, as by default, nothing locks and the pad is stock's.

  `lock()` locks it as the chord does, such as for a plugin's lock action, and returns 0, or -1 when nothing could unlock it (the chord is off) or where `cfg_get` is refused; the launcher logs each refusal. A chord held as it locks doesn't count towards unlocking until it's let go. Plugins can't unlock it. `is_locked()` returns 1 while it's locked, by the chord, the switch or a plugin, and 0 otherwise.

  Plugins add to the lock screen. `lock_text(out, size)` in `MosaicPlugin` runs for every plugin, in load order, each time the lock screen draws, and writes up to 3 lines, separated by `\n`, into `out`, empty on entry; they show in the small font under the time, each cut to fit, and empty lines are skipped. Call `request_redraw` from `tick` to change them. A `lock_text` that returns anything but 0 is logged, and that plugin's lines are left out until the launcher restarts. `lock_button(button, action_id)`, from `init` only, puts an action from `register_action` on a button of the lock screen, `A`, `B`, `X`, `Y`, `L1`, `R1`, `L2`, `R2` or the d-pad: a press there runs it, with no path, while it's available, and its hint shows at the bottom right, labelled with the action's label, up to 3 of them. A button in the user's chord does nothing there. It returns 0, or -1 for any other button, an unknown action, a button that already has one, or a call from anywhere but `init`; the launcher logs each refusal. The bundled `music` adds the track playing and puts play or pause on X, and its `music.lock` action, Y on its player, calls `lock`.

  ```c
  static int lockText(char* out, size_t size) {
      if (playing) snprintf(out, size, "Playing: %s", title);
      return 0;
  }
  // in init, with .lock_text = lockText in the plugin
  if (MOSAIC_HOST_HAS(host, register_ambient)) host->lock_button(MOSAIC_BUTTON_X, "music.toggle");
  // in the music.lock action
  if (host->lock() != 0) host->toast("Set a lock chord on the Launcher page");
  ```
- `register_ambient(screen_id, wants, data)` makes one of the plugin's screens an ambient screen: one that opens by itself, such as a charging screen or a screensaver. The user puts the ambient screens in order on the Launcher page, with **Ambient screens**, which lists the lock and the screens that plugins turned on list in their `plugin.txt`'s `ambient=`, labelled with the plugin's name (and the part of the id after the dot, for a plugin with more than one); A picks one up, up and down move it, and A or B puts it down. By default the order is the lock, `charging-screen.main`, `screensaver.main`, then the rest as they load. Each frame while the game list, the home screen or an ambient screen is on top, with no dialog showing and no game starting, the launcher calls `wants(idle_ms, data)`, as the plugin (as for `cfg_get`), for each ambient screen before the one showing, in order, with how long it's been since a button was down, the device woke, an ambient screen closed or another screen showed. It returns 1 to show. The first that does opens, as `open_screen` would, over the ambient screen showing, which closes first, so one earlier in the order takes over from a later one. An ambient screen whose `enter` declines isn't asked again until a button is pressed. The launcher never closes a plugin's ambient screen: it closes itself, such as on a button or when the device stops charging, as any screen does; the next one is then asked after its own wait, from 0. Keep `wants` cheap, since it runs every frame. `register_ambient` works from `init` only, and returns 0, or -1 for a screen the plugin didn't register, one its `ambient=` doesn't list, one that's already ambient, past 16 ambient screens, no `wants`, or a call from anywhere else; the launcher logs each refusal, and logs the order once every plugin has loaded, as `plugins: ambient screens in order: ...`. Check for it, the lock calls and `lock_text` with `MOSAIC_HOST_HAS(host, register_ambient)`. The bundled `charging-screen` and `screensaver` are ambient screens, each wanting to show once the idle time reaches its own setting, and the charging screen only while the device charges.

  ```c
  static int wantsToShow(unsigned long idle_ms, void* data) {
      return host->is_charging() == 1 && idle_ms >= 60000;
  }
  // in init, with ambient=charging-screen.main in plugin.txt
  host->register_screen("charging-screen.main", &hooks);
  if (MOSAIC_HOST_HAS(host, register_ambient)) host->register_ambient("charging-screen.main", wantsToShow, NULL);
  ```

## Without writing C

Shell paks can hook into the launcher too:

- **Before a launch:** `Tools/<platform>/<name>.pak/pre-launch.sh` runs before every game or pak launch, in pak name order. It gets `HOOK_PHASE=pre`, `HOOK_TYPE` (`rom` or `pak`), `HOOK_ROM_PATH` and `HOOK_PAK_PATH`. Exiting 1 cancels the launch; any other failure is logged and the launch goes ahead. This sits alongside stock NextUI's `.userdata/<platform>/.hooks/*.d/` folders, which still work and can't cancel.
- **Staying awake:** while `/tmp/stay_awake` or `/tmp/stay_alive` exists, the launcher holds off auto-sleep. Create it during a long download or sync and remove it afterwards. Stock only powers off on its own after a period asleep, so this also keeps the device from powering off. The power button still sleeps the device, and stock's power-off after the suspend timeout still applies once it's asleep.
- **Showing progress:** a pak that writes `$USERDATA_PATH/launcher/status.d/<pak>.status` gets a pill in the header, next to stock's status pill, on the game list, the game switcher, the quick menu and the dialogs. Plugins' header widgets move left to make room, and the top row is shortened as it is for them. Each line of the file is `key=value`:

  | Key | Value |
  |---|---|
  | `state` | `running`, `done` or `error`; required |
  | `done` | how much is done, a whole number from 0 to 999999999 |
  | `total` | out of how much, the same; with it, a running pill shows the percentage done, rounded down |
  | `label` | what the pill says, up to 32 characters; the pak's name (the file's, without `.status`) without it |

  The pill shows the label, then the percentage while running with a `total` above 0, `done` once done, or `failed` for `error`. Blank lines, lines starting with `#` and keys other than these are skipped, but a line without an `=` or with no key, another `state`, a number with anything but digits in it or a larger one, a NUL, a file over 1024 bytes or one without `state` is ignored whole, and the launcher logs why once each time the file changes. A `done` past `total` counts as `total`. The label keeps printable ASCII only, showing anything else as `?`, so it always draws in the launcher's font.

  The launcher looks in `status.d` once a second, and reads a file again only when its size, mtime or inode changes. It reads regular files named `<pak>.status`, never through a link and never a name starting with a dot, and up to 16 of them; `status.d` itself can't be a link either. A `running` or `error` file shows until it's gone 2 minutes without changing, counted from its mtime and from when the launcher last saw it change, whichever is sooner, so a pak that stopped without cleaning up, or a file left from before the launcher started, doesn't keep its pill up. A `done` file shows for 5 seconds from when the launcher first sees it done, and not at all once it's 2 minutes old. When several files are showing, the pill shows the one changed most recently, by mtime and then by name.

  Write each update to a file whose name starts with a dot, then `mv` it into place, so the launcher never reads half a file, and delete the file or leave it at `done` when finished:

  ```sh
  STATUS="$USERDATA_PATH/launcher/status.d"
  mkdir -p "$STATUS"
  printf 'state=running\ndone=%s\ntotal=%s\nlabel=Syncing saves\n' "$i" "$n" >"$STATUS/.sync.tmp"
  mv "$STATUS/.sync.tmp" "$STATUS/sync.status"
  ```
- **Game metadata:** a `.info` sidecar gives plugins a game's details for their display hooks, through `entry_info` (see "Features added since the first API" above); the launcher doesn't show them itself. It lives beside stock's thumbnails, at `<rom folder>/.media/<rom file name without its last extension>.info`, so `Roms/Game Boy (GB)/Tetris (World).gb` has `Roms/Game Boy (GB)/.media/Tetris (World).info`, and a folder or pak has one in its parent's `.media` by the same rule. Each line is `key=value`, such as `year=1989`, `publisher=Nintendo` or `genre=Puzzle`; which keys mean what is up to the plugins and whatever writes the files, such as a scraper. Blank lines and lines starting with `#` are skipped, and so are lines without an `=` or with a key that's empty, over 31 characters or has anything but letters, digits, `_`, `-` and `.` in it. Keys are matched without case, and a key given twice keeps its last value. Values are trimmed, have control characters (tabs too) turned to spaces, and are cut to 255 bytes, between characters. Up to 32 keys are kept. A file over 4096 bytes, with a NUL in it, with no keys, or that isn't a regular file is ignored, and the launcher logs it. This is the launcher's own format, after kUI's sidecars, whose format wasn't available to match.

## When a plugin crashes

The launcher marks plugin loading as in progress and clears the mark once its first frame is on screen. If it crashes before then, the next start pauses plugins and runs without them. The Launcher pak shows "plugins paused after a crash" and **Try again** turns them back on. A crash after the first frame, such as inside a plugin screen, isn't caught this way: NextUI starts the launcher again with plugins loaded, on the game list, and after two crashes in a row the Launcher pak's wrapper hands the device back to stock NextUI until **Try again**.

The Launcher pak's settings pages, which load only the `strings` and `theme` slots' owners, mark them loaded for as long as the pages are open, since their translations are asked as each page is drawn. If the pages crash with the mark there, the next open leaves the owners out and shows English and stock's theme, so the page that turns a crashing owner off can always be reached; the open after that loads them again. While the launcher has plugins paused after a crash, the pages load none either.

The launcher logs why each plugin did or didn't load to `.userdata/<platform>/logs/nextui.txt`, as `plugins: ...` lines.
