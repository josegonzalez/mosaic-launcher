# layout-fancy

Shows the game list as the built-in list's rows beside a panel with the selected game's art.

## What it does

The rows, their names, the selected name's pill and how the d-pad moves through them are the built-in list's, so every button stays the launcher's, and they end where they do beside stock's thumbnail.

The panel is on the right, as stock's thumbnail is. It shows:

- The game's box art, or else its thumbnail, from the art cache.
- The preview of its last save state: the slot minarch last used, named in `.userdata/shared/.minui/<tag>/<rom>.txt`, whose preview is `<rom>.<slot>.bmp` beside it, found as stock finds them for resuming (a disc by its game's `.m3u`).

With Prefer state preview on, the preview stands in for the box when there is one. With it off, the box goes in the top half and the preview in the bottom half. The state files are looked for once each time the selection moves to another game, and again back from a game.

Art shows only while Show game art is on. With it off the rows take the width. A name a plugin hides shows only on the selected row while the panel has no art, as on the built-in list.

## Turn it on

Turn on **Fancy layout** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)). It owns the `layout` [slot](../../docs/glossary.md#slot), so only one layout is on at a time.

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Prefer state preview (`prefer_preview`) | on, off | on | Shows the last save state's preview in place of the box art when there is one |

## For plugin authors

- Owns the `layout` slot (`slots=layout`).
- Row decorators draw on each row, and art decorators under the panel's art.
- A plugin's `art_replace` surface takes the box's place in the panel. The save state's preview never stands in for it: with `prefer_preview` off the preview goes under it.
- Reads the save state files named above.

## Credits

MyMinUI.
