# video-previews

Plays a short clip of the selected game in place of its thumbnail.

## What it does

The clip is `<rom dir>/.videos/<rom name>.mp4`, H.264 with AAC or MP3 sound, as scrapers make them. It plays once the selection has rested on the game for the Play after seconds (after the selection settles), looping until the selection moves.

It plays on the built-in list in place of the thumbnail, or in a plugin layout's own spot for the selected game's art. The bundled layouts draw it. A layout that doesn't ask for it with `art_replacement` shows none.

- The sound plays at the system's volume, unless Sound is off. Where the audio device can't open, such as beside stock's own on a device, clips play silently.
- Off the game list, or with Show game art off, there's no thumbnail to stand in for, so nothing plays. The plugin tries again later, waiting longer each time up to 16 seconds.
- Decoding runs on a thread of the plugin's own, at most 30 frames a second. When it can't keep up it skips frames nothing refers to, so a clip gets choppy rather than slow.
- The audio device stays open on silence between clips, as closing it clicks on some devices, and closes after 5 seconds without one. Up to a quarter of a second of a clip's sound can play on after the selection moves.

## Turn it on

Turn on **Video previews** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)). On a build made without ffmpeg, as CI does, or on a desktop without ffmpeg's libraries, the plugin logs that it's unavailable and plays nothing.

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Play after, in seconds (`delay`) | 1, 2, 3, 5 | 2 | Seconds the selection rests on a game before its clip plays |
| Sound (`sound`) | on, off | on | The clip's sound, at the system's volume |

## For plugin authors

- Nothing is decoded until a clip is due: ffmpeg is loaded then, with `dlopen`.
- On devices that's a trimmed LGPL build of ffmpeg 6.1.2 in the plugin's own lib folder, which `make devices` builds (see [DEVELOPMENT.md](../../docs/DEVELOPMENT.md)). On desktop it loads the system's ffmpeg (Homebrew's on macOS).
- Draws through the layouts' `art_replacement` spot, and reads `<rom dir>/.videos/<rom name>.mp4`.

## Credits

NextUI-plus.
