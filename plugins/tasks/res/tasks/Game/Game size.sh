#!/bin/sh
# Shows how much space the selected game takes. It only reads.
# $1 is the game's full path, a file or a multi-disc game's folder
du -sh "$1"
size=$(du -sh "$1" | cut -f1)
echo "$(basename "$1"): $size"
