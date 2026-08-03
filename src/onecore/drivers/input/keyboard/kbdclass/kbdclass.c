/*++

Copyright (c) Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    kbdclass.c
    
Abstract:

    Keyboard class driver.

Author:

    Noah Juopperi <nipfswd@gmail.com>
    
Environment:

    Kernel-mode only.
    
--*/

#include "kbdclass.h"

#ifdef KBDCLASS_STANDALONE
#include "drvload.h"

//
// Standalone build, no ke.h, so inb/outb are defined locally.
// Keep these byte for byte identical to ke.h's so behavior
// never diverges between the two build modes.
//
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
#else
#include "ke.h"
#endif

int     shift_pressed = 0;
uint8_t last_scancode = 0;

/*++

Routine Description:

    Reads a single keypress from the keyboard controller, trans-
    lating scancodes to ASCII
    
Arguments:

    None.
    
Return Value:

    ACII character, '\n' for enter, 0x08 for BACKSPACE, 27 for ESC,
    or 0 if no key available.
    
--*/

char KbdClassReadInput(void) {
    if (!(inb(0x64) & 1)) return 0;

    uint8_t status = inb(0x64);

    // Bit 5 set means this byte is from the mouse
    if (status & 0x20) {
        inb(0x60); // discard mouse byte here. UpdateMouse got us covered
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

#ifdef KBDCLASS_STANDALONE
/*++

Routine Description:

    Sole exported entry point when kbdclass.c is built standalone
    
Arguments:

    Imports - kernel-supplied services. Unused today
    Exports - output parameter

Return Value:

    0 on success. Nonzero causes the loader to bugcheck
    
--*/
int KbdClassDriverEntry(const KBDCLASS_IMPORTS* Imports, KBDCLASS_EXPORTS* Exports*) {
    (void)Imports;

    Exports->ReadInput = KbdClassReadInput;
    return 0;
}
#endif
