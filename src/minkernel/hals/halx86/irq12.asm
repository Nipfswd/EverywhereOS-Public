;++
;
; Copyright (c) 2026  Everywhere Computing, Inc. All Rights Reserved.
;
; Module Name:
;
;     irq12.asm
;
; Abstract:
;
;     IRQ12 (PS/2 mouse) interrupt service routine stub.
;     Saves registers, calls the C handler, sends EOI, and returns.
;
; Author:
;
;     Noah Juopperi <nipfswd@gmail.com>
;     Clay Sanders (made the first version of the kernel) <claylikepython@yahoo.com>
;
; Environment:
;
;     Kernel-mode only (HAL)
;
;--

[BITS 32]

section .text
global Irq12Stub
global IrqIgnoreStub
extern MouseIsr

; Default handler for unregistered IRQs - just sends EOI and returns
IrqIgnoreStub:
    pushad
    mov al, 0x20
    out 0xA0, al
    out 0x20, al
    popad
    iretd

Irq12Stub:
    pushad

    call MouseIsr

    ; Send EOI to slave PIC (IRQ12 is on slave)
    mov al, 0x20
    out 0xA0, al
    out 0x20, al

    popad
    iretd
