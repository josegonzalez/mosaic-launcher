# music

A music player that keeps playing while you play a game.

It plays MP3, FLAC and WAV files (mono or stereo) from a folder on the card, `Music` by default, and the folders inside it. Ogg Vorbis isn't supported: the stb_vorbis decoder has memory-safety reports upstream that were never fixed.

## Turn it on

Turn on **Music** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)), and put music in the Music folder on the card.

## How to use

Open the player from the Music tile on the [quick menu](../../docs/glossary.md#quick-menu), from a game list button (the Music button setting, none by default), or with `action:music.open`.

It lists the folder's folders, then its tracks, in file name order with numbers by value. Each track shows its title tag (ID3v2.2, 2.3 or 2.4, or ID3v1, in an MP3; a Vorbis comment in a FLAC), or its file name without one. What's playing shows with its title, artist, time and cover art.

| Button | What it does |
|---|---|
| **A** | Plays a track and then the rest of its folder, or opens a folder |
| **B** | Goes back up, and closes the player from the music folder |
| **X** | Pauses or plays on |
| **L1** and **R1** | Previous track (or the track's start after 3 seconds) and next |
| **L2** and **R2** | Seek 10 seconds |
| **SELECT** | Turns shuffle on or off |
| **START** | Steps repeat through off, all and one |
| **Y** | Opens the lock screen |

Shuffle and repeat are kept as the Shuffle and Repeat settings.

- **Lock screen** (also `action:music.lock`): the clock and the track, dim on black, with the cover art dim above the clock where there's room and the artist under the title. It takes every button but power until **SELECT** and then **START** within a second.
- **Play and pause from anywhere:** `action:music.toggle` pauses or plays on, from an [FN button](../../docs/glossary.md#fn-button) for example.
- **Now playing pill:** over the game list, quick menu, switcher and other plugin screens, a pill with the title and artist shows at the bottom for the Now playing pill setting's seconds after a track starts or plays on.

### Cover art

The art is the track's embedded picture (an ID3v2 APIC or PIC frame, a FLAC PICTURE block or a Vorbis comment's METADATA_BLOCK_PICTURE, a JPEG or PNG, the front cover first), else the first of `cover.jpg`, `cover.png` and `folder.jpg` in its folder, matched without case.

### Games and sleep

- While music plays it holds off auto-sleep (the Stay awake while playing setting). The power button still sleeps and pauses it.
- Before a game or [pak](../../docs/glossary.md#pak) launches the music pauses and the player closes the audio device, so the game can have it, and it plays on when the launcher comes back. The During games setting's `play` keeps it playing through games instead, where the device's audio allows two players.
- A sleep pauses it (it doesn't play on after waking), a pause closes the device after 10 seconds, and a power-off quits the player.

### Picking up after a power-off

After a power-off, with Resume after power-off on (the default), the next start shows the last track paused where it was, on the pill and the player, without starting the player. The queue's tracks no longer on the card are dropped, and nothing is shown when the track itself is gone. **X** or `action:music.toggle` plays on from there, with the Shuffle and Repeat settings as they are then.

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Music folder (`folder`) | text | Music | The folder on the card the player lists, with its folders. MP3, FLAC and WAV files |
| During games (`games`) | pause, play | pause | Pause the music before a game or pak and play on after it, or keep it playing through it |
| Now playing pill, in seconds (`overlay`) | off, 3, 5, 10, always | 5 | How long the now playing pill shows over other screens after a track starts |
| Stay awake while playing (`awake`) | on, off | on | Holds off auto-sleep while music plays. The power button still sleeps and pauses it |
| Shuffle (`shuffle`) | on, off | off | The queue's order. **SELECT** on the player changes it too |
| Repeat (`repeat`) | off, all, one | off | What happens at the queue's end. **START** on the player changes it too |
| Resume after power-off (`resume`) | on, off | on | After a power-off, shows the last track paused where it was, for **X** to play on |
| Quick menu tile (`tile`) | on, off | on | A Music tile on the quick menu |
| Music button (`button`) | none, Y, L2, R2, SELECT | none | A game list button that opens the player |

## For plugin authors

The player is in two parts: `musicd`, a daemon that ships beside the plugin's `plugin.so` and plays outside the launcher, so music goes on while the launcher exits for a game, and the plugin, its front end.

- The plugin starts `musicd` by its path, in a session of its own, the first time something is played. It talks to it over a Unix socket, `/tmp/mosaic-music.sock`, and its pid is in `/tmp/mosaic-music.pid`. It logs to `.userdata/<platform>/logs/music.txt`, and exits by itself after 10 minutes stopped. A power-off quits it, by the socket or else by its pid.
- It plays through SDL's audio, decoded with the dr_libs decoders in `vendor/dr_libs`.
- Registers the actions `music.open`, `music.lock` and `music.toggle`; `button` follows `music.open`. Registers a quick menu tile.
- An embedded picture is written to `/tmp/mosaic-music-art/` (a folder of the user's own, with each file made with `O_EXCL` and `O_NOFOLLOW` and renamed into place, only the playing track's kept) for the launcher to load with `art_load`, and scaled once for where it's drawn.
- Tags are read on a thread of the plugin's own, one track at a time, so a big folder doesn't hold up a frame: names show until titles come in.
- Tag files are treated as untrusted:
  - every length is checked against the file, tag or block it's in
  - text frames and comments are read up to 4 KiB, and titles, artists and albums are cut to 255 bytes
  - pictures over 4 MiB, unsynchronised ID3v2.2 and 2.3 tags over 16 MiB, compressed or encrypted frames and anything past 4096 frames, blocks or comments are skipped
  - the text shown is always valid UTF-8 (mended with U+FFFD) without control characters
- The daemon saves the queue (with its shuffled order), the track, its place and repeat in `.userdata/<platform>/launcher/music/resume.txt`. It writes when it quits or is stopped by its pid, and when a track is played, paused, held for a game or put to sleep, through a temporary file it syncs and renames. It removes the file when the music stops at the queue's end. The daemon keeps the file, not the plugin, as it has the queue and is what runs when the device powers off from a game.
- `awake` uses the host's keep-awake call.

## Credits

Bramley and MaxUI.
