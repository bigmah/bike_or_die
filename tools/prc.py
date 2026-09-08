import struct, sys, collections

def parse(path):
    d = open(path,'rb').read()
    name = d[0:32].split(b'\0')[0].decode('latin1')
    attrs, ver = struct.unpack('>HH', d[32:36])
    typ = d[60:64].decode('latin1'); creator = d[64:68].decode('latin1')
    nrec, = struct.unpack('>H', d[76:78])
    recs=[]
    off=78
    for i in range(nrec):
        rt = d[off:off+4].decode('latin1')
        rid, = struct.unpack('>H', d[off+4:off+6])
        ro, = struct.unpack('>I', d[off+6:off+10])
        recs.append([rt,rid,ro])
        off+=10
    for i,r in enumerate(recs):
        end = recs[i+1][2] if i+1<len(recs) else len(d)
        r.append(end-r[2])
    return dict(name=name,attrs=attrs,ver=ver,type=typ,creator=creator,recs=recs,data=d)

if __name__=='__main__':
    p=parse(sys.argv[1])
    print(f"name={p['name']} type={p['type']} creator={p['creator']} attrs={p['attrs']:#x} ver={p['ver']} nres={len(p['recs'])}")
    c=collections.Counter(r[0] for r in p['recs'])
    tot=collections.Counter()
    for r in p['recs']: tot[r[0]]+=r[3]
    for t,n in c.most_common():
        print(f"  {t!r:8} count={n:4d} bytes={tot[t]:9d}")
    if len(sys.argv)>2:
        for r in p['recs']:
            if r[0]==sys.argv[2]:
                print(f"  {r[0]} id={r[1]} off={r[2]:#x} len={r[3]}")
