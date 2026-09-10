@ Pixel-double a half-size frame into the screen, two source bytes at a
@ time (the source rows sit two bytes past a word boundary): each byte
@ becomes a halfword, each row is written twice. Runs from IWRAM.
@ void bod_copy2x(const uint8_t *src, uint32_t srcRow, uint8_t *dst, uint32_t dstRow, uint32_t w, uint32_t h)
    .section .iwram, "ax"
    .arm
    .align 2
    .global bod_copy2x
    .type bod_copy2x, %function
bod_copy2x:
    stmfd   sp!, {r4-r9, lr}
    ldr     r8, [sp, #28]           @ w (bytes per source row, even)
    ldr     r9, [sp, #32]           @ h
    mov     r8, r8, lsr #1          @ halfwords per source row
1:  mov     r4, r0                  @ source row
    mov     r5, r2                  @ destination row
    add     r6, r2, r3              @ ... and the one below it
    mov     lr, r8
2:  ldrh    r7, [r4], #2            @ b1 b0
    and     ip, r7, #0xFF
    orr     ip, ip, ip, lsl #8      @ b0 b0
    and     r7, r7, #0xFF00
    orr     ip, ip, r7, lsl #8
    orr     ip, ip, r7, lsl #16     @ b1 b1 b0 b0
    str     ip, [r5], #4
    str     ip, [r6], #4
    subs    lr, lr, #1
    bne     2b
    add     r0, r0, r1
    add     r2, r2, r3, lsl #1
    subs    r9, r9, #1
    bne     1b
    ldmfd   sp!, {r4-r9, lr}
    bx      lr
