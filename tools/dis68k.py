"""Recursive-descent 68k code discovery over the Bike or Die code segments."""
import struct, sys, collections
from capstone import *
from capstone.m68k import *

CS = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)
CS.detail = True

UNCOND = {'bra.b','bra.w','bra.l','jmp','rts','rte','rtr','rtd','trap','illegal','bkpt','stop','reset'}
CALLS  = {'bsr.b','bsr.w','bsr.l','jsr'}
def is_cond_branch(m):
    return m[0]=='b' and m not in ('bra.b','bra.w','bra.l','bsr.b','bsr.w','bsr.l','bkpt','bchg','bclr','bset','btst','bfextu','bfins','bftst','bfchg','bfclr','bfset','bfexts','bfffo')

class Seg:
    def __init__(self, n, data):
        self.n=n; self.d=data; self.insn={}; self.bad=set()

def decode_at(seg, a):
    if a in seg.insn: return seg.insn[a]
    if a<0 or a+2>len(seg.d): return None
    for i in CS.disasm(seg.d[a:min(a+16,len(seg.d))], a, count=1):
        seg.insn[a]=i
        return i
    seg.bad.add(a)
    return None

def branch_target(i):
    # first operand is an immediate absolute address for b*/bsr/jmp/jsr pc-rel
    for op in i.operands:
        if op.type == M68K_OP_IMM: return op.imm
        if op.type == M68K_OP_BR_DISP: return i.address + 2 + op.br_disp.disp
    return None

def walk(seg, entries, verbose=False):
    todo=list(entries); seen=set()
    while todo:
        a=todo.pop()
        while True:
            if a in seen: break
            if a<0 or a>=len(seg.d): break
            i=decode_at(seg,a)
            if i is None: break
            seen.add(a)
            m=i.mnemonic
            nxt=a+i.size
            if m in CALLS:
                t=branch_target(i)
                if t is not None and 0<=t<len(seg.d): todo.append(t)
                a=nxt; continue
            if is_cond_branch(m) or m.startswith('db'):
                t=branch_target(i)
                if t is not None and 0<=t<len(seg.d): todo.append(t)
                a=nxt; continue
            if m in ('bra.b','bra.w','bra.l'):
                t=branch_target(i)
                if t is not None and 0<=t<len(seg.d): a=t; continue
                break
            if m in ('rts','rte','rtr','rtd','jmp','illegal','stop','reset'):
                if m=='jmp':
                    t=branch_target(i)
                    if t is not None and 0<=t<len(seg.d): a=t; continue
                break
            if m=='trap':
                a=nxt+2   # PalmOS: trap #15 is followed by a 16-bit trap selector word
                continue
            a=nxt
    return seen

if __name__=='__main__':
    total_seen=0; total_bytes=0
    mnem=collections.Counter()
    for n in (1,2,3):
        d=open(f'build/res/code_{n}.bin','rb').read()
        seg=Seg(n,d)
        entries=[0] if n==1 else []
        # every segment: also seed with prologue pattern 'link.w a6,#x' (4e56)
        for k in range(0,len(d)-1,2):
            if d[k]==0x4e and d[k+1]==0x56: entries.append(k)
        seen=walk(seg,entries)
        cov=sum(seg.insn[a].size for a in seen if a in seg.insn)
        for a in seen:
            if a in seg.insn: mnem[seg.insn[a].mnemonic]+=1
        print(f"code_{n}: {len(d)} bytes, {len(seen)} insns discovered, {cov} bytes covered ({100.0*cov/len(d):.1f}%), {len(seg.bad)} undecodable addrs")
        total_seen+=len(seen); total_bytes+=len(d)
    print(f"TOTAL: {total_seen} instructions over {total_bytes} bytes")
    print(f"distinct mnemonics: {len(mnem)}")
    print(' '.join(f'{k}:{v}' for k,v in mnem.most_common()))
