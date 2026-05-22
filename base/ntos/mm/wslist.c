/*++

Copyright (c) 2026  The EverywhereOS Authors. All Rights Reserved.

Module Name:

    wslist.c

Abstract:

    Working set list management for the memory manager.

    A working set list (MMWSL) tracks which virtual pages are currently
    resident.  Each slot is an MMWSLE union: when the slot is occupied,
    e1.Valid = 1 and e1.VirtualPageNumber holds bits 12-31 of the VA;
    when the slot is free, Long holds the index of the next free slot
    (forming a singly-linked free list; WSLE_NULL_INDEX terminates).

    MiInitializeWorkingSetList - Prepares an MMWSL for first use.
    MiInsertWsle               - Records a newly faulted-in page.
    MiRemoveWsle               - Frees the slot for a trimmed/freed page.

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only.  Working set lock held by caller.

--*/

#include "../inc/mm.h"
#include "mi.h"

/* -----------------------------------------------------------------------
 * MiInitializeWorkingSetList
 * ----------------------------------------------------------------------- */

/*++

Routine Description:

    Initialises an MMWSL before first use.  All slots are placed on the
    free list by chaining each entry's Long to the index of the successor.
    The last slot terminates the chain with WSLE_NULL_INDEX.

    The caller must have allocated the MMWSL (via MmAllocatePool or static
    storage) and set LastInitializedWsle to the total slot count minus one.
    VmWorkingSetList in WsInfo must point to the MMWSL.

Arguments:

    WsInfo - MMSUPPORT structure whose VmWorkingSetList is the MMWSL.

Return Value:

    None.

--*/
VOID
MiInitializeWorkingSetList(
    PMMSUPPORT WsInfo
    )
{
    PMMWSL WslHeader;
    ULONG  i;
    ULONG  Capacity;

    WslHeader = WsInfo->VmWorkingSetList;
    Capacity  = WslHeader->LastInitializedWsle + 1;

    //
    // Initialize the Wsle convenience pointer to the embedded array.
    //
    WslHeader->Wsle = &WslHeader->WsleArray[0];

    //
    // Chain all slots into the free list.
    //
    for (i = 0; i < Capacity; i++) {
        WslHeader->WsleArray[i].Long = i + 1;
    }

    WslHeader->FirstFree             = 0;
    WslHeader->FirstDynamic          = 0;
    WslHeader->LastEntry             = 0;
    WslHeader->NextSlot              = 0;
    WslHeader->NonDirectCount        = 0;
    WslHeader->WorkingSetSize        = 0;
    WslHeader->MaximumWorkingSetSize = Capacity;
    WslHeader->MinimumWorkingSetSize = 0;
    WslHeader->WsleMappingCount      = 0;

    WsInfo->WorkingSetSize           = 0;
    WsInfo->MaximumWorkingSetSize    = Capacity;
    WsInfo->MinimumWorkingSetSize    = 0;
    WsInfo->PageFaultCount           = 0;
}

/* -----------------------------------------------------------------------
 * MiInsertWsle
 * ----------------------------------------------------------------------- */

/*++

Routine Description:

    Records that the virtual page at VirtualAddress is now resident.
    Obtains a free slot from the WSLE free list, fills in the virtual
    page number and flags, and stores the slot index in the PFN entry's
    u1.Flink field (the union member used as WsIndex when a page is
    active).

Arguments:

    VirtualAddress - Page-aligned virtual address of the resident page.

    WsInfo         - Working set that has acquired the page.

    Pfn1           - PFN database entry for the physical page backing
                     VirtualAddress.

Return Value:

    WSLE slot index on success, or WSLE_NULL_INDEX if the list is full.

--*/
WSLE_NUMBER
MiInsertWsle(
    PVOID      VirtualAddress,
    PMMSUPPORT WsInfo,
    PMMPFN     Pfn1
    )
{
    PMMWSL   WslHeader;
    ULONG    WsleIndex;
    PMMWSLE  Entry;

    WslHeader = WsInfo->VmWorkingSetList;

    if (WslHeader->FirstFree > WslHeader->LastInitializedWsle) {
        return WSLE_NULL_INDEX;
    }

    WsleIndex = WslHeader->FirstFree;
    Entry     = &WslHeader->WsleArray[WsleIndex];

    //
    // Advance the free-list head to the next chained slot.
    //
    WslHeader->FirstFree = Entry->Long;

    //
    // Fill in the WSLE.  Store the virtual page number in bits 12-31
    // and set Valid in bit 0.
    //
    Entry->Long                    = 0;
    Entry->e1.Valid                = 1;
    Entry->e1.VirtualPageNumber    = (ULONG)((ULONG_PTR)VirtualAddress >> PAGE_SHIFT);
    Entry->e1.LockedInWs           = 0;
    Entry->e1.LockedInMemory       = 0;
    Entry->e1.Protection           = 0;
    Entry->e1.Age                  = 0;

    //
    // Record the WSLE index in the PFN entry.  When a page is active,
    // u1.Flink is repurposed to hold the working-set slot index.
    //
    Pfn1->u1.Flink = WsleIndex;

    WslHeader->WorkingSetSize++;
    WsInfo->WorkingSetSize++;

    return (WSLE_NUMBER)WsleIndex;
}

/* -----------------------------------------------------------------------
 * MiRemoveWsle
 * ----------------------------------------------------------------------- */

/*++

Routine Description:

    Marks the WSLE at WslEntry as free and returns its slot to the
    free list.  The working set size counters are decremented.

Arguments:

    WslEntry        - Index into WsleArray[] of the entry to remove.

    WorkingSetList  - The MMWSL that owns the entry.

Return Value:

    None.

--*/
VOID
MiRemoveWsle(
    WSLE_NUMBER WslEntry,
    PMMWSL      WorkingSetList
    )
{
    PMMWSLE Entry;

    ASSERT(WslEntry <= WorkingSetList->LastInitializedWsle);

    Entry = &WorkingSetList->WsleArray[WslEntry];

    ASSERT(Entry->e1.Valid != 0);

    //
    // Chain this slot onto the head of the free list and mark it invalid.
    //
    Entry->Long                = WorkingSetList->FirstFree;
    WorkingSetList->FirstFree  = WslEntry;

    if (WorkingSetList->WorkingSetSize > 0) {
        WorkingSetList->WorkingSetSize--;
    }
}
