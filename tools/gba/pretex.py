#!/usr/bin/env python3
"""Grey texture images for the GBA port.

The engine tints a texture at load time: it doubles the tex0 source (plain,
or with a 5x5 box smoothing for palette-mapped textures), maps grey to a
per-level colour gradient, and builds a mipmap pyramid. On the GBA the
sampler applies the tint through a lookup table instead (tools/gba/patches),
so all it needs from ROM is the grey data the loader would have tinted:

    gry1  the source texels        grm1  its mipmaps
    gry2  plain 2x doubling        grm2
    gry3  smoothed 2x doubling     grm3

Mipmaps follow the engine's layout: successive half-size levels, contiguous,
while the parent is wider and taller than 31 texels.
"""
import hashlib, os, struct

def parse_tex0(data):
    version, flags, w, h = struct.unpack_from('<HHHH', data, 0)
    n = data[8]
    name = data[9:9 + n].decode('latin1')
    off = 9 + n + 1
    return {'version': version, 'flags': flags, 'w': w, 'h': h, 'name': name, 'pixels': data[off:off + w * h], 'pixoff': off}

def double_plain(px, w, h):
    out = bytearray(4 * w * h)
    W = 2 * w
    for y in range(h):
        row = px[y * w:(y + 1) * w]
        d = bytearray(W)
        d[0::2] = row; d[1::2] = row
        out[(2 * y) * W:(2 * y + 1) * W] = d
        out[(2 * y + 1) * W:(2 * y + 2) * W] = d
    return bytes(out)

def double_smooth(px, w, h):
    """sub_34a0: a 5x5 box average over the plain-doubled image, computed in
    place in raster order (so pixels above and to the left are already
    smoothed), columns wrapping, rows clipped."""
    W, H = 2 * w, 2 * h
    buf = bytearray(double_plain(px, w, h))
    mask = W - 1
    for y in range(H):
        rows = [r for r in range(y - 2, y + 3) if 0 <= r < H]
        cnt = 5 * len(rows)
        bases = [r * W for r in rows]
        yoff = y * W
        for x in range(W):
            s = 0
            xs = ((x - 2) & mask, (x - 1) & mask, x, (x + 1) & mask, (x + 2) & mask)
            for b in bases:
                s += buf[b + xs[0]] + buf[b + xs[1]] + buf[b + xs[2]] + buf[b + xs[3]] + buf[b + xs[4]]
            buf[yoff + x] = s // cnt
    return bytes(buf)

def mipmaps(px, w, h):
    out = bytearray()
    while w > 31 and h > 31:
        W, H = w // 2, h // 2
        lvl = bytearray(W * H)
        for y in range(H):
            r0 = px[(2 * y) * w:(2 * y + 1) * w]
            r1 = px[(2 * y + 1) * w:(2 * y + 2) * w]
            for x in range(W):
                lvl[y * W + x] = (r0[2 * x] + r0[2 * x + 1] + r1[2 * x] + r1[2 * x + 1] + 2) >> 2
        out += lvl
        px, w, h = bytes(lvl), W, H
    return bytes(out)

def average(px):
    return sum(px) // len(px) if px else 0

def precompute(data, cache_dir=None):
    """All six images for one tex0 resource, as a dict."""
    key = hashlib.sha1(data).hexdigest()
    if cache_dir:
        p = os.path.join(cache_dir, key + '.bin')
        if os.path.exists(p):
            d = open(p, 'rb').read()
            hdr = struct.unpack_from('<6I', d, 0)
            off = 24; res = {}
            for name, n in zip(('gry1', 'grm1', 'gry2', 'grm2', 'gry3', 'grm3'), hdr):
                res[name] = d[off:off + n]; off += n
            t = parse_tex0(data)
            res.update(w=t['w'], h=t['h'])
            return res
    t = parse_tex0(data)
    w, h, px = t['w'], t['h'], t['pixels']
    g2 = double_plain(px, w, h)
    g3 = double_smooth(px, w, h)
    res = {'gry1': px, 'grm1': mipmaps(px, w, h), 'gry2': g2, 'grm2': mipmaps(g2, 2 * w, 2 * h),
           'gry3': g3, 'grm3': mipmaps(g3, 2 * w, 2 * h), 'w': w, 'h': h}
    if cache_dir:
        os.makedirs(cache_dir, exist_ok=True)
        names = ('gry1', 'grm1', 'gry2', 'grm2', 'gry3', 'grm3')
        with open(os.path.join(cache_dir, key + '.bin'), 'wb') as f:
            f.write(struct.pack('<6I', *[len(res[n]) for n in names]))
            for n in names: f.write(res[n])
    return res

def rgb444_table(tclt):
    """Nearest palette index for every 12-bit RGB, from a Palm colour table
    resource (count, then index/r/g/b per entry). The weights match the
    runtime's fallback in gba/src/texhook.c."""
    n = struct.unpack_from('>H', tclt, 0)[0]
    pal = [(tclt[2 + 4 * i + 1], tclt[2 + 4 * i + 2], tclt[2 + 4 * i + 3]) for i in range(n)]
    out = bytearray(4096)
    for k in range(4096):
        r, g, b = ((k >> 8) & 15) * 17, ((k >> 4) & 15) * 17, (k & 15) * 17
        best, bd = 0, 1 << 30
        for i, (pr, pg, pb) in enumerate(pal):
            d = 2 * (r - pr) ** 2 + 4 * (g - pg) ** 2 + 3 * (b - pb) ** 2
            if d < bd: bd, best = d, i
        out[k] = best
    return bytes(out)

# The index the runtime reads: 'TXIX', count, then per texture
#   u32 tex0 data address, u16 w, u16 h, u32 x6 image addresses, u8 x3 averages, u8 pad, u32 reserved
TXIX_ENTRY = '<IHHIIIIIIBBBBI'
TXIX_ENTRY_SIZE = struct.calcsize(TXIX_ENTRY)

def txix_pack(entries):
    out = struct.pack('<4sI', b'TXIX', len(entries))
    for e in entries:
        out += struct.pack(TXIX_ENTRY, e['tex_addr'], e['w'], e['h'], e['gry1'], e['grm1'], e['gry2'], e['grm2'], e['gry3'], e['grm3'],
                           e['avg1'], e['avg2'], e['avg3'], 0, 0)
    return out

if __name__ == '__main__':
    import sys, time
    d = open(sys.argv[1], 'rb').read()
    n = struct.unpack_from('>H', d, 0x4c)[0]
    res = sorted([struct.unpack_from('>4sHI', d, 0x4e + i * 10) for i in range(n)], key=lambda r: r[2])
    for i, (t, id_, off) in enumerate(res):
        if t != b'tex0': continue
        end = res[i + 1][2] if i + 1 < len(res) else len(d)
        tx = parse_tex0(d[off:end])
        t0 = time.time()
        r = precompute(d[off:end], sys.argv[2] if len(sys.argv) > 2 else None)
        print(id_, tx['name'], tx['w'], tx['h'], 'flags', hex(tx['flags']), 'pix range', min(tx['pixels']), max(tx['pixels']),
              'trailing', end - off - tx['pixoff'] - tx['w'] * tx['h'], 'sizes', [len(r[k]) for k in ('gry1', 'grm1', 'gry2', 'grm2', 'gry3', 'grm3')], f'{time.time() - t0:.1f}s')
