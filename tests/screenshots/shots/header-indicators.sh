# shellcheck shell=sh

begin
on header-indicators
shot default

begin
on header-indicators
cfg header-indicators fps=on
press "wait wait wait wait wait wait wait"
shot fps

begin
on header-indicators
cfg header-indicators profile=Sam
shot profile

# the sound going to a Bluetooth device, from the launcher's desktop
# stand-in for audiomon's .asoundrc
begin
on header-indicators
printf 'defaults.bluealsa.device "AA:BB:CC:DD:EE:FF"\n\npcm.!default {\n    type plug\n    slave.pcm {\n        type bluealsa\n        device "AA:BB:CC:DD:EE:FF"\n        profile "a2dp"\n    }\n}\nctl.!default {\n    type bluealsa\n}\n' >"$WORK/tmp/asoundrc.fake"
MOSAIC_LAUNCHER_AUDIO_SINK_FAKE="$WORK/tmp/asoundrc.fake"
export MOSAIC_LAUNCHER_AUDIO_SINK_FAKE
shot bt-audio
unset MOSAIC_LAUNCHER_AUDIO_SINK_FAKE
