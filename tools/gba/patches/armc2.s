@ Patches to `armc` 2, the renderer half of the Bike or Die 2 engine, for
@ the GBA port. See armc3.s for the why.
@
@ Textures are grey images in ROM; each level texture slot has a pair of
@ 256-entry lookup tables in IWRAM at 0x03000400 + slot*768, laid out
@ [A][B][A] so that a pointer to A or B can be advanced by 256 for the
@ other. A texel at (u,v) is tinted through A when u+v is even and B when
@ odd, which bakes a checkerboard dither into textures whose tint came from
@ error diffusion on the Palm. The slot is the texture's index in the
@ level's texture table (144-byte entries), recovered from the entry
@ pointer with a reciprocal multiply.

.macro PAIR
    mov     r3, r0, asr #16
    and     r3, r3, r4
    ldrb    r2, [r1, r3]
    tst     r0, #0x10000
    addne   r2, r2, #256
    ldrb    r2, [r8, r2]
    orr     r2, r2, r2, lsl #8
    strh    r2, [r5], #2
    add     r0, r0, lr
.endm

.equ loc_421c, 0x421c
.equ sub_4220, 0x4220

@ sub_17c4(table, row): pointer to a row's count word, packed layout
@@ at 0x17c4 len 0x34
    ldr     r3, [r0, #0x10]
    lsl     r1, r1, #16
    asr     r1, r1, #16
    ldr     r2, [r3, r1, lsl #2]
    add     r0, r3, r2
    bx      lr

@ Whole functions the renderer spends its time in run from IWRAM as well,
@ relocated by tools/gba/hotmove.py: the span dispatcher, the scanline
@ walker, the sprite blitters, the line drawers and their helpers.
@@ move 0xab0 0xc90
@@ move 0x4138 0x4388
@@ move 0x4700 0x4f2c
@@ move 0xa6c4 0xacbc
@@ move 0xc214 0xc748
@@ move 0xe718 0xf3e0

@ The samplers run from IWRAM (gba/src/engine.c copies them there): the
@ texel reads from ROM would otherwise break the cartridge prefetch and cost
@ a non-sequential fetch for every instruction after them.
@
@ Scaled sampler (mipmaps, zoomed rendering): r7 = level struct, r6 =
@ renderer, r4 = u mask, r5 = destination, ip = destination end.
@@ at 0x41dc len 8
    ldr     pc, [pc, #-4]
    .word   p_s1
@@ iwram 0x03002800
p_s1:
    ldr     r1, [r6, #0xf18]
    ldr     r3, [r7, #0xc]
    ldr     r0, [r7, #0x18]
    ldr     r2, [r7, #0x10]
    and     r1, r1, r3              @ v
    ldr     r8, [r6, #4]
    sub     r8, r7, r8
    mov     r3, #0x1c00
    add     r3, r3, #0x72           @ 7282/2^20 ~ 1/144
    mul     r8, r3, r8
    mov     r8, r8, lsr #20         @ slot
    add     r8, r8, r8, lsl #1
    mov     r3, #0x03000000
    add     r3, r3, #0x400
    add     r8, r3, r8, lsl #8      @ slot * 768
    tst     r1, #1
    addne   r8, r8, #256            @ odd v: B for even u
    add     r1, r0, r1, lsl r2      @ row
    ldr     lr, [r6, #0xf20]        @ u step
    ldr     r0, [r6, #0xf0c]        @ u
    cmp     r5, ip
    ldrhs   pc, rel_421c
    tst     r5, #1                  @ an odd first pixel keeps the pairs aligned
    beq     1f
    mov     r3, r0, asr #16
    and     r3, r3, r4
    ldrb    r2, [r1, r3]
    tst     r0, #0x10000
    addne   r2, r2, #256
    ldrb    r2, [r8, r2]
    strb    r2, [r5], #1
    add     r0, r0, lr
    cmp     r5, ip
    ldrhs   pc, rel_421c
1:  sub     r3, ip, r5
    cmp     r3, #2
    blt     3f
    mov     lr, lr, lsl #1          @ one texel serves two pixels
    cmp     r3, #4
    blt     25f
2:  PAIR
    PAIR
    add     r3, r5, #3
    cmp     r3, ip
    blo     2b
    add     r3, r5, #1
    cmp     r3, ip
    bhs     26f
25: PAIR
26: mov     lr, lr, lsr #1
    cmp     r5, ip
    ldrhs   pc, rel_421c
3:  mov     r3, r0, asr #16         @ a last single pixel
    and     r3, r3, r4
    ldrb    r2, [r1, r3]
    tst     r0, #0x10000
    addne   r2, r2, #256
    ldrb    r2, [r8, r2]
    strb    r2, [r5], #1
    add     r0, r0, lr
    ldr     pc, rel_421c
rel_421c: .word 0x421c
rel_4220: .word 0x4220

@ 1:1 span copy: r7 = level struct, r6 = renderer, r8 = texel count,
@ r5 = destination, [sp] = row width, [sp+4] = x offset.
@@ at 0x426c len 8
    ldr     pc, [pc, #-4]
    .word   p_s2
@@ iwram
p_s2:
    ldr     r2, [r6, #0xf0c]
    ldr     r3, [r6, #0xf20]
    mla     r4, r3, r8, r2
    str     r4, [r6, #0xf0c]
    ldr     ip, [r7, #0xc]
    ldr     r1, [r6, #0xf1c]
    ldr     r3, [r6, #0xf14]
    and     r1, r1, ip              @ v
    ldr     ip, [sp, #4]
    ldr     lr, [r7, #8]
    ldr     r0, [r7, #0x10]
    ldr     r2, [r7, #0x18]
    add     r3, r3, ip
    add     r5, r5, ip
    ldr     r4, [r6, #4]
    sub     r4, r7, r4
    mov     ip, #0x1c00
    add     ip, ip, #0x72
    mul     r4, ip, r4
    mov     r4, r4, lsr #20         @ slot
    add     r4, r4, r4, lsl #1
    mov     ip, #0x03000000
    add     ip, ip, #0x400
    add     r4, ip, r4, lsl #8
    tst     r1, #1
    addne   r4, r4, #256            @ table for even u on this row
    add     r1, r2, r1, lsl r0      @ row
    and     r2, r3, lr              @ u
    and     lr, r2, #1
    add     lr, r4, lr, lsl #8      @ table for the current texel
    eor     ip, lr, #256            @ ... and the next one
    add     r1, r1, r2              @ source
    ldr     r3, [sp]
    rsb     r2, r2, r3              @ texels left in this row
2:  cmp     r8, r2
    blt     5f
    sub     r8, r8, r2
    tst     r2, #1
    beq     3f
    ldrb    r3, [r1], #1
    ldrb    r3, [lr, r3]
    strb    r3, [r5], #1
    eor     lr, lr, #256
    eor     ip, ip, #256
3:  movs    r0, r2, asr #1
    beq     4f
31: ldrb    r3, [r1], #1
    ldrb    r3, [lr, r3]
    strb    r3, [r5], #1
    ldrb    r3, [r1], #1
    ldrb    r3, [ip, r3]
    strb    r3, [r5], #1
    subs    r0, r0, #1
    bne     31b
4:  ldr     r2, [r7]                @ wrap to the row start
    sub     r1, r1, r2
    mov     lr, r4
    eor     ip, lr, #256
    b       2b
5:  tst     r8, #1
    beq     6f
    ldrb    r3, [r1], #1
    ldrb    r3, [lr, r3]
    strb    r3, [r5], #1
    eor     lr, lr, #256
    eor     ip, ip, #256
6:  movs    r0, r8, asr #1
    ldreq   pc, rel_4220
61: ldrb    r3, [r1], #1
    ldrb    r3, [lr, r3]
    strb    r3, [r5], #1
    ldrb    r3, [r1], #1
    ldrb    r3, [ip, r3]
    strb    r3, [r5], #1
    subs    r0, r0, #1
    bne     61b
    ldr     pc, rel_4220

@ sub_b84 clears a buffer a byte at a time; the runtime's MemSet does it in words.
@@ at 0xc00 len 0x14
    ldr     r3, [r0, #4]
    mul     r1, lr, r3
    ldr     r0, [r0, #8]
    bl      0xf254
    b       0xbc8

@ The MemMove stub: the level parser reads its data four bytes at a time
@ through it, tens of thousands of times per level. Do the small cases here.
@@ at 0x0f234 len 8
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
