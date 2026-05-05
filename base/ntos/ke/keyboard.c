/*++

Copyright (c) 2026  The EverywhereOS Authors. All Rights Reserved.

Module Name:

    keyboard.c

Abstract:

    Keyboard scancode translation and input handling.

Author:

    Noah Juopperi <nipfswd@gmail.com>
    Clay Sanders (made the first version of the kernel) <claylikepython@yahoo.com>

Environment:

    Kernel-mode only

--*/

#include "ke.h"

int shift_pressed = 0;
uint8_t last_scancode = 0;

/*++

Routine Description:

    Reads a single keypress from the keyboard controller, translating
    scancodes to ASCII. Handles shift state.

Arguments:

    None.

Return Value:

    ASCII character, '\n' for Enter, 0x08 for Backspace, 27 for ESC,
    or 0 if no key is available.

--*/

char GetKeyChar(void) {
    if (!(inb(0x64) & 1)) return 0;

    uint8_t status = inb(0x64);

    /* Bit 5 set means this byte is from the mouse, not the keyboard */
    if (status & 0x20) {
        inb(0x60); /* discard mouse byte here; UpdateMouse handles it */
        return 0;
    }

    uint8_t sc = inb(0x60);
    last_scancode = sc;

    if (sc == 0x2A || sc == 0x36) { shift_pressed = 1; return 0; }
    if (sc == 0xAA || sc == 0xB6) { shift_pressed = 0; return 0; }

    if (sc & 0x80) return 0;

    if (sc == 0x1C) return '\n';
    if (sc == 0x0E) return 0x08;
    if (sc == 0x01) return 27;

    static char Lower[] = {
        0,0,'1','2','3','4','5','6','7','8','9','0','-','=',0,0,
        'q','w','e','r','t','y','u','i','o','p','[',']',0,0,
        'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
        'z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
    };

    static char Upper[] = {
        0,0,'!','@','#','$','%','^','&','*','(',')','_','+',0,0,
        'Q','W','E','R','T','Y','U','I','O','P','{','}',0,0,
        'A','S','D','F','G','H','J','K','L',':','\"','~',0,'|',
        'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' '
    };

    if (sc < sizeof(Lower)) {
        return shift_pressed ? Upper[sc] : Lower[sc];
    }

    return 0;
}
