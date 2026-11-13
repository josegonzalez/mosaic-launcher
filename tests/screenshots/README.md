# Screenshot sample card

The card `make screenshots` takes the plugins' doc screenshots on (see "Screenshots" in [docs/DEVELOPMENT.md](../../docs/DEVELOPMENT.md)). `card/` is copied over NextUI's skeleton, `play-history.sql` becomes its `game_logs.sqlite`, and `shots/<id>.sh` lists each plugin's shots.

The games are real homebrew. Their rom files are empty placeholders, as no shot runs a game. The art is the projects' own images, unchanged except that save state previews (`*.bmp.png`) are turned into BMPs when the card is made. The year, publisher and genre in the `.info` files come from each project's repository: its copyright year, its author and how it describes itself. The screenshots in `plugins/<id>/docs/` show this art, under the same licences.

| Title | System | Author | Licence of the art | Source | Files |
|---|---|---|---|---|---|
| Tobu Tobu Girl | Game Boy | Tangram Games | CC BY 4.0 (code MIT) | https://github.com/SimonLarsen/tobutobugirl | `data/bg/titlescreen.png` as its thumbnail, `data/bg/select.png` as its save state preview |
| Tobu Tobu Girl Deluxe | Game Boy Color | Tangram Games | CC BY 4.0 (code MIT) | https://github.com/SimonLarsen/tobutobugirl-dx | `data/bg/titlescreen_dx.png` as its thumbnail, `data/bg/select_dx.png` as a screenshot |
| µCity | Game Boy Color | Antonio Niño Díaz (AntonioND, SkyLyrac) | CC BY-SA 4.0 (media; code GPL-3.0-or-later) | https://github.com/AntonioND/ucity, https://codeberg.org/SkyLyrac/ucity | `screenshot.png` as its thumbnail and save state preview |
| Libbet and the Magic Floor | Game Boy | Damian Yerrick, after Martin Korth's Magic Floor | zlib | https://github.com/pinobatch/libbet | `07-biggar/title_mockup.png` as its thumbnail, `07-biggar/Libbet-repocard.png` as a screenshot |
| Thwaite | NES | Damian Yerrick | GPL-3.0-or-later | https://github.com/pinobatch/thwaite-nes | `docs/manual_ingame.png` as its thumbnail, `docs/manual_title.png` as a screenshot |
| Concentration Room | NES | Damian Yerrick | GPL-3.0-or-later | https://github.com/pinobatch/croom-nes | `docs/croom_screenshot01.png` as its thumbnail and collection art |

Licence texts: [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/), [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/), [GPL-3.0](https://www.gnu.org/licenses/gpl-3.0.html). Libbet's zlib licence:

```
The zlib License
================

Copyright (c) 2018 Damian Yerrick

This software is provided 'as-is', without any express or implied warranty. In
no event will the authors be held liable for any damages arising from the use of
this software.

Permission is granted to anyone to use this software for any purpose, including
commercial applications, and to alter it and redistribute it freely, subject to
the following restrictions:

1.  The origin of this software must not be misrepresented; you must not claim
    that you wrote the original software. If you use this software in a product,
    an acknowledgment in the product documentation would be appreciated but is
    not required.

2.  Altered source versions must be plainly marked as such, and must not be
    misrepresented as being the original software.

3.  This notice may not be removed or altered from any source distribution.
```

Only add a title whose licence clearly allows redistribution, such as CC0, CC BY, CC BY-SA, MIT, zlib or the GPL, and add its row here with the files it brings.
