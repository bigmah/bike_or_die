#!/usr/bin/env python3
"""Binary-patch an `armc` code resource from an assembly spec.

The spec is GNU assembler syntax split into blocks by `@@` lines:

    .equ loc_1888, 0x1888          preamble: anything before the first block
    @@ at 0x1988 len 4             replace the bytes at this offset (must fit)
        b p_edge
    @@ append                      appended after the original code
    p_edge: ...

Everything is assembled as one image whose address 0 is the start of the
resource, so branches between patches and original code resolve to the right
offsets, and the code stays position independent (the engine is loaded
wherever the resource lands). Only the bytes covered by blocks are copied
back over the original.
"""
import os, re, struct, subprocess, sys, tempfile
import hotmove

def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"{' '.join(cmd)}\n{r.stdout}{r.stderr}")
    return r.stdout

def apply(orig, spec_text, prefix='arm-none-eabi-', name='patch'):
    pre, blocks = [], []
    cur = None
    for line in spec_text.split('\n'):
        mv = re.match(r'\s*@@\s+move\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s*$', line)
        if mv:
            blocks.append({'kind': 'move', 'addr': int(mv.group(1), 16), 'end': int(mv.group(2), 16), 'lines': []})
            cur = None
            continue
        m = re.match(r'\s*@@\s+(at|append|iwram)(?:\s+(0x[0-9a-fA-F]+|\d+))?(?:\s+len\s+(0x[0-9a-fA-F]+|\d+))?\s*$', line)
        if m:
            kind = m.group(1)
            addr = int(m.group(2), 0) if m.group(2) else None
            ln = int(m.group(3), 0) if m.group(3) else None
            cur = {'kind': kind, 'addr': addr, 'len': ln, 'lines': []}
            blocks.append(cur)
        elif cur is None:
            pre.append(line)
        else:
            cur['lines'].append(line)
    append_at = (len(orig) + 3) & ~3
    inplace = sorted([b for b in blocks if b['kind'] == 'at'], key=lambda b: b['addr'])
    appends = [b for b in blocks if b['kind'] == 'append']
    iwram = [b for b in blocks if b['kind'] == 'iwram']
    iw_syms, iw_img, iw_relocs = {}, b'', []
    if iwram:
        # Hot code the runtime copies into IWRAM at a fixed address (the
        # `@@ iwram 0x030xxxxx` line). Words labelled rel_* hold offsets into
        # the resource; the runtime adds the resource's address to them.
        iw_at = iwram[0]['addr']
        asm = ['.arm', '.text', '.syntax unified'] + pre + ['__iw_s:']
        for b in iwram: asm += b['lines']
        asm += ['.ltorg', '__iw_e:']
        iw_img, iw_syms = assemble(asm, iw_at, prefix, name + '_iwram')
        iw_img = iw_img[iw_syms['__iw_s'] - iw_at:iw_syms['__iw_e'] - iw_at]
        iw_relocs = sorted(v - iw_at for k, v in iw_syms.items() if k.startswith('rel_'))
        pre = pre + [f'.equ {k}, 0x{v:x}' for k, v in iw_syms.items() if not k.startswith('__') and not k.startswith('rel_')]
    asm = ['.arm', '.text', '.syntax unified'] + pre
    for i, b in enumerate(inplace):
        asm += [f'.org 0x{b["addr"]:x}', f'__blk{i}_s:'] + b['lines'] + [f'__blk{i}_e:']
    asm += [f'.org 0x{append_at:x}', '__app_s:']
    for b in appends:
        asm += b['lines']
    asm += ['.ltorg', '__app_e:']
    img, syms = assemble(asm, 0, prefix, name)
    out = bytearray(orig)
    for i, b in enumerate(inplace):
        s, e = syms[f'__blk{i}_s'], syms[f'__blk{i}_e']
        if b['len'] is not None and e - s > b['len']:
            sys.exit(f'{name}: block at 0x{b["addr"]:x} is {e - s} bytes, allowed {b["len"]}')
        out[s:e] = img[s:e]
    s, e = syms['__app_s'], syms['__app_e']
    if e > s:
        out += b'\0' * (append_at - len(out))
        out += img[s:e]
    moves = [(b['addr'], b['end']) for b in blocks if b['kind'] == 'move']
    if moves:
        if not iwram: sys.exit('hotmove needs an @@ iwram block to set the address')
        iw_at = iwram[0]['addr']
        while len(iw_img) % 4: iw_img += b'\0'
        entries = []
        dis = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'build', 'dis', name + '.txt')
        if os.path.exists(dis):
            entries = [int(m.group(1), 16) for m in re.finditer(r'^sub_([0-9a-f]+):', open(dis).read(), re.M)]
        mv_bytes, mv_relocs, rom_patches, rom_tramps, entry_list, ntramp = hotmove.move(bytes(out), moves, iw_at + len(iw_img), entries, prefix)
        while len(out) % 4: out.append(0)
        tramp_blob = rom_tramps(len(out))
        out += tramp_blob
        for a, w in rom_patches.items(): struct.pack_into('<I', out, a, w)
        iw_relocs = list(iw_relocs) + [len(iw_img) + r for r in mv_relocs]
        iw_img = iw_img + mv_bytes
        print(f'{name}: moved {sum(e - s for s, e in moves)} bytes of code to IWRAM ({len(entry_list)} entries, {ntramp} trampolines out, block {len(iw_img)} bytes)')
    if iwram:
        # [block][reloc offsets][footer: 'IWRM', block offset, length, count, destination]
        while len(out) % 4: out.append(0)
        blk = len(out)
        out += iw_img
        while len(out) % 4: out.append(0)
        for r in iw_relocs: out += struct.pack('<I', r)
        out += struct.pack('<4sIIII', b'IWRM', blk, len(iw_img), len(iw_relocs), iwram[0]['addr'])
    allsyms = {k: v for k, v in syms.items() if not k.startswith('__')}
    allsyms.update({k: v for k, v in iw_syms.items() if not k.startswith('__')})
    return bytes(out), allsyms

def assemble(asm, at, prefix, name):
    with tempfile.TemporaryDirectory() as td:
        s = os.path.join(td, name + '.s'); o = os.path.join(td, name + '.o')
        e = os.path.join(td, name + '.elf'); bfile = os.path.join(td, name + '.bin')
        open(s, 'w').write('\n'.join(asm) + '\n')
        run([prefix + 'as', '-mcpu=arm7tdmi', '-o', o, s])
        run([prefix + 'ld', f'-Ttext=0x{at:x}', '-o', e, o])
        run([prefix + 'objcopy', '-O', 'binary', e, bfile])
        img = open(bfile, 'rb').read()
        syms = {}
        for line in run([prefix + 'nm', e]).split('\n'):
            p = line.split()
            if len(p) == 3: syms[p[2]] = int(p[0], 16)
    return img, syms

if __name__ == '__main__':
    if len(sys.argv) < 4: sys.exit('usage: armpatch.py in.bin spec.s out.bin')
    data, syms = apply(open(sys.argv[1], 'rb').read(), open(sys.argv[2]).read())
    open(sys.argv[3], 'wb').write(data)
    print(f'{sys.argv[3]}: {len(data)} bytes; ' + ' '.join(f'{k}=0x{v:x}' for k, v in sorted(syms.items(), key=lambda x: x[1])))
