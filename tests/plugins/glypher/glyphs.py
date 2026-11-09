#!/usr/bin/env python3
# Writes glyphs.ttf, the fallback font glypher's case uses: a TrueType font
# made here (under this repo's licence), with a filled box for each of
# printable ASCII and the Hangul 한 and 글, which the stock fonts don't
# have. The boxes are what the case looks for: ASCII must never be drawn
# with them, and Hangul must. Run it again only to change the font:
#
#   python3 -I tests/plugins/glypher/glyphs.py tests/plugins/glypher/glyphs.ttf

import struct
import sys

EM = 1000
ASCENT = 900
DESCENT = 300

# (code point, advance, box as x0, y0, x1, y1 or None for an empty glyph)
GLYPHS = [(0x20, 600, None)]
GLYPHS += [(c, 600, (100, 0, 500, 700)) for c in range(0x21, 0x7F)]
GLYPHS += [(0xAE00, 1000, (100, -100, 900, 800)), (0xD55C, 1000, (100, -100, 900, 800))]


def checksum(data):
    data += b"\0" * (-len(data) % 4)
    return sum(struct.unpack(">%dI" % (len(data) // 4), data)) & 0xFFFFFFFF


def box(x0, y0, x1, y1):
    # one clockwise contour of four on-curve points
    points = [(x0, y0), (x0, y1), (x1, y1), (x1, y0)]
    out = struct.pack(">hhhhh", 1, x0, y0, x1, y1)
    out += struct.pack(">HH", 3, 0) + b"\x01" * 4
    last = 0
    for x, _ in points:
        out += struct.pack(">h", x - last)
        last = x
    last = 0
    for _, y in points:
        out += struct.pack(">h", y - last)
        last = y
    return out


def font():
    # glyph 0 is .notdef, empty; the rest are in GLYPHS' order
    glyf = b""
    loca = [0, 0]
    hmtx = struct.pack(">Hh", 500, 0)
    for _, advance, rect in GLYPHS:
        if rect:
            glyf += box(*rect)
        loca.append(len(glyf))
        hmtx += struct.pack(">Hh", advance, rect[0] if rect else 0)
    count = len(GLYPHS) + 1

    # format 4, one segment per code point
    codes = [c for c, _, _ in GLYPHS] + [0xFFFF]
    ids = list(range(1, count)) + [0]
    seg = len(codes)
    search = 1
    selector = 0
    while search * 2 <= seg:
        search *= 2
        selector += 1
    sub = struct.pack(">HHHH", seg * 2, search * 2, selector, seg * 2 - search * 2)
    sub += struct.pack(">%dH" % seg, *codes) + b"\0\0"
    sub += struct.pack(">%dH" % seg, *codes)
    sub += struct.pack(">%dH" % seg, *[(g - c) & 0xFFFF for c, g in zip(codes, ids)])
    sub += struct.pack(">%dH" % seg, *([0] * seg))
    sub = struct.pack(">HHH", 4, len(sub) + 6, 0) + sub
    cmap = struct.pack(">HH", 0, 2) + struct.pack(">HHI", 0, 3, 20) + struct.pack(">HHI", 3, 1, 20) + sub

    head = struct.pack(">IIIIHHqqhhhhHHhhh", 0x00010000, 0x00010000, 0, 0x5F0F3CF5, 0x000B, EM, 0, 0,
                       0, -DESCENT, 900, ASCENT, 0, 8, 2, 0, 0)
    hhea = struct.pack(">IhhhHhhhhhhhhhhhH", 0x00010000, ASCENT, -DESCENT, 0, 1000, 0, 0, 900,
                       1, 0, 0, 0, 0, 0, 0, 0, count)
    maxp = struct.pack(">IHHHHHHHHHHHHHH", 0x00010000, count, 4, 1, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0)
    os2 = struct.pack(">HhHHHhhhhhhhhhhh", 4, 600, 400, 5, 0, 650, 700, 0, 140, 650, 700, 0, 480, 50, 250, 0)
    os2 += b"\0" * 10 + struct.pack(">IIII", 1, 0x01000000, 0, 0) + b"MOSA"
    os2 += struct.pack(">HHHhhhHHIIhhHHH", 0x40, 0x20, 0xD55C, ASCENT, -DESCENT, 0, ASCENT, DESCENT,
                       1, 0, 500, 700, 0, 0x20, 1)
    post = struct.pack(">IIhhIIIII", 0x00030000, 0, -100, 50, 0, 0, 0, 0, 0)
    names = [(1, "Glypher"), (2, "Regular"), (4, "Glypher Regular"), (6, "Glypher-Regular")]
    strings = b""
    records = b""
    for nid, text in names:
        data = text.encode("utf-16-be")
        records += struct.pack(">HHHHHH", 3, 1, 0x409, nid, len(data), len(strings))
        strings += data
    name = struct.pack(">HHH", 0, len(names), 6 + len(records)) + records + strings

    tables = {
        b"OS/2": os2, b"cmap": cmap, b"glyf": glyf, b"head": head, b"hhea": hhea, b"hmtx": hmtx,
        b"loca": struct.pack(">%dH" % len(loca), *[o // 2 for o in loca]), b"maxp": maxp,
        b"name": name, b"post": post,
    }
    tags = sorted(tables)
    search = 1
    selector = 0
    while search * 2 <= len(tags):
        search *= 2
        selector += 1
    out = struct.pack(">IHHHH", 0x00010000, len(tags), search * 16, selector, len(tags) * 16 - search * 16)
    offset = 12 + 16 * len(tags)
    body = b""
    head_at = 0
    for tag in tags:
        data = tables[tag]
        if tag == b"head":
            head_at = offset + len(body)
        out += struct.pack(">4sIII", tag, checksum(data), offset + len(body), len(data))
        body += data + b"\0" * (-len(data) % 4)
    whole = bytearray(out + body)
    struct.pack_into(">I", whole, head_at + 8, (0xB1B0AFBA - checksum(bytes(whole))) & 0xFFFFFFFF)
    return bytes(whole)


with open(sys.argv[1], "wb") as f:
    f.write(font())
