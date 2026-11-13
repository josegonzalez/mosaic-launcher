# playtime-limit

Sets a daily limit on how long games can be played, from the play history stock's [play tracker](../../docs/glossary.md#play-tracker) keeps.

## What it does

Once today's play across every game reaches the Daily limit, launching a game is refused with a message. [Paks](../../docs/glossary.md#pak) such as Tools still open.

- The limit is off by default, so turning the plugin on changes nothing until one is set.
- A header widget beside the status pill shows the time left, such as `45m left`, in the theme's accent, and in a warning colour at or under the Warn at setting. Coming back from a game with little or no time left says so too. The widget is redrawn only when the minute it shows changes, or the day does.
- A game already running isn't stopped when the time runs out. That needs minarch to save and quit on a signal, which stock minarch doesn't do, so that part is left out.
- When the play history can't be read (no `game_logs.sqlite` before the first game, or no sqlite library), games launch as usual, the widget stays hidden, and the plugin logs why once.

### Extra time

With a Code for extra time set, a refused launch offers the Extra time minutes for today after typing that code. The grant is kept with its date in `.userdata/<platform>/launcher/playtime-limit.cfg`, along with the code, and runs out at midnight. The code is never logged.

### PIN

The daily limit, warning, extra time and code are protected, and the code is secret. With the launcher's PIN set, the Plugins page asks for it before showing or changing them or turning the plugin off, and never shows the code once it's set (see [the PIN](../../docs/plugin-api.md#the-pin)).

## Turn it on

Turn on **Play time limit** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)), then pick a limit.

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Daily limit, in minutes (`limit`) | off, 15, 30, 45, 60, 90, 120, 180, 240 | off | Minutes of games a day, across every game. Once they're used up, games don't launch |
| Warn at, minutes left (`warn`) | off, 5, 10, 15, 30 | 10 | The time left in the header turns the warning colour at or under this many minutes |
| Extra time, in minutes (`extra`) | 15, 30, 60 | 15 | With a code set, a refused launch offers this many more minutes for today after typing it |
| Code for extra time (`code`) | text | empty | Kept only in this plugin's cfg and never logged or shown. Empty for no extra time |

All four are protected, and `code` is also secret.

## For plugin authors

- Refuses launches in `pre_launch` and draws a header widget.
- Reads stock's play tracker.
- Writes the day's grant and the code to its `playtime-limit.cfg`.
- `limit`, `warn`, `extra` and `code` carry the `protected` flag, and `code` the `secret` flag in `plugin.txt` (see [plugin-api.md](../../docs/plugin-api.md#plugintxt)).

## Credits

dalexanco (FlexUI).
