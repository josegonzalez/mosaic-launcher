# The palettes stock's Settings lists, as its palette.c reads them: its
# own folder, then the card's, each in the order the folder is read (not
# sorted), a .txt not starting with . whose version is at most 1, named by
# its name= or else its file name. Prints "<builtin> <name>" for each.
import os
import re
import sys


def names(folder, builtin):
    try:
        entries = os.listdir(folder)  # readdir's order, as stock's
    except OSError:
        return
    for entry in entries:
        if entry.startswith(".") or len(entry) < 5 or not entry.lower().endswith(".txt"):
            continue
        path = os.path.join(folder, entry)
        try:
            with open(path, "rb") as f:
                lines = f.read().decode("utf-8", "replace").splitlines()
        except OSError:
            continue
        version, name = 1, ""
        for line in lines:
            m = re.match(r"version=\s*([+-]?(0[xX][0-9a-fA-F]+|0[0-7]*|[1-9][0-9]*))", line)
            if m:
                version = int(m.group(1), 0)
                continue
            if line.startswith("name="):
                name = line[5:].rstrip("\r\n")[:63]
        if version > 1:
            continue
        if not name:
            name = (entry[:-4] if entry.lower().endswith(".txt") else entry).replace("_", " ")[:63]
        yield builtin, name


listed = list(names(sys.argv[1], 1)) + list(names(sys.argv[2], 0))
for builtin, name in listed[:64]:
    print(builtin, name)
