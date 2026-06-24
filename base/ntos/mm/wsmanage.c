/*++

Copyright (c) 2026  Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    wsmanage.c

Abstract:

    Working set trimming and management policy.

    MiTrimWorkingSet walks a working set list using a clock-sweep
    algorithm and evicts pages whose Age has reached the trim threshold.
    Evicted pages have their PTEs replaced with transition PTEs and are
    moved to the standby list (clean) or modified list (dirty).

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only.  Working set lock held by caller.

--*/

#include "../inc/mm.h"
#include "mi.h"

/* -----------------------------------------------------------------------
 * MiTrimWorkingSet
 * ----------------------------------------------------------------------- */

/*++

Routine Description:

    Trims up to TrimCount pages from WsInfo's working set using a clock
    sweep.  Pages whose Age field equals 3 are evicted; all others have
    their Age incremented and are skipped.

    For each evicted page:
      1. The hardware PTE is replaced with a transition PTE so that a
         subsequent fault can recover the page without re-reading from disk.
      2. The PFN is moved to the standby list (dirty pages go to modified).
      3. The WSLE slot is returned to the free list.

Arguments:

    WsInfo    - Working set to trim.

    TrimCount - Maximum number of pages to evict.

Return Value:

    Number of pages actually trimmed.

--*/
ULONG
MiTrimWorkingSet(
    PMMSUPPORT WsInfo,
    ULONG      TrimCount
    )
{
    PMMWSL    WslHeader;
    PMMWSLE   Entry;
    PMMPFN    Pfn1;
    PMMPTE    PointerPte;
    MMPTE     TempPte;
    PFN_NUMBER PageFrameIndex;
    ULONG     OldIrql;
    ULONG     Trimmed;
    ULONG     SlotIndex;
    ULONG     Capacity;
    ULONG     Scanned;

    WslHeader = WsInfo->VmWorkingSetList;
    Capacity  = WslHeader->LastInitializedWsle + 1;
    Trimmed   = 0;
    Scanned   = 0;
    SlotIndex = WslHeader->NextSlot;

    while (Trimmed < TrimCount && Scanned < Capacity) {

        if (SlotIndex >= Capacity) {
            SlotIndex = 0;
        }

        Entry = &WslHeader->WsleArray[SlotIndex];
        Scanned++;

        if (!Entry->e1.Valid) {
            SlotIndex++;
            continue;
        }

        //
        // Never evict locked pages.
        //
        if (Entry->e1.LockedInWs || Entry->e1.LockedInMemory) {
            SlotIndex++;
            continue;
        }

        //
        // Age the page: increment and skip until Age reaches the threshold.
        //
        if (Entry->e1.Age < 3) {
            Entry->e1.Age++;
            SlotIndex++;
            continue;
        }

        //
        // Compute the PTE address from the virtual page number stored in the
        // WSLE.
        //
        PointerPte    = MI_GET_PTE_ADDRESS(
                            (ULONG_PTR)Entry->e1.VirtualPageNumber << PAGE_SHIFT);
        PageFrameIndex = (PFN_NUMBER)PointerPte->Hard.PageFrameNumber;
        Pfn1           = MI_PFN_ELEMENT(PageFrameIndex);

        //
        // Build a transition PTE: not valid, Transition=1, page frame
        // and protection preserved from the hardware PTE.
        //
        TempPte.Long                     = 0;
        TempPte.Trans.Transition         = 1;
        TempPte.Trans.Write              = PointerPte->Hard.Write;
        TempPte.Trans.Protection         = Entry->e1.Protection;
        TempPte.Trans.PageFrameNumber    = (ULONG)PageFrameIndex;
        PointerPte->Long                 = TempPte.Long;

        //
        // Move the physical page to the appropriate eviction list.
        //
        LOCK_PFN(OldIrql);

        if (Pfn1->u3.e1.Modified) {
            Pfn1->u3.e1.PageLocation = (ULONG)ModifiedPageList;
            MiInsertPageInModifiedList(PageFrameIndex);
        } else {
            Pfn1->u3.e1.PageLocation = (ULONG)StandbyPageList;
            MiInsertPageInStandbyList(PageFrameIndex);
        }

        Pfn1->u1.ShareCount   = 0;
        Pfn1->u2.ReferenceCount = 0;

        UNLOCK_PFN(OldIrql);

        //
        // Remove the WSLE and update the MMSUPPORT counter.
        //
        MiRemoveWsle((WSLE_NUMBER)SlotIndex, WslHeader);

        if (WsInfo->WorkingSetSize > 0) {
            WsInfo->WorkingSetSize--;
        }

        Trimmed++;
        SlotIndex++;
    }

    WslHeader->NextSlot = SlotIndex;
    return Trimmed;
}
