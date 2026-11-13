# layout-hero-rail

Shows the game list as a row of cards along the bottom of the screen, under big art for the selected game.

## What it does

- **The hero** across the top is the selected entry's backdrop, or else its screenshot, thumbnail or box, the first that has a file. Generated art such as [`art-placeholder`](../art-placeholder/README.md)'s cards isn't blown up. It's scaled to fill the space and cropped at the edges, tinted toward the theme's accent colour and fading into the theme's background colour above the cards. Where there's no art, stock's background stays.
- **The logo** (from [`art-boxart`](../art-boxart/README.md), out of `.media/wheel`), or else the entry's name in the large font, sits at the hero's bottom left and fades in once the selection settles.
- **The cards** show the entry's thumbnail, or else its box art. While an entry has none, or it's still loading, its card is a tile in the theme's accent colour with the first letter of its name.
- Names show under the cards when the launcher shows names. A name a plugin hides doesn't show where there's art to stand in for it.
- The selected card is raised and framed in the theme's main colour.
- Art only shows while Show game art is on.

The art of the entries either side of the selection, and of the cards just outside the rail, is asked for ahead so it's in when it shows.

## Turn it on

Turn on **Hero and card rail layout** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)). It owns the `layout` [slot](../../docs/glossary.md#slot), so only one layout is on at a time.

## How to use

| Button | What it does |
|---|---|
| Left and right | Move one card |
| Up and down | Move a rail's worth of cards back or on, stopping at the first and last |
| **A**, **B**, **X**, the shoulders and the letter jumps | Stay the launcher's |

A press past an end wraps around to the other end, and holding the direction stops there instead.

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Card size (`cards`) | theme, small, medium, large | theme | How tall the cards are: 22, 28 or 34 percent of the screen above the hints. `theme` uses the theme's `layout-hero-rail.cards` key, one of those three, or `medium` without one. A new theme is followed at once |
| Tint (`tint`) | 0, 20, 40 | 20 | How much the hero art is tinted toward the theme's accent colour, in percent |
| Logo fade (`fade`) | 0, 150, 300, 600 | 300 | How long the logo takes to fade in once the selection settles, in milliseconds |

## For plugin authors

- Owns the `layout` slot (`slots=layout`).
- Reads the `layout-hero-rail.cards` theme key with `theme_value` (see [plugin-api.md](../../docs/plugin-api.md)).
- Row decorators draw on each card. A plugin's `art_replace` surface stands in for the hero's art, tinted and faded the same way. Art decorators don't draw under the hero, as the cards are there.

## Credits

tapionallanaire.
