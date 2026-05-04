;++
;
; Copyright (c) 2026  The EverywhereOS Authors
;
; Module Name:
;
;     ixclock.asm
;
; Abstract:
;
;     Interval clock (8254 PIT channel 0) IRQ0 interrupt service routine
;     stub. Saves the full general-purpose register set, transfers control
;     to the C-level HalpClockIsr handler, sends EOI to the master 8259
;     PIC, and returns to the interrupted context.
;
; Author:
;
;     Noah Juopperi <nipfswd@gmail.com>
;
; Environment:
;
;     Kernel-mode only (HAL, IRQL CLOCK_LEVEL)
;
;--

[BITS 32]

section .text
global Irq0Stub
extern HalpClockIsr

;++
;
; Routine Description:
;
;     IRQ0 (PIT channel 0) interrupt stub. Preserves all general-purpose
;     registers across the C handler call. Sends EOI only to the master
;     PIC because IRQ0 is not cascaded through the slave.
;
;--

Irq0Stub:
    pushad

    call HalpClockIsr

    ; EOI to master PIC only (IRQ0 lives on master)
    mov al, 0x20
    out 0x20, al

    popad
    iretd
