#!/usr/bin/env python3
"""Annotated disassembly of the armc blobs: labels at function entries and
branch targets, cross-references, and syscall stub names.
    disarm2.py <n>  -> build/dis/armc<n>.txt"""
import sys, re
from capstone import *
sys.path.insert(0, 'tools')
CS = Cs(CS_ARCH_ARM, CS_MODE_ARM | CS_MODE_LITTLE_ENDIAN)
CS.detail = True

def names_from_armsyscall():
    src = open('vendor/PumpkinOS/src/libpumpkin/emulation/armsyscall.c').read()
    names = {}
    for m in re.finditer(r'case (0x[0-9A-Fa-f]+): \{?', src):
        off = int(m.group(1), 16)
        n = re.search(r'//\s*[^\n]*?([A-Za-z0-9_]+)\s*\(', src[m.start():m.start()+400])
        names.setdefault(off, n.group(1) if n else '?')
    names.update({0xB78: 'WinGetDisplayWindow?', 0xC70: 'BmpGetDensity?', 0xC80: 'BmpSetDensity?', 0x434: 'FtrGet', 0x59C: 'MemPtrSize'})
    return names

def main():
    n = int(sys.argv[1])
    d = open(f'build/res/armc_{n}.bin', 'rb').read()
    names = names_from_armsyscall()
    ins = {}
    for a in range(0, len(d) - 3, 4):
        r = list(CS.disasm(d[a:a+4], a, count=1))
        ins[a] = r[0] if r else None
    # syscall stubs
    stubs = {}
    for a, i in ins.items():
        if i and i.mnemonic == 'ldr' and re.search(r'\[sb, #-(0x[0-9a-f]+|\d+)\]', i.op_str):
            j = ins.get(a + 4)
            m = re.search(r'#(0x[0-9a-f]+|\d+)\]', j.op_str) if j and j.mnemonic == 'ldr' else None
            if m:
                g = 4 - int(re.search(r'#-(0x[0-9a-f]+|\d+)', i.op_str).group(1), 0) // 4
                f = int(m.group(1), 0)
                stubs[a] = f'sys_{names.get(f, "?")}_{f:x}' if g == 2 else f'sys_g{g}_{f:x}'
    # branch targets and function entries
    targets, calls, funcs = {}, {}, set()
    for a, i in ins.items():
        if not i: continue
        if i.mnemonic.startswith('b') and i.op_str.startswith('#'):
            t = int(i.op_str[1:], 16)
            if i.mnemonic.startswith('bl'):
                funcs.add(t); calls.setdefault(t, []).append(a)
            else:
                targets.setdefault(t, []).append(a)
        if i.mnemonic == 'push' and 'lr' in i.op_str: funcs.add(a)
        if i.mnemonic == 'mov' and i.op_str == 'ip, sp': funcs.add(a)
    out = open(f'build/dis/armc{n}.txt', 'w')
    for a in range(0, len(d) - 3, 4):
        i = ins[a]
        if a in stubs: out.write(f'\n{stubs[a]}:\n')
        elif a in funcs: out.write(f'\nsub_{a:x}:  ; called from {", ".join(f"{c:x}" for c in calls.get(a, [])[:8])}\n')
        elif a in targets: out.write(f'loc_{a:x}:  ; from {", ".join(f"{c:x}" for c in targets[a][:6])}\n')
        if i:
            op = i.op_str
            m = re.match(r'#(0x[0-9a-f]+)$', op)
            if i.mnemonic.startswith('b') and m:
                t = int(m.group(1), 16)
                op = stubs.get(t) or (f'sub_{t:x}' if t in funcs else f'loc_{t:x}')
            if i.mnemonic == 'ldr' and '[pc, #' in i.op_str:
                mm = re.search(r'\[pc, #(-?0x[0-9a-f]+|-?\d+)\]', i.op_str)
                if mm:
                    la = (a + 8 + int(mm.group(1), 0)) & ~3
                    if 0 <= la < len(d) - 3:
                        op += f'   ; =0x{int.from_bytes(d[la:la+4], "little"):08x}'
            out.write(f'  {a:05x}  {i.mnemonic:8s} {op}\n')
        else:
            out.write(f'  {a:05x}  .word    0x{int.from_bytes(d[a:a+4], "little"):08x}\n')
    out.close()
    print(f'armc{n}: {len(funcs)} functions, {len(stubs)} syscall stubs')

if __name__ == '__main__':
    main()
