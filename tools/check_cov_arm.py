"""How many *reachable* ARM instructions does the recompiler translate?"""
import sys, collections
sys.path.insert(0,'tools')
from recomparm import Gen, xlate, Unsupported
from disarm import read_table, walk
fails=collections.Counter(); ex={}; tot=0
for n in (1,2,3,4):
    d=open(f'build/res/armc_{n}.bin','rb').read()
    t=read_table(d)
    seen,insns,bad,ind=walk(d,t)
    g=Gen(n,d); g.lo=0; g.hi=len(d)
    ok=b=0
    for a in sorted(seen):
        i=g.decode(a)
        if i is None: continue
        tot+=1
        try: xlate(g,i); ok+=1
        except Exception as e:
            b+=1; fails[i.mnemonic]+=1; ex.setdefault(i.mnemonic,(hex(a),i.op_str,repr(e)))
    print(f"armc_{n}: reachable={len(seen)} translated={ok} failed={b}")
print(f"TOTAL reachable {tot}, failures {sum(fails.values())}")
for k,v in fails.most_common(15): print(f"   {k:<12} {v:4d}  e.g. {ex[k]}")
