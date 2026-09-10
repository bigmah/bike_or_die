@ Cartridge header and startup for the Bike or Die 2 GBA port.
@ The Nintendo logo and the header complement are written by fixrom.py.
    .section .crt0, "ax"
    .arm
    .align 2
    .global _start
_start:
    b       rom_start
    .space  156                     @ Nintendo logo
    .ascii  "BIKE OR DIE2"          @ title, 12 bytes
    .ascii  "BOD2"                  @ game code
    .ascii  "00"                    @ maker code
    .byte   0x96                    @ fixed value
    .byte   0x00                    @ main unit code
    .byte   0x00                    @ device type
    .space  7                       @ reserved
    .byte   0x00                    @ software version
    .byte   0x00                    @ complement check
    .space  2                       @ reserved

rom_start:
    mov     r0, #0x12               @ IRQ mode stack
    msr     cpsr_c, r0
    ldr     sp, =0x03007FA0
    mov     r0, #0x1F               @ System mode, interrupts off
    msr     cpsr_c, r0
    ldr     sp, =0x03007F00

    mov     r0, #0x04000000         @ IME = 0
    add     r0, r0, #0x208
    mov     r1, #0
    str     r1, [r0]

    ldr     r0, =__iwram_lma        @ .iwram: ROM -> IWRAM
    ldr     r1, =__iwram_start
    ldr     r2, =__iwram_end
    bl      copy_words
    ldr     r0, =__data_lma         @ .data: ROM -> EWRAM
    ldr     r1, =__data_start
    ldr     r2, =__data_end
    bl      copy_words
    ldr     r1, =__bss_start        @ .bss, .iwram_bss: zero
    ldr     r2, =__bss_end
    bl      zero_words
    ldr     r1, =__iwram_bss_start
    ldr     r2, =__iwram_bss_end
    bl      zero_words

    ldr     r0, =irq_handler        @ BIOS interrupt vector
    ldr     r1, =0x03007FFC
    str     r0, [r1]

    ldr     r0, =main
    mov     lr, pc
    bx      r0
1:  b       1b

copy_words:                         @ r0 = src, r1 = dst, r2 = dst end
    cmp     r1, r2
    ldrlo   r3, [r0], #4
    strlo   r3, [r1], #4
    blo     copy_words
    bx      lr
zero_words:                         @ r1 = dst, r2 = dst end
    mov     r0, #0
1:  cmp     r1, r2
    strlo   r0, [r1], #4
    blo     1b
    bx      lr
    .pool

@ Interrupt handler: acknowledge, count, and note which sources fired.
@ Runs in IRQ mode from IWRAM; the BIOS has already saved what it needs.
    .section .iwram, "ax"
    .arm
    .align 2
    .global irq_handler
irq_handler:
    mov     r0, #0x04000000
    add     r0, r0, #0x200
    ldr     r1, [r0]                @ IE | IF << 16
    and     r1, r1, r1, lsr #16     @ IE & IF
    strh    r1, [r0, #2]            @ acknowledge REG_IF
    ldr     r2, =0x03007FF8         @ acknowledge for the BIOS (IntrWait)
    ldrh    r3, [r2]
    orr     r3, r3, r1
    strh    r3, [r2]
    ldr     r2, =irq_seen
    ldr     r3, [r2]
    orr     r3, r3, r1
    str     r3, [r2]
    tst     r1, #1                  @ VBlank
    ldrne   r2, =vblank_count
    ldrne   r3, [r2]
    addne   r3, r3, #1
    strne   r3, [r2]
    ldr     r2, =irq_hook
    ldr     r2, [r2]
    cmp     r2, #0
    bxeq    lr
    stmfd   sp!, {r1, lr}
    mov     r0, r1
    mov     lr, pc
    bx      r2
    ldmfd   sp!, {r1, lr}
    bx      lr
    .pool

    .section .iwram_bss, "aw", %nobits
    .align 2
    .global irq_seen, vblank_count, irq_hook
irq_seen:     .space 4
vblank_count: .space 4
irq_hook:     .space 4
