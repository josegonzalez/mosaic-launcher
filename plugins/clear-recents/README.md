# clear-recents

Clears the Recently Played list, one game at a time or all at once.

## Turn it on

Turn on **Clear recents** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)).

## How to use

- In Recently Played, press **Y** (the Recently Played button setting picks another) to remove the selected game or clear them all. When both apply, **Y** opens a menu of the two.
- A Clear recents tile on the [quick menu](../../docs/glossary.md#quick-menu) shows while there are recents.
- Clearing asks first, then lists the folders again, so the quick menu's Recents tile goes away.

Outside Recently Played the button is left to other plugins.

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Recently Played button (`button`) | Y, L2, R2 | Y | The game list button in Recently Played that removes the selected game or clears them all |
| Clear recents tile on the quick menu (`tile`) | on, off | on | Shows a Clear recents tile on the quick menu while there are recents |

## For plugin authors

- Registers the actions `clear-recents.remove` and `clear-recents.clear`; the `button` setting follows both (`button:clear-recents.remove` and `button:clear-recents.clear`).
- Registers a quick menu tile.

## Credits

MyMinUI, MinUI-Zero, nx-redux and Nocturne.
