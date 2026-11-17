# ui-sounds

Plays short sounds as you use the launcher: moving through a list, selecting, going back, opening and closing menus, launching a game, a message showing and a mistake. It comes with a set of sounds, and a theme can bring its own.

## What it does

- A sound plays for each of these:

  | Sound | When |
  |---|---|
  | Move | Up, down, left, right, L1 or R1, on every press and as a held button repeats |
  | Select | A |
  | Back | B |
  | Menu open and menu close | The [quick menu](../../docs/glossary.md#quick-menu) or a dialog, such as a yes or no question or the keyboard, opening or closing |
  | Toast | A short message showing at the bottom of the screen, such as "Added to Favorites" |
  | Error | A wrong PIN |
  | Launch | A game or [pak](../../docs/glossary.md#pak) starting |

- The sounds come from your theme when it has them (see [Theme sounds](#theme-sounds)), and from the set that comes with the plugin otherwise.
- They have a volume of their own, under the system's volume: the volume buttons change both. When the system is muted, they're silent too.
- They play over [music](../music/README.md) and over a [video preview](../video-previews/README.md)'s sound, without pausing or quieting them, where the device can play two sounds at once.

## Turn it on

Turn on **UI sounds** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)).

## How to use

There's nothing to do once it's on. To make the sounds quieter or louder, change its Volume setting; 0 turns them off.

## Theme sounds

With [`theme-plus`](../theme-plus/README.md) on and a theme that's a folder, `<sd>/Themes/<name>/`, a `sounds/` folder in it holds the theme's sounds, one WAV file for each sound:

`move.wav`, `select.wav`, `back.wav`, `error.wav`, `launch.wav`, `menu_open.wav`, `menu_close.wav` and `toast.wav`.

- A theme's sound plays in place of the plugin's own. A sound the theme doesn't have, or one that won't read, plays the plugin's own.
- A WAV file can be 8, 16, 24 or 32-bit PCM or 32-bit float, mono or stereo, at 8000 to 96000 Hz, and up to 1 MiB. A sound longer than 2 seconds is cut to 2 seconds. Short sounds work best, as each sound stops the one before it.
- The sounds are read when the launcher starts and again when you pick another theme. Why a file didn't read is in the launcher's log.

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Volume (`volume`) | 0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 | 50 | How loud the sounds are, as a share of the system's volume. 0 turns them off |

## The bundled sounds

| File | Source | License |
|---|---|---|
| `move.wav`, `select.wav`, `back.wav`, `error.wav`, `launch.wav`, `menu_open.wav`, `menu_close.wav`, `toast.wav` | Sine tones made by [`tools/make-sounds.py`](tools/make-sounds.py) in this repo | [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) |

`res/sounds/SOURCES.txt` beside them says the same.

## For plugin authors

- Plays its sounds with `sound_play`, at its volume, scaling the samples, and nothing while `system_volume` is 0. Moves, selects and backs come from an `input_filter` that takes no buttons, and sees only the buttons filters before it in load order left. The rest come from the launcher's events: `launcher.launch`, `launcher.menu_open`, `launcher.menu_close`, `launcher.toast` and `launcher.error`.
- Reads the theme's sounds folder with `theme_value("ui-sounds.dir")`, a full path, which `theme-plus` sets for a folder theme with a `sounds/` folder, and reads them again on `launcher.theme_changed`. Reads the bundled ones from its `res/sounds/`.
- Provides the `ui-sounds.play` service (`provides=ui-sounds.play`) for other plugins: `sound=<move, select, back, error, launch, menu_open, menu_close or toast>` plays it and gives `played=1`, or `played=0` when it's silent (the volume is 0, the system is muted or there's no file), and any other sound gives `error=sound`.
- With `MOSAIC_UI_SOUNDS_LOG` set in its environment it logs each sound it plays, for the desktop tests.
- Writes no files. No slots, requires or conflicts.

## Credits

muOS, spruceOS and MinUI-Plus.
