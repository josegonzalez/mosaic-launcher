# tags

Lets you put your own tags on games, and shows each tag as a folder.

## Turn it on

Turn on **Tags** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)).

## How to use

Press **Y** on a game (the Tags button setting picks another). It shares the button with other plugins' actions there. A checklist of every tag opens for that game, with how many games on the card have each.

| Button | What it does |
|---|---|
| **A** | Ticks or clears a tag |
| **A** on New tag | Names a new one on the keyboard and puts it on the game. A name another tag has, ignoring case, picks that tag |
| **X** | Deletes a tag no game on the card has, after a confirm |
| **B** | Goes back |

The action is `tags.edit`, so an [FN button](../../docs/glossary.md#fn-button) or [`button-remap`](../button-remap/README.md) can open it too.

A game is tagged as stock records it in recents: a game folder or a disc of a multi-disc game as its `.m3u` (or a cue-sheet folder as its `.cue`), and a pak or rom as itself.

### Where tags show

- A Tags folder on the first screen, shown while a tag has a game on the card, has a folder per tag with a game on the card, each listing those games. One folder, rather than a row per tag, keeps the first screen's consoles together however many tags there are.
- Each tag with a game on the card also gets a tile in the [quick menu](../../docs/glossary.md#quick-menu)'s top row that opens its folder, for up to 8 tags by name. Tiles are made as the launcher starts, so a new tag gets its tile the next time it starts, such as after a game.

Names are trimmed, up to 63 bytes, and can't have a slash or start with a dot or `#`.

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Tags button (`button`) | Y, L2, R2 | Y | The game list button that opens the selected game's tag checklist, shared with other plugins' actions on it |
| Tags folder on the root (`root`) | on, off | on | A Tags folder on the first screen, with a folder per tag, shown while a tag has a game |
| Tag tiles on the quick menu (`tiles`) | on, off | on | A quick menu tile per tag with games, as the launcher starts |

## For plugin authors

- Provides the `tags:<name>` entry sets (`provides=sets.tags`), with the name exactly as the tag has it. They're announced as changed (`launcher.set_changed`) each time a game is tagged or untagged with it, or it's deleted.
- Registers the `tags.edit` action, which the `button` setting follows.
- The tags are in `.userdata/<platform>/launcher/tags.txt`: a tag's name on a line of its own, then a line for each of its games with the name, a tab and the game's card-relative path. It's written to a temporary file that replaces it, so a power cut leaves the old file or the new one, and read again whenever it changes. Games no longer on the card stay in the file in case the card comes back.

## Credits

radther.
