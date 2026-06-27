/*++

Copyright (c) Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    kbdclass.h
    
Abstract:

    Public header for the keyboard class driver

Author:

    Noah Juopperi <nipfswd@gmail.com>
    
Environment:

    Kernel-mode only.
    
--*/

#ifndef _KBDCLASS_H_
#define _KBDCLASS_H_

#include <stdint.h>

/* /// Convential on-disk identity*/
#define KBDCLASS_SYS_NAME   "KBDCLASS.SYS"
#define KBDCLASS_SYS_PATH   "C:\\Everywhere\\System32\\drivers\\kdbclass.sys"

// Statically-linked build API

extern int      shift_pressed;
extern uint8_t  last_scancode;

char KbdClassReadInput(void);

#endif /* _KBDCLASS_H_*/