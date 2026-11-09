# edge.py <stock|smooth> <res dir> <log line>: checks the alpha themer
# logged across the end of the bottom right hint pill ("edge pill <h> scale
# <s> alpha <a>...", from the pill's right edge in, a quarter of the way
# down) against the white pill in stock's sheet. At stock's size it's the
# sheet's own, unscaled. At another, it's smooth: at least one pixel is a
# value nearest-neighbour can't give (none of the sheet's pixels it could
# pick there), and each is within those pixels' range, so the edge is in
# its place.
#
# With an "icon pill <h> button <h> scale <s> box <gap> <top> <w> <h>"
# line it checks the battery's box in the status pill instead: at stock's
# size it's exactly the sheet's art in stock's place, and at another it's
# the art scaled by the pill's size over stock's, in its scaled place,
# within 2 pixels each way.

import math
import re
import struct
import sys
import zlib

TOLERANCE = 6  # blending and working the alpha back out round a little
NEW = 12       # how far from every pixel nearest-neighbour could pick
ICON = 110     # the alpha themer counts as part of an icon


def sheet(path):
    data = open(path, "rb").read()
    pos, idat = 8, b""
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    if depth != 8 or ctype != 6:
        sys.exit("%s isn't 8 bit RGBA" % path)
    raw, stride, prev, rows, i = zlib.decompress(idat), w * 4, bytearray(w * 4), [], 0
    for _ in range(h):
        f, cur = raw[i], bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = cur[x - 4] if x >= 4 else 0
            b = prev[x]
            c = prev[x - 4] if x >= 4 else 0
            if f == 1:
                cur[x] = (cur[x] + a) & 255
            elif f == 2:
                cur[x] = (cur[x] + b) & 255
            elif f == 3:
                cur[x] = (cur[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                cur[x] = (cur[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(cur)
        prev = cur
    return rows


def icon(want, res, line):
    m = re.search(r"icon pill (\d+) button (\d+) scale (\d+) box (-?\d+) (-?\d+) (\d+) (\d+)", line)
    if not m:
        sys.exit("no icon in: %s" % line)
    pill, button, scale, gap, top, w, h = (int(v) for v in m.groups())
    rows = sheet("%s/assets@%dx.png" % (res, scale))
    # the battery, at (47, 51, 17, 10) in layout units, and its art's box,
    # with the same threshold themer uses
    rx, ry, rw, rh = (v * scale for v in (47, 51, 17, 10))
    lit = [(x, y) for y in range(rh) for x in range(rw) if rows[ry + y][(rx + x) * 4 + 3] >= ICON]
    bx0, by0 = min(x for x, _ in lit), min(y for _, y in lit)
    bw, bh = max(x for x, _ in lit) - bx0 + 1, max(y for _, y in lit) - by0 + 1
    stock = 30 * scale
    k = pill / stock

    def scaled(v):  # as the patch scales a size
        return (v * pill + stock // 2) // stock

    margin = (pill - button) // 2
    want_box = (margin + scaled(rw) - (bx0 + bw) * k, (pill - scaled(rh)) // 2 + by0 * k, bw * k, bh * k)
    names = ("gap", "top", "width", "height")
    ok = True
    if want == "stock":
        if pill != stock:
            sys.exit("the pill is %d high, not stock's %d" % (pill, stock))
        for name, a, b in zip(names, (gap, top, w, h), want_box):
            if a != b:
                print("the battery's %s is %d, stock's is %d" % (name, a, b))
                ok = False
    else:
        if pill == stock:
            sys.exit("the pill is stock's size")
        if (w, h) == (bw, bh):
            print("the battery is stock's %dx%d" % (w, h))
            ok = False
        for name, a, b in zip(names, (gap, top, w, h), want_box):
            if abs(a - b) > 2:
                print("the battery's %s is %d, not about %.1f" % (name, a, b))
                ok = False
    sys.exit(0 if ok else 1)


def main():
    want, res, line = sys.argv[1:4]
    if "icon pill" in line:
        icon(want, res, line)
    m = re.search(r"edge pill (\d+) scale (\d+) alpha ([\d ]+)", line)
    if not m:
        sys.exit("no edge in: %s" % line)
    pill, scale = int(m.group(1)), int(m.group(2))
    got = [int(v) for v in m.group(3).split()]
    rows = sheet("%s/assets@%dx.png" % (res, scale))
    size = 30 * scale  # the white pill, at (1, 1, 30, 30) in layout units

    def alpha(x, y):
        return rows[scale + y][(scale + x) * 4 + 3]

    # the source pixels any nearest-neighbour scale could pick for output
    # pixel d of n
    def picks(d, n):
        s = (d + 0.5) * size / n - 0.5
        return sorted({min(size - 1, max(0, v)) for v in
                       (d * size // n, int((d + 0.5) * size // n), math.floor(s), math.ceil(s))})

    y = pill // 4
    ok = True
    if want == "stock":
        if pill != size:
            sys.exit("the pill is %d high, not stock's %d" % (pill, size))
        for k, a in enumerate(got, 1):
            if abs(a - alpha(size - k, y)) > TOLERANCE:
                print("pixel %d in is %d, the sheet's is %d" % (k, a, alpha(size - k, y)))
                ok = False
    else:
        if pill == size:
            sys.exit("the pill is stock's size")
        new = 0
        for k, a in enumerate(got, 1):
            near = [alpha(x, sy) for x in picks(pill - k, pill) for sy in picks(y, pill)]
            if a < min(near) - TOLERANCE or a > max(near) + TOLERANCE:
                print("pixel %d in is %d, outside %s" % (k, a, near))
                ok = False
            if all(abs(a - v) > NEW for v in near):
                new += 1
        if not new:
            print("every pixel is one nearest-neighbour could give")
            ok = False
    sys.exit(0 if ok else 1)


main()
