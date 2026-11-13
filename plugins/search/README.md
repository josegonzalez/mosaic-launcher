# search

Finds games from every system by typing part of a name.

## Turn it on

Turn on **Search** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)).

## How to use

- Open the Search row on the first screen (the Search on the root setting). It asks for the query on the on-screen keyboard, and cancelling backs out of the folder again.
- Or run the `search.open` action from anywhere on the game list. It asks for the query and opens the results. The Search button setting puts it on a button, and [`button-remap`](../button-remap/README.md) or an [FN button](../../docs/glossary.md#fn-button) set to `action:search.open` can too.
- With Remember recent searches on, the last 8 queries are offered before the keyboard, after New search.

A game matches when every word of the query is in the name the game list shows for it (`map.txt` names included), in any order, with case, accents and punctuation left out. So "pokemon" finds "Pokémon Red" and "links" finds "Link's Awakening".

Words can be mixed with filters, and a game has to match every word and pass every filter. A `-` in front of a filter turns it around.

| Filter | Finds |
|---|---|
| `fav:` | Your [favorites](../favorites/README.md) |
| `tag:<name>` | Games with that [tag](../tags/README.md), the name typed without spaces, case or accents, so `tag:beatemup` finds "Beat 'em Up" |
| `unplayed` | Games you haven't played, as [`row-badges`](../row-badges/README.md) counts them while it's on, and otherwise games not in Recently Played |
| `system:<name>` | Games in a system, by the tag in its folder's name (`system:gb` for `Game Boy (GB)`) or the start of its name (`system:gameboy`) |

So `fav: system:gb mario` finds your Game Boy favorites with Mario in the name, and `-tag:beaten` every game without the Beaten tag. A filter on its own lists every game that passes it. While the plugin a filter needs is off, `fav:` and `tag:` find nothing.

The best matches come first, each by name:

1. The name itself.
2. Names starting with the query.
3. Names in which each word starts a word.
4. The rest.

Games are found as stock lists them, in every system with an emulator and up to three folders deep, with a folder holding a `.cue` or `.m3u` named after itself as one game. Games the [`hidden`](../hidden/README.md) plugin hides are left out.

The card is scanned the first time a search needs it, and again only when a folder it read changes. The query is kept, so the folder lists the same results after the launcher restarts, and back from a game launched from them the results open again with it selected.

## Screenshots

Opening Search on the first screen, with the default settings.

![Opening Search on the first screen, with the default settings](docs/default.png)

Results for "tobu".

![Results for "tobu"](docs/results.png)

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Search on the root (`root`) | on, off | on | A Search row on the first screen that asks for a query when it's opened |
| Search button (`button`) | none, Y, L2, R2, SELECT | none | A game list button that asks for a query from anywhere |
| Remember recent searches (`recents`) | on, off | on | Recent queries are offered before the keyboard |

## For plugin authors

- Registers the `search.open` action, which the `button` setting follows, and adds a root folder.
- Keeps the query in `.userdata/<platform>/launcher/search.cfg`.
- Reads the `favorites` and `tags:<name>` entry sets for `fav:` and `tag:`, and searches again on `launcher.set_changed`.
- Calls the `games.played` service for `unplayed`, from row-badges.
- Games any plugin's filter hides, such as [`hidden`](../hidden/README.md)'s, are left out of the results.

## Credits

nx-redux and LodorOS.
