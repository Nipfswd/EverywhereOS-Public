/*++

Copyright (c) 2026  Everywhere Computing, Inc. All Rights Reserved.

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

//
// Number of pages pulled off the free list per PFN-lock acquisition.
// Amortises the cli/sti round trip (LOCK_PFN/UNLOCK_PFN) across a batch
// instead of paying it twice per single page. Kept small so a burst of
// incoming page faults isn't held off the free list for too long while
// this thread holds the lock filling the batch.
//
#define MI_ZERO_BATCH_SIZE  32

//
// MiZeroPageSse2 / MiZeroPageFallback: zero one PAGE_SIZE-aligned page.
//
// The hyperspace mapping is a scratch window this thread owns exclusively
// for the duration of the zero -- nothing else can observe the page until
// it is unmapped and handed to the zeroed list under the PFN lock. That
// means the plain compiler-visible aliasing rules are enough to keep the
// stores from being reordered past the unmap; "volatile" on the pointer
// was never required for correctness here and only blocked vectorisation.
//
// Non-temporal stores (MOVNTDQ) are used deliberately: this data has no
// reuse -- it is written once and not read again until some future,
// unrelated allocation -- so caching it would only evict lines that are
// actually live for other code. NT stores also bypass write-combining
// stalls associated with RFO (read-for-ownership) on a normal store to a
// cold line, since they don't fetch the line first.
//
#if defined(__SSE2__)
#include <emmintrin.h>

static inline VOID
MiZeroPageSse2(
    PVOID PageVa
    )
{
    __m128i        Zero;
    __m128i       *Dst;
    const __m128i *End;

    Zero = _mm_setzero_si128();
    Dst  = (__m128i *)PageVa;
    End  = Dst + (PAGE_SIZE / sizeof(__m128i));

    while (Dst < End) {
        _mm_stream_si128(Dst + 0, Zero);
        _mm_stream_si128(Dst + 1, Zero);
        _mm_stream_si128(Dst + 2, Zero);
        _mm_stream_si128(Dst + 3, Zero);
        Dst += 4;
    }

    //
    // Non-temporal stores are weakly ordered with respect to other memory
    // traffic -- a fence is required before this page can be safely
    // unmapped and handed off, otherwise the inserting thread (or another
    // CPU, once this kernel grows past uniprocessor) could observe a
    // partially-zeroed page.
    //
    _mm_sfence();
}
#endif

static inline VOID
MiZeroPageFallback(
    PVOID PageVa
    )
{
    ULONG *Dst;
    ULONG  i;

    //
    // Plain (non-volatile) ULONG stores. Without SSE2 this is the
    // best the compiler can do unassisted; it's free to unroll and
    // pair these stores, which a volatile loop would have forbidden.
    //
    Dst = (ULONG *)PageVa;

    for (i = 0; i < PAGE_SIZE / sizeof(ULONG); i++) {
        Dst[i] = 0;
    }
}

static inline VOID
MiZeroPage(
    PVOID PageVa
    )
{
#if defined(__SSE2__)
    MiZeroPageSse2(PageVa);
#else
    MiZeroPageFallback(PageVa);
#endif
}

/*++

Routine Description:

    Continuously drains the free-page list in batches, zeroing each page
    via the hyperspace window, and moves the zeroed pages to the
    zeroed-page list.

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
    PFN_NUMBER  Batch[MI_ZERO_BATCH_SIZE];
    ULONG       BatchCount;
    PFN_NUMBER  PageFrameIndex;
    PVOID       ZeroVa;
    ULONG       OldIrql;
    ULONG       i;

    for (;;) {

        //
        // Pull up to a full batch of pages from the free list under a
        // single PFN-lock acquisition, instead of one lock round trip
        // per page.
        //
        BatchCount = 0;

        LOCK_PFN(OldIrql);

        while (BatchCount < MI_ZERO_BATCH_SIZE) {

            PageFrameIndex = MiRemovePageFromFreeList();

            if (PageFrameIndex == MM_EMPTY_LIST) {
                break;
            }

            Batch[BatchCount++] = PageFrameIndex;
        }

        UNLOCK_PFN(OldIrql);

        if (BatchCount == 0) {
            //
            // No free pages at the moment; spin (a real implementation
            // would wait on an event, but there is no scheduler yet).
            //
            __asm__ __volatile__("pause" ::: "memory");
            continue;
        }

        //
        // Zero every page in the batch with the PFN lock released --
        // this is the expensive part and there's no reason to hold off
        // the rest of the system while it happens.
        //
        for (i = 0; i < BatchCount; i++) {

            ZeroVa = MiMapPageInHyperSpace(Batch[i], &OldIrql);

            MiZeroPage(ZeroVa);

            MiUnmapPageInHyperSpace(ZeroVa, OldIrql);
        }

        //
        // Hand the whole batch to the zeroed list under one more lock
        // acquisition.
        //
        LOCK_PFN(OldIrql);

        for (i = 0; i < BatchCount; i++) {
            MiInsertPageInZeroedList(Batch[i]);
        }

        UNLOCK_PFN(OldIrql);
    }
}