#!/usr/bin/env python3
"""Move ranges of an `armc` resource's code into IWRAM.

The engine executes from cartridge ROM, where every data read from ROM
(textures, level data) breaks the prefetch and turns the next instruction
fetch into a slow one. Copying its hottest functions to IWRAM makes them run
at full speed. The code is position independent except for branches and
pc-relative address arithmetic, which this rewrites:

  * branches to code that also moved: re-encoded for the new distance
  * branches out to ROM: redirected through a trampoline in the block
    (`ldr pc, [pc, #-4]; .word address`, the word relocated at load time)
  * `add/sub rX, pc, #imm` naming ROM: replaced by a literal load
  * literal loads must find their pool inside the range (else an error)

Every original entry point (the `sub_` labels of the disassembly, plus every
address branched to from outside the range) gets a branch in ROM to a
trampoline that jumps into the block, so callers need no changes.
"""
import os, re, struct, subprocess, sys, tempfile

COND_ALWAYS = 0xE

def disassemble(data, prefix):
    with tempfile.TemporaryDirectory() as td:
        p = os.path.join(td, 'x.bin'); open(p, 'wb').write(data)
        out = subprocess.run([prefix + 'objdump', '-D', '-b', 'binary', '-m', 'arm7tdmi', p], capture_output=True, text=True).stdout
    ins = {}
    for line in out.split('\n'):
        m = re.match(r'\s*([0-9a-f]+):\t([0-9a-f]{8}) \t(\S+)\s*(.*)', line)
        if m:
            ins[int(m.group(1), 16)] = (int(m.group(2), 16), m.group(3), m.group(4).split('\t')[0].strip())
    return ins

def branch_target(addr, word):
    off = word & 0xFFFFFF
    if off & 0x800000: off -= 0x1000000
    return addr + 8 + off * 4

def is_branch(word):
    return (word & 0x0E000000) == 0x0A000000 and (word >> 28) != 0xF

def encode_branch(word, addr, target):
    off = (target - (addr + 8)) >> 2
    if off < -0x800000 or off >= 0x800000: sys.exit(f'hotmove: branch at 0x{addr:x} to 0x{target:x} out of range')
    return (word & 0xFF000000) | (off & 0xFFFFFF)

def pc_literal(word):
    """ldr rX, [pc, #imm] -> (rd, signed imm) or None"""
    if (word & 0x0F7F0000) == 0x051F0000:      # ldr/ldrb rd, [pc, #+-imm], no writeback
        imm = word & 0xFFF
        return (word >> 12) & 15, imm if word & 0x00800000 else -imm
    return None

def pc_arith(word):
    """add/sub rd, pc, #imm -> (rd, signed offset, cond) or None"""
    op = word & 0x0FEF0000
    if op in (0x028F0000, 0x024F0000) and (word >> 28) != 0xF:   # add / sub (immediate), rn = pc, no S
        rot = (word >> 8) & 15; imm = word & 0xFF
        val = ((imm >> (2 * rot)) | (imm << (32 - 2 * rot))) & 0xFFFFFFFF if rot else imm
        return (word >> 12) & 15, (val if op == 0x028F0000 else -val), (word >> 28)
    return None

def arith_imm(word):
    """add/sub rd, rn, #imm -> (rd, rn, signed imm, cond) or None"""
    op = word & 0x0FE00000
    if op in (0x02800000, 0x02400000) and (word >> 28) != 0xF:
        rot = (word >> 8) & 15; imm = word & 0xFF
        val = ((imm >> (2 * rot)) | (imm << (32 - 2 * rot))) & 0xFFFFFFFF if rot else imm
        return (word >> 12) & 15, (word >> 16) & 15, (val if op == 0x02800000 else -val), (word >> 28)
    return None

def pc_chain(ins, a):
    """An address computed from pc: `add/sub rd, pc, #a` optionally followed by
    `add/sub rd, rd, #b` (the compiler splits offsets that do not fit one
    immediate). Returns (rd, target, cond, words) or None."""
    w = ins.get(a, (0,))[0]
    pa = pc_arith(w)
    if not pa: return None
    rd, off, cond = pa
    target = a + 8 + off
    n = 1
    w2 = ins.get(a + 4, (0,))[0]
    ai = arith_imm(w2)
    if ai and ai[0] == rd and ai[1] == rd and ai[3] == cond:
        target += ai[2]; n = 2
        w3 = ins.get(a + 8, (0,))[0]
        ai3 = arith_imm(w3)
        if ai3 and ai3[0] == rd and ai3[1] == rd: sys.exit(f'hotmove: three-instruction address at 0x{a:x}')
    return rd, target, cond, n

def move(rom, ranges, iw_at, entries, prefix='arm-none-eabi-'):
    """rom: patched resource bytes. ranges: [(start, end)]. iw_at: IWRAM address
    for the moved code. entries: extra entry addresses (function starts).
    Returns (iwram_bytes, relocs, rom_patches, rom_tramp_bytes_fn) where
    rom_patches is {addr: word} and rom_tramp_bytes_fn(append_addr) gives the
    bytes to append to the resource."""
    ins = disassemble(rom, prefix)
    # a range must end after a return or an unconditional branch: code inside
    # it must never fall through into whatever follows the copy in IWRAM
    for (s, e) in ranges:
        w, mn, ops = ins.get(e - 4, (0, '', ''))
        if not (is_branch(w) and (w >> 28) == COND_ALWAYS) and not (mn == 'bx' and ops == 'lr') and not ('pc' in ops and mn in ('ldmdb', 'ldmfd', 'pop', 'ldr', 'mov')):
            sys.exit(f'hotmove: range 0x{s:x}-0x{e:x} does not end after a return ({mn} {ops} at 0x{e - 4:x})')
    moved = []            # (rom_start, rom_end, iw_start)
    cur = iw_at
    # pre-scan: literal slots needed per range (pc arithmetic to outside)
    def in_ranges(a): return any(s <= a < e for s, e in ranges)
    layout = []
    for (s, e) in ranges:
        need = 0
        a = s
        while a < e:
            ch = pc_chain(ins, a)
            if ch:
                if ch[3] == 2 or not (s <= ch[1] < e): need += 1
                a += 4 * ch[3]
            else:
                a += 4
        layout.append((s, e, cur, need))
        cur += (e - s) + need * 4
    def to_iw(a):
        for s, e, iw, _ in layout:
            if s <= a < e: return iw + (a - s)
        return None
    out = bytearray()
    relocs = []
    tramps = {}          # rom target -> iw address of trampoline
    tramp_words = []     # (iw addr, target rom offset)
    literal_refs = set()
    # collect literal pool words so they are not mistaken for code
    for (s, e) in ranges:
        for a in range(s, e, 4):
            w = ins.get(a, (0,))[0]
            pl = pc_literal(w)
            if pl:
                lit = a + 8 + pl[1]
                if not (s <= lit < e): sys.exit(f'hotmove: literal at 0x{a:x} -> 0x{lit:x} outside the moved range 0x{s:x}-0x{e:x}')
                literal_refs.add(lit)
    tramp_base = cur
    def tramp_for(target):
        nonlocal cur
        if target not in tramps:
            tramps[target] = cur
            tramp_words.append((cur, target))
            cur += 8
        return tramps[target]
    pools = {}
    for (s, e, iw, need) in layout:
        code = bytearray(rom[s:e])
        pool_at = iw + (e - s)
        pool = []
        a = s
        while a < e:
            i = a - s
            if a in literal_refs: a += 4; continue
            w = ins.get(a, (0,))[0]
            if is_branch(w):
                t = branch_target(a, w)
                nt = to_iw(t)
                if nt is None: nt = tramp_for(t)
                struct.pack_into('<I', code, i, encode_branch(w, iw + i, nt))
                a += 4
                continue
            ch = pc_chain(ins, a)
            if ch:
                rd, t, cond, n = ch
                if n == 2 or not (s <= t < e):
                    # ldr rd, [pc, #k] with the literal in this range's pool; a second word becomes a nop
                    slot = pool_at + 4 * len(pool)
                    nt = to_iw(t)
                    pool.append((slot, t, nt))
                    k = slot - (iw + i + 8)
                    if k < 0 or k > 4095: sys.exit(f'hotmove: pool too far at 0x{a:x}')
                    struct.pack_into('<I', code, i, (cond << 28) | 0x059F0000 | (rd << 12) | k)
                    if n == 2: struct.pack_into('<I', code, i + 4, (cond << 28) | 0x01A00000)
                a += 4 * n
                continue
            if (w & 0x0C0F0000) == 0x040F0000 and (w >> 28) != 0xF and pc_literal(w) is None:
                sys.exit(f'hotmove: unsupported pc-relative access at 0x{a:x}: {ins[a][1]} {ins[a][2]}')
            a += 4
        out += code
        for (slot, t, nt) in pool:
            if nt is not None: out += struct.pack('<I', nt)
            else: relocs.append(len(out)); out += struct.pack('<I', t)
        while len(out) % 4: out.append(0)
        pools[s] = pool
    assert iw_at + len(out) == tramp_base, (hex(iw_at + len(out)), hex(tramp_base))
    for (ta, target) in tramp_words:
        out += struct.pack('<I', 0xE51FF004)
        relocs.append(len(out)); out += struct.pack('<I', target)
    # ROM side: every entry becomes a branch to a ROM trampoline into the block
    entry_set = set(entries)
    for a, (w, mn, ops) in ins.items():
        if is_branch(w):
            t = branch_target(a, w)
            if in_ranges(t) and not in_ranges(a): entry_set.add(t)
        ch = pc_chain(ins, a)
        if ch and not in_ranges(a) and in_ranges(ch[1]): entry_set.add(ch[1])
    for (s, e) in ranges: entry_set.add(s)
    entry_list = sorted(x for x in entry_set if in_ranges(x))
    rom_patches = {}
    def rom_tramps(append_addr):
        blob = bytearray()
        for k, a in enumerate(entry_list):
            ta = append_addr + 8 * k
            rom_patches[a] = encode_branch(0xEA000000, a, ta)
            blob += struct.pack('<II', 0xE51FF004, to_iw(a))
        return bytes(blob)
    return bytes(out), relocs, rom_patches, rom_tramps, entry_list, len(tramp_words)
