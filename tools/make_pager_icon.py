"""Draw the 123x52 'scanning' artwork for the pager app: a pager on the left,
signal arcs sweeping up and right. Leaves room for the overlaid text the view
draws -- "Int"/"Ext" near (44,3..11) and "Scanning..." at (63,39..47)."""

import struct
import sys
import zlib

W, H = 123, 52
ON, OFF = 0, 255
px = [[OFF] * W for _ in range(H)]


def put(x, y):
    if 0 <= x < W and 0 <= y < H:
        px[y][x] = ON


def hline(x0, x1, y):
    for x in range(min(x0, x1), max(x0, x1) + 1):
        put(x, y)


def vline(x, y0, y1):
    for y in range(min(y0, y1), max(y0, y1) + 1):
        put(x, y)


def rect(x0, y0, x1, y1):
    hline(x0, x1, y0)
    hline(x0, x1, y1)
    vline(x0, y0, y1)
    vline(x1, y0, y1)


def fill(x0, y0, x1, y1):
    for y in range(y0, y1 + 1):
        hline(x0, x1, y)


def rounded(x0, y0, x1, y1):
    """Rectangle outline with the four corner pixels dropped."""
    rect(x0, y0, x1, y1)
    for cx, cy in ((x0, y0), (x1, y0), (x0, y1), (x1, y1)):
        px[cy][cx] = OFF


def line(x0, y0, x1, y1):
    dx, dy = abs(x1 - x0), -abs(y1 - y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    err = dx + dy
    while True:
        put(x0, y0)
        if x0 == x1 and y0 == y1:
            return
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def arc(cx, cy, r, a0, a1, thick=1):
    """Circle segment, angles in degrees, 0 = +x, negative = upward."""
    import math

    steps = max(24, int(r * 6))
    for i in range(steps + 1):
        a = math.radians(a0 + (a1 - a0) * i / steps)
        for t in range(thick):
            x = cx + (r + t) * math.cos(a)
            y = cy + (r + t) * math.sin(a)
            put(int(round(x)), int(round(y)))


# A landscape pager filling the canvas. The view draws two strings over
# this: "Int"/"Ext" at x44..57 y3..11, which the gap between the grille and
# the screen leaves clear, and "Scanning..." at x63..120 y39..47, which
# falls inside the screen - so the word appears on the pager's own display,
# and the screen's lower half is kept empty for it.

# ---- body -------------------------------------------------------------
rounded(0, 0, 122, 51)
rounded(1, 1, 121, 50)

# ---- screen, filling everything above the buttons ---------------------
# Rounded, as the Advisor's LCD is, and double-walled so it reads as a
# recessed display rather than a drawn box.
SX0, SY0, SX1, SY1 = 4, 4, 118, 36
rounded(SX0, SY0, SX1, SY1)
rounded(SX0 + 1, SY0 + 1, SX1 - 1, SY1 - 1)
# Interior left empty: the view writes "Int"/"Ext" and "Scanning..." into
# it, so the words read as being on the pager's own display.

# ---- buttons, a row of four along the bottom --------------------------
for i in range(4):
    bx = 4 + i * 29
    rounded(bx, 40, bx + 25, 47)
    fill(bx + 6, 43, bx + 19, 44)

# ---- encode -----------------------------------------------------------
raw = bytearray()
for y in range(H):
    raw.append(0)
    for x in range(W):
        v = px[y][x]
        raw += bytes((v, v, v))


def chunk(tag, data):
    return (
        struct.pack(">I", len(data))
        + tag
        + data
        + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    )


png = b"\x89PNG\r\n\x1a\n"
png += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
png += chunk(b"IEND", b"")

open(sys.argv[1], "wb").write(png)

# preview
for y in range(H):
    print("".join("#" if px[y][x] == ON else "." for x in range(W)))
