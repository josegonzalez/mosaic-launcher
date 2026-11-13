# screensaver

Cycles through images on a full screen once the game list or the home screen has been left alone.

## Turn it on

Turn on **Screensaver** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)). It shows after the Show after idle time (30 seconds by default, so it shows before stock's screen timeout, 60 seconds by default, turns the screen off).

## What it shows

The Images setting picks the source:

| Source | Images |
|---|---|
| `games` (the default) | The art of the recently played games and, while the [`favorites`](../favorites/README.md) plugin is loaded, the favorites, as box art (else the thumbnail) or screenshots by the Game art setting |
| `screenshots` | The card's `Screenshots` folder |
| `folder` | The images in the Image folder setting's folder on the card (`Screensaver` by default) |

PNG, JPEG and BMP files are shown in name order, fitted to the screen on black, up to 256 of them. Each shows for the Seconds per image setting (10 by default) and then changes to the next with a slow cross-fade or, with the Change setting, a slide. It starts after the image it last showed. The Clock setting puts a dim clock in the bottom right corner.

## How it behaves

- It opens only over the game list or the home [slot](../../docs/glossary.md#slot)'s screen, not over the quick menu, the game switcher or other plugins' screens.
- Any button closes it, back to where it opened, and is taken until it's let go, so that press doesn't also act on the screen under it.
- The power button still sleeps the device, and waking closes the screen.
- It never holds off sleep: stock counts its screen timeout from the last button press, not from redraws, so the screen still turns off and the device sleeps on time while it shows. With a screen timeout shorter than the idle time it never shows.
- While the device charges, stock never turns the screen off, so it keeps cycling until a button is pressed.
- With the [`charging-screen`](../charging-screen/README.md) plugin loaded and its idle setting on, charging-screen wins: while the device charges the screensaver doesn't open, so the charging screen opens instead, and neither opens over the other.
- It holds at most two scaled images, the one showing and the next, and frees them when it closes, and redraws every frame only while an image changes.

## Screenshots

µCity's art, with Show after idle set to 15 seconds and the other settings at their defaults.

![µCity's art, with Show after idle set to 15 seconds and the other settings at their defaults](docs/default.png)

Clock set to 24h.

![Clock set to 24h](docs/clock.png)

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Show after idle, in seconds (`idle`) | off, 15, 30, 45, 60, 120, 300, 600 | 30 | How long the game list or home is left alone before the screensaver shows |
| Images (`source`) | games, screenshots, folder | games | Where the images come from |
| Game art (`art`) | box, screenshot | box | Which of the games' art: box art (else the thumbnail) or screenshots |
| Image folder (`folder`) | text | Screensaver | A folder on the card, for the folder source |
| Seconds per image (`interval`) | 5, 10, 20, 30, 60, 120 | 10 | How long each image shows |
| Change (`effect`) | fade, slide | fade | How one image changes to the next |
| Clock (`clock`) | off, 24h, 12h | off | A dim clock in the bottom right corner |

## For plugin authors

- Lists `favorites` and `charging-screen` as `optional`, so it loads after them, since it asks about them as it starts. The favorites are the `favorites` entry set, read with `set_list`.
- Leaves out games `is_filtered` says a plugin's filter hides, such as the [`hidden`](../hidden/README.md) plugin's.
- Reads the card's art, `Screenshots/` and its image folder.

## Credits

msigl.
