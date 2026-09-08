"""How many *reachable* 68k instructions does the recompiler translate?"""
import sys, collections
sys.path.insert(0, 'tools')
import capstone
from recomp68k import Gen, xlate, Unsupported, CS
from dis68k import Seg, walk

fails = collections.Counter(); ex = {}
tot = 0
for n in (1,2,3):
    data = open(f'build/res/code_{n}.bin','rb').read()
    seg = Seg(n, data)
    entries = [0] if n == 1 else []
    for k in range(0, len(data)-1, 2):
        if data[k] == 0x4E and data[k+1] == 0x56: entries.append(k)
    seen = walk(seg, entries)
    g = Gen(n, data); g.lo = 0; g.hi = len(data)
    ok = 0; bad = 0
    for a in sorted(seen):
        i = g.decode(a)
        if i is None: continue
        tot += 1
        try:
            xlate(g, i); ok += 1
        except Exception as e:
            bad += 1
            key = i.mnemonic
            fails[key] += 1
            ex.setdefault(key, (hex(a), i.op_str, repr(e)))
    print(f"code_{n}: reachable={len(seen)} translated={ok} failed={bad}")
print(f"TOTAL reachable {tot}, failures {sum(fails.values())}")
for k,v in fails.most_common(20):
    print(f"   {k:<12} {v:5d}  e.g. {ex[k]}")
