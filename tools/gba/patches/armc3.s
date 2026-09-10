@ Patches to `armc` 3, the level loader half of the Bike or Die 2 engine,
@ for the GBA port. Two themes:
@
@ 1. Textures live in ROM as grey images (tools/gba/pretex.py) and are tinted
@    at render time through a lookup table (see armc2.s), so the loader's
@    doubling, smoothing, colourising and mipmap generation have nothing to
@    do when the buffer it was handed is in ROM. Each of those routines gets
@    an early return for a ROM destination; the buffers themselves come from
@    the runtime's MemPtrNew hook (gba/src/texhook.c).
@
@ 2. The scanline edge table is built in two passes into rows of exactly the
@    size they need (gba/src/edge.c), instead of fixed-stride rows grown by
@    1.5x until nothing overflows. The row-pointer helpers are rewritten for
@    the packed layout: [offsets: rows x u32][row: cap u16, count u16, entries]
@    where a row pointer addresses its count and entries follow at +2.

.equ sub_2dcc, 0x2dcc
.equ loc_35a0, 0x35a0
.equ loc_1888, 0x1888

@ ---- textures ----------------------------------------------------------

@ sub_2d38, plain 2x doubling: r0 = the buffer just allocated
@@ at 0x2d70 len 4
    b       p_2d70
@@ append
p_2d70:
    cmp     r0, #0x08000000
    bhs     sub_2dcc            @ ROM: return it as is
    cmp     r0, #0
    b       0x2d74

@ sub_34a0, smoothed doubling: r0 = the plain-doubled buffer
@@ at 0x3508 len 4
    b       p_3508
@@ append
p_3508:
    cmp     r0, #0x08000000
    bhs     loc_35a0
    cmp     r0, #0
    b       0x350c

@ sub_232c, table colourise in place: r1 = destination
@@ at 0x232c len 4
    b       p_232c
@@ append
p_232c:
    cmp     r1, #0x08000000
    bxhs    lr
    push    {r4, lr}
    b       0x2330

@ sub_25c4, error-diffusion colourise: r1 = destination
@@ at 0x25c4 len 4
    b       p_25c4
@@ append
p_25c4:
    cmp     r1, #0x08000000
    bxhs    lr
    push    {r4, r5, r6, r7, r8, fp, ip, lr, pc}
    b       0x25c8

@ mipmap generators: destination in r0 (sub_37d4, sub_3900) or r1 (sub_3640)
@@ at 0x37d4 len 4
    b       p_37d4
@@ append
p_37d4:
    cmp     r0, #0x08000000
    bxhs    lr
    mov     ip, sp
    b       0x37d8
@@ at 0x3900 len 4
    b       p_3900
@@ append
p_3900:
    cmp     r0, #0x08000000
    bxhs    lr
    mov     ip, sp
    b       0x3904
@@ at 0x3640 len 4
    b       p_3640
@@ append
p_3640:
    cmp     r1, #0x08000000
    bxhs    lr
    mov     ip, sp
    b       0x3644

@ ---- edge table --------------------------------------------------------

@ sub_1840: after the row range is known, let the runtime size and allocate
@ the table (ctx in r6; the table header is ctx+0x458c).
@@ at 0x1988 len 4
    b       p_edge
@@ append
p_edge:
    mov     r0, r6
    bl      hook_edge
    b       loc_1888
hook_edge:                      @ PACE syscall group 1, offset 0xff8
    ldr     ip, [sb, #-12]
    ldr     pc, [ip, #0xff8]

@ sub_106c, insert (x=r1, row=r2, id=r3) into the row: in the counting pass
@ the header's stride word holds a per-row counter array instead.
@@ at 0x106c len 4
    b       p_ins
@@ append
p_ins:
    ldr     ip, [r0, #8]
    cmp     ip, #0
    beq     p_ins_real
    lsl     r2, r2, #16
    asr     r2, r2, #16
    ldr     r3, [r0]
    cmp     r2, r3
    bxlt    lr
    ldr     r3, [r0, #4]
    cmp     r2, r3
    bxgt    lr
    ldr     r3, [r0]
    sub     r2, r2, r3
    ldr     r3, [ip, r2, lsl #2]
    add     r3, r3, #1
    str     r3, [ip, r2, lsl #2]
    bx      lr
p_ins_real:
    mov     ip, sp
    b       0x1070

@ sub_1a88(table, row): pointer to a row's count word
@@ at 0x1a88 len 0x30
    ldr     r3, [r0, #0x10]
    lsl     r1, r1, #16
    asr     r1, r1, #16
    ldr     r2, [r3, r1, lsl #2]
    add     r0, r3, r2
    bx      lr

@ sub_1abc(table, rowptr): pointer to the next row
@@ at 0x1abc len 0x10
    ldrh    r3, [r1, #-2]
    add     r1, r1, r3, lsl #2
    add     r0, r1, #4
    bx      lr

@ Walkers that start from the table base need the first row instead, and
@ have nothing to walk during the counting pass (stride word != 0).
@@ at 0xba0 len 4
    b       p_b70
@@ append
p_b70:
    ldr     r0, [lr, r0]
    add     r1, lr, #0x4500
    ldr     r1, [r1, #0x94]
    cmp     r1, #0
    movne   r0, #0
    cmp     r0, #0
    ldrne   r1, [r0]
    addne   r0, r0, r1
    b       0xba4
@@ at 0xc88 len 4
    b       p_c88
@@ append
p_c88:
    ldr     r0, [ip, r0]
    add     lr, ip, #0x4500
    ldr     lr, [lr, #0x94]
    cmp     lr, #0
    movne   r0, #0
    cmp     r0, #0
    ldrne   lr, [r0]
    addne   r0, r0, lr
    b       0xc8c
@@ at 0x1624 len 4
    b       p_1624
@@ append
p_1624:
    ldr     r8, [r0, #0x10]
    ldr     r1, [r0, #8]
    cmp     r1, #0
    movne   r8, #0
    cmp     r8, #0
    ldrne   r1, [r8]
    addne   r8, r8, r1
    b       0x1628

@ sub_2b64, the two-texture blend written a row at a time: r0 = destination,
@ r3 = texels; returns the advanced destination.
@@ at 0x2b64 len 4
    b       p_2b64
@@ append
p_2b64:
    cmp     r0, #0x08000000
    addhs   r0, r0, r3
    bxhs    lr
    mov     ip, sp
    b       0x2b68

@ The MemMove stub: the level parser reads its data four bytes at a time
@ through it, tens of thousands of times per level. Do the small cases here.
@@ at 0x0f4e0 len 8
    b       p_memmove
@@ append
p_memmove:
    cmp     r2, #4
    bne     1f
    ldrb    r3, [r1]
    strb    r3, [r0]
    ldrb    r3, [r1, #1]
    strb    r3, [r0, #1]
    ldrb    r3, [r1, #2]
    strb    r3, [r0, #2]
    ldrb    r3, [r1, #3]
    strb    r3, [r0, #3]
    mov     r0, #0
    bx      lr
1:  cmp     r2, #2
    bne     2f
    ldrb    r3, [r1]
    strb    r3, [r0]
    ldrb    r3, [r1, #1]
    strb    r3, [r0, #1]
    mov     r0, #0
    bx      lr
2:  ldr     ip, [sb, #-8]
    ldr     pc, [ip, #0x558]
