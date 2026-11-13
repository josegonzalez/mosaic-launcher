# hidden

Drops the games you hide from every list. **Y** on the game list hides the selected game or pak (the `button` setting picks another), and a Hidden folder on the root, shown while something is hidden, lists them so the same button unhides them. The list is `.userdata/<platform>/launcher/hidden.txt`, one full path per line. Other plugins read it as the `hidden` entry set, which also has whatever is in a hidden folder, announced as changed (`launcher.set_changed`) each time something is hidden or unhidden. From MyMinUI.
