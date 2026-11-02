"""sheet.py <frames.csv> <out.png> [every_ms] [cols] [from_ms] [to_ms]: a contact sheet of the intro's frames.
Each tile is one frame: every key of the half at its coords, coloured by its LED (gamma 1/2.2,
roughly what the eye makes of PWM). Tiles run left to right, top to bottom, every_ms apart from 0."""
import csv, struct, sys, zlib
from collections import defaultdict

src, dst = sys.argv[1], sys.argv[2]
every = int(sys.argv[3]) if len(sys.argv) > 3 else 150
cols = int(sys.argv[4]) if len(sys.argv) > 4 else 8
frames = defaultdict(list)
for t, i, d, x, y, r, g, b in csv.reader(open(src)):
    frames[int(t)].append((int(x), int(y), int(r), int(g), int(b)))
lo = int(sys.argv[5]) if len(sys.argv) > 5 else 0
hi = int(sys.argv[6]) if len(sys.argv) > 6 else 1 << 30
times = [t for t in sorted(frames) if t % every == 0 and lo <= t <= hi]
xs = [k[0] for k in frames[times[0]]]; ys = [k[1] for k in frames[times[0]]]
x0, y0 = min(xs), min(ys)
U = 14  # px per key unit
KEY = 12
tw = (max(xs) - x0) * U // 100 + U + 6
th = (max(ys) - y0) * U // 100 + U + 6
rows = (len(times) + cols - 1) // cols
W, Hh = cols * tw, rows * th
img = [[(24, 24, 24)] * W for _ in range(Hh)]
gam = [round(255 * (v / 255) ** (1 / 2.2)) for v in range(256)]
for n, t in enumerate(times):
    ox, oy = (n % cols) * tw + 3, (n // cols) * th + 3
    for x, y, r, g, b in frames[t]:
        px, py = ox + (x - x0) * U // 100, oy + (y - y0) * U // 100
        c = (gam[r], gam[g], gam[b])
        if (r, g, b) == (0, 0, 0):
            c = (48, 48, 48)
        for yy in range(py, py + KEY):
            for xx in range(px, px + KEY):
                img[yy][xx] = c
raw = b"".join(b"\x00" + bytes(v for p in row for v in p) for row in img)
def chunk(k, d):
    return struct.pack(">I", len(d)) + k + d + struct.pack(">I", zlib.crc32(k + d) & 0xFFFFFFFF)
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, Hh, 8, 2, 0, 0, 0)) + \
      chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
open(dst, "wb").write(png)
print(f"{dst}: {len(times)} frames, {every} ms apart, {cols} a row, {W}x{Hh}")
