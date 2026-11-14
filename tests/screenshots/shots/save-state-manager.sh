# shellcheck shell=sh

# Tobu Tobu Girl's states
begin
on save-state-manager
press "down down a down y"
shot default

begin
on save-state-manager
press "down down a down y a"
shot preview

begin
on save-state-manager
press "menu wait left a"
shot all-games

# with romm set up (no server is asked: these are the files it keeps), on
# Tobu Tobu Girl, a game from RomM: slot 1, which romm restored, marked
# synced, and Restore from server selected
begin
on romm save-state-manager
cfg romm url=http://127.0.0.1:9 button=none
mkdir -p "$STATE/romm"
printf '1\tGB\t-\t%s\n' "$CARD/Roms/Game Boy (GB)/Tobu Tobu Girl.gb" >"$STATE/romm/games.txt"
state_file="$CARD/.userdata/shared/GB-gambatte/Tobu Tobu Girl.gb.st0"
state_stat=$(python3 -I -c 'import os, sys; s = os.lstat(sys.argv[1]); print(s.st_size, int(s.st_mtime))' "$state_file")
printf 'down\t%s\t%s\t%s\t%s\n' "$(date +%s)" "${state_stat% *}" "${state_stat#* }" "$state_file" >"$STATE/romm/synced.txt"
press "down down a down y right right"
shot with-romm
