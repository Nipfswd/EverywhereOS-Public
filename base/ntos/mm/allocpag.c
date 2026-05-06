/*++

Copyright (c) 2026  The EverywhereOS Authors. All Rights Reserved.

Module Name:

    allocpag.c

Abstract:

    Non-paged pool allocation and deallocation.

    Implements MmAllocatePool and MmFreePool using an address-sorted,
    doubly-linked free list with immediate coalescing of physically
    adjacent free blocks.

    Each pool block is prefixed by an MM_POOL_HEADER (defined in mm.h).
    Allocated blocks carry MM_POOL_TAG_ALLOC in the Magic field; free
    blocks carry MM_POOL_TAG_FREE.  The FreeNext and FreePrev fields of
    live allocations are overwritten with MM_POOL_POISON so that
    use-after-free writes are detectable at the next MmFreePool call.

    On every MmFreePool call the freed block is coalesced forward with its
    next physical neighbour if that neighbour is free, then backward with
    its previous physical neighbour under the same condition.  PrevBlockSize
    in all affected adjacent blocks is updated to keep backward navigation
    consistent.

    MmQueryPoolStats returns a snapshot of total pool size and current free
    bytes for diagnostic use.

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only

--*/

#include "mi.h"

/*++

Routine Description:

    Rounds Value up to the nearest multiple of Align.
    Align must be a non-zero power of two.

Arguments:

    Value - Value to round.
    Align - Alignment boundary (power of two).

Return Value:

    Rounded value.

--*/

static uint32_t
MiAlignUp(
    uint32_t Value,
    uint32_t Align
    )
{
    return (Value + Align - 1) & ~(Align - 1);
}

/*++

Routine Description:

    Inserts Block into the address-sorted free list.  The block is placed
    immediately before the first existing free entry whose address is
    strictly greater than Block's address, preserving ascending order.

Arguments:

    Block - Block to insert.  Must not already be on the free list.

Return Value:

    None.

--*/

static void
MiInsertFreeBlock(
    PMM_POOL_HEADER Block
    )
{
    PMM_POOL_HEADER pos = MiPoolFreeListHead.FreeNext;

    while (pos != &MiPoolFreeListHead && pos < Block) {
        pos = pos->FreeNext;
    }

    Block->FreeNext         = pos;
    Block->FreePrev         = pos->FreePrev;
    pos->FreePrev->FreeNext = Block;
    pos->FreePrev           = Block;
}

/*++

Routine Description:

    Removes Block from the free list and writes MM_POOL_POISON into its
    FreeNext and FreePrev fields so that a subsequent double-free attempt
    is caught during the Magic check in MmFreePool.

Arguments:

    Block - Block currently linked into the free list.

Return Value:

    None.

--*/

static void
MiRemoveFreeBlock(
    PMM_POOL_HEADER Block
    )
{
    Block->FreePrev->FreeNext = Block->FreeNext;
    Block->FreeNext->FreePrev = Block->FreePrev;
    Block->FreeNext           = (PMM_POOL_HEADER)(uintptr_t)MM_POOL_POISON;
    Block->FreePrev           = (PMM_POOL_HEADER)(uintptr_t)MM_POOL_POISON;
}

/*++

Routine Description:

    Allocates NumberOfBytes bytes from the non-paged pool.

    The payload size is rounded up to MM_POOL_GRANULARITY and a header is
    prepended, giving the total block size.  The free list is walked from
    the lowest-address entry (first-fit) until a block large enough is
    found.

    If the chosen block has enough remaining space after the allocation to
    form a valid new block (at least MM_POOL_MIN_BLOCK bytes), it is split:
    the tail portion becomes a new free block inserted in the free list
    immediately after the allocated block, and the PrevBlockSize of the
    block physically following the tail is updated.

    The selected block is removed from the free list, its Magic is set to
    MM_POOL_TAG_ALLOC, and a pointer past the header is returned.

Arguments:

    NumberOfBytes - Usable bytes requested.  Must be greater than zero.

    Tag - Four-byte caller tag stored in the block header.

Return Value:

    Pointer to the first usable byte of the allocation, or NULL if the
    pool has no sufficiently large free block.

--*/

void *
MmAllocatePool(
    uint32_t NumberOfBytes,
    uint32_t Tag
    )
{
    uint32_t        payload;
    uint32_t        needed;
    uint32_t        leftover;
    PMM_POOL_HEADER block;
    PMM_POOL_HEADER split;
    PMM_POOL_HEADER next_phys;

    if (NumberOfBytes == 0) {
        return (void *)0;
    }

    payload = MiAlignUp(NumberOfBytes, MM_POOL_GRANULARITY);
    needed  = MM_POOL_HEADER_SIZE + payload;

    block = MiPoolFreeListHead.FreeNext;

    while (block != &MiPoolFreeListHead) {

        if (block->BlockSize >= needed) {
            leftover = block->BlockSize - needed;

            if (leftover >= MM_POOL_MIN_BLOCK) {
                /*
                 * Split the block.  The tail becomes a new free block
                 * inserted right after the current block in the free
                 * list -- this keeps address order intact since split
                 * is physically above block.
                 */
                split                = (PMM_POOL_HEADER)((uint8_t *)block + needed);
                split->Magic         = MM_POOL_TAG_FREE;
                split->BlockSize     = leftover;
                split->PrevBlockSize = needed;
                split->Tag           = 0;
                split->FreeNext      = block->FreeNext;
                split->FreePrev      = block;
                block->FreeNext->FreePrev = split;
                block->FreeNext           = split;

                block->BlockSize = needed;

                /* Fix PrevBlockSize of the block physically after split. */
                next_phys = (PMM_POOL_HEADER)((uint8_t *)split + leftover);
                if ((uint8_t *)next_phys < MiPoolEnd) {
                    next_phys->PrevBlockSize = leftover;
                }
            }

            /* Remove block from free list and mark allocated. */
            MiRemoveFreeBlock(block);
            block->Magic = MM_POOL_TAG_ALLOC;
            block->Tag   = Tag;
            MmPoolFreeBytes -= block->BlockSize;

            return (void *)((uint8_t *)block + MM_POOL_HEADER_SIZE);
        }

        block = block->FreeNext;
    }

    return (void *)0;
}

/*++

Routine Description:

    Returns a pool block previously obtained from MmAllocatePool back to
    the free pool, with immediate coalescing of adjacent free blocks.

    The embedded MM_POOL_HEADER is located by subtracting MM_POOL_HEADER_SIZE
    from BaseAddress.  If Magic does not equal MM_POOL_TAG_ALLOC, the system
    spins (halts) to prevent silent data corruption from a double-free or
    a stale pointer.

    Coalescing sequence:
      1. Mark the block free and credit MmPoolFreeBytes.
      2. Forward coalesce: if the physically next block is free, merge it
         into the current block and remove it from the free list.
      3. Backward coalesce: if the physically previous block is free,
         grow it to absorb the current block, remove it from the free list,
         then re-insert it (to update its size in the list) and return.
      4. If no backward coalesce occurred, insert the current block into
         the free list.

    PrevBlockSize in the first block beyond the merged region is updated
    after each coalesce step to reflect the new preceding block size.

Arguments:

    BaseAddress - Pointer returned by a prior MmAllocatePool call.
                  NULL is accepted as a safe no-op.

Return Value:

    None.

--*/

void
MmFreePool(
    void *BaseAddress
    )
{
    PMM_POOL_HEADER hdr;
    PMM_POOL_HEADER next_phys;
    PMM_POOL_HEADER prev_phys;
    PMM_POOL_HEADER after_merge;

    if (!BaseAddress) {
        return;
    }

    hdr = (PMM_POOL_HEADER)((uint8_t *)BaseAddress - MM_POOL_HEADER_SIZE);

    if (hdr->Magic != MM_POOL_TAG_ALLOC) {
        //
        // The block header does not carry the expected allocated-block
        // sentinel.  This indicates one of:
        //   - a double-free (Magic == MM_POOL_TAG_FREE)
        //   - a write through a stale pointer that corrupted MM_POOL_POISON
        //   - a caller that passed an address not returned by MmAllocatePool
        //
        // Emit the corrupted magic value to COM1 so the fault is visible
        // even on a headless system, then halt.
        //
        KeBugCheckEx(0x19u, (uint32_t)(uintptr_t)hdr, hdr->Magic, 0, 0);
    }

    hdr->Magic = MM_POOL_TAG_FREE;
    MmPoolFreeBytes += hdr->BlockSize;

    /* Forward coalesce. */
    next_phys = (PMM_POOL_HEADER)((uint8_t *)hdr + hdr->BlockSize);
    if ((uint8_t *)next_phys < MiPoolEnd && next_phys->Magic == MM_POOL_TAG_FREE) {
        hdr->BlockSize += next_phys->BlockSize;
        MiRemoveFreeBlock(next_phys);

        after_merge = (PMM_POOL_HEADER)((uint8_t *)hdr + hdr->BlockSize);
        if ((uint8_t *)after_merge < MiPoolEnd) {
            after_merge->PrevBlockSize = hdr->BlockSize;
        }
    }

    /* Backward coalesce. */
    if (hdr->PrevBlockSize != 0) {
        prev_phys = (PMM_POOL_HEADER)((uint8_t *)hdr - hdr->PrevBlockSize);

        if (prev_phys->Magic == MM_POOL_TAG_FREE) {
            prev_phys->BlockSize += hdr->BlockSize;
            MiRemoveFreeBlock(prev_phys);

            after_merge = (PMM_POOL_HEADER)((uint8_t *)prev_phys + prev_phys->BlockSize);
            if ((uint8_t *)after_merge < MiPoolEnd) {
                after_merge->PrevBlockSize = prev_phys->BlockSize;
            }

            MiInsertFreeBlock(prev_phys);
            return;
        }
    }

    MiInsertFreeBlock(hdr);
}

/*++

Routine Description:

    Returns a snapshot of pool statistics.

Arguments:

    TotalBytes - Receives the total number of bytes under pool management.
                 May be NULL.

    FreeBytes  - Receives the number of bytes currently free.
                 May be NULL.

Return Value:

    None.

--*/

void
MmQueryPoolStats(
    uint32_t *TotalBytes,
    uint32_t *FreeBytes
    )
{
    if (TotalBytes) {
        *TotalBytes = MmPoolTotalBytes;
    }
    if (FreeBytes) {
        *FreeBytes = MmPoolFreeBytes;
    }
}
