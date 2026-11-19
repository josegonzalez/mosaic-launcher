# home-dashboard

A home screen that comes before the game list, with your last game ready to resume.

## What it does

The launcher starts on it, and **B** at the first screen goes back to it. A game or [pak](../../docs/glossary.md#pak) opened from it comes back to it afterwards.

## Turn it on

Turn on **Home dashboard** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)). It owns the `home` [slot](../../docs/glossary.md#slot).

## How to use

| Button | What it does |
|---|---|
| **X** (labelled GAMES) | Go on to the game list |
| **MENU** | Opens the [quick menu](../../docs/glossary.md#quick-menu) |
| **SELECT** | Opens the game switcher |
| **A** | Opens the selected game: a recent through the switcher's own call, resuming its save state when it has one, and anything else as the game list opens it |
| **B** | Does nothing, as at the first screen |
| **L1** and **R1** | Switch tabs, in the dashboard mode |
| Up and down | Move between the continue card and the rows under it, and scroll the Stats tab when it doesn't fit |

### Modes

The Home mode setting picks the home.

- **dashboard** (the default) has tabs, switched with **L1** and **R1** and kept when the home screen opens again:
  - Home: a continue card for the last game played, with its save state's preview (or its thumbnail or box) and its [`playtime-captions`](../playtime-captions/README.md) caption, such as "1h 25m · today", in that plugin's parts and format, which **A** resumes. While playtime-captions is off, the card shows the game's play time instead, such as "1h 25m played". Under it are the rows the Home rows setting names, one at a time: down from the card goes to the first, down again to the next, and left and right move along a row. A row with nothing in it is left out, and while more than one shows, the one shown has its heading over it, or at the right of the tab bar where there's no room over it, when it fits there. The Top shelf setting turns the rows off. The rows are:
    - `continue`: the other recent games with a save state to resume.
    - `recent`: the games played before the last one.
    - `added`: the games put on the card last.
    - `played`: the most played games: the [`activity-stats`](../activity-stats/README.md) plugin's Most Played games while it's on, else the play history's.
    - `random`: games picked at random, again each time the launcher starts.
    - `shortcuts`: tiles that run the actions the Shortcuts setting names, each with its plugin's quick menu tile icon (or its label's first letter) and label. A name is an action's id, such as `search.open`, or a plugin's id, such as `random-game`, for its quick menu tile's action, else its first one. A name that isn't there, or whose action can't run now, is left out and noted once in the log.
  - Pinned: favorites' games in the favorites collection's order, shown while the [`favorites`](../favorites/README.md) plugin is loaded (the Pinned tab setting).
  - Stats: today's play time, the time left today while [`playtime-limit`](../playtime-limit/README.md) has a daily limit set (such as "35m of 1h 0m", of today's limit with any extra time), the recently played games' total and the five most played of the recent and pinned games (the Stats tab setting). While [`romm`](../romm/README.md) is on and has downloaded games still on the card, a Recently downloaded section lists the last three, with the day each came. When they don't all fit, up and down scroll the tab, and the Most played and Recently downloaded headings only show with a game under them. The launcher has no call for the whole play history, so Stats covers only the games the dashboard knows about. With no play history (no database yet, or no sqlite) it says so rather than showing numbers.
- **minui:** a plain list of the recently played games, then Games, which goes on to the game list.
- **retroachievements:** the recent and pinned games with RetroAchievements progress on the card, as "12/40 · 150/400 points", from what minarch keeps there through the host's `achievements` call. Games with nothing cached are left out, and nothing is fetched.
- **pico8:** the Pico-8 carts (`.p8` and `.p8.png`) in the `Roms` folders whose tag is one of the Pico-8 folder tags setting's, as a grid, with their art or else their first letter.
- **minimal:** just the continue card and the clock, in 24 or 12 hour time (the Minimal clock setting).
- **consoles:** every system with games as a tile, with its icon or art, or else its name on a tile in the theme's colours, and its game count under it. **A** opens the system's folder in the game list.

Hidden games, and anything else a plugin's filter hides, are left out of every row and count.

## Screenshots

The Home tab, with the default settings.

![The Home tab, with the default settings](docs/default.png)

The Stats tab.

![The Stats tab](docs/stats.png)

With favorites: the Pinned tab.

![With favorites: the Pinned tab](docs/pinned-with-favorites.png)

Home mode set to minui.

![Home mode set to minui](docs/mode-minui.png)

Home rows set to `continue, recent, added, played, random`, on the Recently added row.

![Home rows set to continue, recent, added, played, random, on the Recently added row](docs/rows.png)

The shortcuts row, with search, random-game, activity-stats and system-info on.

![The shortcuts row, with search, random-game, activity-stats and system-info on](docs/shortcuts.png)

Home mode set to minimal.

![Home mode set to minimal](docs/mode-minimal.png)

Home mode set to consoles.

![Home mode set to consoles](docs/mode-consoles.png)

With playtime-captions: its caption on the continue card.

![With playtime-captions: its caption on the continue card](docs/with-playtime-captions.png)

With playtime-limit's daily limit set to 60 minutes: the time left on the Stats tab.

![With playtime-limit's daily limit set to 60 minutes: the time left on the Stats tab](docs/with-playtime-limit.png)

The same, scrolled down to the most played games.

![The same, scrolled down to the most played games](docs/with-playtime-limit-scrolled.png)

With romm and two games it downloaded: the Stats tab scrolled down to Recently downloaded.

![With romm and two games it downloaded: the Stats tab scrolled down to Recently downloaded](docs/with-romm.png)

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Home mode (`mode`) | dashboard, minui, retroachievements, pico8, minimal, consoles | dashboard | Which home to show, as above |
| Top shelf (`shelf`) | on, off | on | The rows under the continue card |
| Home rows (`rows`) | text | `continue, recent` | The rows under the continue card, in order: `continue`, `recent`, `added`, `played`, `random` and `shortcuts` |
| Shortcuts (`shortcuts`) | text | `search, random-game, activity-stats, system-info` | The actions the shortcuts row runs, by action or plugin id |
| Minimal clock (`clock`) | 24h, 12h | 24h | The minimal mode's clock |
| Pinned tab (`pinned`) | on, off | on | Favorites' games, while favorites is on |
| Stats tab (`stats`) | on, off | on | Play time from stock's play tracker |
| Pico-8 folder tags (`pico8_tags`) | text | `P8,PICO8,PICO` | The tags of the folders the pico8 mode lists carts from, such as `Roms/Pico-8 (P8)` |

## For plugin authors

- Owns the `home` slot (`slots=home`).
- Provides the `home-dashboard.setup` service (`provides=home-dashboard.setup`), which [`first-run`](../first-run/README.md) sets the home mode through. With no arguments it gives the mode now as `mode=<mode>`, `mode.count=6` and each mode as `mode.<i>=<mode>` with its label, as the Plugins page shows it in the user's language, as `mode.label.<i>`, from 0, and `restart=1`, as a new mode shows the next time the launcher starts. `mode=<dashboard, minui, retroachievements, pico8, minimal or consoles>` saves the Home mode setting first, and any other mode gives `error=mode` and saves nothing. Other keys are ignored.
- Lists `favorites` as `optional`, so it loads after it, since it asks as it starts whether favorites is loaded. The Pinned tab is the `favorites` entry set, read with `set_list` each time the screen opens.
- Leaves out of every tab what `is_filtered` says a plugin's filter hides, such as the [`hidden`](../hidden/README.md) plugin's games.
- Reads stock's play tracker (`play_game_info` for the played row), the switcher's recents, the host's `achievements` call, and the card index for the added and random rows and the consoles' counts, which it builds again on `launcher.index_ready`, `launcher.set_changed` and `launcher.profile_changed`, not as it draws.
- Reads `activity-stats`' `activity-stats.most-played` entry set for the played row while activity-stats is on.
- Finds the shortcuts with `action_info`, checks them with `action_available`, draws each tile's icon from `quick_tile_icon` and runs them with `run_action`.
- Asks `playtime-captions`' `captions.text` service for the continue card's caption, and `playtime-limit`'s `limit.left` service for the Stats tab's time left. Without them it shows the play time on the card and no time left.
- Asks `romm`'s `romm.downloads` service for the Stats tab's Recently downloaded section as the screen opens. Without it there's no section.

## Credits

nx-redux, Bramley, kUI and MaxUI (home modes); nx-redux and Bramley (rows, shortcuts, console tiles, and the minimal and consoles modes).
