/*++

Copyright (c) 2026  Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    allocpag.c

Abstract:

    Physical page allocation and reference count management.

    Provides the internal routines used throughout the memory manager to
    allocate and free individual physical page frames from the PFN database.

    MiAllocatePfn   - Removes one page from the available lists, initialises
                      its MMPFN entry, and wires it to the supplied PTE.

    MiFreePfn       - Decrements the share count of a page; when the count
                      reaches zero the page is placed back on the free list.

    MiInitializePfnEntry - Fills in a freshly allocated MMPFN entry.

    MiDecrementShareCount - Decrements the share count of the PFN entry.
                      When the count drops to zero, the page transitions
                      to the free or standby list depending on whether it
                      is dirty.

    All routines in this file assume the caller holds the PFN lock
    (LOCK_PFN/UNLOCK_PFN).

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only.  PFN lock must be held on entry to all routines.

--*/

#include "../inc/mm.h"
#include "mi.h"

/*++

Routine Description:

    Fills in a freshly allocated MMPFN entry.  Called immediately after
    removing a page from one of the available lists.

Arguments:

    PageFrameIndex - The physical page frame number whose MMPFN is to
                     be initialised.

    TargetPte      - The PTE that will map this page.  Stored in the
                     PteAddress field so the page can be unmapped on
                     eviction.  May be NULL during early bootstrap.

    OldIrql        - Saved interrupt state (unused on single-CPU; kept
                     for future SMP compatibility).

Return Value:

    None.

Environment:

    PFN lock held.

--*/
VOID
MiInitializePfnEntry(
    PFN_NUMBER  PageFrameIndex,
    PMMPTE      TargetPte,
    ULONG       OldIrql
    )
{
    PMMPFN Pfn;

    (VOID)OldIrql;

    ASSERT(PageFrameIndex <= MmHighestPhysicalPage);

    Pfn = MI_PFN_ELEMENT(PageFrameIndex);

    Pfn->PteAddress             = TargetPte;
    Pfn->u2.ReferenceCount      = 1;
    Pfn->u1.ShareCount          = 1;
    Pfn->u3.e1.PageLocation     = (ULONG)ActiveAndValid;
    Pfn->u3.e1.Modified         = 0;
    Pfn->u3.e1.WriteInProgress  = 0;
    Pfn->u3.e1.ReadInProgress   = 0;
    Pfn->u3.e1.PrototypePte     = 0;
    Pfn->u3.e1.PageTransition   = 0;
    Pfn->Blink                  = MM_EMPTY_LIST;
}

/*++

Routine Description:

    Allocates a physical page from the available page lists and records
    the mapping in the PFN database.

    The preference order is zeroed > free > standby.  If no page is
    available at all, the system bug-checks (MEMORY_MANAGEMENT).

Arguments:

    TargetPte - The PTE that will be set to map the returned page.
                Stored in the MMPFN so the page can be located for
                eviction.  May be NULL during early initialisation.

Return Value:

    Physical page frame number of the allocated page.

Environment:

    PFN lock held.

--*/
PFN_NUMBER
MiAllocatePfn(
    PMMPTE TargetPte
    )
{
    PFN_NUMBER  PageFrameIndex;
    ULONG       OldIrql;

    OldIrql = 0;

    PageFrameIndex = MiRemoveAnyPage(0);

    if (PageFrameIndex == MM_EMPTY_LIST) {
        KeBugCheckEx(MEMORY_MANAGEMENT, 0, 0, 0, 0);
    }

    MiInitializePfnEntry(PageFrameIndex, TargetPte, OldIrql);

    return PageFrameIndex;
}

/*++

Routine Description:

    Decrements the share count of the PFN entry for PageFrameIndex.
    When the share count reaches zero the page is returned to the
    appropriate list:

        - If the Modified flag is set, the page goes to the modified list
          (to be written back before reuse).
        - Otherwise it goes to the free list.

    The reference count is also decremented.  When both counts reach zero
    the transition PTE (if any) is cleared.

Arguments:

    Pfn1           - Pointer to the MMPFN entry for the page.

    PageFrameIndex - Page frame number corresponding to Pfn1.

Return Value:

    None.

Environment:

    PFN lock held.

--*/
VOID
MiDecrementShareCount(
    PMMPFN     Pfn1,
    PFN_NUMBER PageFrameIndex
    )
{
    ASSERT(Pfn1 != NULL);
    ASSERT(Pfn1->u1.ShareCount != 0);

    Pfn1->u1.ShareCount -= 1;

    if (Pfn1->u1.ShareCount == 0) {

        if (Pfn1->u3.e1.Modified) {
            MiInsertPageInModifiedList(PageFrameIndex);
        } else {
            MiInsertPageInFreeList(PageFrameIndex);
        }
    }
}

/*++

Routine Description:

    Frees a physical page back to the available pool by decrementing its
    reference count.  When the reference count reaches zero,
    MiDecrementShareCount is called to place the page on the appropriate
    list.

Arguments:

    PageFrameIndex - Physical page frame number to free.

Return Value:

    None.

Environment:

    PFN lock held.

--*/
VOID
MiFreePfn(
    PFN_NUMBER PageFrameIndex
    )
{
    PMMPFN Pfn1;

    ASSERT(PageFrameIndex <= MmHighestPhysicalPage);

    Pfn1 = MI_PFN_ELEMENT(PageFrameIndex);

    ASSERT(Pfn1->u2.ReferenceCount != 0);

    Pfn1->u2.ReferenceCount -= 1;

    if (Pfn1->u2.ReferenceCount == 0) {
        MiDecrementShareCount(Pfn1, PageFrameIndex);
    }
}
