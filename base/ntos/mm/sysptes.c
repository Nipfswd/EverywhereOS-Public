/*++

Copyright (c) 2026  Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    sysptes.c

Abstract:

    System PTE management.

    System PTEs are a pool of virtual-address slots in kernel space used
    to map physical pages temporarily (I/O buffers, hyperspace operations,
    etc.).  This implementation manages a bitmap of PTE slots that begins
    at MI_SYSTEM_PTE_BASE and extends for MI_SYSTEM_PTE_COUNT entries.

    MiReserveSystemPtes  - Allocates a contiguous run of system PTE slots.
    MiReleaseSystemPtes  - Returns previously reserved slots.

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only.

--*/

#include "../inc/mm.h"
#include "mi.h"

/* -----------------------------------------------------------------------
 * Constants
 * ----------------------------------------------------------------------- */

//
// Virtual base of the system PTE region.  Placed 4 MB above the PTE
// self-map window (0xC0400000) so it does not overlap the mapping used
// by hypermap.c.
//
#define MI_SYSTEM_PTE_BASE  0xC0800000UL
#define MI_SYSTEM_PTE_COUNT 1024UL

/* -----------------------------------------------------------------------
 * Private state
 * ----------------------------------------------------------------------- */

//
// Flat bitmap: bit N is set when PTE slot N is free.
//
#define BITMAP_WORDS    ((MI_SYSTEM_PTE_COUNT + 31) / 32)

static ULONG MiSysPteBitmap[BITMAP_WORDS];
static ULONG MiSysPteInitialized;

/* -----------------------------------------------------------------------
 * MiInitializeSystemPtes (called once from MmInit)
 * ----------------------------------------------------------------------- */

static VOID
MiInitializeSystemPtes(VOID)
{
    ULONG i;

    for (i = 0; i < BITMAP_WORDS; i++) {
        MiSysPteBitmap[i] = ~0UL;
    }

    MiSysPteInitialized = 1;
}

/* -----------------------------------------------------------------------
 * MiReserveSystemPtes
 * ----------------------------------------------------------------------- */

/*++

Routine Description:

    Allocates a contiguous block of NumberOfPtes system PTE slots.
    The slots are located by scanning the bitmap for a run of NumberOfPtes
    consecutive set bits.

Arguments:

    NumberOfPtes - Number of contiguous PTE slots to reserve.

Return Value:

    Pointer to the first MMPTE in the reserved run, or NULL if the
    request cannot be satisfied.

--*/
PMMPTE
MiReserveSystemPtes(
    ULONG NumberOfPtes
    )
{
    ULONG i;
    ULONG j;
    ULONG Run;
    ULONG StartBit;

    if (NumberOfPtes == 0 || NumberOfPtes > MI_SYSTEM_PTE_COUNT) {
        return NULL;
    }

    if (!MiSysPteInitialized) {
        MiInitializeSystemPtes();
    }

    //
    // Linear scan for a run of NumberOfPtes consecutive free bits.
    //
    Run      = 0;
    StartBit = MI_SYSTEM_PTE_COUNT;

    for (i = 0; i < MI_SYSTEM_PTE_COUNT; i++) {

        ULONG Word = i / 32;
        ULONG Bit  = i % 32;

        if (MiSysPteBitmap[Word] & (1UL << Bit)) {
            if (Run == 0) {
                StartBit = i;
            }
            Run++;
            if (Run == NumberOfPtes) {
                break;
            }
        } else {
            Run      = 0;
            StartBit = MI_SYSTEM_PTE_COUNT;
        }
    }

    if (Run < NumberOfPtes) {
        return NULL;
    }

    //
    // Mark the bits as reserved (clear them).
    //
    for (j = StartBit; j < StartBit + NumberOfPtes; j++) {
        ULONG Word = j / 32;
        ULONG Bit  = j % 32;
        MiSysPteBitmap[Word] &= ~(1UL << Bit);
    }

    return (PMMPTE)(MI_SYSTEM_PTE_BASE + StartBit * sizeof(MMPTE));
}

/* -----------------------------------------------------------------------
 * MiReleaseSystemPtes
 * ----------------------------------------------------------------------- */

/*++

Routine Description:

    Returns a previously reserved run of system PTE slots to the free pool
    and clears each PTE so stale mappings are not visible.

Arguments:

    StartingPte  - First PTE in the run (as returned by MiReserveSystemPtes).

    NumberOfPtes - Number of PTEs in the run.

Return Value:

    None.

--*/
VOID
MiReleaseSystemPtes(
    PMMPTE StartingPte,
    ULONG  NumberOfPtes
    )
{
    ULONG StartBit;
    ULONG i;

    if (StartingPte == NULL || NumberOfPtes == 0) {
        return;
    }

    StartBit = (ULONG)((ULONG_PTR)StartingPte - MI_SYSTEM_PTE_BASE) /
               sizeof(MMPTE);

    if (StartBit >= MI_SYSTEM_PTE_COUNT) {
        return;
    }

    for (i = StartBit; i < StartBit + NumberOfPtes && i < MI_SYSTEM_PTE_COUNT; i++) {

        ULONG Word = i / 32;
        ULONG Bit  = i % 32;

        //
        // Clear the PTE before returning the slot so that stale hardware
        // state is not inadvertently visible via a subsequent mapping.
        //
        ((PMMPTE)(MI_SYSTEM_PTE_BASE + i * sizeof(MMPTE)))->Long = 0;

        MiSysPteBitmap[Word] |= (1UL << Bit);
    }
}
