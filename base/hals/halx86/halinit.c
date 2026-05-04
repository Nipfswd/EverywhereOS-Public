/*++

Copyright (c) 2026  The EverywhereOS Authors

Module Name:

    halinit.c

Abstract:

    Hardware Abstraction Layer initialization. Remaps the 8259 PIC,
    sets up the IDT, and installs hardware interrupt handlers.

Author:

    Noah Juopperi <nipfswd@gmail.com>
    Clay Sanders (made the first version of the kernel) <claylikepython@yahoo.com>

Environment:

    Kernel-mode only (HAL)

--*/

#include "ke.h"

/*
// 8254 PIT channel 0 port addresses and programming constants.
// Channel 0 drives IRQ0 at the configured tick rate.
//
// PERFORMANCE_FREQUENCY is the native 8254 input frequency (1.193182 MHz).
// HALP_CLOCK_RATE_HZ is our desired interrupt rate (100 Hz = 10 ms per tick).
// The divisor loaded into the PIT is PERFORMANCE_FREQUENCY / HALP_CLOCK_RATE_HZ.
*/

#define HALP_PIT_DATA_PORT0       0x40
#define HALP_PIT_CONTROL_PORT     0x43
#define HALP_PIT_CMD_COUNTER0     0x00
#define HALP_PIT_CMD_RW_16BIT     0x30
#define HALP_PIT_CMD_MODE2        0x04
#define HALP_PIT_CMD_BINARY       0x00
#define HALP_PERFORMANCE_FREQ     1193182UL
#define HALP_CLOCK_RATE_HZ        100
#define HALP_CLOCK_DIVISOR        (HALP_PERFORMANCE_FREQ / HALP_CLOCK_RATE_HZ)

/*
// HalpTickCount is the raw count of IRQ0 clock ticks since PIT
// initialization.  It is incremented by HalpClockIsr on every tick and
// read by HalQueryTickCount / HalStallExecution.
//
// volatile so the compiler never caches a stale read in a polling loop.
*/

static volatile uint32_t HalpTickCount = 0;

/* IDT gate descriptor */
struct idt_entry {
    uint16_t offset_lo;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_hi;
} __attribute__((packed));

/* IDT pointer for LIDT */
struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr   idtp;

/*++

Routine Description:

    Sets a single IDT gate entry.

Arguments:

    num       - Interrupt vector number (0-255).
    handler   - Address of the ISR stub.
    selector  - Code segment selector (0x08 for flat 32-bit).
    type_attr - Type and attributes (0x8E = 32-bit interrupt gate, ring 0).

Return Value:

    None.

--*/

static void IdtSetGate(uint8_t num, uint32_t handler, uint16_t selector, uint8_t type_attr) {
    idt[num].offset_lo = handler & 0xFFFF;
    idt[num].offset_hi = (handler >> 16) & 0xFFFF;
    idt[num].selector  = selector;
    idt[num].zero      = 0;
    idt[num].type_attr = type_attr;
}

/*++

Routine Description:

    Remaps the 8259 PIC so that IRQ 0-7 map to vectors 0x20-0x27
    and IRQ 8-15 map to vectors 0x28-0x2F, preventing collision
    with CPU exception vectors (0x00-0x1F).

Arguments:

    None.

Return Value:

    None.

--*/

static void PicRemap(void) {
    uint8_t mask1 = inb(0x21);
    uint8_t mask2 = inb(0xA1);

    outb(0x20, 0x11);
    outb(0xA0, 0x11);

    outb(0x21, 0x20);
    outb(0xA1, 0x28);

    outb(0x21, 0x04);
    outb(0xA1, 0x02);

    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    outb(0x21, mask1);
    outb(0xA1, mask2);
}

/*++

Routine Description:

    Sends End-Of-Interrupt to the PIC(s) for the given IRQ.

Arguments:

    irq - IRQ number (0-15).

Return Value:

    None.

--*/

/*++

Routine Description:

    Programs 8254 PIT channel 0 to fire IRQ0 at HALP_CLOCK_RATE_HZ using
    mode 2 (rate generator).  The 16-bit divisor is written LSB-first,
    MSB-second per Intel 8254 protocol.

Arguments:

    None.

Return Value:

    None.

--*/

static void HalpInitializePit(void) {
    uint16_t divisor = (uint16_t)HALP_CLOCK_DIVISOR;

    outb(HALP_PIT_CONTROL_PORT,
         HALP_PIT_CMD_COUNTER0 |
         HALP_PIT_CMD_RW_16BIT |
         HALP_PIT_CMD_MODE2    |
         HALP_PIT_CMD_BINARY);

    outb(HALP_PIT_DATA_PORT0, (uint8_t)(divisor & 0xFF));
    outb(HALP_PIT_DATA_PORT0, (uint8_t)((divisor >> 8) & 0xFF));
}

/*++

Routine Description:

    C-level IRQ0 clock interrupt service routine.  Called from the
    Irq0Stub assembly thunk on every PIT tick.  Increments the global
    tick counter.  EOI is issued by the assembly stub after this
    function returns.

Arguments:

    None.

Return Value:

    None.

Environment:

    Kernel-mode only.  Interrupts are disabled on entry (interrupt gate).

--*/

void HalpClockIsr(void) {
    HalpTickCount++;
}

/*++

Routine Description:

    Returns the current raw tick count since PIT initialization.
    Each tick represents 1000 / HALP_CLOCK_RATE_HZ milliseconds
    (10 ms at 100 Hz).

Arguments:

    None.

Return Value:

    Current tick count (uint32_t).

--*/

uint32_t HalQueryTickCount(void) {
    return HalpTickCount;
}

/*++

Routine Description:

    Busy-waits for at least the requested number of milliseconds by
    polling the PIT-driven tick counter.  Because HalpTickCount advances
    at HALP_CLOCK_RATE_HZ ticks per second, one tick equals
    1000/HALP_CLOCK_RATE_HZ ms.  We over-approximate by waiting an extra
    tick to guarantee the full interval has elapsed regardless of where
    in the current tick period we start.

Arguments:

    Milliseconds - Minimum number of milliseconds to stall.

Return Value:

    None.

Environment:

    Caller must be running with interrupts enabled so the tick ISR fires.

--*/

void HalStallExecution(uint32_t Milliseconds) {
    uint32_t TicksNeeded;
    uint32_t Deadline;

    //
    // Convert the requested millisecond count to ticks, rounding up.
    // The +1 adds a guard tick to cover partial periods at both ends.
    //
    TicksNeeded = (Milliseconds * HALP_CLOCK_RATE_HZ + 999) / 1000 + 1;
    Deadline    = HalpTickCount + TicksNeeded;

    while (HalpTickCount < Deadline) {
        __asm__ __volatile__("pause");
    }
}

void HalEndOfInterrupt(uint8_t irq) {
    if (irq >= 8) {
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);
}

/*++

Routine Description:

    Initializes the HAL: remaps the PIC, sets up the IDT with the
    mouse IRQ12 handler, unmasks IRQ2 (cascade) and IRQ12, loads
    the IDT, and enables hardware interrupts.

Arguments:

    None.

Return Value:

    None.

--*/

void HalInitInterrupts(void) {
    extern void Irq0Stub(void);
    extern void Irq12Stub(void);
    extern void IrqIgnoreStub(void);

    /* Install a default ignore handler for all 256 vectors
       so any unexpected interrupt won't triple-fault */
    for (int i = 0; i < 256; i++) {
        IdtSetGate(i, (uint32_t)IrqIgnoreStub, 0x08, 0x8E);
    }

    PicRemap();

    /* Mask ALL IRQs first, then only unmask what we need */
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);

    /* Install clock ISR (IRQ0 = vector 0x20) */
    IdtSetGate(0x20, (uint32_t)Irq0Stub, 0x08, 0x8E);

    /* Install mouse ISR (IRQ12 = vector 0x2C) */
    IdtSetGate(0x2C, (uint32_t)Irq12Stub, 0x08, 0x8E);

    /* Load IDT */
    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (uint32_t)&idt;
    __asm__ __volatile__("lidt %0" : : "m"(idtp));

    /* Program the PIT before unmasking IRQ0 so the first tick lands clean */
    HalpInitializePit();

    /* Unmask IRQ0 (clock), IRQ2 (cascade), and IRQ12 (mouse) */
    outb(0x21, 0xFA);   /* master: IRQ0 and IRQ2 unmasked (bit 0 and bit 2 clear) */
    outb(0xA1, 0xEF);   /* slave:  only IRQ12 unmasked */

    /* Enable interrupts */
    __asm__ __volatile__("sti");
}
