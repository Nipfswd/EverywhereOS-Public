/*++

Copyright (c) 2026  The EverywhereOS Authors

Module Name:

    ke.h

Abstract:

    Kernel executive header. Core types, I/O primitives, VGA framebuffer,
    font, mouse, keyboard, window manager, and physics declarations.

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

/* ********** Mouse ********** */

extern int mouse_x;
extern int mouse_y;
extern int mouse_buttons;
extern int mouse_prev_buttons;

void InitMouse(void);
void UpdateMouse(void);
void DrawMouseCursor(void);
void MouseIsr(void);

/* ********** HAL ********** */

void     HalInitInterrupts(void);
void     HalEndOfInterrupt(uint8_t irq);
uint32_t HalQueryTickCount(void);
void     HalStallExecution(uint32_t Milliseconds);

/* ********** Keyboard ********** */

extern int     shift_pressed;
extern uint8_t last_scancode;

char GetKeyChar(void);

/* ********** Utility ********** */

int StrEq(const char* a, const char* b);

#endif /* _KE_H_ */
