# activity-stats

Shows your play history on a screen of its own: how long you played each day, and which games you played most.

The history is what the Game Tracker tool keeps in `game_logs.sqlite`. This plugin only reads it.

## Turn it on

Turn on **Activity stats** in Launcher > Plugins. See [turning plugins on and off](../../docs/plugins.md#turning-plugins-on-and-off).

## How to use

Open the screen from the Activity tile on the [quick menu](../../docs/glossary.md#quick-menu), or from a button on the game list if you set one in Settings below. [`button-remap`](../button-remap/README.md) or an [FN button](../../docs/glossary.md#fn-button) set to `action:activity-stats.open` can open it too.

There are four tabs:

- Last 7 days: a bar chart of the time played each day, with each day's time over its bar, and the total and the average a day.
- Last 30 days: the same chart for 30 days.
- Games: every game in the history, most played first, with its time and sessions.
- Launches: the same games with the most sessions first.

| Button | What it does |
|---|---|
| **L1** and **R1**, or left and right | Step through the tabs |
| Up and down | Move through a list |
| **A** | Open the selected game, while its file is still on the card |
| **B** | Go back |

Games carried over from Onion are known only by their name, so they can't be opened. Launch counts are the tracker's sessions, and waking from sleep starts a new one. With no play history (no database yet, or no sqlite) every tab says "No play history".

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Quick menu tile (`tile`) | on, off | on | Shows the Activity tile on the quick menu |
| Activity button (`button`) | none, Y, L2, R2, SELECT | none | Puts the `activity-stats.open` action on a game list button |

## For plugin authors

- Reads `game_logs.sqlite`, the play tracker's database, and never writes it.
- Registers the `activity-stats.open` action, which the `button` setting binds (it is the setting `button:activity-stats.open` names), and a quick menu tile.
- Takes no slot and provides nothing.

## Credits

Bramley, casualducko and kUI.
