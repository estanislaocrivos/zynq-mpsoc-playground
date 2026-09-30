/*
 * Minimal Cortex-R5 startup: vector table, per-mode stacks, VFP enable,
 * .bss clear and jump to main. No MPU, no caches, no interrupts.
 */

    .syntax unified
    .arm

/* CPSR mode encodings (ARMv7-R ARM, B1.3.1) */
    .equ MODE_FIQ, 0x11
    .equ MODE_IRQ, 0x12
    .equ MODE_SVC, 0x13
    .equ MODE_ABT, 0x17
    .equ MODE_UND, 0x1B
    .equ MODE_SYS, 0x1F

/* ------------------------------------------------------------------------- */
/* Vector table: placed at 0x00000000 (ATCM) by the linker script.           */
/* Each entry is one ARM instruction; all targets live in ATCM, so a plain   */
/* B reaches them.                                                           */
/* ------------------------------------------------------------------------- */
    .section .vectors, "ax"
    .global _vector_table
_vector_table:
    b   _boot               /* 0x00 reset                  */
    b   undef_handler       /* 0x04 undefined instruction  */
    b   svc_handler         /* 0x08 supervisor call        */
    b   prefetch_handler    /* 0x0C prefetch abort         */
    b   data_handler        /* 0x10 data abort             */
    b   .                   /* 0x14 reserved               */
    b   irq_handler         /* 0x18 IRQ                    */
    b   fiq_handler         /* 0x1C FIQ                    */

/* ------------------------------------------------------------------------- */
/* Reset handler                                                             */
/* ------------------------------------------------------------------------- */
    .section .boot, "ax"
    .global _boot
    .type   _boot, %function
_boot:
    /* 1. One stack per exception mode. Out of reset the core is in SVC with
     *    IRQ/FIQ masked; CPS only changes the mode, the masks stay set. */
    cps     #MODE_SVC
    ldr     sp, =__supervisor_stack
    cps     #MODE_IRQ
    ldr     sp, =__irq_stack
    cps     #MODE_FIQ
    ldr     sp, =__fiq_stack
    cps     #MODE_ABT
    ldr     sp, =__abort_stack
    cps     #MODE_UND
    ldr     sp, =__undef_stack

    /* 2. main runs in System mode (privileged, shares SP/LR with User). */
    cps     #MODE_SYS
    ldr     sp, =_stack

    /* Enable the VFP: the toolchain builds with -mfloat-abi=hard, so the
     * compiler may emit VFP instructions, which are undefined while the
     * FPU is off. Grant CP10/CP11 full access in CPACR, then set FPEXC.EN. */
    mrc     p15, 0, r0, c1, c0, 2
    orr     r0, r0, #(0xF << 20)
    mcr     p15, 0, r0, c1, c0, 2
    isb
    mov     r0, #(1 << 30)
    vmsr    fpexc, r0

    /* 3. Zero .bss (both bounds are 4-byte aligned by the linker script). */
    ldr     r0, =__bss_start__
    ldr     r1, =__bss_end__
    mov     r2, #0
bss_loop:
    cmp     r0, r1
    strlo   r2, [r0], #4
    blo     bss_loop

    /* 4. Enter C. */
    bl      main

    /* 5. main should never return; park the core if it does. */
hang:
    b       hang

    .ltorg                  /* literal pool for the ldr =symbol above */
    .size   _boot, . - _boot

/* ------------------------------------------------------------------------- */
/* Placeholder exception handlers: park the core so a debugger shows which   */
/* exception was taken (check the PC and the banked LR).                     */
/* ------------------------------------------------------------------------- */
undef_handler:      b   .
svc_handler:        b   .
prefetch_handler:   b   .
data_handler:       b   .
irq_handler:        b   .
fiq_handler:        b   .
