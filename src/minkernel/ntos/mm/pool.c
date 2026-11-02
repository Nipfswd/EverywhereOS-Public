/*++

Copyright (c) 2026  Everywhere-Computing, Inc. All Rights Reserved.

Module Name:

    pool.c

Abstract:

    Self-contained non-paged pool allocator.

Author(s):

    Noah Juopperi <nipfswd@gmail.com>
    Vlad Lymar <ggcc98765432110@gmail.com>

Environment:

    Kernel-mode only

--*/

#include "../inc/mm.h"
#include "mi.h"

//
// Pool block type identifiers
//
#define POOL_TYPE_FREE      0x46524545 // 'FREE'
#define POOL_TYPE_ALLOCATED 0x414C4C4F // 'ALLO'

static ULONG_PTR MiPoolStart = 0;
static ULONG_PTR MiPoolEnd   = 0;

//
// Helper: Round up block size and align
//
static ULONG
MiRoundBlockSize(
    ULONG NumberOfBytes
    )
{
    ULONG TotalSize = NumberOfBytes + MM_POOL_HEADER_SIZE;
    if (TotalSize % 8 != 0) {
        TotalSize = ((TotalSize + 7) & ~7);
    }
    if (TotalSize < MM_POOL_MIN_BLOCK) {
        TotalSize = MM_POOL_MIN_BLOCK;
    }
    return TotalSize;
}

//
// Helper: Get free list bucket index
//
static ULONG
MiPoolIndexFromSize(
    ULONG BlockBytes
    )
{
    ULONG Index = BlockBytes / 8;
    if (Index > POOL_SMALL_LISTS) {
        Index = POOL_SMALL_LISTS;
    }
    return Index;
}

//
// Initialize Pool
//
VOID
MiInitializeNonPagedPool(
    ULONG_PTR PoolBase,
    ULONG     PoolSize
    )
{
    PPOOL_HEADER InitialBlock;
    ULONG        AlignedSize;
    ULONG        Index;
    ULONG        i;

    for (i = 0; i <= POOL_SMALL_LISTS; i++) {
        InitializeListHead(&MiNonPagedPoolFreeListHead[i]);
    }

    MiPoolStart = PoolBase;
    MiPoolEnd   = PoolBase + PoolSize;
    AlignedSize = PoolSize & ~7;

    if (AlignedSize < MM_POOL_MIN_BLOCK) {
        KeBugCheckEx(MEMORY_MANAGEMENT, 0x31, PoolBase, PoolSize, 0);
    }

    InitialBlock = (PPOOL_HEADER)PoolBase;
    InitialBlock->PreviousSize = 0;
    InitialBlock->BlockSize    = AlignedSize;
    InitialBlock->PoolType     = POOL_TYPE_FREE;
    InitialBlock->PoolTag      = 0;

    Index = MiPoolIndexFromSize(AlignedSize);
    InsertHeadList(&MiNonPagedPoolFreeListHead[Index], (PLIST_ENTRY)&InitialBlock->FreeNext);
}

//
// Allocate Pool Memory
//
PVOID
MmAllocatePool(
    MM_POOL_TYPE PoolType,
    ULONG        NumberOfBytes,
    ULONG        Tag
    )
{
    ULONG        TotalSize;
    ULONG        ScanIndex;
    ULONG        FoundIndex = 0;
    PLIST_ENTRY  ListEntry;
    PPOOL_HEADER Block;
    PPOOL_HEADER RemainingBlock;
    ULONG        RemainingSize;
    ULONG        RemIndex;

    (VOID)PoolType;

    if (NumberOfBytes == 0 || Tag == 0) {
        return NULL;
    }

    TotalSize = MiRoundBlockSize(NumberOfBytes);

    LOCK_POOL();

    // Find a suitable block from small lists or overflow
    for (ScanIndex = MiPoolIndexFromSize(TotalSize); ScanIndex <= POOL_SMALL_LISTS; ScanIndex++) {
        if (!IsListEmpty(&MiNonPagedPoolFreeListHead[ScanIndex])) {
            FoundIndex = ScanIndex;
            break;
        }
    }

    if (FoundIndex == 0 || IsListEmpty(&MiNonPagedPoolFreeListHead[FoundIndex])) {
        UNLOCK_POOL();
        return NULL;
    }

    ListEntry = MiNonPagedPoolFreeListHead[FoundIndex].Flink;
    RemoveEntryList(ListEntry);

    Block = CONTAINING_RECORD(ListEntry, POOL_HEADER, FreeNext);

    // Sanity check
    if ((ULONG_PTR)Block < MiPoolStart || (ULONG_PTR)Block >= MiPoolEnd || Block->PoolType != POOL_TYPE_FREE) {
        UNLOCK_POOL();
        KeBugCheckEx(BAD_POOL_HEADER, 1, (ULONG_PTR)Block, 0, 0);
    }

    // Check if block can be split
    RemainingSize = Block->BlockSize - TotalSize;
    if (RemainingSize >= MM_POOL_MIN_BLOCK) {
        RemainingBlock = (PPOOL_HEADER)((PUCHAR)Block + TotalSize);
        RemainingBlock->PreviousSize = TotalSize;
        RemainingBlock->BlockSize    = RemainingSize;
        RemainingBlock->PoolType     = POOL_TYPE_FREE;
        RemainingBlock->PoolTag      = 0;

        Block->BlockSize = TotalSize;

        // Update previous size for the block after the remainder (if within pool)
        PPOOL_HEADER NextAfterRem = (PPOOL_HEADER)((PUCHAR)RemainingBlock + RemainingSize);
        if ((ULONG_PTR)NextAfterRem < MiPoolEnd) {
            NextAfterRem->PreviousSize = RemainingSize;
        }

        RemIndex = MiPoolIndexFromSize(RemainingSize);
        InsertHeadList(&MiNonPagedPoolFreeListHead[RemIndex], (PLIST_ENTRY)&RemainingBlock->FreeNext);
    } else {
        // Take entire block, update next block's PreviousSize indicator if needed
        PPOOL_HEADER NextBlock = (PPOOL_HEADER)((PUCHAR)Block + Block->BlockSize);
        if ((ULONG_PTR)NextBlock < MiPoolEnd) {
            NextBlock->PreviousSize = Block->BlockSize;
        }
    }

    Block->PoolType = POOL_TYPE_ALLOCATED;
    Block->PoolTag  = Tag;

    UNLOCK_POOL();

    return (PVOID)((PUCHAR)Block + MM_POOL_HEADER_SIZE);
}

//
// Free Pool Memory with FULL Coalescing (Prevents fragmentation & Page Faults)
//
VOID
MmFreePool(
    PVOID P,
    ULONG Tag
    )
{
    PPOOL_HEADER Block;
    PPOOL_HEADER NextBlock;
    PPOOL_HEADER PrevBlock;
    ULONG        TotalSize;
    ULONG        FreeIndex;

    (VOID)Tag;

    if (P == NULL) {
        return;
    }

    Block = (PPOOL_HEADER)((PUCHAR)P - MM_POOL_HEADER_SIZE);

    LOCK_POOL();

    if ((ULONG_PTR)Block < MiPoolStart || (ULONG_PTR)Block >= MiPoolEnd) {
        UNLOCK_POOL();
        KeBugCheckEx(BAD_POOL_CALLER, 0, (ULONG_PTR)P, 0, 0);
    }

    if (Block->PoolType != POOL_TYPE_ALLOCATED) {
        UNLOCK_POOL();
        KeBugCheckEx(BAD_POOL_HEADER, 2, (ULONG_PTR)P, 0, 0);
    }

    TotalSize = Block->BlockSize;

    //
    // 1. Coalesce with NEXT block if it's free
    //
    NextBlock = (PPOOL_HEADER)((PUCHAR)Block + TotalSize);
    if ((ULONG_PTR)NextBlock < MiPoolEnd && NextBlock->PoolType == POOL_TYPE_FREE) {
        RemoveEntryList((PLIST_ENTRY)&NextBlock->FreeNext);
        TotalSize += NextBlock->BlockSize;
    }

    //
    // 2. Coalesce with PREVIOUS block if it's free
    //
    if (Block->PreviousSize != 0) {
        PrevBlock = (PPOOL_HEADER)((PUCHAR)Block - Block->PreviousSize);
        if ((ULONG_PTR)PrevBlock >= MiPoolStart && PrevBlock->PoolType == POOL_TYPE_FREE) {
            RemoveEntryList((PLIST_ENTRY)&PrevBlock->FreeNext);
            PrevBlock->BlockSize += TotalSize;
            Block = PrevBlock;
            TotalSize = Block->BlockSize;
        }
    }

    // Mark current block as free
    Block->PoolType = POOL_TYPE_FREE;
    Block->PoolTag  = 0;
    Block->BlockSize = TotalSize;

    // Update PreviousSize for the block following this free region
    NextBlock = (PPOOL_HEADER)((PUCHAR)Block + TotalSize);
    if ((ULONG_PTR)NextBlock < MiPoolEnd) {
        NextBlock->PreviousSize = TotalSize;
    }

    // Insert back into free lists
    FreeIndex = MiPoolIndexFromSize(TotalSize);
    InsertHeadList(&MiNonPagedPoolFreeListHead[FreeIndex], (PLIST_ENTRY)&Block->FreeNext);

    UNLOCK_POOL();
}
