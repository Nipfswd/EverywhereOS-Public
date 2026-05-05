/*++

Copyright (c) 2026  The EverywhereOS Authors. All Rights Reserved.

Module Name:

    mi.h

Abstract:

    Memory Manager private declarations.  Included only by MM source
    files (mminit.c, allocpag.c).  Not to be included by modules outside
    base/ntos/mm.

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only

--*/

#ifndef _MI_H_
#define _MI_H_

#include "mm.h"

/*
 * Bounds of the pool arena.  Set once by MiInitPool; read by the
 * allocator on every call to MmAllocatePool and MmFreePool.
 */
extern uint8_t *MiPoolBase;
extern uint8_t *MiPoolEnd;

/*
 * MiPoolFreeListHead
 *
 * Sentinel node for the address-sorted, doubly-linked free list.
 * Its Magic field is 0 so it can never be mistaken for a real block.
 * The invariant is:
 *
 *   Head.FreeNext -> lowest-address free block -> ... -> Head
 *   Head.FreePrev -> highest-address free block -> ... -> Head
 */
extern MM_POOL_HEADER MiPoolFreeListHead;

/*
 * MiInitPool
 *
 * Initialises pool state over [Base, Base+Size).  The entire region is
 * presented as a single free block.  Base must be aligned to
 * MM_POOL_GRANULARITY; Size must be >= MM_POOL_MIN_BLOCK.
 */
void MiInitPool(uint8_t *Base, uint32_t Size);

#endif /* _MI_H_ */
