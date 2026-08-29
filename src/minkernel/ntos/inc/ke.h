/*++

Copyright (c) 2026  Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    ke.h

Abstract:

    Kernel executive header. Core types, I/O primitives, VGA framebuffer,
    font, keyboard, window manager, and physics declarations. Pulls in
    the mouse class driver's public header for mouse declarations.

Author:

    Noah Juopperi <nipfswd@gmail.com>
    Clay Sanders (made the first version of the kernel) <claylikepython@yahoo.com>

Environment:

    Kernel-mode only

--*/

#ifndef _KE_H_
#define _KE_H_

#include <stdint.h>
#include "mm.h"

/* ********** Basic I/O ********** */

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* ********** Reboot ********** */

void RebootSystem(void);

/* ********** VGA ********** */

#define SCR_W 640
#define SCR_H 480

extern uint8_t* FB;
extern uint8_t backbuf[];

void FlipBuffers(void);
void SetupFramebuffer(uint32_t* mbi);
void PutPixel(int x, int y, uint8_t c);
void FillRect(int x, int y, int w, int h, uint8_t c);

/* ********** Font ********** */

extern uint8_t Font8x8[128][8];

void InitFont(void);
void DrawChar(int x, int y, char ch, uint8_t color);
void DrawString(int x, int y, const char* s, uint8_t color);

/* ********** Keyboard Input Routing ********** */

void HandleKeyboardInput(char ch);

/* ********** Window ********** */

typedef struct {
    int x, y, w, h;
    int vx, vy;
    const char* title;
    int visible;
    int minimized;
    int dragging;
    int drag_off_x;
    int drag_off_y;
    int fullscreen;
    int prev_x, prev_y, prev_w, prev_h;
} WINDOW;

int  PointInRect(int x, int y, int rx, int ry, int rw, int rh);
void DrawWindowFrame(WINDOW* w);
void UpdateWindowPhysics(WINDOW* w);
void HandleWindowMouse(WINDOW* w, int win_id);

extern int active_window;

/* ********** Mouse **********
 * Declarations now live in the mouse class driver's public header;
 * see src/onecore/drivers/input/mouse/mouclass/mouclass.h
 */

#include "mouclass.h"

/* ********** HAL ********** */

void     HalInitInterrupts(void);
void     HalEndOfInterrupt(uint8_t irq);
uint32_t HalQueryTickCount(void);
void     HalStallExecution(uint32_t Milliseconds);

/* ********** Kernel Time ********** */

uint32_t KernelGetTickCount(void);

/* ********** Keyboard ********** */
// new driver

#include "kbdclass.h"

/* ********** Utility ********** */

int StrEq(const char* a, const char* b);

/* ********** Serial Debug Transport (COM1) ********** */

#define KD_COM1_BASE    0x3F8u

void     KdComPortInitialize(void);
void     KdComPortWriteString(const char *String);

extern uint32_t KiBugCheckData[5];

/* ********** Kernel Bug-Check ********** */

void     KeBugCheck(uint32_t BugCheckCode);
void     KeBugCheckEx(uint32_t BugCheckCode,
                      uint32_t Parameter1,
                      uint32_t Parameter2,
                      uint32_t Parameter3,
                      uint32_t Parameter4);

/*
 * ASSERT -- active only in debug builds (DBG defined).
 * On failure raises bug-check 0x7F (UNEXPECTED_KERNEL_MODE_TRAP),
 * which writes the STOP line to COM1 and halts.
 */
#if DBG
#define ASSERT(exp) \
    do { \
        if (!(exp)) { \
            KeBugCheckEx(0x7Fu, 0, 0, 0, 0); \
        } \
    } while (0)
#else
#define ASSERT(exp) ((void)(exp))
#endif

#endif /* _KE_H_ */