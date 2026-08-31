/*++

Copyright (c) 2026  Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    super.c

Abstract:

    EVRYFS superblock cache and volume mount / auto-format.

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only

--*/

#include "evryfsp.h"

uint8_t g_SuperBuf[512];
uint8_t g_DirBuf [512];
int     g_FsPresent = 0;

EVRYFS_SUPER* EvryFsSuper(void)
{
    return (EVRYFS_SUPER*)g_SuperBuf;
}

int EvryFsInit(void)
{
    g_FsPresent = 0;

    if (AtaReadSector(EVRYFS_SUPER_LBA, g_SuperBuf) < 0)
        return -1;

    EVRYFS_SUPER* super = EvryFsSuper();

    if (super->magic != EVRYFS_MAGIC) {
        /* fresh disk -- format in place */
        for (int i = 0; i < 512; i++) g_SuperBuf[i] = 0;
        for (int i = 0; i < 512; i++) g_DirBuf [i] = 0;

        super->magic         = EVRYFS_MAGIC;
        super->version       = EVRYFS_VERSION;
        super->next_free_lba = EVRYFS_DATA_START;

        if (AtaWriteSector(EVRYFS_SUPER_LBA, g_SuperBuf) < 0) return -1;
        if (AtaWriteSector(EVRYFS_DIR_LBA,   g_DirBuf)   < 0) return -1;
    } else {
        if (AtaReadSector(EVRYFS_DIR_LBA, g_DirBuf) < 0) return -1;
    }

    g_FsPresent = 1;
    return 0;
}
