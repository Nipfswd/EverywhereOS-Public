/*++

Copyright (c) 2026  Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    pfnlist.c

Abstract:

    PFN page-list management routines.  Each physical page tracked by the
    PFN database belongs to exactly one of the five global page lists:

        Zeroed   -- free, contents zeroed
        Free     -- free, contents arbitrary
        Standby  -- was mapped, now evicted but still holds valid data
        Modified -- was mapped, dirty, waiting for write-behind
        Bad      -- hardware error; never reused

    All insertions and removals assume the caller holds the PFN lock
    (LOCK_PFN/UNLOCK_PFN, which is cli/sti on single-CPU).

    The lists are maintained as singly-linked chains threaded through the
    u1.Flink / Blink fields of MMPFN.  MM_EMPTY_LIST (0xFFFFFFFF) serves
    as the null terminator.  A separate MM_PAGE_LIST_HEAD tracks both the
    head and tail so insertions at the tail are O(1).

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only.  PFN lock must be held by caller.

--*/

#include "../inc/mm.h"
#include "mi.h"

/* -----------------------------------------------------------------------
 * Local helpers
 * ----------------------------------------------------------------------- */

//
// MiInsertPageInList
//
// Appends PageFrameIndex to the tail of *Head and updates the MMPFN's
// PageLocation field.
//
static VOID
MiInsertPageInList(
    PMM_PAGE_LIST_HEAD Head,
    PFN_NUMBER         PageFrameIndex,
    MMLISTS            ListName
    )
{
    PMMPFN Pfn;

    ASSERT(PageFrameIndex != MM_EMPTY_LIST);
    ASSERT(PageFrameIndex <= MmHighestPhysicalPage);

    Pfn = MI_PFN_ELEMENT(PageFrameIndex);
    Pfn->u3.e1.PageLocation = (ULONG)ListName;
    Pfn->u1.Flink           = MM_EMPTY_LIST;
    Pfn->Blink              = Head->Blink;

    if (Head->Flink == MM_EMPTY_LIST) {

        //
        // List was empty; this page becomes both head and tail.
        //
        Head->Flink = PageFrameIndex;

    } else {

        //
        // Attach to the current tail.
        //
        MI_PFN_ELEMENT(Head->Blink)->u1.Flink = PageFrameIndex;
    }

    Head->Blink = PageFrameIndex;
    Head->Total += 1;
}

//
// MiRemovePageFromList
//
// Removes and returns the page at the head of *Head.  Returns
// MM_EMPTY_LIST if the list is empty.
//
static PFN_NUMBER
MiRemovePageFromList(
    PMM_PAGE_LIST_HEAD Head
    )
{
    PFN_NUMBER  PageFrameIndex;
    PMMPFN      Pfn;

    PageFrameIndex = Head->Flink;

    if (PageFrameIndex == MM_EMPTY_LIST) {
        return MM_EMPTY_LIST;
    }

    Pfn = MI_PFN_ELEMENT(PageFrameIndex);
    Head->Flink = Pfn->u1.Flink;

    if (Head->Flink == MM_EMPTY_LIST) {
        Head->Blink = MM_EMPTY_LIST;
    } else {
        MI_PFN_ELEMENT(Head->Flink)->Blink = MM_EMPTY_LIST;
    }

    Pfn->u1.Flink           = MM_EMPTY_LIST;
    Pfn->Blink              = MM_EMPTY_LIST;
    Pfn->u3.e1.PageLocation = (ULONG)ActiveAndValid;
    Head->Total            -= 1;

    return PageFrameIndex;
}

/* -----------------------------------------------------------------------
 * Exported insertion routines
 * ----------------------------------------------------------------------- */

/*++

Routine Description:

    Inserts a page into the zeroed-page list.  The caller is responsible
    for having zeroed the page contents before calling.

Arguments:

    PageFrameIndex - Physical page frame number to insert.

Return Value:

    None.

Environment:

    PFN lock held (cli).

--*/
VOID
MiInsertPageInZeroedList(
    PFN_NUMBER PageFrameIndex
    )
{
    MiInsertPageInList(&MmZeroedPageListHead, PageFrameIndex, ZeroedPageList);
    MmAvailablePages += 1;
    MmResidentAvailablePages += 1;
}

/*++

Routine Description:

    Inserts a page into the free-page list.

Arguments:

    PageFrameIndex - Physical page frame number to insert.

Return Value:

    None.

Environment:

    PFN lock held.

--*/
VOID
MiInsertPageInFreeList(
    PFN_NUMBER PageFrameIndex
    )
{
    MiInsertPageInList(&MmFreePageListHead, PageFrameIndex, FreePageList);
    MmAvailablePages += 1;
    MmResidentAvailablePages += 1;
}

/*++

Routine Description:

    Inserts a page into the standby-page list.

Arguments:

    PageFrameIndex - Physical page frame number to insert.

Return Value:

    None.

Environment:

    PFN lock held.

--*/
VOID
MiInsertPageInStandbyList(
    PFN_NUMBER PageFrameIndex
    )
{
    MiInsertPageInList(&MmStandbyPageListHead, PageFrameIndex, StandbyPageList);
    MmAvailablePages += 1;
}

/*++

Routine Description:

    Inserts a page into the modified-page list.

Arguments:

    PageFrameIndex - Physical page frame number to insert.

Return Value:

    None.

Environment:

    PFN lock held.

--*/
VOID
MiInsertPageInModifiedList(
    PFN_NUMBER PageFrameIndex
    )
{
    MiInsertPageInList(&MmModifiedPageListHead, PageFrameIndex,
                       ModifiedPageList);
}

/*++

Routine Description:

    Inserts a page into the bad-page list.  Pages on this list are never
    reused.

Arguments:

    PageFrameIndex - Physical page frame number to insert.

Return Value:

    None.

Environment:

    PFN lock held.

--*/
VOID
MiInsertPageInBadList(
    PFN_NUMBER PageFrameIndex
    )
{
    MiInsertPageInList(&MmBadPageListHead, PageFrameIndex, BadPageList);
}

/* -----------------------------------------------------------------------
 * Exported removal routines
 * ----------------------------------------------------------------------- */

/*++

Routine Description:

    Removes a zeroed page from the zeroed-page list.

Arguments:

    Color - Ignored on this architecture; accepted for API symmetry.

Return Value:

    Page frame number of the removed page, or MM_EMPTY_LIST if none
    are available.

Environment:

    PFN lock held.

--*/
PFN_NUMBER
MiRemoveZeroPage(
    ULONG Color
    )
{
    PFN_NUMBER PageFrameIndex;

    (VOID)Color;

    PageFrameIndex = MiRemovePageFromList(&MmZeroedPageListHead);

    if (PageFrameIndex != MM_EMPTY_LIST) {
        MmAvailablePages         -= 1;
        MmResidentAvailablePages -= 1;
    }

    return PageFrameIndex;
}

/*++

Routine Description:

    Removes any available page, preferring zeroed > free > standby.

Arguments:

    Color - Ignored; accepted for API symmetry.

Return Value:

    Page frame number of the removed page, or MM_EMPTY_LIST if no
    physical memory is available at all.

Environment:

    PFN lock held.

--*/
PFN_NUMBER
MiRemoveAnyPage(
    ULONG Color
    )
{
    PFN_NUMBER PageFrameIndex;

    (VOID)Color;

    //
    // Prefer zeroed pages so we hand the caller zero-initialised memory
    // without an extra pass.
    //
    PageFrameIndex = MiRemovePageFromList(&MmZeroedPageListHead);

    if (PageFrameIndex != MM_EMPTY_LIST) {
        MmAvailablePages         -= 1;
        MmResidentAvailablePages -= 1;
        return PageFrameIndex;
    }

    PageFrameIndex = MiRemovePageFromList(&MmFreePageListHead);

    if (PageFrameIndex != MM_EMPTY_LIST) {
        MmAvailablePages         -= 1;
        MmResidentAvailablePages -= 1;
        return PageFrameIndex;
    }

    PageFrameIndex = MiRemovePageFromList(&MmStandbyPageListHead);

    if (PageFrameIndex != MM_EMPTY_LIST) {
        MmAvailablePages -= 1;
    }

    return PageFrameIndex;
}

/*++

Routine Description:

    Removes a page directly from the free-page list (bypassing zeroed
    and standby).  Used by the pool initialiser and PFN bootstrap code
    where caller supplies its own zeroing.

Arguments:

    None.

Return Value:

    Page frame number of the removed page, or MM_EMPTY_LIST if the
    free list is empty.

Environment:

    PFN lock held.

--*/
PFN_NUMBER
MiRemovePageFromFreeList(
    VOID
    )
{
    PFN_NUMBER PageFrameIndex;

    PageFrameIndex = MiRemovePageFromList(&MmFreePageListHead);

    if (PageFrameIndex != MM_EMPTY_LIST) {
        MmAvailablePages         -= 1;
        MmResidentAvailablePages -= 1;
    }

    return PageFrameIndex;
}

/* -----------------------------------------------------------------------
 * MiUnlinkPageFromList
 * ----------------------------------------------------------------------- */

/*++

Routine Description:

    Removes a specific page from whatever page list it currently inhabits.
    The caller must know that PageFrameIndex is on one of the five lists
    (zeroed, free, standby, modified, bad) before calling; the routine
    determines the correct head from the PFN entry's PageLocation field
    and unlinks the node from the doubly-linked chain.

    MmAvailablePages and MmResidentAvailablePages are updated to match
    the conventions of the per-list insert/remove routines.

Arguments:

    PageFrameIndex - Physical page frame number to unlink.

Return Value:

    None.

Environment:

    PFN lock held.

--*/
VOID
MiUnlinkPageFromList(
    PFN_NUMBER PageFrameIndex
    )
{
    PMMPFN             Pfn;
    PMM_PAGE_LIST_HEAD Head;
    PFN_NUMBER         NextPfn;
    PFN_NUMBER         PrevPfn;
    MMLISTS            Location;

    ASSERT(PageFrameIndex != MM_EMPTY_LIST);
    ASSERT(PageFrameIndex <= MmHighestPhysicalPage);

    Pfn      = MI_PFN_ELEMENT(PageFrameIndex);
    Location = (MMLISTS)Pfn->u3.e1.PageLocation;

    switch (Location) {
    case ZeroedPageList:
        Head = &MmZeroedPageListHead;
        MmAvailablePages         -= 1;
        MmResidentAvailablePages -= 1;
        break;

    case FreePageList:
        Head = &MmFreePageListHead;
        MmAvailablePages         -= 1;
        MmResidentAvailablePages -= 1;
        break;

    case StandbyPageList:
        Head = &MmStandbyPageListHead;
        MmAvailablePages -= 1;
        break;

    case ModifiedPageList:
        Head = &MmModifiedPageListHead;
        break;

    case BadPageList:
        Head = &MmBadPageListHead;
        break;

    default:
        KeBugCheckEx(PFN_LIST_CORRUPT,
                     (ULONG_PTR)PageFrameIndex,
                     (ULONG_PTR)Location,
                     0, 0);
        return;
    }

    NextPfn = Pfn->u1.Flink;
    PrevPfn = Pfn->Blink;

    //
    // Unlink from previous element (or update list head).
    //
    if (PrevPfn == MM_EMPTY_LIST) {
        Head->Flink = NextPfn;
    } else {
        MI_PFN_ELEMENT(PrevPfn)->u1.Flink = NextPfn;
    }

    //
    // Unlink from next element (or update list tail).
    //
    if (NextPfn == MM_EMPTY_LIST) {
        Head->Blink = PrevPfn;
    } else {
        MI_PFN_ELEMENT(NextPfn)->Blink = PrevPfn;
    }

    Pfn->u1.Flink           = MM_EMPTY_LIST;
    Pfn->Blink              = MM_EMPTY_LIST;
    Pfn->u3.e1.PageLocation = (ULONG)ActiveAndValid;
    Head->Total            -= 1;
}
