/*++

Copyright (c) 2026  Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    video.c

Abstract:

    VGA Mode 13h framebuffer, double buffering, and drawing primitives.

Author:

    Noah Juopperi <nipfswd@gmail.com>
    Clay Sanders (made the first version of the kernel) <claylikepython@yahoo.com>

Environment:

    Kernel-mode only

--*/

#include "ke.h"

uint8_t* FB = (uint8_t*)0xA0000;
uint8_t backbuf[SCR_W * SCR_H];

/*++

Routine Description:

    Copies the back buffer to the VGA framebuffer in one operation,
    eliminating visible flicker.

Arguments:

    None.

Return Value:

    None.

--*/

void FlipBuffers(void) {
    uint32_t* dst = (uint32_t*)FB;
    uint32_t* src = (uint32_t*)backbuf;
    int count = (SCR_W * SCR_H) / 4;
    for (int i = 0; i < count; i++) {
        dst[i] = src[i];
    }
}

/*++

Routine Description:

    Switches the video adapter to VGA Mode 13h (320x200x256 color).

Arguments:

    None.

Return Value:

    None.

--*/

/*++

Routine Description:

    Reads the framebuffer address from the multiboot info structure
    provided by GRUB. Falls back to 0xA0000 if unavailable.

Arguments:

    mbi - Pointer to the multiboot info structure.

Return Value:

    None.

--*/

void SetupFramebuffer(uint32_t* mbi) {
    if (!mbi) return;

    uint32_t flags = mbi[0];

    /* Bit 12 = framebuffer info available */
    if (flags & (1 << 12)) {
        /* framebuffer_addr is at byte offset 88 (uint64_t) */
        uint32_t fb_lo = mbi[22]; /* offset 88 / 4 */
        FB = (uint8_t*)(uintptr_t)fb_lo;
    }
}

/*++

Routine Description:

    Sets a single pixel in the back buffer.

Arguments:

    x - X coordinate.
    y - Y coordinate.
    c - Color index.

Return Value:

    None.

--*/

void PutPixel(int x, int y, uint8_t c) {
    if (x < 0 || x >= SCR_W || y < 0 || y >= SCR_H) return;
    backbuf[y * SCR_W + x] = c;
}

/*++

Routine Description:

    Fills a rectangular region with a solid color.

Arguments:

    x - Left edge.
    y - Top edge.
    w - Width in pixels.
    h - Height in pixels.
    c - Color index.

Return Value:

    None.

--*/

void FillRect(int x, int y, int w, int h, uint8_t c) {
    for (int yy = 0; yy < h; yy++) {
        int py = y + yy;
        if (py < 0 || py >= SCR_H) continue;
        for (int xx = 0; xx < w; xx++) {
            int px = x + xx;
            if (px < 0 || px >= SCR_W) continue;
            backbuf[py * SCR_W + px] = c;
        }
    }
}
