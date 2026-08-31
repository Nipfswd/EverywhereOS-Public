/*++

Copyright (c) 2026  Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    allocsup.c

Abstract:

    EVRYFS data-LBA allocation. No free list -- space is handed out
    sequentially and never reclaimed.

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only

--*/

#include "evryfsp.h"

uint32_t EvryFsAllocSectors(int sectors)
{
    EVRYFS_SUPER* s = EvryFsSuper();
    uint32_t start_lba = s->next_free_lba;
    s->next_free_lba  += (uint32_t)sectors;
    return start_lba;
}
