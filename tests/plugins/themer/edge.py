# edge.py <stock|smooth> <res dir> <log line>: checks the alpha themer
# logged across the end of the bottom right hint pill ("edge pill <h> scale
# <s> alpha <a>...", from the pill's right edge in, a quarter of the way
# down) against the white pill in stock's sheet. At stock's size it's the
# sheet's own, unscaled. At another, it's smooth: at least one pixel is a
# value nearest-neighbour can't give (none of the sheet's pixels it could
# pick there), and each is within those pixels' range, so the edge is in
# its place.

import math
import re
import struct
import sys
import zlib

TOLERANCE = 6  # blending and working the alpha back out round a little
NEW = 12       # how far from every pixel nearest-neighbour could pick


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


def main():
    want, res, line = sys.argv[1:4]
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
