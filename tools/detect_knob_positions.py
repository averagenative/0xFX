#!/usr/bin/env python3
"""
Detect red dot markers in amp/pedal images to find knob positions.

Place bright red dots (R>200, G<60, B<60) at knob centers in the image,
then run this script to get normalized (x,y) coordinates.

Usage:
    python3 tools/detect_knob_positions.py resources/amps/brit_crunch_nobg.png
    python3 tools/detect_knob_positions.py resources/pedals/*_body_nobg.png
"""

import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    # Fallback: use stb-style raw PNG parsing
    import struct, zlib

    def load_image(path):
        with open(path, 'rb') as f:
            sig = f.read(8)
            chunks = []
            while True:
                raw = f.read(4)
                if len(raw) < 4: break
                length = struct.unpack('>I', raw)[0]
                ctype = f.read(4)
                data = f.read(length)
                f.read(4)  # crc
                chunks.append((ctype, data))

        ihdr = [c for c in chunks if c[0] == b'IHDR'][0][1]
        w, h, bd, ct = struct.unpack('>IIBB', ihdr[:10])
        idat = b''.join(c[1] for c in chunks if c[0] == b'IDAT')
        raw = zlib.decompress(idat)
        bpp = 4 if ct == 6 else 3
        stride = w * bpp + 1
        prev_row = bytes(w * bpp)
        pixels = []

        def paeth(a, b, c):
            p = a + b - c
            pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
            if pa <= pb and pa <= pc: return a
            elif pb <= pc: return b
            return c

        for y in range(h):
            offset = y * stride
            filt = raw[offset]
            row = bytearray(raw[offset+1:offset+1+w*bpp])
            if filt == 1:
                for i in range(bpp, len(row)): row[i] = (row[i] + row[i-bpp]) & 0xFF
            elif filt == 2:
                for i in range(len(row)): row[i] = (row[i] + prev_row[i]) & 0xFF
            elif filt == 3:
                for i in range(len(row)):
                    a = row[i-bpp] if i >= bpp else 0
                    row[i] = (row[i] + (a + prev_row[i]) // 2) & 0xFF
            elif filt == 4:
                for i in range(len(row)):
                    a = row[i-bpp] if i >= bpp else 0
                    b = prev_row[i]
                    c = prev_row[i-bpp] if i >= bpp else 0
                    row[i] = (row[i] + paeth(a, b, c)) & 0xFF
            prev_row = bytes(row)
            for x in range(w):
                idx = x * bpp
                r, g, b_ = row[idx], row[idx+1], row[idx+2]
                a = row[idx+3] if bpp == 4 else 255
                pixels.append((r, g, b_, a))
        return w, h, pixels
else:
    def load_image(path):
        img = Image.open(path).convert('RGBA')
        w, h = img.size
        pixels = list(img.getdata())
        return w, h, pixels


def find_knob_positions(path):
    w, h, pixels = load_image(path)

    # Find red pixels
    red_px = []
    for y in range(h):
        for x in range(w):
            r, g, b, a = pixels[y * w + x]
            if r > 200 and g < 60 and b < 60 and a > 128:
                red_px.append((x, y))

    if not red_px:
        return w, h, []

    # Cluster nearby red pixels (within 2.5% of image dims)
    threshold_x = w * 0.025
    threshold_y = h * 0.025
    used = set()
    clusters = []
    for i, (px, py) in enumerate(red_px):
        if i in used: continue
        cluster = [(px, py)]
        used.add(i)
        for j, (qx, qy) in enumerate(red_px):
            if j in used: continue
            if abs(qx - px) < threshold_x and abs(qy - py) < threshold_y:
                cluster.append((qx, qy))
                used.add(j)
        clusters.append(cluster)

    # Get center of each cluster, normalize
    centers = []
    for cluster in clusters:
        cx = sum(p[0] for p in cluster) / len(cluster)
        cy = sum(p[1] for p in cluster) / len(cluster)
        centers.append((cx / w, cy / h))

    centers.sort(key=lambda c: c[0])
    return w, h, centers


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return

    for path in sys.argv[1:]:
        p = Path(path)
        if not p.exists():
            print(f"  {path}: not found")
            continue

        w, h, centers = find_knob_positions(path)
        name = p.stem.replace('_nobg', '').replace('_body', '')
        print(f"\n{name} ({w}x{h}): {len(centers)} knobs")

        if not centers:
            print("  No red dots found")
            continue

        # Output as C struct initializer
        print(f"  /* {name} knob positions (from red dot detection) */")
        for i, (nx, ny) in enumerate(centers):
            print(f"  {{ PARAM_{i},  {nx:.3f}f, {ny:.3f}f }},  /* knob {i} */")

        print()
        # Also output raw for reference
        for i, (nx, ny) in enumerate(centers):
            print(f"  knob {i}: x={nx:.3f}, y={ny:.3f}  (px={nx*w:.0f}, py={ny*h:.0f})")


if __name__ == "__main__":
    main()
