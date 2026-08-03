/*++

Copyright (c) 2026  Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    hypermap.c

Abstract:

    Hyperspace mapping for physical pages.

    Hyperspace is a single 4 KB page-table entry at virtual address
    0xC0400000 that can be written to map any physical page temporarily.
    It is used by the zero-page worker (zeropage.c) and by transition-
    fault resolution when the physical address of a page differs from its
    virtual address.

    MiMapPageInHyperSpace   - Maps PageFrameIndex and returns its VA.
    MiUnmapPageInHyperSpace - Clears the mapping and re-enables interrupts.

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only.  Interrupts are disabled while a mapping is held.

--*/

#include "../inc/mm.h"
#include "mi.h"

//
// The hyperspace window is a single 4 KB page at the start of the
// first page-table frame above the self-map window.
//
#define MI_HYPERSPACE_VA    0xC0400000UL

/* -----------------------------------------------------------------------
 * MiMapPageInHyperSpace
 * ----------------------------------------------------------------------- */

/*++

Routine Description:

    Maps PageFrameIndex into the hyperspace window and returns its
    virtual address.  Interrupts are disabled on exit to prevent the
    mapping from being stolen by the zero-page worker running on another
    CPU (or a later preemption when SMP is supported).

Arguments:

    PageFrameIndex - Physical frame to map.

    OldIrql        - Receives the caller's interrupt state (for
                     MiUnmapPageInHyperSpace).

Return Value:

    Virtual address of the mapped page (MI_HYPERSPACE_VA).

--*/
PVOID
MiMapPageInHyperSpace(
    PFN_NUMBER  PageFrameIndex,
    ULONG      *OldIrql
    )
{
    PMMPTE HyperspaceMap;
    MMPTE  TempPte;

    ASSERT(PageFrameIndex != MM_EMPTY_LIST);
    ASSERT(PageFrameIndex <= MmHighestPhysicalPage);

    //
    // Disable interrupts before touching the shared PTE.
    //
    LOCK_PFN(*OldIrql);

    HyperspaceMap = (PMMPTE)MI_HYPERSPACE_VA - 1;

    //
    // Actually, the hyperspace PTE lives in the page table that maps
    // virtual address 0xC0400000.  Compute its address via the PTE window.
    //
    HyperspaceMap = MI_GET_PTE_ADDRESS(MI_HYPERSPACE_VA);

    TempPte.Long                = 0;
    TempPte.Hard.Valid           = 1;
    TempPte.Hard.Write           = 1;
    TempPte.Hard.PageFrameNumber = PageFrameIndex;

    HyperspaceMap->Long = TempPte.Long;

    //
    // Flush the TLB entry for this address.
    //
    __asm__ __volatile__(
        "invlpg (%0)" :
        : "r"((ULONG_PTR)MI_HYPERSPACE_VA)
        : "memory");

    return (PVOID)MI_HYPERSPACE_VA;
}

/* -----------------------------------------------------------------------
 * MiUnmapPageInHyperSpace
 * ----------------------------------------------------------------------- */

/*++

Routine Description:

    Clears the hyperspace window PTE and re-enables interrupts.

Arguments:

    VirtualAddress - The value returned by MiMapPageInHyperSpace.

    OldIrql        - The value saved by MiMapPageInHyperSpace.

Return Value:

    None.

--*/
VOID
MiUnmapPageInHyperSpace(
    PVOID VirtualAddress,
    ULONG OldIrql
    )
{
    PMMPTE HyperspaceMap;

    (VOID)VirtualAddress;

    HyperspaceMap = MI_GET_PTE_ADDRESS(MI_HYPERSPACE_VA);
    HyperspaceMap->Long = 0;

    //
    // Flush the TLB entry.
    //
    __asm__ __volatile__(
        "invlpg (%0)" :
        : "r"((ULONG_PTR)MI_HYPERSPACE_VA)
        : "memory");

    UNLOCK_PFN(OldIrql);
}
