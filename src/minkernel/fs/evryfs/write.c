/*++

Copyright (c) 2026  Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    write.c

Abstract:

    EVRYFS file write path.

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only

--*/

#include "evryfsp.h"

int EvryFsWriteFile(const char* name, const uint8_t* data, int len)
{
    if (!g_FsPresent) return -1;

    int slot = EvryFsFindSlot(name);
    if (slot < 0) return -1;

    EVRYFS_DIRENT* d = EvryFsDirEntry(slot);

    int      sectors   = (len + 511) / 512;
    uint32_t start_lba = EvryFsAllocSectors(sectors);

    uint8_t sector_buf[512];
    for (int sec = 0; sec < sectors; sec++) {
        for (int b = 0; b < 512; b++) sector_buf[b] = 0;
        int offset = sec * 512;
        int chunk  = len - offset;
        if (chunk > 512) chunk = 512;
        for (int b = 0; b < chunk; b++) sector_buf[b] = data[offset + b];
        if (AtaWriteSector(start_lba + (uint32_t)sec, sector_buf) < 0)
            return -1;
    }

    EvryFsStrCpy(d->name, name, EVRYFS_NAME_LEN);
    d->flags     = 1;
    d->start_lba = start_lba;
    d->size      = (uint32_t)len;

    if (AtaWriteSector(EVRYFS_DIR_LBA,   g_DirBuf)   < 0) return -1;
    if (AtaWriteSector(EVRYFS_SUPER_LBA, g_SuperBuf) < 0) return -1;

    return 0;
}
