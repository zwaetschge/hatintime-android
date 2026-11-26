#!/usr/bin/env python3
"""Pure-Python anti-aliased launcher icon generator (purple top hat + golden Time Piece hourglass).
usage: make_icon.py <res dir>"""
import math, os, struct, sys, zlib

def write_png(path, w, h, rgba):
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    raw = bytearray()
    stride = w * 4
    for y in range(h):
        raw.append(0)
        raw.extend(rgba[y * stride:(y + 1) * stride])
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)
    ihdr = struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) + chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b''))

def sd_rrect(px, py, cx, cy, hx, hy, r):
    qx = abs(px - cx) - hx + r
    qy = abs(py - cy) - hy + r
    return math.hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - r

def in_poly(x, y, pts):
    inside = False
    n = len(pts)
    j = n - 1
    for i in range(n):
        xi, yi = pts[i]
        xj, yj = pts[j]
        if ((yi > y) != (yj > y)) and (x < (xj - xi) * (y - yi) / (yj - yi + 1e-12) + xi):
            inside = not inside
        j = i
    return inside

def sample(u, v):
    # u, v in [0, 1]
    x, y = u * 512.0, v * 512.0
    if sd_rrect(x, y, 256.0, 256.0, 240.0, 240.0, 110.0) > 0.0:
        return (0, 0, 0, 0)
    # Twilight gradient background
    r = int(28 + 24 * v)
    g = int(16 + 14 * v)
    b = int(58 + 34 * (1.0 - v))
    # Stars
    for sx, sy, sr, sc in [
        (96, 110, 8, (255, 228, 128)), (414, 126, 7, (170, 224, 255)),
        (112, 396, 6, (170, 224, 255)), (402, 384, 9, (255, 228, 128)),
        (154, 78, 5, (255, 244, 200)), (362, 86, 5, (255, 244, 200)),
    ]:
        if (x - sx) ** 2 + (y - sy) ** 2 <= sr * sr:
            r, g, b = sc
    # Hat crown
    if in_poly(x, y, [(154, 112), (358, 112), (334, 314), (178, 314)]):
        r, g, b = (118, 56, 190)
        if in_poly(x, y, [(174, 122), (212, 122), (200, 304), (184, 304)]):
            r, g, b = (154, 92, 226)
        # Golden band
        if 262.0 <= y <= 312.0:
            r, g, b = (255, 206, 72)
    # Hat brim (ellipses)
    if ((x - 256.0) / 178.0) ** 2 + ((y - 332.0) / 38.0) ** 2 <= 1.0:
        r, g, b = (92, 40, 156)
    if ((x - 256.0) / 158.0) ** 2 + ((y - 326.0) / 26.0) ** 2 <= 1.0:
        r, g, b = (132, 68, 206)
    # Time Piece hourglass
    if in_poly(x, y, [(224, 260), (288, 260), (256, 304)]):
        r, g, b = (150, 236, 255)
    if in_poly(x, y, [(238, 268), (274, 268), (256, 294)]):
        r, g, b = (255, 214, 86)
    if in_poly(x, y, [(256, 300), (288, 346), (224, 346)]):
        r, g, b = (255, 214, 86)
    if sd_rrect(x, y, 256.0, 254.0, 40.0, 8.0, 5.0) <= 0.0 or sd_rrect(x, y, 256.0, 352.0, 40.0, 8.0, 5.0) <= 0.0:
        r, g, b = (255, 234, 138)
    return (r, g, b, 255)

def render(size, ss=2):
    out = bytearray(size * size * 4)
    inv = 1.0 / (size * ss)
    n2 = ss * ss
    for y in range(size):
        for x in range(size):
            sr = sg = sb = sa = 0
            for sy in range(ss):
                for sx in range(ss):
                    r, g, b, a = sample((x * ss + sx + 0.5) * inv, (y * ss + sy + 0.5) * inv)
                    sr += r * a; sg += g * a; sb += b * a; sa += a
            i = (y * size + x) * 4
            if sa:
                out[i] = sr // sa
                out[i + 1] = sg // sa
                out[i + 2] = sb // sa
                out[i + 3] = sa // n2
    return out

def main():
    res = sys.argv[1]
    sizes = {'mdpi': 48, 'hdpi': 72, 'xhdpi': 96, 'xxhdpi': 144, 'xxxhdpi': 192}
    for k, s in sizes.items():
        write_png(f'{res}/mipmap-{k}/ic_launcher.png', s, s, render(s, ss=2))
    write_png('docs/icon_512.png', 512, 512, render(512, ss=2))
    print('Generated A Hat in Time launcher icons + docs/icon_512.png')

if __name__ == '__main__':
    main()
