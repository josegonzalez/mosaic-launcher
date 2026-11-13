# screenshot-viewer

Browses the screenshots stock saves in a game, newest first, on a screen of its own, so you can look at them without a Tools [pak](../../docs/glossary.md#pak).

The screenshots are `Screenshots/<game>.<YYYY-MM-DD-HH-MM-SS>.png` on the card.

## Turn it on

Turn on **Screenshot viewer** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)).

## How to use

Open it from a Screenshots tile on the [quick menu](../../docs/glossary.md#quick-menu), which shows while there are screenshots, or from a game list button if you set one. The `screenshot-viewer.open` action opens it too, and [`button-remap`](../button-remap/README.md) or an [FN button](../../docs/glossary.md#fn-button) set to `action:screenshot-viewer.open` can.

It starts on a grid of thumbnails with each game's name under it. The selected one's game and date, from the file name, show on a pill in the top left. Files named another way show their name, and sort by when they were changed.

| Button | What it does |
|---|---|
| D-pad | Moves through the grid |
| **A** | Shows the selected screenshot across the screen, where left and right step to the next and previous |
| **B** | Goes back |
| **Y** | Deletes the selected screenshot after a confirm (Delete with Y), removing only that file |

Only regular files directly in the folder are listed or deleted, and links are never followed. Thumbnails and full views are scaled copies kept in two small pools, the least recently drawn going first, and freed when the screen closes.

### Taking a screenshot

The `screenshot-viewer.capture` action saves what's on screen as `Screenshots/Launcher.<YYYY-MM-DD-HH-MM-SS>.png`, with stock's notification when its screenshot notification setting is on. Run from a menu or dialog, it saves what was on screen before the menu opened. It binds no button itself: put it on an FN button in Settings (`action:screenshot-viewer.capture`) or on another button through `button-remap`.

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Quick menu tile (`tile`) | on, off | on | A Screenshots tile on the quick menu, shown while there are screenshots |
| Screenshots button (`button`) | none, Y, L2, R2, SELECT | none | A game list button that opens the screenshots |
| Delete with Y (`delete`) | on, off | on | **Y** deletes the selected screenshot, after a confirm |

## For plugin authors

- Registers the actions `screenshot-viewer.open` and `screenshot-viewer.capture`; `button` follows `screenshot-viewer.open`. Registers a quick menu tile.
- `screenshot-viewer.capture` uses the launcher's `save_screenshot`.
- Reads and deletes files in `Screenshots/`, and writes `Launcher.<date>.png` there.

## Credits

MaxUI.
