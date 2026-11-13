# numeric-prefix

Sorts names that start with a number by that number, so 2 comes before 10.

## What it does

Names starting with `1) `, `01. ` or `[1] ` sort by that number, and the number is left off the name shown. It reads the shown name, so it works for games, folders and `map.txt` names. Numbered entries come first, then the rest in the usual order.

- On the first screen only consoles move, staying between Recently Played and Collections. A first screen another plugin replaced, such as [`all-games`](../all-games/README.md)' or [`focus-mode`](../focus-mode/README.md)'s, keeps its owner's order.
- Recently Played and [collections](../../docs/glossary.md#collection) keep their own order.

## Turn it on

Turn on **Numeric prefix ordering** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)).

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Hide the number (`hide`) | on, off | on | Leaves the number off names like "1) Foo" while sorting by it. Turn it off to keep it |

## For plugin authors

- Uses the `display_name`, `sort_compare` and `list_ready` hooks.
- Has no slot, service or file of its own.

## Credits

marcelopcx and Ru_UI.
