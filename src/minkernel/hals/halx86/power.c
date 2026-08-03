/*++

Copyright (C) Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    power.c
    
Abstract:

    System power control. Reboot via the 8042 kbd controller.
    
Author:

    NoahJ <nipfswd@gmail.com>
    
Environment:

    HAL
    
--*/

// Prototype in here
#include "ke.h"

/*++

Routine Description:

    Attempts to reboot the machine via the keyboard controller.
    
Arguments:

    None.
    
Return Value:

    None.
    
--*/

// Reboot the System!
void RebootSystem(void) {
    while (inb(0x64) & 0x02) { }
    outb(0x64, 0xFE);
    for (;;) { }
}