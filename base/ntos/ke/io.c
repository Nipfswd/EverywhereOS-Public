/*++

Copyright (c) 2026  Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    io.c

Abstract:

    System reboot via keyboard controller.

Author:

    Noah Juopperi <nipfswd@gmail.com>
    Clay Sanders (made the first version of the kernel) <claylikepython@yahoo.com>

Environment:

    Kernel-mode only

--*/

#include "ke.h"

/*++

Routine Description:

    Attempts to reboot the machine via the keyboard controller.

Arguments:

    None.

Return Value:

    None. Does not return on success.

--*/

void RebootSystem(void) {
    while (inb(0x64) & 0x02) { }
    outb(0x64, 0xFE);
    for (;;) { }
}
