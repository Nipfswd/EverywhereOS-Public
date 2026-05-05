/*++

Copyright (c) 2026  The EverywhereOS Authors. All Rights Reserved.

Module Name:

    mouse.c

Abstract:

    PS/2 mouse initialization, polling, and cursor drawing.

Author:

    Noah Juopperi <nipfswd@gmail.com>
    Clay Sanders (made the first version of the kernel) <claylikepython@yahoo.com>

Environment:

    Kernel-mode only

--*/

#include "ke.h"

int mouse_x = SCR_W / 2;
int mouse_y = SCR_H / 2;
int mouse_buttons = 0;
int mouse_prev_buttons = 0;

/*++

Routine Description:

    Waits for the PS/2 controller to be ready for input or output.

Arguments:

    type - 0 to wait for output buffer full, 1 to wait for input buffer empty.

Return Value:

    None.

--*/

void MouseWait(uint8_t type) {
    uint32_t timeout = 100000;
    if (type == 0) {
        while (timeout--) {
            if (inb(0x64) & 1) return;
        }
    } else {
        while (timeout--) {
            if (!(inb(0x64) & 2)) return;
        }
    }
}

/*++

Routine Description:

    Sends a byte to the PS/2 mouse device.

Arguments:

    data - Byte to send.

Return Value:

    None.

--*/

void MouseWrite(uint8_t data) {
    MouseWait(1);
    outb(0x64, 0xD4);
    MouseWait(1);
    outb(0x60, data);
}

/*++

Routine Description:

    Reads a byte from the PS/2 mouse device.

Arguments:

    None.

Return Value:

    Byte read from the mouse.

--*/

uint8_t MouseRead(void) {
    MouseWait(0);
    return inb(0x60);
}

/*++

Routine Description:

    Initializes the PS/2 mouse by enabling the auxiliary device,
    setting defaults, and enabling data reporting.

Arguments:

    None.

Return Value:

    None.

--*/

void InitMouse(void) {
    uint8_t status;

    /* Enable auxiliary PS/2 device */
    outb(0x64, 0xA8);
    MouseWait(1);
    outb(0x64, 0x20);
    MouseWait(0);
    status = inb(0x60) | 2;
    MouseWait(1);
    outb(0x64, 0x60);
    MouseWait(1);
    outb(0x60, status);

    /* Reset to defaults */
    MouseWrite(0xF6);
    MouseRead();

    /* Set 1:1 scaling (no acceleration) */
    MouseWrite(0xE6);
    MouseRead();

    /* Set resolution: 8 counts per mm (highest) */
    MouseWrite(0xE8);
    MouseRead();
    MouseWrite(0x03);
    MouseRead();

    /* Set sample rate: 200 samples/sec */
    MouseWrite(0xF3);
    MouseRead();
    MouseWrite(200);
    MouseRead();

    /* Enable data reporting */
    MouseWrite(0xF4);
    MouseRead();

    /* Flush any leftover bytes in the PS/2 output buffer */
    while (inb(0x64) & 1) {
        inb(0x60);
    }
}

/*++

Routine Description:

    Polls the PS/2 controller for a complete 3-byte mouse packet and
    updates the global mouse position and button state.

Arguments:

    None.

Return Value:

    None.

--*/

void UpdateMouse(void) {
    /* Mouse position is now updated by the IRQ12 ISR (MouseIsr).
       Nothing to poll here. */
    (void)0;
}

/*++

Routine Description:

    IRQ12 interrupt service routine. Called by the HAL stub whenever
    the PS/2 mouse sends a byte. Assembles 3-byte packets and updates
    the global mouse position and button state immediately.

Arguments:

    None.

Return Value:

    None.

--*/

void MouseIsr(void) {
    uint8_t status = inb(0x64);
    if (!(status & 1)) return;
    if (!(status & 0x20)) return;

    static uint8_t packet[3];
    static int idx = 0;

    packet[idx++] = inb(0x60);
    if (idx < 3) return;
    idx = 0;

    if (!(packet[0] & 0x08)) return;
    if (packet[0] & 0xC0) return;

    int dx = (int8_t)packet[1];
    int dy = (int8_t)packet[2];

    mouse_prev_buttons = mouse_buttons;
    mouse_buttons = packet[0] & 0x07;

    mouse_x += dx;
    mouse_y -= dy;

    if (mouse_x < 0) mouse_x = 0;
    if (mouse_x >= SCR_W) mouse_x = SCR_W - 1;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_y >= SCR_H) mouse_y = SCR_H - 1;
}

/*++

Routine Description:

    Draws a small triangular mouse cursor at the current mouse position.

Arguments:

    None.

Return Value:

    None.

--*/

void DrawMouseCursor(void) {
    /*
     * 16x16 arrow cursor inspired by classic desktop cursors.
     * 0 = transparent, 1 = black outline, 2 = white fill.
     */
    static const uint8_t cursor[16][16] = {
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,2,2,1,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,2,2,2,1,0,0,0,0,0,0,0,0,0,0,0},
        {1,2,2,2,2,1,0,0,0,0,0,0,0,0,0,0},
        {1,2,2,2,2,2,1,0,0,0,0,0,0,0,0,0},
        {1,2,2,2,2,2,2,1,0,0,0,0,0,0,0,0},
        {1,2,2,2,2,2,2,2,1,0,0,0,0,0,0,0},
        {1,2,2,2,2,2,2,2,2,1,0,0,0,0,0,0},
        {1,2,2,2,2,2,2,2,2,2,1,0,0,0,0,0},
        {1,2,2,2,2,2,2,1,1,1,1,0,0,0,0,0},
        {1,2,2,2,1,2,2,1,0,0,0,0,0,0,0,0},
        {1,2,2,1,0,1,2,2,1,0,0,0,0,0,0,0},
        {1,2,1,0,0,1,2,2,1,0,0,0,0,0,0,0},
        {1,1,0,0,0,0,1,1,0,0,0,0,0,0,0,0},
    };
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            if (cursor[y][x] == 1)
                PutPixel(mouse_x + x, mouse_y + y, 0x00);
            else if (cursor[y][x] == 2)
                PutPixel(mouse_x + x, mouse_y + y, 0x0F);
        }
    }
}
