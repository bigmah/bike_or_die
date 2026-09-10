#!/usr/bin/env python3
"""Pack Palm databases into the ROM data image the GBA runtime reads in place.

    packdata.py --base 0x08400000 -o data.bin prc:app.prc pkdir:BOOT_dir prc:pack.prc ...

Every resource and record is laid out as a chunk with the same 24-byte header
the runtime's heap uses (see gba/src/heap.h), so the memory manager handles a
pointer into ROM exactly like one into RAM: MemHandleLock reads the master
word, MemPtrSize reads the size, and writes are simply ignored by the bus.
"""
import argparse, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import armpatch, pretex

CHUNK_MAGIC = 0xA5
CF_ROM, CF_RES, CF_REC = 0x02, 0x04, 0x08
DB_RESOURCE = 1

def align4(n):
    return (n + 3) & ~3

def fourcc(s):
    return struct.unpack('>I', s.encode('latin1'))[0]

class DB:
    def __init__(self, name, typ, creator, attrs=0, version=0, crDate=0, modDate=0, seed=0):
        self.name, self.type, self.creator = name, typ, creator
        self.attrs, self.version, self.crDate, self.modDate, self.seed = attrs, version, crDate, modDate, seed
        self.resources = []   # (type, id, bytes)
        self.records = []     # (attr, uid, bytes)
        self.appinfo = b''
        self.is_res = True

def parse_prc(path):
    d = open(path, 'rb').read()
    if len(d) < 78: print(f'skipping {path}: not a database'); return None
    name = d[0:32].split(b'\0')[0].decode('latin1')
    attrs, ver, crDate, modDate = struct.unpack('>HHII', d[32:44])
    appInfoID, = struct.unpack('>I', d[52:56])
    typ, creator = d[60:64].decode('latin1'), d[64:68].decode('latin1')
    seed, nextList, nrec = struct.unpack('>IIH', d[68:78])
    db = DB(name, typ, creator, attrs, ver, crDate, modDate, seed)
    db.is_res = bool(attrs & 1)
    ents = []
    off = 78
    for i in range(nrec):
        if db.is_res:
            rt = d[off:off+4].decode('latin1'); rid, ro = struct.unpack('>HI', d[off+4:off+10])
            ents.append((rt, rid, ro)); off += 10
        else:
            ro, = struct.unpack('>I', d[off:off+4]); attr = d[off+4]; uid = int.from_bytes(d[off+5:off+8], 'big')
            ents.append((attr, uid, ro)); off += 8
    offs = sorted(set(e[2] for e in ents) | ({appInfoID} if appInfoID else set()) | {len(d)})
    def blob(o):
        nxt = offs[offs.index(o) + 1]
        return d[o:nxt]
    if appInfoID:
        db.appinfo = blob(appInfoID)
    for e in ents:
        if db.is_res: db.resources.append((e[0], e[1], blob(e[2])))
        else: db.records.append((e[0], e[1], blob(e[2])))
    return db

def parse_pkdir(path):
    kv = {}
    for line in open(os.path.join(path, 'header'), encoding='latin1'):
        if '=' in line:
            k, v = line.strip().split('=', 1); kv[k] = v.strip("'")
    db = DB(os.path.basename(path).rsplit('_', 1)[0].replace('_', ' '), kv.get('type', '    '), kv.get('creator', '    '),
            int(kv.get('attributes', '1')), int(kv.get('version', '0')), int(kv.get('crDate', '0')), int(kv.get('modDate', '0')), int(kv.get('uniqueIDSeed', '0')))
    db.name = kv.get('name', db.name)
    for f in sorted(os.listdir(path)):
        parts = f.split('.')
        if len(parts) != 3: continue
        typ, hexname, rid = parts
        typ = bytes.fromhex(hexname).decode('latin1')
        db.resources.append((typ, int(rid), open(os.path.join(path, f), 'rb').read()))
    return db

def build(dbs, base):
    out = bytearray()
    addrs = {}
    def pad():
        while len(out) % 4: out.append(0)
    nd = len(dbs)
    header_size = 16 + nd * 96
    out += struct.pack('<4sIII', b'BODR', 1, nd, 0)
    out += b'\0' * (nd * 96)
    chunks_meta = []
    for i, db in enumerate(dbs):
        pad()
        items = db.resources if db.is_res else db.records
        entries_off = len(out)
        out += b'\0' * (12 * len(items))
        appinfo_addr = 0
        if db.appinfo:
            pad()
            appinfo_addr = base + len(out) + 24
            out += struct.pack('<IIHHIBBHI', 0, 0, 0, 0, len(db.appinfo), CHUNK_MAGIC, CF_ROM, 0, 0)
            out += db.appinfo
        masters = []
        for it in items:
            pad()
            chunk_addr = base + len(out)
            data_addr = chunk_addr + 24
            data = it[2]
            if db.is_res:
                typ, rid, attr = fourcc(it[0]), it[1], 0
                flags = CF_ROM | CF_RES
            else:
                typ, rid, attr = 0, it[1] & 0xFFFF, (it[0] << 8) | (it[1] >> 16)
                flags = CF_ROM | CF_REC
            out += struct.pack('<IIHHIBBHI', data_addr, typ, rid, attr, len(data), CHUNK_MAGIC, flags, 0, chunk_addr)
            out += data
            addrs[(i, len(masters))] = data_addr
            masters.append((typ, rid, it, chunk_addr))
        for j, (typ, rid, it, chunk_addr) in enumerate(masters):
            if db.is_res:
                struct.pack_into('<IHHI', out, entries_off + 12 * j, typ, rid, 0, chunk_addr)
            else:
                struct.pack_into('<III', out, entries_off + 12 * j, (it[0] << 24) | (it[1] & 0xFFFFFF), chunk_addr, 0)
        name = db.name.encode('latin1')[:31]
        struct.pack_into('<32sIIHHIIIIIII', out, 16 + i * 96, name, fourcc(db.type), fourcc(db.creator), db.attrs, db.version,
                         db.crDate, db.modDate, len(items), base + entries_off, DB_RESOURCE if db.is_res else 0, appinfo_addr, db.seed)
    pad()
    return out, addrs

def patch_engine(db, patch_dir):
    """Apply tools/gba/patches/armc<id>.s to the app's ARM code resources."""
    for k, (typ, rid, data) in enumerate(db.resources):
        spec = os.path.join(patch_dir, f'{typ}{rid}.s')
        if typ == 'armc' and os.path.exists(spec):
            patched, syms = armpatch.apply(data, open(spec).read(), name=f'{typ}{rid}')
            db.resources[k] = (typ, rid, patched)
            print(f'patched {typ} {rid}: {len(data)} -> {len(patched)} bytes')

def precomputed_db(dbs, cache):
    """The 'Precomputed' database: grey images for every tex0 resource in the
    image, plus the 'txix' index the runtime looks textures up in. The index
    holds ROM addresses, filled in by fixup_txix() once everything is laid out."""
    pre = DB('Precomputed', 'data', 'BiKD', attrs=1)
    sources = []     # (db index, item index, images)
    for i, db in enumerate(dbs):
        if not db.is_res: continue
        for k, (typ, rid, data) in enumerate(db.resources):
            if typ != 'tex0' or len(data) < 12: continue
            sources.append((i, k, pretex.precompute(data, cache)))
    n = 0
    for (i, k, img) in sources:
        n += 1
        for name in ('gry1', 'grm1', 'gry2', 'grm2', 'gry3', 'grm3'):
            pre.resources.append((name, n, img[name]))
    pre.txix_index = len(pre.resources)
    pre.resources.append(('txix', 1, bytearray(8 + pretex.TXIX_ENTRY_SIZE * n)))
    for db in dbs:
        for (typ, rid, data) in db.resources:
            if typ == 'tclt' and rid == 10008:
                pre.resources.append(('pal4', 1, pretex.rgb444_table(data)))
    prefs = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'bikd-prefs-3.bin')
    if os.path.exists(prefs):
        pre.resources.append(('pref', 3, open(prefs, 'rb').read()))   # the game's settings on first boot
    pre.sources = sources
    print(f'precomputed {n} textures')
    return pre

def fixup_txix(out, addrs, dbs, pre_index):
    pre = dbs[pre_index]
    entries = []
    for j, (i, k, img) in enumerate(pre.sources):
        e = {'tex_addr': addrs[(i, k)], 'w': img['w'], 'h': img['h']}
        for m, name in enumerate(('gry1', 'grm1', 'gry2', 'grm2', 'gry3', 'grm3')):
            e[name] = addrs[(pre_index, j * 6 + m)]
        e['avg1'] = pretex.average(img['gry1']); e['avg2'] = pretex.average(img['gry2']); e['avg3'] = pretex.average(img['gry3'])
        entries.append(e)
    blob = pretex.txix_pack(entries)
    a = addrs[(pre_index, pre.txix_index)] - ROM_BASE
    out[a:a + len(blob)] = blob

ROM_BASE = 0

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--base', type=lambda s: int(s, 0), required=True)
    ap.add_argument('-o', required=True)
    ap.add_argument('--patches', help='directory of armc<id>.s patch specs for the first prc')
    ap.add_argument('--pretex', help='precompute grey textures into a Precomputed database (value: cache dir)')
    ap.add_argument('sources', nargs='+')
    a = ap.parse_args()
    global ROM_BASE
    ROM_BASE = a.base
    dbs = []
    names = set()
    for s in a.sources:
        kind, path = s.split(':', 1)
        if kind == 'prc': db = parse_prc(path)
        elif kind == 'pkdir': db = parse_pkdir(path)
        elif kind == 'prcdir':
            for f in sorted(os.listdir(path)):
                if f.lower().endswith(('.prc', '.pdb')):
                    db = parse_prc(os.path.join(path, f))
                    if db is None: continue
                    if db.name in names: print(f'skipping duplicate database {db.name!r} ({f})'); continue
                    names.add(db.name); dbs.append(db)
            continue
        else: sys.exit(f'unknown source {s}')
        if db.name in names: print(f'skipping duplicate database {db.name!r}'); continue
        names.add(db.name); dbs.append(db)
    if a.patches and dbs:
        patch_engine(dbs[0], a.patches)
    pre_index = None
    if a.pretex:
        dbs.append(precomputed_db(dbs, a.pretex)); pre_index = len(dbs) - 1
    img, addrs = build(dbs, a.base)
    if pre_index is not None:
        fixup_txix(img, addrs, dbs, pre_index)
    open(a.o, 'wb').write(img)
    print(f'{a.o}: {len(dbs)} databases, {len(img)} bytes ({len(img)/1048576:.2f} MB)')

if __name__ == '__main__':
    main()
