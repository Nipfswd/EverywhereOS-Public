/*++

Copyright (c) 2026  The EverywhereOS Authors. All Rights Reserved.

Module Name:

    zeropage.c

Abstract:

    Zero-page worker.

    MmZeroPageThread loops indefinitely, draining the free-page list into
    the zeroed-page list.  It maps each page through the hyperspace window,
    zeroes it with a DWORD fill loop, then moves it to the zeroed list.

    The caller is expected to jump to MmZeroPageThread from a dedicated
    kernel thread created during system initialisation and never return.

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only.  Runs in a dedicated kernel thread.

--*/

#include "../inc/mm.h"
#include "mi.h"

/* -----------------------------------------------------------------------
 * MmZeroPageThread
 * ----------------------------------------------------------------------- */

/*++

Routine Description:

    Continuously drains the free-page list, zeroing each page via the
    hyperspace window, and moves zeroed pages to the zeroed-page list.

    This routine never returns.

Arguments:

    None.

Return Value:

    Does not return.

--*/
VOID
MmZeroPageThread(
    VOID
    )
{
    PFN_NUMBER      PageFrameIndex;
    PVOID           ZeroVa;
    ULONG           OldIrql;
    volatile ULONG *Page;
    ULONG           i;

    for (;;) {

        //
        // Take one page from the free list under the PFN lock.
        //
        LOCK_PFN(OldIrql);
        PageFrameIndex = MiRemovePageFromFreeList();
        UNLOCK_PFN(OldIrql);

        if (PageFrameIndex == MM_EMPTY_LIST) {
            //
            // No free pages at the moment; spin (a real implementation
            // would wait on an event, but there is no scheduler yet).
            //
            __asm__ __volatile__("pause" ::: "memory");
            continue;
        }

        //
        // Map the physical page through hyperspace and zero it.
        //
        ZeroVa = MiMapPageInHyperSpace(PageFrameIndex, &OldIrql);
        Page   = (volatile ULONG *)ZeroVa;

        for (i = 0; i < PAGE_SIZE / sizeof(ULONG); i++) {
            Page[i] = 0;
        }

        MiUnmapPageInHyperSpace(ZeroVa, OldIrql);

        //
        // Move the page to the zeroed list.
        //
        LOCK_PFN(OldIrql);
        MiInsertPageInZeroedList(PageFrameIndex);
        UNLOCK_PFN(OldIrql);
    }
}
