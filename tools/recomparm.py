#!/usr/bin/env python3
"""Static recompiler: ARM32 -> C, for the Bike or Die 2 `armc` resources.

Same shape as tools/recomp68k.py: every 4-byte slot is translated and the chunk
indexes a label array, so jump tables and computed branches need no analysis.
"""
import os, re, sys, struct
import capstone
from capstone import *
from capstone.arm import *

CS = Cs(CS_ARCH_ARM, CS_MODE_ARM | CS_MODE_LITTLE_ENDIAN)
CS.detail = True

# Instruction slots (4 bytes each) per generated C function; see recomp68k.py.
# An ARM instruction translates to roughly twice the C a 68k one does, so this
# is half of what that one uses.
CHUNK_SLOTS = int(os.environ.get('BOD_CHUNK_SLOTS', '256'))

CCMAP = {
    ARM_CC_EQ:'ACC_EQ', ARM_CC_NE:'ACC_NE', ARM_CC_HS:'ACC_HS', ARM_CC_LO:'ACC_LO',
    ARM_CC_MI:'ACC_MI', ARM_CC_PL:'ACC_PL', ARM_CC_VS:'ACC_VS', ARM_CC_VC:'ACC_VC',
    ARM_CC_HI:'ACC_HI', ARM_CC_LS:'ACC_LS', ARM_CC_GE:'ACC_GE', ARM_CC_LT:'ACC_LT',
    ARM_CC_GT:'ACC_GT', ARM_CC_LE:'ACC_LE', ARM_CC_AL:'ACC_AL',
}
SHT = {ARM_SFT_LSL:0, ARM_SFT_LSR:1, ARM_SFT_ASR:2, ARM_SFT_ROR:3, ARM_SFT_RRX:4,
       ARM_SFT_LSL_REG:0, ARM_SFT_LSR_REG:1, ARM_SFT_ASR_REG:2, ARM_SFT_ROR_REG:3,
       ARM_SFT_RRX_REG:4}
SHT_ISREG = {ARM_SFT_LSL_REG, ARM_SFT_LSR_REG, ARM_SFT_ASR_REG, ARM_SFT_ROR_REG, ARM_SFT_RRX_REG}

class Unsupported(Exception): pass

REGNUM = {}
for _n in range(13):
    REGNUM[getattr(capstone.arm, f'ARM_REG_R{_n}')] = _n
REGNUM[ARM_REG_SP] = 13
REGNUM[ARM_REG_LR] = 14
REGNUM[ARM_REG_PC] = 15
REGNUM[ARM_REG_FP] = 11
REGNUM[ARM_REG_IP] = 12
REGNUM[ARM_REG_SL] = 10

def rnum(r):
    if r in REGNUM: return REGNUM[r]
    raise Unsupported(f'reg {r}')

class Ctx:
    def __init__(self, gen, i):
        self.gen = gen; self.i = i; self.out = []; self.k = 0
    def n(self, p='t'):
        self.k += 1; return f'{p}{self.k}'
    def w(self, s): self.out.append(s)

def R(c, r):
    """C expression reading a register; r15 reads as the address of the
    instruction plus 8."""
    n = rnum(r)
    if n == 15:
        return f'({c.i.address + 8}u + SEGBASE)'
    return f'r{n}'

def WR(r):
    n = rnum(r)
    if n == 15: raise Unsupported('write to pc handled separately')
    return f'r{n}'

def shift_of(op):
    """(type, amount_expr_or_None, is_reg) for an operand's shift, or None."""
    st = op.shift.type
    if not st: return None
    t = SHT.get(st)
    if t is None: raise Unsupported(f'shift {st}')
    return (t, op.shift.value, st in SHT_ISREG)

def op2(c, op, want_carry):
    """Value of a data-processing operand 2. Returns (expr, carry_expr|None)."""
    if op.type == ARM_OP_IMM:
        # A data-processing immediate is an 8-bit value rotated right by 2*rot.
        # capstone resolves that for most encodings but reports the two halves
        # separately for non-canonical ones ("sub r0, r0, #4, #24"), so always
        # decode it from the instruction word.
        w = int.from_bytes(c.i.bytes, 'little')
        val = op.imm & 0xFFFFFFFF
        rot = 0
        if ((w >> 26) & 3) == 0 and ((w >> 25) & 1):
            imm8 = w & 0xFF
            rot = ((w >> 8) & 0xF) * 2
            val = (((imm8 >> rot) | (imm8 << (32 - rot))) & 0xFFFFFFFF) if rot else imm8
        # a rotated immediate also produces a shifter carry-out
        if want_carry and rot:
            return (f'{val}u', f'{(val >> 31) & 1}u')
        return (f'{val}u', None)
    if op.type != ARM_OP_REG:
        raise Unsupported(f'op2 type {op.type}')
    base = R(c, op.reg)
    sh = shift_of(op)
    if sh is None:
        return (base, None)
    t, val, isreg = sh
    amt = R(c, val) if isreg else f'{val}u'
    fn = 'arm_sh_reg' if isreg else 'arm_sh'
    v = c.n('sv'); co = c.n('sc')
    c.w(f'uint32_t {co} = cf; uint32_t {v} = {fn}({base}, {t}, {amt}, cf, &{co});')
    return (v, co)

# ---------------------------------------------------------------- translation

DP_ARITH = {
    ARM_INS_ADD: ('+', 'add'), ARM_INS_SUB: ('-', 'sub'), ARM_INS_RSB: ('-', 'rsb'),
    ARM_INS_ADC: ('+', 'adc'), ARM_INS_SBC: ('-', 'sbc'), ARM_INS_RSC: ('-', 'rsc'),
}
DP_LOGIC = {
    ARM_INS_AND: '&', ARM_INS_ORR: '|', ARM_INS_EOR: '^', ARM_INS_BIC: '&~',
}
LDST = {
    ARM_INS_LDR:  ('ard32', 4, None), ARM_INS_LDRB: ('ard8', 1, None),
    ARM_INS_LDRH: ('ard16', 2, None), ARM_INS_LDRSB:('ard8', 1, 'ASE8'),
    ARM_INS_LDRSH:('ard16', 2, 'ASE16'),
    ARM_INS_STR:  ('awr32', 4, None), ARM_INS_STRB: ('awr8', 1, None),
    ARM_INS_STRH: ('awr16', 2, None),
}
SHIFT_INS = {ARM_INS_LSL:0, ARM_INS_LSR:1, ARM_INS_ASR:2, ARM_INS_ROR:3, ARM_INS_RRX:4}
LDM_MODE = {ARM_INS_LDM:('ia',0), ARM_INS_LDMIB:('ib',0), ARM_INS_LDMDA:('da',0),
            ARM_INS_LDMDB:('db',0), ARM_INS_STM:('ia',1), ARM_INS_STMIB:('ib',1),
            ARM_INS_STMDA:('da',1), ARM_INS_STMDB:('db',1)}

def mem_addr(c, i, memop, extra):
    """Emit address computation. Returns (addr_expr, post_writeback_stmt|None)."""
    m = memop.mem
    base = R(c, m.base)
    basew = None
    try: basew = WR(m.base)
    except Unsupported: basew = None

    def off_expr(op):
        if op is None:
            if m.index:
                idx = R(c, m.index)
                sh = shift_of(memop)
                if sh:
                    t, val, isreg = sh
                    amt = R(c, val) if isreg else f'{val}u'
                    tmp = c.n('mo'); co = c.n('mc')
                    c.w(f'uint32_t {co}; uint32_t {tmp} = arm_sh({idx}, {t}, {amt}, cf, &{co}); (void){co};')
                    e = tmp
                elif m.lshift:
                    e = f'({idx} << {m.lshift})'
                else:
                    e = idx
                return f'-{e}' if m.scale < 0 else e
            return f'{m.disp & 0xFFFFFFFF}u'
        if op.type == ARM_OP_IMM:
            v = op.imm
            return f'{v & 0xFFFFFFFF}u' if not op.subtracted else f'(0u - {v & 0xFFFFFFFF}u)'
        idx = R(c, op.reg)
        sh = shift_of(op)
        if sh:
            t, val, isreg = sh
            amt = R(c, val) if isreg else f'{val}u'
            tmp = c.n('mo'); co = c.n('mc')
            c.w(f'uint32_t {co}; uint32_t {tmp} = arm_sh({idx}, {t}, {amt}, cf, &{co}); (void){co};')
            idx = tmp
        return f'(0u - {idx})' if op.subtracted else idx

    if extra is not None:                     # post-indexed
        a = c.n('ea'); c.w(f'uint32_t {a} = {base};')
        post = f'{basew} = {basew} + {off_expr(extra)};' if basew else None
        return a, post
    a = c.n('ea'); c.w(f'uint32_t {a} = {base} + {off_expr(None)};')
    if i.writeback and basew:
        return a, f'{basew} = {a};'
    return a, None

def xlate(gen, i):
    c = Ctx(gen, i)
    nxt = i.address + 4
    cond = CCMAP.get(i.cc, 'ACC_AL')
    ops = i.operands
    ID = i.id
    body = []
    falls = True

    def emit(lines, terminal=False):
        return lines, not terminal

    # --- branches -----------------------------------------------------------
    if ID in (ARM_INS_B, ARM_INS_BL, ARM_INS_BLX) and ops and ops[0].type == ARM_OP_IMM:
        tgt = ops[0].imm & 0xFFFFFFFF
        pre = []
        if ID in (ARM_INS_BL, ARM_INS_BLX):
            pre.append(f'r14 = {nxt}u + SEGBASE;')
        stmt = ' '.join(pre) + ' ' + gen.goto(tgt)
        if cond == 'ACC_AL':
            return c.out + [stmt], False
        return c.out + [f'if ({cond}) {{ {stmt} }}'], True

    if ID in (ARM_INS_BX, ARM_INS_BLX):
        t = c.n(); c.w(f'uint32_t {t} = {R(c, ops[0].reg)};')
        pre = f'r14 = {nxt}u + SEGBASE; ' if ID == ARM_INS_BLX else ''
        stmt = pre + gen.goto_dyn(t)
        if cond == 'ACC_AL':
            return c.out + [stmt], False
        return c.out + [f'if ({cond}) {{ {stmt} }}'], True

    # --- data processing ----------------------------------------------------
    if ID in DP_ARITH or ID in DP_LOGIC or ID in (ARM_INS_MOV, ARM_INS_MVN,
            ARM_INS_CMP, ARM_INS_CMN, ARM_INS_TST, ARM_INS_TEQ) or ID in SHIFT_INS:
        return xlate_dp(c, i, cond, nxt, gen)

    # --- load / store -------------------------------------------------------
    if ID in LDST:
        return xlate_ldst(c, i, cond, gen)

    # --- block transfer -----------------------------------------------------
    if ID in LDM_MODE or ID in (ARM_INS_PUSH, ARM_INS_POP):
        return xlate_block(c, i, cond, gen)

    # --- multiply -----------------------------------------------------------
    if ID in (ARM_INS_MUL, ARM_INS_MLA):
        d = WR(ops[0].reg); a = R(c, ops[1].reg); b = R(c, ops[2].reg)
        e = f'({a} * {b})'
        if ID == ARM_INS_MLA:
            e = f'({a} * {b} + {R(c, ops[3].reg)})'
        c.w(f'if ({cond}) {{ {d} = {e};' + (f' ANZ({d});' if i.update_flags else '') + ' }')
        return c.out, True
    if ID in (ARM_INS_UMULL, ARM_INS_SMULL, ARM_INS_UMLAL, ARM_INS_SMLAL):
        lo = WR(ops[0].reg); hi = WR(ops[1].reg)
        a = R(c, ops[2].reg); b = R(c, ops[3].reg)
        sg = ID in (ARM_INS_SMULL, ARM_INS_SMLAL)
        cast = '(int64_t)(int32_t)' if sg else '(uint64_t)'
        acc = ID in (ARM_INS_UMLAL, ARM_INS_SMLAL)
        t = c.n()
        c.w(f'if ({cond}) {{ uint64_t {t} = (uint64_t)({cast}{a} * {cast}{b});')
        if acc:
            c.w(f'  {t} += ((uint64_t){hi} << 32) | {lo};')
        c.w(f'  {lo} = (uint32_t){t}; {hi} = (uint32_t)({t} >> 32);')
        if i.update_flags:
            c.w(f'  nf = {hi} >> 31; zf = ({t} == 0);')
        c.w('}')
        return c.out, True

    if ID == ARM_INS_CLZ:
        d = WR(ops[0].reg); a = R(c, ops[1].reg)
        c.w(f'if ({cond}) {{ {d} = {a} ? (uint32_t)__builtin_clz({a}) : 32u; }}')
        return c.out, True

    if ID in (ARM_INS_NOP,):
        return ['/* nop */'], True

    raise Unsupported(i.mnemonic)

def xlate_dp(c, i, cond, nxt, gen):
    ID = i.id; ops = i.operands
    S = i.update_flags
    is_test = ID in (ARM_INS_CMP, ARM_INS_CMN, ARM_INS_TST, ARM_INS_TEQ)

    if ID in SHIFT_INS:
        # capstone renders MOV-with-shift as lsl/lsr/asr/ror/rrx
        t = SHIFT_INS[ID]
        src = R(c, ops[1].reg)
        if ID == ARM_INS_RRX:
            amt, fn = '0u', 'arm_sh'
        elif len(ops) >= 3:
            amt = R(c, ops[2].reg) if ops[2].type == ARM_OP_REG else f'{ops[2].imm}u'
            fn = 'arm_sh_reg' if ops[2].type == ARM_OP_REG else 'arm_sh'
        else:
            sh = shift_of(ops[1])
            if sh is None: raise Unsupported('shift without amount')
            _t, val, isreg = sh
            t = _t
            amt = R(c, val) if isreg else f'{val}u'
            fn = 'arm_sh_reg' if isreg else 'arm_sh'
        co = c.n('sc'); v = c.n('sv')
        c.w(f'if ({cond}) {{ uint32_t {co} = cf; uint32_t {v} = {fn}({src}, {t}, {amt}, cf, &{co});')
        if rnum(ops[0].reg) == 15:
            c.w(f'  {gen.goto_dyn(v)} }}')
            return c.out, True
        c.w(f'  {WR(ops[0].reg)} = {v};')
        if S: c.w(f'  cf = {co}; ANZ({v});')
        c.w('}')
        return c.out, True

    # operand positions: 2-operand forms (mov/mvn/cmp/cmn/tst/teq) vs 3-operand
    two = ID in (ARM_INS_MOV, ARM_INS_MVN) or is_test
    if two:
        rn = None if ID in (ARM_INS_MOV, ARM_INS_MVN) else ops[0]
        rd = ops[0] if ID in (ARM_INS_MOV, ARM_INS_MVN) else None
        src = ops[1]
    else:
        rd, rn, src = ops[0], ops[1], ops[2]

    want_carry = S and (ID in DP_LOGIC or ID in (ARM_INS_MOV, ARM_INS_MVN,
                                                 ARM_INS_TST, ARM_INS_TEQ))
    c.w(f'if ({cond}) {{')
    v, carry = op2(c, src, want_carry)
    res = c.n('rv')
    if ID in (ARM_INS_MOV, ARM_INS_MVN):
        c.w(f'  uint32_t {res} = ' + (f'~{v};' if ID == ARM_INS_MVN else f'{v};'))
    elif ID in DP_LOGIC:
        opn = DP_LOGIC[ID]
        a = R(c, rn.reg)
        c.w(f'  uint32_t {res} = {a} {opn} {v};')
    elif ID in (ARM_INS_TST, ARM_INS_TEQ):
        a = R(c, rn.reg)
        opn = '&' if ID == ARM_INS_TST else '^'
        c.w(f'  uint32_t {res} = {a} {opn} {v};')
    else:
        a = R(c, rn.reg) if rn is not None else R(c, ops[0].reg)
        # AADD also writes C and V, so it may only be used when the S bit is set
        # (or for the compare forms, which always update the flags).
        if S or is_test:
            if ID == ARM_INS_ADD:   c.w(f'  uint32_t {res}; AADD({a}, {v}, 0u, {res});')
            elif ID == ARM_INS_ADC: c.w(f'  uint32_t {res}; AADD({a}, {v}, cf, {res});')
            elif ID in (ARM_INS_SUB, ARM_INS_CMP):
                c.w(f'  uint32_t {res}; AADD({a}, ~({v}), 1u, {res});')
            elif ID == ARM_INS_SBC: c.w(f'  uint32_t {res}; AADD({a}, ~({v}), cf, {res});')
            elif ID == ARM_INS_CMN: c.w(f'  uint32_t {res}; AADD({a}, {v}, 0u, {res});')
            elif ID == ARM_INS_RSB: c.w(f'  uint32_t {res}; AADD({v}, ~({a}), 1u, {res});')
            elif ID == ARM_INS_RSC: c.w(f'  uint32_t {res}; AADD({v}, ~({a}), cf, {res});')
            else: raise Unsupported(i.mnemonic)
        else:
            if ID == ARM_INS_ADD:   e = f'{a} + {v}'
            elif ID == ARM_INS_ADC: e = f'{a} + {v} + cf'
            elif ID == ARM_INS_SUB: e = f'{a} - {v}'
            elif ID == ARM_INS_SBC: e = f'{a} - {v} - (1u - cf)'
            elif ID == ARM_INS_RSB: e = f'{v} - {a}'
            elif ID == ARM_INS_RSC: e = f'{v} - {a} - (1u - cf)'
            else: raise Unsupported(i.mnemonic)
            c.w(f'  uint32_t {res} = {e};')

    if is_test:
        c.w(f'  ANZ({res});')
        # Only the logical tests take C from the barrel shifter; CMP/CMN take it
        # from the subtraction/addition itself.
        if carry is not None and want_carry: c.w(f'  cf = {carry};')
        c.w('}')
        return c.out, True

    if rnum(rd.reg) == 15:
        c.w(f'  {gen.goto_dyn(res)} }}')
        return c.out, True
    c.w(f'  {WR(rd.reg)} = {res};')
    if S:
        c.w(f'  ANZ({res});')
        if carry is not None and want_carry: c.w(f'  cf = {carry};')
    c.w('}')
    return c.out, True

def xlate_ldst(c, i, cond, gen):
    ID = i.id; ops = i.operands
    fn, sz, sx = LDST[ID]
    store = fn.startswith('awr')
    memop = ops[1]
    extra = ops[2] if len(ops) > 2 else None
    c.w(f'if ({cond}) {{')
    addr, post = mem_addr(c, i, memop, extra)
    if store:
        rt = R(c, ops[0].reg)
        c.w(f'  {fn}({addr}, {rt});')
    else:
        v = c.n('lv')
        e = f'{fn}({addr})'
        if sx: e = f'{sx}({e})'
        c.w(f'  uint32_t {v} = {e};')
        if rnum(ops[0].reg) == 15:
            if post: c.w(f'  {post}')
            c.w(f'  {gen.goto_dyn(v)} }}')
            return c.out, True
        c.w(f'  {WR(ops[0].reg)} = {v};')
    if post: c.w(f'  {post}')
    c.w('}')
    return c.out, True

def xlate_block(c, i, cond, gen):
    ID = i.id; ops = i.operands
    if ID == ARM_INS_PUSH:
        regs = [rnum(o.reg) for o in ops]; base = 13; mode = 'db'; store = 1; wb = True
    elif ID == ARM_INS_POP:
        regs = [rnum(o.reg) for o in ops]; base = 13; mode = 'ia'; store = 0; wb = True
    else:
        mode, store = LDM_MODE[ID]
        base = rnum(ops[0].reg)
        if base == 15: raise Unsupported('ldm/stm with pc base')
        regs = [rnum(o.reg) for o in ops[1:]]
        wb = i.writeback
    regs = sorted(regs)
    n = len(regs)
    c.w(f'if ({cond}) {{')
    a = c.n('ba')
    if mode == 'ia':   c.w(f'  uint32_t {a} = r{base};')
    elif mode == 'ib': c.w(f'  uint32_t {a} = r{base} + 4u;')
    elif mode == 'da': c.w(f'  uint32_t {a} = r{base} - {4*n}u + 4u;')
    else:              c.w(f'  uint32_t {a} = r{base} - {4*n}u;')
    pc_val = None
    for k, rg in enumerate(regs):
        off = f'{a} + {4*k}u'
        if store:
            src = f'({i.address + 12}u + SEGBASE)' if rg == 15 else f'r{rg}'
            c.w(f'  awr32({off}, {src});')
        else:
            if rg == 15:
                pc_val = c.n('pv'); c.w(f'  uint32_t {pc_val} = ard32({off});')
            else:
                c.w(f'  r{rg} = ard32({off});')
    if wb:
        if mode in ('ia', 'ib'): c.w(f'  r{base} = r{base} + {4*n}u;')
        else:                    c.w(f'  r{base} = r{base} - {4*n}u;')
    if pc_val:
        c.w(f'  {gen.goto_dyn(pc_val)}')
    c.w('}')
    return c.out, True

# ---------------------------------------------------------------- emission

class Gen:
    def __init__(self, segno, data):
        self.segno = segno
        self.data = data
        self.nslots = len(data) // 4
        self.lo = 0
        self.hi = 0
        self.stats = {'ok': 0, 'bad': 0}
        self.badmn = {}

    def goto(self, t):
        if t & 3:
            return f'rarm_panic(S, {t}u + SEGBASE, "misaligned branch target"); SAVE(); return;'
        if self.lo <= t < self.hi:
            return f'goto I_{t:06X};'
        return f'S->r[15] = {t}u + SEGBASE; SAVE(); return;'

    def goto_dyn(self, e):
        return (f'{{ uint32_t _t = ({e}) & ~1u; '
                f'if (!(_t & 3u) && _t >= SEGBASE + {self.lo}u && _t < SEGBASE + {self.hi}u) '
                f'goto *L[(_t - SEGBASE - {self.lo}u) >> 2]; '
                f'S->r[15] = _t; SAVE(); return; }}')

    def decode(self, a):
        for i in CS.disasm(self.data[a:a+4], a, count=1):
            return i
        return None

    def slot(self, a):
        i = self.decode(a)
        if i is None:
            self.stats['bad'] += 1
            return [f'rarm_panic(S, {a}u + SEGBASE, "undecodable"); SAVE(); return;']
        try:
            lines, fall = xlate(self, i)
            self.stats['ok'] += 1
        except (Unsupported, KeyError, IndexError, AttributeError, capstone.CsError) as e:
            self.stats['bad'] += 1
            self.badmn[i.mnemonic] = self.badmn.get(i.mnemonic, 0) + 1
            return [f'rarm_panic(S, {a}u + SEGBASE, "unsupported: {i.mnemonic}"); SAVE(); return;']
        if fall and a + 4 >= self.hi:
            lines = lines + [self.goto(a + 4)]
        return lines

    def emit_chunk(self, k, f):
        self.lo = k * CHUNK_SLOTS * 4
        self.hi = min(self.lo + CHUNK_SLOTS * 4, len(self.data))
        n = (self.hi - self.lo) // 4
        f.write(f'\nvoid arm{self.segno}_c{k}(rarm_state *S) {{\n')
        f.write('  AREGS();\n  ALOAD();\n')
        f.write(f'  const uint32_t SEGBASE = rarm_segbase[{self.segno}];\n')
        f.write('  static void *const L[] = {\n')
        for j in range(n):
            f.write(f'    &&I_{self.lo + j*4:06X},\n')
        f.write('  };\n')
        f.write(f'  goto *L[(S->r[15] - SEGBASE - {self.lo}u) >> 2];\n')
        for j in range(n):
            a = self.lo + j * 4
            f.write(f' I_{a:06X}: {{\n')
            f.write(f'#ifdef RECOMP_GUARD\n  rarm_cur_pc = {a}u + SEGBASE;\n'
                    f'  if (rarm_tracing) {{ S->r[15] = {a}u + SEGBASE; SAVE(); rarm_trace(S); }}\n#endif\n')
            for ln in self.slot(a):
                f.write(f'  {ln}\n')
            f.write(' }\n')
        f.write(f'  S->r[15] = {self.hi}u + SEGBASE; SAVE(); return;\n')
        f.write('}\n')

    def generate(self, outdir):
        nchunks = (self.nslots + CHUNK_SLOTS - 1) // CHUNK_SLOTS
        path = os.path.join(outdir, f'arm{self.segno}.c')
        with open(path, 'w') as f:
            f.write('/* Generated by tools/recomparm.py -- do not edit. */\n')
            f.write('#include "rarm.h"\n#include "rarm_gen.h"\n')
            for k in range(nchunks):
                self.emit_chunk(k, f)
        return nchunks

HDR = r'''/* Generated by tools/recomparm.py -- do not edit. */
#ifndef RARM_GEN_H
#define RARM_GEN_H
#include "rarm.h"

#define AREGS() \
  uint32_t r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14; \
  uint32_t nf,zf,cf,vf
#define ALOAD() do { \
  r0=S->r[0];r1=S->r[1];r2=S->r[2];r3=S->r[3];r4=S->r[4];r5=S->r[5];r6=S->r[6];r7=S->r[7]; \
  r8=S->r[8];r9=S->r[9];r10=S->r[10];r11=S->r[11];r12=S->r[12];r13=S->r[13];r14=S->r[14]; \
  nf=S->nf;zf=S->zf;cf=S->cf;vf=S->vf; } while (0)
#define SAVE() do { \
  S->r[0]=r0;S->r[1]=r1;S->r[2]=r2;S->r[3]=r3;S->r[4]=r4;S->r[5]=r5;S->r[6]=r6;S->r[7]=r7; \
  S->r[8]=r8;S->r[9]=r9;S->r[10]=r10;S->r[11]=r11;S->r[12]=r12;S->r[13]=r13;S->r[14]=r14; \
  S->nf=nf;S->zf=zf;S->cf=cf;S->vf=vf; } while (0)

typedef void (*rarm_chunk_fn)(rarm_state *);
extern const rarm_chunk_fn *const rarm_chunks[8];
extern const int rarm_nchunks[8];
#define RARM_CHUNK_BYTES %d
%s
#endif
'''

def emit_stepper(root, outdir, n, per=1024):
    """Single-step core: executes exactly one instruction and returns.

    Emitted with an empty in-range window, so every branch and every
    fall-through degrades to "set r15, save, return". Split into modest
    functions -- one function with 16k computed-goto labels is quadratic in
    clang's register allocator."""
    data = open(os.path.join(root, 'build', 'res', f'armc_{n}.bin'), 'rb').read()
    g = Gen(n, data)
    g.lo = 0
    g.hi = 0                      # nothing is "in range" -> no local gotos
    nslots = len(data) // 4
    nfun = (nslots + per - 1) // per
    path = os.path.join(outdir, f'arm{n}_step.c')
    with open(path, 'w') as f:
        f.write('/* Generated by tools/recomparm.py -- single-step core for lockstep checking. */\n')
        f.write('#include "rarm.h"\n#include "rarm_gen.h"\n')
        for k in range(nfun):
            lo, hi = k * per, min((k + 1) * per, nslots)
            f.write(f'\nstatic void step{n}_{k}(rarm_state *S) {{\n  AREGS();\n  ALOAD();\n')
            f.write(f'  const uint32_t SEGBASE = rarm_segbase[{n}];\n')
            f.write('  static void *const L[] = {\n')
            for j in range(lo, hi):
                f.write(f'    &&I_{j*4:06X},\n')
            f.write('  };\n')
            f.write(f'  goto *L[(S->r[15] - SEGBASE - {lo*4}u) >> 2];\n')
            for j in range(lo, hi):
                a = j * 4
                f.write(f' I_{a:06X}: {{\n')
                for ln in g.slot(a):
                    f.write(f'  {ln}\n')
                f.write(' }\n')
            f.write('  S->r[15] = SEGBASE; SAVE(); return;\n}\n')
        f.write(f'\nvoid arm{n}_step(rarm_state *S) {{\n')
        f.write(f'  uint32_t off = S->r[15] - rarm_segbase[{n}];\n')
        f.write(f'  switch (off / {per*4}u) {{\n')
        for k in range(nfun):
            f.write(f'    case {k}: step{n}_{k}(S); return;\n')
        f.write('    default: rarm_panic(S, S->r[15], "step out of range"); return;\n  }\n}\n')
    print(f'stepper: arm{n}_step.c ({nslots} slots in {nfun} functions)')

def emit_stepper_table(outdir):
    """Lockstep dispatch over whatever single-step cores are on disk.

    The steppers are large and only used by BOD_ARM_LOCKSTEP, so they are not
    generated by default; the Makefile compiles the ones that are present and
    this table tells the checker which blobs it can step."""
    have = sorted(int(m.group(1)) for m in
                  (re.match(r'arm(\d)_step\.c$', f) for f in os.listdir(outdir)) if m)
    with open(os.path.join(outdir, 'rarm_steppers.c'), 'w') as f:
        f.write('/* Generated by tools/recomparm.py -- do not edit. */\n')
        f.write('#include "rarm.h"\n\n')
        for n in have:
            f.write(f'void arm{n}_step(rarm_state *S);\n')
        f.write('\nrarm_step_fn rarm_stepper(int n) {\n  switch (n) {\n')
        for n in have:
            f.write(f'    case {n}: return arm{n}_step;\n')
        f.write('  }\n  return 0;\n}\n')
    print('steppers available for lockstep:', have or 'none')


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    outdir = os.path.join(root, 'src', 'gen')
    os.makedirs(outdir, exist_ok=True)
    protos = []; counts = {}
    for n in (1, 2, 3, 4):
        data = open(os.path.join(root, 'build', 'res', f'armc_{n}.bin'), 'rb').read()
        g = Gen(n, data)
        nc = g.generate(outdir)
        counts[n] = nc
        for k in range(nc):
            protos.append(f'void arm{n}_c{k}(rarm_state *S);')
        print(f'armc{n}: {nc} chunks, {g.stats["ok"]} translated, {g.stats["bad"]} unsupported '
              f'{sorted(g.badmn.items(), key=lambda x: -x[1])[:8]}')
    with open(os.path.join(outdir, 'rarm_gen.h'), 'w') as f:
        f.write(HDR % (CHUNK_SLOTS * 4, '\n'.join(protos)))
    with open(os.path.join(outdir, 'rarm_tables.c'), 'w') as f:
        f.write('/* Generated by tools/recomparm.py -- do not edit. */\n')
        f.write('#include "rarm.h"\n#include "rarm_gen.h"\n\n')
        for n in (1, 2, 3, 4):
            f.write(f'static const rarm_chunk_fn atab{n}[] = {{ ' +
                    ', '.join(f'arm{n}_c{k}' for k in range(counts[n])) + ' };\n')
        f.write('\nconst rarm_chunk_fn *const rarm_chunks[8] = { 0, atab1, atab2, atab3, atab4, 0, 0, 0 };\n')
        f.write('const int rarm_nchunks[8] = { 0, %d, %d, %d, %d, 0, 0, 0 };\n'
                % (counts[1], counts[2], counts[3], counts[4]))
    if len(sys.argv) > 1 and sys.argv[1] == '--stepper':
        for n in [int(x) for x in sys.argv[2:]] or [2]:
            emit_stepper(root, outdir, n)
    emit_stepper_table(outdir)
    print('wrote', outdir)

if __name__ == '__main__':
    main()
