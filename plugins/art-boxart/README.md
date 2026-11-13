# art-boxart

Finds box art in the art folders other forks use, so games show it without you moving files.

It works with other art providers: it only answers for files that exist, so a game without any falls through to the next provider and stock.

## Turn it on

Turn on **Box art** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)). Every source below is on by default except where noted.

## Where it looks

A game's art is named by its file name without its last extension, as stock names its `.media` thumbnail. A folder's art is named by its whole name. For the box, the first of these that exists wins:

1. A Pico-8 cart (`*.p8.png`), which is its own art.
2. A folder's `cover.png`.
3. Beside the game, nx-redux's flat box in `.media/boxart2d/<name>`, then its `.media/boxart/<name>`: the 3D box, or the flat one when there's no 3D.
4. `Imgs/<name>.png`.
5. `.res/<name>.png`.
6. `Images/<name>.png`.

The `media` setting also gives the 3D box from `.media/boxart/`, the logo from `.media/wheel/` and the screenshot from `.media/screenshot/`. In nx-redux's folders a `.png` is looked for, then a `.jpg` and a `.jpeg`, as nx-redux reads them. Its `.webp` art isn't read, as the art cache can't decode it, and its `.media/mix/` composite is left out because it isn't any one kind of art.

The built-in list shows the box when a game has no thumbnail. Folders named `Imgs` or `Images` are left out of the game list while their setting and the `hide` setting are both on.

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Use as the thumbnail (`thumbnail`) | on, off | off | Also uses the box as the thumbnail, before stock's `.media/<name>.png` |
| Box, wheel and screenshot in .media (`media`) | on, off | on | nx-redux's `.media/boxart2d`, `boxart`, `wheel` and `screenshot` beside the games |
| Imgs folders (`imgs`) | on, off | on | MyMinUI's `Imgs/<game>.png` beside the games |
| .res folders (`res`) | on, off | on | UnuOS's `.res/<game>.png` beside the games |
| Images folders (`images`) | on, off | on | Visible art folders, `Images/<game>.png` beside the games |
| Hide art folders (`hide`) | on, off | on | Leaves the `Imgs` and `Images` folders out of the list |
| Folder covers (`cover`) | on, off | on | Uses a folder's `cover.png` |
| Hide covered folder names (`cover_name`) | on, off | off | Hides the names of folders with a `cover.png`, while Folder covers is on, so the cover stands in for the folder |
| Pico-8 carts as art (`rom_art`) | on, off | on | Pico-8 `.p8.png` carts are their own art |

## For plugin authors

- Answers the launcher's art requests (the box, 3D box, logo and screenshot kinds) and chains with other art providers.
- Reads art files only; it writes nothing.

## Credits

Box art folders are from MyMinUI (`Imgs`), UnuOS (`.res`) and nx-redux (box, 3D box, wheel and screenshot). Visible art folders are from meszaroszoltan and MyMinUI, folder as cover is from Ru_UI, and rom as art is from LazaroFilm.
