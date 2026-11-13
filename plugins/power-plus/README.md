# power-plus

Guards against a flat battery: it warns when the battery runs low and powers the device off cleanly, and can power it off after a long sleep.

On the desktop build, which has no battery, it loads and does nothing, saying so in the log.

## Turn it on

Turn on **Power plus** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)).

## Low battery guard

While the launcher is on screen it reads the battery every 5 seconds.

1. At or below the warning level (5% by default) for 3 readings in a row, it warns once.
2. At or below the power-off level (2% by default) for 3 readings in a row, it counts down 30 seconds on screen and then powers off the way the quick menu's Poweroff does, so every plugin's `before_poweroff` runs.

Charging, or a reading above the power-off level, stops the countdown. A failed reading counts for nothing, and it never powers off while charging.

The launcher doesn't run while a game does, so the guard only watches the battery in the launcher. Turn it off with the Low battery guard setting.

## Power off after a long sleep

Nothing in the launcher runs while the device sleeps. So for power-off after a long sleep, it sets the RTC's wake alarm before a sleep from the launcher, for the chosen hours.

- When the device wakes at that alarm, it says so and powers off.
- Waking it yourself, before or after then, resumes as usual and clears the alarm.
- It doesn't set the alarm while charging or while `/tmp/stay_awake` or `/tmp/stay_alive` exists, and doesn't power off if one of them holds when the alarm wakes it.
- Without a writable wake alarm this part is off.
- It's off by default and not yet tested on hardware.

Stock's screen and suspend timeouts still decide when the device sleeps.

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Low battery guard (`guard`) | on, off | on | Warns, then powers off cleanly, when the battery runs low |
| Low battery warning at, in % (`warn_at`) | 3, 5, 10, 15 | 5 | The level at or below which it warns |
| Low battery power off at, in % (`off_at`) | 1, 2, 3, 5 | 2 | The level at or below which it counts down and powers off |
| Power off after a long sleep (`sleep_off`) | on, off | off | Powers off when the device has been asleep this long, by waking it with the RTC's wake alarm |
| Hours asleep before powering off (`sleep_hours`) | 1, 2, 4, 8, 12, 24 | 4 | How long it sleeps before powering off |

## For plugin authors

- Powers off the way the quick menu's Poweroff does, so every plugin's `before_poweroff` hook runs.
- Sets the RTC's wake alarm before a sleep, and reads `/tmp/stay_awake` and `/tmp/stay_alive`.

## Credits

The low battery guard is from Bramley, and power-off after a sleep from NextUI-plus.
