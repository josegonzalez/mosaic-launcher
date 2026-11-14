# shellcheck shell=sh
# shard: 3

# first-run sets other plugins up through their setup services: with the
# bundled favorites, theme-plus and localization on, screener walks the
# wizard to its Plugin settings page and changes each row, which saves
# favorites' collection, theme-plus's theme and localization's language
# in their own cfg. The Buttons page lists favorites' action on Y, which it
# binds on the game list, and the Plugins page describes favorites from its
# plugin.txt. With localization off its row is left out, and the Buttons
# page opens on its own from its action.
FI_CFG="$STATE/first-run.cfg"
FI_COLLECTION="$CARD/Collections/First Run Arcade.txt"
for name in first-run favorites theme-plus localization; do
	make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" "$name" >/dev/null || fail "building $name"
done
build_plugin screener 'id=screener\nname=Screener\napi=1\n'
mkdir -p "$STATE" "$(dirname "$SETTINGS")" "$(dirname "$FI_COLLECTION")"
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$WORK/first-run-interop-settings.saved"
rm -f "$FI_CFG" "$STATE/favorites.cfg" "$STATE/theme-plus.cfg" "$STATE/localization.cfg" "$HOME_LEFT"
: >"$FI_COLLECTION"
printf 'first-run=on\nfavorites=on\ntheme-plus=on\nlocalization=on\nscreener=on\n' >"$STATE/plugins.txt"
printf 'suggest=favorites\n' >"$FI_CFG"
plugin=first-run

# on to the Plugin settings page, each row one to the right, then on to the end
printf 'steps=a,a,right,down,right,down,right,a,a,a,a,a\n' >"$STATE/screener.cfg"
log="$WORK/first-run-interop.log"
run_launcher "$log" "plugin screener: done" || fail "the launcher exited early with first-run and its setup services"
expect "setup: favorites.setup Favorites, theme-plus.setup stock, localization.setup off"
expect "about favorites: Adds a Favorites folder on the first screen, and a button to add or remove the selected game."
expect "on Y: FAVORITE (gamelist)"
expect "showing setup, 3 of 7"
expect "localization.setup now ca"
# the next collection and theme by name, after the card's others
fi_collection=$(sed -n 's/.*plugin first-run: favorites.setup now //p' "$log")
fi_theme=$(sed -n 's/.*plugin first-run: theme-plus.setup now //p' "$log")
[ -n "$fi_collection" ] && [ "$fi_collection" != Favorites ] || fail "first-run didn't change the collection: $(grep "favorites" "$log")"
[ -n "$fi_theme" ] && [ "$fi_theme" != stock ] || fail "first-run didn't change the theme: $(grep "theme-plus" "$log")"
expect "showing buttons, 6 of 7"
expect "finished"
grep -qxF "collection=$fi_collection" "$STATE/favorites.cfg" || fail "favorites didn't save the collection: $(cat "$STATE/favorites.cfg")"
grep -qx "language=ca" "$STATE/localization.cfg" || fail "localization didn't save the language: $(cat "$STATE/localization.cfg")"
grep -qxF "theme=$fi_theme" "$STATE/theme-plus.cfg" || fail "theme-plus didn't save the theme: $(cat "$STATE/theme-plus.cfg")"
plugin=favorites
expect "setup: collection $fi_collection, from the next start"

# localization off: its row is left out; the Buttons page alone, closed with B
printf 'first-run=on\nfavorites=on\ntheme-plus=on\nlocalization=off\nscreener=on\n' >"$STATE/plugins.txt"
printf 'steps=action:first-run.open,y,action:first-run.buttons,b\n' >"$STATE/screener.cfg"
log="$WORK/first-run-interop-off.log"
run_launcher "$log" "plugin screener: done" || fail "the launcher exited early with first-run and localization off"
plugin=first-run
fi_rows=$(sed -n 's/.*plugin first-run: setup: //p' "$log")
[ "$fi_rows" = "favorites.setup $fi_collection, theme-plus.setup $fi_theme" ] || fail "first-run didn't leave localization's row out: $fi_rows"
expect "skipped"
expect "buttons opened"
expect "showing buttons, 1 of 1"
case "$(seen)" in
*"first-run.buttons gamelist ") ;;
*) fail "the Buttons page didn't open on its own and close: $(seen)" ;;
esac

rm -f "$SETTINGS" "$FI_CFG" "$FI_COLLECTION" "$STATE/favorites.cfg" "$STATE/theme-plus.cfg" "$STATE/localization.cfg" \
	"$STATE/plugins.txt" "$STATE/screener.cfg" "$HOME_LEFT"
[ -e "$WORK/first-run-interop-settings.saved" ] && mv "$WORK/first-run-interop-settings.saved" "$SETTINGS"
rm -rf "$WORK/plugins/first-run.plugin" "$WORK/plugins/favorites.plugin" "$WORK/plugins/theme-plus.plugin" \
	"$WORK/plugins/localization.plugin" "$WORK/plugins/screener.plugin"
case_log=""
