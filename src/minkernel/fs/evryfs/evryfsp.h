/*++

Copyright (c) 2026  Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    evryfsp.h

Abstract:

    Private, cross-module declarations for the EVRYFS driver (super.c,
    dirsup.c, allocsup.c, read.c, write.c, strsup.c). Not part of the
    public API -- see evryfs.h for that.

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only

--*/

#ifndef _EVRYFSP_H_
#define _EVRYFSP_H_

#include "evryfs.h"

extern uint8_t g_SuperBuf[512];
extern uint8_t g_DirBuf [512];
extern int     g_FsPresent;

EVRYFS_SUPER*  EvryFsSuper(void);
EVRYFS_DIRENT* EvryFsDirEntry(int index);

int  EvryFsFindEntry(const char* name);
int  EvryFsFindSlot(const char* name);

uint32_t EvryFsAllocSectors(int sectors);

int  EvryFsStrEq(const char* a, const char* b);
void EvryFsStrCpy(char* dst, const char* src, int n);

#endif /* _EVRYFSP_H_ */
