"""Recursive-descent discovery over the ARM32 armc resources."""
import struct, sys, collections
from capstone import *
from capstone.arm import *

CS = Cs(CS_ARCH_ARM, CS_MODE_ARM | CS_MODE_LITTLE_ENDIAN)
CS.detail = True

def read_table(d):
    """Leading u32 table of function entry offsets; ends at first implausible value."""
    t=[]
    for i in range(0, min(len(d), 4096), 4):
        v, = struct.unpack_from('<I', d, i)
        if v >= len(d) or v < 4: break
        t.append(v)
    return t

def writes_pc(i):
    for op in i.operands:
        if op.type == ARM_OP_REG and op.access & CS_AC_WRITE and op.reg == ARM_REG_PC:
            return True
    if i.mnemonic.startswith(('pop','ldm')) and ' pc' in i.op_str: return True
    return False

def walk(d, entries):
    seen=set(); insns={}; todo=list(entries); bad=set(); indirect=[]
    while todo:
        a=todo.pop()
        while True:
            if a in seen or a<0 or a+4>len(d): break
            got=None
            for i in CS.disasm(d[a:a+4], a, count=1): got=i
            if got is None:
                bad.add(a); break
            seen.add(a); insns[a]=got
            m=got.mnemonic; nxt=a+4
            cond = got.cc not in (ARM_CC_AL, ARM_CC_INVALID) if hasattr(got,'cc') else False
            if m.startswith('bl') and not m.startswith('bls') and not m.startswith('blt') and not m.startswith('ble'):
                # bl / blx
                for op in got.operands:
                    if op.type==ARM_OP_IMM and 0<=op.imm<len(d): todo.append(op.imm)
                a=nxt; continue
            if m.startswith('b') and not m.startswith('bic') and not m.startswith('bfi') and not m.startswith('bkpt'):
                tgt=None
                for op in got.operands:
                    if op.type==ARM_OP_IMM: tgt=op.imm
                if m.startswith('bx'):
                    indirect.append(a)
                    if cond: a=nxt; continue
                    break
                if tgt is not None and 0<=tgt<len(d): todo.append(tgt)
                if cond: a=nxt; continue
                break
            if writes_pc(got):
                indirect.append(a)
                if cond: a=nxt; continue
                break
            a=nxt
    return seen, insns, bad, indirect

if __name__=='__main__':
    tot=0; totb=0; mn=collections.Counter(); ind=0
    for n in (1,2,3,4):
        d=open(f'build/res/armc_{n}.bin','rb').read()
        t=read_table(d)
        seen,insns,bad,indirect=walk(d,t)
        cov=len(seen)*4
        for a in seen: mn[insns[a].mnemonic]+=1
        print(f"armc_{n}: {len(d)} bytes, table={len(t)} entries, {len(seen)} insns, {cov} bytes ({100.0*cov/len(d):.1f}%), bad={len(bad)}, indirect-pc sites={len(indirect)}")
        tot+=len(seen); totb+=len(d); ind+=len(indirect)
    print(f"TOTAL: {tot} ARM instructions over {totb} bytes, {ind} indirect-PC sites")
    print(f"distinct mnemonics: {len(mn)}")
    print(' '.join(f'{k}:{v}' for k,v in mn.most_common(70)))
