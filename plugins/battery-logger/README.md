# battery-logger

Logs how much battery each game used, one row per play session, to a file you can open in a spreadsheet.

It needs a battery, so it does nothing on the desktop build, and its name on the Plugins page says "(unsupported on desktop)".

## Turn it on

Turn on **Battery logger** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)).

## What gets logged

The log is `.userdata/<platform>/launcher/battery-logger.csv`. Each row has these columns:

| Column | Meaning |
|---|---|
| `date` | Local time at launch |
| `game` | The game's name |
| `path` | The game's path, relative to the card |
| `seconds` | Time between the two readings, which leaves out time the device was suspended |
| `start_percent`, `end_percent` | Battery level when the game started and when you came back |
| `drain_percent`, `percent_per_hour` | How much the battery dropped, and how fast |
| `charged` | `yes` for a session that charged, otherwise empty |

Some sessions are skipped:

- Paks aren't logged.
- A session that ends in a reboot or power cut is dropped.
- No row is written when either reading isn't a level from 1 to 100, for sessions shorter than the shortest-session setting, or for sessions that charged (charging at either reading, or the level went up), unless Log sessions that charged is on. Those are logged with `charged` set to `yes` and the drain columns empty.

Fields with commas, quotes or spaces at either end are quoted, with quotes doubled, and newlines become spaces. Each row goes in with one synced write, and a last row a power cut left unfinished is cut off before the next is added. When the file would grow past the largest-size setting, it's renamed to `battery-logger.old.csv`, replacing the last one, and a new file is started.

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Shortest session to log, in minutes (`min_minutes`) | 0, 1, 5, 10 | 1 | Sessions shorter than this aren't logged |
| Largest log size, in KB (`max_kb`) | 64, 256, 1024 | 256 | Past this the log is renamed to `battery-logger.old.csv` and a new one started |
| Log sessions that charged (`charged`) | on, off | off | Off skips sessions where the device charged; on logs them with `charged=yes` |

## For plugin authors

- The launcher exits while a game runs, so the plugin reads the battery level and charging state in `pre_launch`, just before the game starts, and keeps that reading in `/tmp`. It reads them again in `post_return`, as the launcher starts after the game.
- `seconds` is from `CLOCK_MONOTONIC`.
- A session that ends in a reboot or power cut is dropped because `/tmp` is cleared and `post_return` doesn't run.
- Provides the `battery.history` service (`provides=battery.history`): the last 64 sessions it logged, on any platform, as each session's start, length and battery levels (see [plugin-api.md](../../docs/plugin-api.md)).
- Writes `battery-logger.csv` and `battery-logger.old.csv`.
- On a platform without a battery it loads but logs that it's unsupported and does nothing.

## Credits

marcelopcx and kUI.
