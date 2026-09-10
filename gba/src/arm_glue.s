@ The ARM-mode side of running the game's armc code natively: entering it the
@ way PACE does, the PACE syscall tables it reaches through r9, and the
@ Call68K pointer it is handed. The rest is C (arm.c), which is Thumb, so
@ every crossing goes through bx.
    .arm
    .text
    .align 2

@ uint32_t pce_call(uint32_t code, uint32_t userData, uint32_t emulState, uint32_t call68k, uint32_t r9)
    .global pce_call
    .type pce_call, %function
pce_call:
    stmfd   sp!, {r4-r11, lr}
    ldr     r9, [sp, #36]           @ the PACE syscall master table (fifth argument)
    mov     r12, r0
    mov     r0, r2                  @ emulStateP
    mov     r2, r3                  @ call68KFuncP
    mov     lr, pc
    bx      r12
    ldmfd   sp!, {r4-r11, lr}
    bx      lr

@ The Call68K pointer: ARM state on entry, then straight into the C function.
    .global call68k_arm
    .type call68k_arm, %function
call68k_arm:
    ldr     r12, =call68K_func
    orr     r12, r12, #1
    bx      r12

@ Every syscall table entry points at one of these; the stub identifies itself
@ by its own address and the common code turns that into a group and function.
    .global arm_syscall_common
    .type arm_syscall_common, %function
arm_syscall_common:
    stmfd   sp!, {r4-r11, lr}
    ldr     r4, =arm_syscall_lr
    str     lr, [r4]
    ldr     r4, =arm_syscall_regs      @ the caller's r4-r11 and lr, for the allocation hook
    str     sp, [r4]
    mov     r4, sp
    add     r5, sp, #36             @ the caller's stack pointer
    bic     sp, sp, #7
    sub     sp, sp, #8
    str     r3, [sp, #0]
    str     r5, [sp, #4]
    ldr     r5, =syscall_stubs
    sub     r12, r12, r5
    mov     r3, r2
    mov     r2, r1
    mov     r1, r0
    mov     r0, r12, lsr #3
    ldr     r12, =arm_syscall_dispatch
    orr     r12, r12, #1
    mov     lr, pc
    bx      r12
    mov     sp, r4
    ldmfd   sp!, {r4-r11, lr}
    bx      lr
    .pool

    .align 3
    .global syscall_stubs
syscall_stubs:
    .rept 3072
    sub     r12, pc, #8
    b       arm_syscall_common
    .endr

    .bss
    .align 2
    .global arm_syscall_lr
arm_syscall_lr:
    .space 4
    .global arm_syscall_regs
arm_syscall_regs:
    .space 4

    .section .rodata
    .align 2
    .global syscall_tables
syscall_tables:
    .set i, 0
    .rept 3072
    .word   syscall_stubs + i * 8
    .set i, i + 1
    .endr
