#!/usr/bin/env python3
"""Static recompiler: Motorola 68000 -> C, for the Bike or Die 2 'code' resources.

Every even offset in each code resource is translated, so indirect branches and
jump tables need no control-flow recovery: the generated chunk indexes a label
array by (pc - segbase)/2. Data regions translate to code that is never reached.
"""
import os, re, sys, struct
import capstone
from capstone import *
from capstone.m68k import *

CS = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)
CS.detail = True

DREG = {globals()[f'M68K_REG_D{i}']: f'd{i}' for i in range(8)}
AREG = {globals()[f'M68K_REG_A{i}']: f'a{i}' for i in range(8)}

# Instruction slots (2 bytes each) per generated C function. This sets how big
# the generated functions get, and they are already unusual: one label and one
# label-array entry per slot. A native compiler takes 3072 without complaint,
# but WebAssembly does not -- at that size clang either exceeds the 50k locals
# a function may have or, at -O1, produces a function that misbehaves -- so
# keep it small enough for both. BOD_CHUNK_SLOTS overrides it.
CHUNK_SLOTS = int(os.environ.get('BOD_CHUNK_SLOTS', '512'))

CC = {'t':'CC_T','f':'CC_F','hi':'CC_HI','ls':'CC_LS','cc':'CC_CC','hs':'CC_CC',
      'cs':'CC_CS','lo':'CC_CS','ne':'CC_NE','eq':'CC_EQ','vc':'CC_VC','vs':'CC_VS',
      'pl':'CC_PL','mi':'CC_MI','ge':'CC_GE','lt':'CC_LT','gt':'CC_GT','le':'CC_LE'}

class Unsupported(Exception): pass

def regname(r):
    if r in DREG: return DREG[r]
    if r in AREG: return AREG[r]
    raise Unsupported(f'reg {r}')

def mask(sz):   return {1:'0xFFu',2:'0xFFFFu',4:'0xFFFFFFFFu'}[sz]
def bits(sz):   return {1:8,2:16,4:32}[sz]
def rdf(sz):    return {1:'rd8',2:'rd16',4:'rd32'}[sz]
def wrf(sz):    return {1:'wr8',2:'wr16',4:'wr32'}[sz]
def sext(sz,e):
    return {1:f'SE8({e})',2:f'SE16({e})',4:f'({e})'}[sz]

ABS_RE = re.compile(r'\$([0-9a-fA-F]+)\.(w|l)')

class Ctx:
    """Emission context for one instruction."""
    def __init__(self, gen, i):
        self.gen = gen; self.i = i; self.out = []; self.tmp = 0
    def n(self, pfx='t'):
        self.tmp += 1; return f'{pfx}{self.tmp}'
    def w(self, s): self.out.append(s)

def abs_addr(i, opidx):
    """capstone does not expose the absolute address in mem.disp; take it from op_str."""
    parts = i.op_str.split(',')
    txt = parts[opidx] if opidx < len(parts) else i.op_str
    m = ABS_RE.search(txt)
    if not m: raise Unsupported('abs addr parse: ' + i.op_str)
    v = int(m.group(1), 16)
    return v & 0xFFFFFFFF

def ea_expr(c, op, opidx, sz, for_write=False):
    """Return a C expression for the operand's effective address, emitting any
    pre/post side effects. Only valid for memory operands."""
    i = c.i; am = op.address_mode; m = op.mem
    # capstone puts the base register in op.reg (not mem.base_reg) for the
    # register-indirect forms, and in mem.base_reg for the displacement forms.
    if am == M68K_AM_REGI_ADDR:
        return regname(op.reg)
    if am == M68K_AM_REGI_ADDR_DISP:
        return f'({regname(m.base_reg)} + {m.disp & 0xFFFFFFFF}u)'
    if am == M68K_AM_REGI_ADDR_POST_INC:
        b = regname(op.reg); step = sz
        if b == 'a7' and sz == 1: step = 2
        t = c.n('ea'); c.w(f'uint32_t {t} = {b}; {b} += {step};')
        return t
    if am == M68K_AM_REGI_ADDR_PRE_DEC:
        b = regname(op.reg); step = sz
        if b == 'a7' and sz == 1: step = 2
        t = c.n('ea'); c.w(f'{b} -= {step}; uint32_t {t} = {b};')
        return t
    if am in (M68K_AM_AREGI_INDEX_8_BIT_DISP, M68K_AM_AREGI_INDEX_BASE_DISP):
        b = regname(m.base_reg)
        idx = regname(m.index_reg)
        idxe = idx if m.index_size else f'SE16({idx})'
        sc = m.scale if m.scale else 1
        return f'({b} + {m.disp & 0xFFFFFFFF}u + {idxe} * {sc}u)'
    if am == M68K_AM_PCI_DISP:
        return f'{(i.address + 2 + m.disp) & 0xFFFFFFFF}u + SEGBASE'
    if am in (M68K_AM_PCI_INDEX_8_BIT_DISP, M68K_AM_PC_MEMI_POST_INDEX, M68K_AM_PC_MEMI_PRE_INDEX):
        idx = regname(m.index_reg)
        idxe = idx if m.index_size else f'SE16({idx})'
        sc = m.scale if m.scale else 1
        return f'({(i.address + 2 + m.disp) & 0xFFFFFFFF}u + SEGBASE + {idxe} * {sc}u)'
    if am in (M68K_AM_ABSOLUTE_DATA_SHORT, M68K_AM_ABSOLUTE_DATA_LONG):
        return f'{abs_addr(i, opidx)}u'
    raise Unsupported(f'ea am={am}')

def rd_op(c, op, opidx, sz):
    """C expression for the operand's value (zero-extended into a uint32_t)."""
    am = op.address_mode
    if am == M68K_AM_REG_DIRECT_DATA:
        r = regname(op.reg)
        return r if sz == 4 else f'({r} & {mask(sz)})'
    if am == M68K_AM_REG_DIRECT_ADDR:
        r = regname(op.reg)
        return r if sz == 4 else f'({r} & {mask(sz)})'
    if am == M68K_AM_IMMEDIATE:
        return f'{op.imm & 0xFFFFFFFF}u'
    ea = ea_expr(c, op, opidx, sz)
    return f'{rdf(sz)}({ea})'

def wr_op(c, op, opidx, sz, val):
    am = op.address_mode
    if am == M68K_AM_REG_DIRECT_DATA:
        r = regname(op.reg)
        if sz == 4: return f'{r} = {val};'
        return f'{r} = ({r} & ~{mask(sz)}) | (({val}) & {mask(sz)});'
    if am == M68K_AM_REG_DIRECT_ADDR:
        r = regname(op.reg)
        return f'{r} = {val};'      # address regs always written full width
    ea = ea_expr(c, op, opidx, sz, for_write=True)
    return f'{wrf(sz)}({ea}, {val});'

def rmw(c, op, opidx, sz):
    """Read-modify-write: returns (read_expr, writer(valexpr)) computing the EA once."""
    am = op.address_mode
    if am in (M68K_AM_REG_DIRECT_DATA, M68K_AM_REG_DIRECT_ADDR):
        r = regname(op.reg)
        rd = r if sz == 4 else f'({r} & {mask(sz)})'
        def wf(v, r=r, sz=sz):
            if sz == 4 or am == M68K_AM_REG_DIRECT_ADDR: return f'{r} = {v};'
            return f'{r} = ({r} & ~{mask(sz)}) | (({v}) & {mask(sz)});'
        return rd, wf
    ea = ea_expr(c, op, opidx, sz)
    t = c.n('ad'); c.w(f'uint32_t {t} = {ea};')
    return f'{rdf(sz)}({t})', (lambda v, t=t, sz=sz: f'{wrf(sz)}({t}, {v});')

# ---------------------------------------------------------------- translation

def split_mnem(m):
    """'add.l' -> ('add', 4); 'bra.w' -> ('bra', 2); 'swap' -> ('swap', None)"""
    if '.' in m:
        base, suf = m.rsplit('.', 1)
        return base, {'b':1,'w':2,'l':4,'s':1}.get(suf)
    return m, None

def br_target(i):
    for op in i.operands:
        if op.type == M68K_OP_BR_DISP:
            return (i.address + 2 + op.br_disp.disp) & 0xFFFFFFFF
        if op.type == M68K_OP_IMM:
            return op.imm & 0xFFFFFFFF
    raise Unsupported('no branch target')

def xlate(gen, i):
    """Translate one instruction. Returns (list_of_c_lines, falls_through)."""
    c = Ctx(gen, i)
    m = i.mnemonic
    base, suf = split_mnem(m)
    sz = i.op_size.size if i.op_size.size in (1,2,4) else (suf or 0)
    ops = i.operands
    nxt = (i.address + i.size) & 0xFFFFFFFF
    B = bits(sz) if sz in (1,2,4) else 32

    def GOTO(t):
        return gen.goto(t)

    # ---- data movement
    if base in ('move','movea'):
        s = rd_op(c, ops[0], 0, sz)
        t = c.n(); c.w(f'uint32_t {t} = {s};')
        if base == 'movea':
            c.w(wr_op(c, ops[1], 1, 4, sext(sz, t)))
        else:
            c.w(wr_op(c, ops[1], 1, sz, t))
            c.w(f'LOGIC{B}({t});')
        return c.out, True

    if base == 'moveq':
        v = ops[0].imm & 0xFF
        t = c.n(); c.w(f'uint32_t {t} = SE8({v}u);')
        c.w(wr_op(c, ops[1], 1, 4, t)); c.w(f'LOGIC32({t});')
        return c.out, True

    if base == 'lea':
        ea = ea_expr(c, ops[0], 0, 4)
        c.w(wr_op(c, ops[1], 1, 4, ea)); return c.out, True

    if base == 'pea':
        ea = ea_expr(c, ops[0], 0, 4)
        t = c.n(); c.w(f'uint32_t {t} = {ea};')
        c.w(f'a7 -= 4; wr32(a7, {t});'); return c.out, True

    if base == 'link':
        r = regname(ops[0].reg); d = ops[1].imm
        d = SE(d, 2) if suf != 'l' else d
        c.w(f'a7 -= 4; wr32(a7, {r}); {r} = a7; a7 = (a7 + {d & 0xFFFFFFFF}u);')
        return c.out, True

    if base == 'unlk':
        r = regname(ops[0].reg)
        c.w(f'a7 = {r}; {r} = rd32(a7); a7 += 4;'); return c.out, True

    if base == 'movem':
        return xlate_movem(c, i, sz), True

    if base == 'swap':
        r = regname(ops[0].reg)
        c.w(f'{r} = ({r} >> 16) | ({r} << 16); LOGIC32({r});'); return c.out, True

    if base == 'ext':
        r = regname(ops[0].reg)
        if sz == 4: c.w(f'{r} = SE16({r}); LOGIC32({r});')
        else:       c.w(f'{r} = ({r} & 0xFFFF0000u) | (SE8({r}) & 0xFFFFu); LOGIC16({r});')
        return c.out, True

    # ---- arithmetic / logic
    if base in ('add','addi','addq','adda'):
        s = rd_op(c, ops[0], 0, 4 if base == 'adda' else sz)
        if base == 'adda':
            src = sext(sz, s) if sz == 2 else s
            if sz == 2: src = f'SE16({rd_op(c, ops[0], 0, 2)})'
            d = regname(ops[1].reg); c.w(f'{d} = {d} + {src};'); return c.out, True
        if ops[1].address_mode == M68K_AM_REG_DIRECT_ADDR:      # addq #n,An
            d = regname(ops[1].reg); c.w(f'{d} = {d} + {s};'); return c.out, True
        rdx, wf = rmw(c, ops[1], 1, sz)
        sv = c.n(); c.w(f'uint32_t {sv} = {s};')
        dv = c.n(); c.w(f'uint32_t {dv} = {rdx};')
        rv = c.n(); c.w(f'uint32_t {rv} = ({dv} + {sv}) & {mask(sz)};')
        c.w(wf(rv)); c.w(f'ADDF({B},{dv},{sv},{rv}); NZ{B}({rv});')
        return c.out, True

    if base in ('sub','subi','subq','suba'):
        if base == 'suba':
            s = rd_op(c, ops[0], 0, sz)
            src = f'SE16({s})' if sz == 2 else s
            d = regname(ops[1].reg); c.w(f'{d} = {d} - {src};'); return c.out, True
        s = rd_op(c, ops[0], 0, sz)
        if ops[1].address_mode == M68K_AM_REG_DIRECT_ADDR:
            d = regname(ops[1].reg); c.w(f'{d} = {d} - {s};'); return c.out, True
        rdx, wf = rmw(c, ops[1], 1, sz)
        sv = c.n(); c.w(f'uint32_t {sv} = {s};')
        dv = c.n(); c.w(f'uint32_t {dv} = {rdx};')
        rv = c.n(); c.w(f'uint32_t {rv} = ({dv} - {sv}) & {mask(sz)};')
        c.w(wf(rv)); c.w(f'SUBF({B},{dv},{sv},{rv}); xf = cf; NZ{B}({rv});')
        return c.out, True

    if base in ('cmp','cmpi','cmpa','cmpm'):
        if base == 'cmpa':
            s = rd_op(c, ops[0], 0, sz)
            src = f'SE16({s})' if sz == 2 else s
            d = regname(ops[1].reg)
            sv = c.n(); c.w(f'uint32_t {sv} = {src};')
            rv = c.n(); c.w(f'uint32_t {rv} = ({d} - {sv}) & 0xFFFFFFFFu;')
            c.w(f'SUBF(32,{d},{sv},{rv}); NZ32({rv});'); return c.out, True
        s = rd_op(c, ops[0], 0, sz)
        sv = c.n(); c.w(f'uint32_t {sv} = {s};')
        d = rd_op(c, ops[1], 1, sz)
        dv = c.n(); c.w(f'uint32_t {dv} = {d};')
        rv = c.n(); c.w(f'uint32_t {rv} = ({dv} - {sv}) & {mask(sz)};')
        c.w(f'SUBF({B},{dv},{sv},{rv}); NZ{B}({rv});'); return c.out, True

    if base in ('and','andi','or','ori','eor','eori'):
        opc = {'and':'&','andi':'&','or':'|','ori':'|','eor':'^','eori':'^'}[base]
        s = rd_op(c, ops[0], 0, sz)
        sv = c.n(); c.w(f'uint32_t {sv} = {s};')
        rdx, wf = rmw(c, ops[1], 1, sz)
        rv = c.n(); c.w(f'uint32_t {rv} = ({rdx} {opc} {sv}) & {mask(sz)};')
        c.w(wf(rv)); c.w(f'LOGIC{B}({rv});'); return c.out, True

    if base == 'not':
        rdx, wf = rmw(c, ops[0], 0, sz)
        rv = c.n(); c.w(f'uint32_t {rv} = (~{rdx}) & {mask(sz)};')
        c.w(wf(rv)); c.w(f'LOGIC{B}({rv});'); return c.out, True

    if base == 'neg':
        rdx, wf = rmw(c, ops[0], 0, sz)
        dv = c.n(); c.w(f'uint32_t {dv} = {rdx};')
        rv = c.n(); c.w(f'uint32_t {rv} = (0u - {dv}) & {mask(sz)};')
        c.w(wf(rv)); c.w(f'SUBF({B},0u,{dv},{rv}); xf = cf; NZ{B}({rv});')
        return c.out, True

    if base == 'clr':
        c.w(wr_op(c, ops[0], 0, sz, '0u')); c.w('nf=0; zf=1; vf=0; cf=0;')
        return c.out, True

    if base == 'tst':
        v = rd_op(c, ops[0], 0, sz)
        t = c.n(); c.w(f'uint32_t {t} = {v}; LOGIC{B}({t});'); return c.out, True

    if base in ('muls','mulu'):
        s = rd_op(c, ops[0], 0, 2)
        d = regname(ops[1].reg)
        if base == 'muls':
            c.w(f'{d} = (uint32_t)((int32_t)(int16_t)({s}) * (int32_t)(int16_t)({d} & 0xFFFFu));')
        else:
            c.w(f'{d} = (uint32_t)(({s} & 0xFFFFu) * ({d} & 0xFFFFu));')
        c.w(f'LOGIC32({d});'); return c.out, True

    if base in ('divs','divu'):
        s = rd_op(c, ops[0], 0, 2)
        d = regname(ops[1].reg)
        sv = c.n(); c.w(f'uint32_t {sv} = {s};')
        c.w(f'if (({sv} & 0xFFFFu) == 0) {{ r68k_panic(S, {i.address}u + SEGBASE, "divide by zero"); }} else {{')
        if base == 'divs':
            c.w(f'  int32_t _q = (int32_t){d} / (int32_t)(int16_t)({sv} & 0xFFFFu);')
            c.w(f'  int32_t _r = (int32_t){d} % (int32_t)(int16_t)({sv} & 0xFFFFu);')
            c.w(f'  if (_q > 32767 || _q < -32768) {{ vf = 1; }} else {{')
        else:
            c.w(f'  uint32_t _q = {d} / ({sv} & 0xFFFFu);')
            c.w(f'  uint32_t _r = {d} % ({sv} & 0xFFFFu);')
            c.w(f'  if (_q > 0xFFFFu) {{ vf = 1; }} else {{')
        c.w(f'    {d} = ((uint32_t)_r << 16) | ((uint32_t)_q & 0xFFFFu);')
        c.w(f'    nf = ((uint32_t)_q >> 15) & 1; zf = (((uint32_t)_q & 0xFFFFu) == 0); vf = 0; cf = 0;')
        c.w('  } }')
        return c.out, True

    # ---- shifts and rotates
    if base in ('asl','asr','lsl','lsr','rol','ror','roxl','roxr'):
        return xlate_shift(c, i, base, sz), True

    # ---- bit operations
    if base in ('btst','bset','bclr','bchg'):
        dst = ops[1] if len(ops) > 1 else ops[0]
        to_reg = dst.address_mode == M68K_AM_REG_DIRECT_DATA
        width = 32 if to_reg else 8
        bn = rd_op(c, ops[0], 0, 4 if ops[0].address_mode != M68K_AM_IMMEDIATE else 1)
        bnv = c.n(); c.w(f'uint32_t {bnv} = ({bn}) & {width-1}u;')
        if base == 'btst':
            v = rd_op(c, dst, 1 if len(ops) > 1 else 0, 4 if to_reg else 1)
            c.w(f'zf = (((({v}) >> {bnv}) & 1u) == 0);')
            return c.out, True
        rdx, wf = rmw(c, dst, 1 if len(ops) > 1 else 0, 4 if to_reg else 1)
        dv = c.n(); c.w(f'uint32_t {dv} = {rdx};')
        c.w(f'zf = ((({dv} >> {bnv}) & 1u) == 0);')
        expr = {'bset': f'{dv} | (1u << {bnv})',
                'bclr': f'{dv} & ~(1u << {bnv})',
                'bchg': f'{dv} ^ (1u << {bnv})'}[base]
        c.w(wf(f'({expr})')); return c.out, True

    # ---- conditional set
    if base.startswith('s') and base[1:] in CC and len(base) == 3:
        cond = CC[base[1:]]
        c.w(wr_op(c, ops[0], 0, 1, f'(({cond}) ? 0xFFu : 0x00u)')); return c.out, True
    if base in ('st','sf'):
        c.w(wr_op(c, ops[0], 0, 1, '0xFFu' if base == 'st' else '0x00u')); return c.out, True

    # ---- control flow
    if base == 'bra':
        return c.out + [GOTO(br_target(i))], False
    if base == 'bsr':
        c.w(f'a7 -= 4; wr32(a7, {nxt}u + SEGBASE);')
        return c.out + [GOTO(br_target(i))], False
    if base.startswith('b') and base[1:] in CC and len(base) == 3:
        cond = CC[base[1:]]
        c.w(f'if ({cond}) {{ {GOTO(br_target(i))} }}')
        return c.out, True

    if base.startswith('db'):
        sub = base[2:]
        cond = 'CC_F' if sub in ('ra','f') else CC.get(sub)
        if cond is None: raise Unsupported(m)
        r = regname(ops[-1].reg if ops[-1].type == M68K_OP_REG else ops[0].reg)
        # capstone: 'dbra d0, $addr' -> op0 reg, op1 br_disp
        r = regname(ops[0].reg)
        c.w(f'if (!({cond})) {{')
        c.w(f'  uint32_t _c = ({r} - 1u) & 0xFFFFu; {r} = ({r} & 0xFFFF0000u) | _c;')
        c.w(f'  if (_c != 0xFFFFu) {{ {GOTO(br_target(i))} }}')
        c.w('}')
        return c.out, True

    if base == 'jmp':
        ea = ea_expr(c, ops[0], 0, 4)
        return c.out + [gen.goto_dyn(ea)], False
    if base == 'jsr':
        ea = ea_expr(c, ops[0], 0, 4)
        t = c.n(); c.w(f'uint32_t {t} = {ea};')
        c.w(f'a7 -= 4; wr32(a7, {nxt}u + SEGBASE);')
        return c.out + [gen.goto_dyn(t)], False
    if base == 'rts':
        c.w('{ uint32_t _r = rd32(a7); a7 += 4;')
        c.w('  if (a7 > S->ret_sp) { S->halt = 1; S->pc = _r; SAVE(); return; }')
        c.w(f'  {gen.goto_dyn("_r")} }}')
        return c.out, False
    if base == 'rtr':
        c.w('{ uint32_t _cc = rd16(a7); a7 += 2; uint32_t _r = rd32(a7); a7 += 4;')
        c.w('  cf=_cc&1; vf=(_cc>>1)&1; zf=(_cc>>2)&1; nf=(_cc>>3)&1; xf=(_cc>>4)&1;')
        c.w(f'  {gen.goto_dyn("_r")} }}')
        return c.out, False

    if base == 'trap':
        vec = ops[0].imm
        if vec == 15:
            tno = struct.unpack('>H', gen.data[i.address+2:i.address+4])[0]
            c.w(f'S->pc = {(i.address + 4) & 0xFFFFFFFF}u + SEGBASE; SAVE();')
            c.w(f'r68k_trap(S, 0x{tno:04X});')
            c.w('LOAD();')
            gen.skip.add((i.address + 2) & 0xFFFFFFFF)
            gen.insn_size[i.address] = 4
            return c.out, True
        c.w(f'r68k_panic(S, {i.address}u + SEGBASE, "trap #{vec}");')
        return c.out, True

    if base == 'nop':
        return ['/* nop */'], True

    if base == 'chk':
        # bounds check; the game uses it once and never expects it to fire
        return ['/* chk (ignored) */'], True

    raise Unsupported(m)

def SE(v, sz):
    if sz == 2: return v - 0x10000 if v & 0x8000 else v
    if sz == 1: return v - 0x100 if v & 0x80 else v
    return v

def xlate_movem(c, i, sz):
    """movem: register list <-> memory."""
    ops = i.operands
    if ops[0].type == M68K_OP_REG_BITS:
        rb, mem, store = ops[0].register_bits, ops[1], True
    else:
        rb, mem, store = ops[1].register_bits, ops[0], False
    names = [f'd{k}' for k in range(8)] + [f'a{k}' for k in range(8)]
    sel = [names[k] for k in range(16) if rb & (1 << k)]
    am = mem.address_mode
    if am == M68K_AM_REGI_ADDR_PRE_DEC:
        b = regname(mem.reg)
        for r in reversed(sel):
            c.w(f'{b} -= {sz}; {wrf(sz)}({b}, {r} & {mask(sz)});')
        return c.out
    if am == M68K_AM_REGI_ADDR_POST_INC:
        b = regname(mem.reg)
        for r in sel:
            c.w(f'{r} = {sext(sz, f"{rdf(sz)}({b})")}; {b} += {sz};')
        return c.out
    ea = ea_expr(c, mem, 0 if not store else 1, sz)
    t = c.n(); c.w(f'uint32_t {t} = {ea};')
    for k, r in enumerate(sel):
        if store: c.w(f'{wrf(sz)}({t} + {k*sz}u, {r} & {mask(sz)});')
        else:     c.w(f'{r} = {sext(sz, f"{rdf(sz)}({t} + {k*sz}u)")};')
    return c.out

def xlate_shift(c, i, base, sz):
    ops = i.operands
    B = bits(sz)
    if len(ops) == 1:                      # memory shift by 1
        rdx, wf = rmw(c, ops[0], 0, sz)
        cnt = '1u'
        dv = c.n(); c.w(f'uint32_t {dv} = {rdx};')
    else:
        if ops[0].address_mode == M68K_AM_IMMEDIATE:
            cnt = f'{ops[0].imm & 63}u'
        else:
            cnt = f'({regname(ops[0].reg)} & 63u)'
        rdx, wf = rmw(c, ops[1], 1, sz)
        dv = c.n(); c.w(f'uint32_t {dv} = {rdx};')
    cv = c.n(); c.w(f'uint32_t {cv} = {cnt};')
    rv = c.n(); c.w(f'uint32_t {rv} = {dv};')
    c.w(f'if ({cv} == 0) {{ cf = 0; vf = 0; }} else {{')
    if base in ('asl','lsl'):
        c.w(f'  cf = ({cv} <= {B}) ? (({dv} >> ({B} - {cv})) & 1u) : 0u; xf = cf;')
        c.w(f'  {rv} = ({cv} >= {B}) ? 0u : (({dv} << {cv}) & {mask(sz)});')
        if base == 'asl':
            c.w(f'  {{ uint32_t _m = ((1u << {cv}) - 1u); if ({cv} >= {B}) _m = {mask(sz)};')
            c.w(f'    uint32_t _top = ({dv} >> ({B} - 1 - (({cv} >= {B}) ? {B}-1 : {cv}))) ;')
            c.w(f'    (void)_top; vf = 0; }}')
        else:
            c.w('  vf = 0;')
    elif base == 'lsr':
        c.w(f'  cf = ({cv} <= {B}) ? (({dv} >> ({cv} - 1)) & 1u) : 0u; xf = cf;')
        c.w(f'  {rv} = ({cv} >= {B}) ? 0u : (({dv} & {mask(sz)}) >> {cv}); vf = 0;')
    elif base == 'asr':
        c.w(f'  {{ int32_t _s = (int32_t){sext(sz, dv)}; uint32_t _n = ({cv} >= {B}) ? {B}-1 : {cv};')
        c.w(f'    cf = (uint32_t)((_s >> (_n ? _n - 1 : 0)) & 1); if ({cv} == 0) cf = 0; xf = cf;')
        c.w(f'    {rv} = (uint32_t)(_s >> _n) & {mask(sz)}; vf = 0; }}')
    elif base in ('rol','ror'):
        c.w(f'  {{ uint32_t _n = {cv} % {B}; uint32_t _v = {dv} & {mask(sz)};')
        if base == 'rol':
            c.w(f'    {rv} = _n ? ((_v << _n) | (_v >> ({B} - _n))) & {mask(sz)} : _v;')
            c.w(f'    cf = {rv} & 1u;')
        else:
            c.w(f'    {rv} = _n ? ((_v >> _n) | (_v << ({B} - _n))) & {mask(sz)} : _v;')
            c.w(f'    cf = ({rv} >> ({B} - 1)) & 1u;')
        c.w('    vf = 0; }')
    else:                                   # roxl / roxr
        c.w(f'  {{ uint32_t _v = {dv} & {mask(sz)}; uint32_t _x = xf;')
        c.w(f'    for (uint32_t _k = 0; _k < {cv}; _k++) {{')
        if base == 'roxl':
            c.w(f'      uint32_t _b = (_v >> ({B} - 1)) & 1u; _v = ((_v << 1) | _x) & {mask(sz)}; _x = _b; }}')
        else:
            c.w(f'      uint32_t _b = _v & 1u; _v = (_v >> 1) | (_x << ({B} - 1)); _v &= {mask(sz)}; _x = _b; }}')
        c.w(f'    {rv} = _v; xf = _x; cf = _x; vf = 0; }}')
    c.w('}')
    c.w(wf(rv)); c.w(f'NZ{B}({rv});')
    return c.out

# ---------------------------------------------------------------- emission

class Gen:
    def __init__(self, segno, data):
        self.segno = segno
        self.data = data
        self.nslots = len(data) // 2
        self.skip = set()
        self.insn_size = {}
        self.lo = 0
        self.hi = 0
        self.stats = {'ok':0,'bad':0}
        self.badmn = {}

    def goto(self, t):
        # Odd targets only arise from decoding data as code; they are never taken.
        if t & 1:
            return f'r68k_panic(S, {t}u + SEGBASE, "odd branch target"); SAVE(); return;'
        if self.lo <= t < self.hi:
            return f'goto I_{t:06X};'
        return f'S->pc = {t}u + SEGBASE; SAVE(); return;'

    def goto_dyn(self, e):
        return (f'{{ uint32_t _t = {e}; '
                f'if (!(_t & 1u) && _t >= SEGBASE + {self.lo}u && _t < SEGBASE + {self.hi}u) '
                f'goto *L[(_t - SEGBASE - {self.lo}u) >> 1]; '
                f'S->pc = _t; SAVE(); return; }}')

    def decode(self, a):
        for i in CS.disasm(self.data[a:min(a+16, len(self.data))], a, count=1):
            return i
        return None

    def slot(self, a):
        """Emit the C body for the instruction slot at byte offset a."""
        i = self.decode(a)
        if i is None:
            self.stats['bad'] += 1
            return [f'r68k_panic(S, {a}u + SEGBASE, "undecodable"); SAVE(); return;']
        try:
            lines, fall = xlate(self, i)
            self.stats['ok'] += 1
        except (Unsupported, KeyError, IndexError, capstone.CsError) as e:
            self.stats['bad'] += 1
            self.badmn[i.mnemonic] = self.badmn.get(i.mnemonic, 0) + 1
            return [f'r68k_panic(S, {a}u + SEGBASE, "unsupported: {i.mnemonic}"); SAVE(); return;']
        size = self.insn_size.get(a, i.size)
        nxt = a + size
        if fall:
            # Every even offset gets its own label, so textual fall-through only
            # lands on the right instruction when the current one is 2 bytes long.
            if nxt != a + 2 or nxt >= self.hi:
                lines = lines + [self.goto(nxt & 0xFFFFFFFF)]
        return lines

    def emit_chunk(self, k, f):
        self.lo = k * CHUNK_SLOTS * 2
        self.hi = min(self.lo + CHUNK_SLOTS * 2, len(self.data))
        n = (self.hi - self.lo) // 2
        f.write(f'\nvoid seg{self.segno}_c{k}(r68k_state *S) {{\n')
        f.write('  REGS();\n  LOAD();\n')
        f.write(f'  const uint32_t SEGBASE = r68k_segbase[{self.segno}];\n')
        f.write('  static void *const L[] = {\n')
        for j in range(n):
            f.write(f'    &&I_{self.lo + j*2:06X},\n')
        f.write('  };\n')
        f.write(f'  goto *L[(S->pc - SEGBASE - {self.lo}u) >> 1];\n')
        for j in range(n):
            a = self.lo + j * 2
            f.write(f' I_{a:06X}: {{\n')
            if a in self.skip:
                f.write(f'  r68k_panic(S, {a}u + SEGBASE, "trap operand word"); SAVE(); return;\n')
            else:
                for ln in self.slot(a):
                    f.write(f'  {ln}\n')
            f.write(' }\n')
        f.write(f'  S->pc = {self.hi}u + SEGBASE; SAVE(); return;\n')
        f.write('}\n')

    def generate(self, outdir):
        nchunks = (self.nslots + CHUNK_SLOTS - 1) // CHUNK_SLOTS
        # pre-pass: find trap selector words so they are not translated as code
        for a in range(0, len(self.data) - 3, 2):
            if self.data[a] == 0x4E and self.data[a+1] == 0x4F:
                self.skip.add(a + 2)
        path = os.path.join(outdir, f'seg{self.segno}.c')
        with open(path, 'w') as f:
            f.write('/* Generated by tools/recomp68k.py -- do not edit. */\n')
            f.write('#include "r68k.h"\n#include "r68k_gen.h"\n')
            for k in range(nchunks):
                self.emit_chunk(k, f)
        return nchunks

HDR = r'''/* Generated by tools/recomp68k.py -- do not edit. */
#ifndef R68K_GEN_H
#define R68K_GEN_H
#include "r68k.h"

#define REGS() \
  uint32_t d0,d1,d2,d3,d4,d5,d6,d7,a0,a1,a2,a3,a4,a5,a6,a7; \
  uint32_t xf,nf,zf,vf,cf
#define LOAD() do { \
  d0=S->d[0];d1=S->d[1];d2=S->d[2];d3=S->d[3];d4=S->d[4];d5=S->d[5];d6=S->d[6];d7=S->d[7]; \
  a0=S->a[0];a1=S->a[1];a2=S->a[2];a3=S->a[3];a4=S->a[4];a5=S->a[5];a6=S->a[6];a7=S->a[7]; \
  xf=S->xf;nf=S->nf;zf=S->zf;vf=S->vf;cf=S->cf; } while (0)
#define SAVE() do { \
  S->d[0]=d0;S->d[1]=d1;S->d[2]=d2;S->d[3]=d3;S->d[4]=d4;S->d[5]=d5;S->d[6]=d6;S->d[7]=d7; \
  S->a[0]=a0;S->a[1]=a1;S->a[2]=a2;S->a[3]=a3;S->a[4]=a4;S->a[5]=a5;S->a[6]=a6;S->a[7]=a7; \
  S->xf=xf;S->nf=nf;S->zf=zf;S->vf=vf;S->cf=cf; } while (0)

typedef void (*r68k_chunk_fn)(r68k_state *);
extern const r68k_chunk_fn *const r68k_chunks[4];
extern const int r68k_nchunks[4];
#define R68K_CHUNK_BYTES %d
%s
#endif
'''

def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    outdir = os.path.join(root, 'src', 'gen')
    os.makedirs(outdir, exist_ok=True)
    protos = []
    counts = {}
    for n in (1, 2, 3):
        data = open(os.path.join(root, 'build', 'res', f'code_{n}.bin'), 'rb').read()
        g = Gen(n, data)
        nc = g.generate(outdir)
        counts[n] = nc
        for k in range(nc):
            protos.append(f'void seg{n}_c{k}(r68k_state *S);')
        print(f'seg{n}: {nc} chunks, {g.stats["ok"]} translated, {g.stats["bad"]} unsupported '
              f'{sorted(g.badmn.items(), key=lambda x:-x[1])[:8]}')
    with open(os.path.join(outdir, 'r68k_gen.h'), 'w') as f:
        f.write(HDR % (CHUNK_SLOTS * 2, '\n'.join(protos)))
    with open(os.path.join(outdir, 'r68k_tables.c'), 'w') as f:
        f.write('/* Generated by tools/recomp68k.py -- do not edit. */\n')
        f.write('#include "r68k.h"\n#include "r68k_gen.h"\n\n')
        for n in (1, 2, 3):
            f.write(f'static const r68k_chunk_fn tab{n}[] = {{ ' +
                    ', '.join(f'seg{n}_c{k}' for k in range(counts[n])) + ' };\n')
        f.write('\nconst r68k_chunk_fn *const r68k_chunks[4] = { 0, tab1, tab2, tab3 };\n')
        f.write('const int r68k_nchunks[4] = { 0, %d, %d, %d };\n' % (counts[1], counts[2], counts[3]))
    print('wrote', outdir)

if __name__ == '__main__':
    main()
