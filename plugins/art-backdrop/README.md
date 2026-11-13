# art-backdrop

Draws the selected game's art behind the game list, dimmed.

## What it does

The art is the game's backdrop. A game with none gets its screenshot, thumbnail or box instead, the first that has a file. The art covers the top of the screen, scaled to fill it and cropped at the edges. Folders, paks, empty folders and games with no art keep stock's background.

Only art files count, so generated art such as [`art-placeholder`](../art-placeholder/README.md)'s cards isn't blown up behind the list. No bundled plugin gives a backdrop yet; [`art-boxart`](../art-boxart/README.md) gives the screenshot and box.

## Turn it on

Turn on **Art backdrop** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)).

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Style (`style`) | gradient, full | gradient | `gradient` shows the art at full strength over the top half of its area and fades it out to stock's background over the bottom half. `full` shows it at full strength over all of it |
| Dim (`dim`) | theme, 0, 25, 50, 75 | theme | How much darker the art is, in percent. `theme` uses the theme's `art-backdrop.dim` key, a percentage from 0 to 100, or 50 without one. A new theme's dim shows at once |
| Height (`height`) | full, half, third | full | How much of the screen the art covers, from the top |
| Fall back to the screenshot (`screenshot`) | on, off | on | Uses the screenshot when a game has no backdrop |
| Fall back to the thumbnail (`thumbnail`) | on, off | on | Uses the thumbnail when there's no backdrop or screenshot |
| Fall back to the box (`box`) | on, off | on | Uses the box when there's nothing else |

## For plugin authors

- Owns the `background` slot (`slots=background`).
- Reads the `art-backdrop.dim` theme key with `theme_value` (see [plugin-api.md](../../docs/plugin-api.md)).

## Credits

taylorbird, bmpriest and hp7hao.
