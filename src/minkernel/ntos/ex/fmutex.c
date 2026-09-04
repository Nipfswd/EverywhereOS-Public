/*++

Copyright (c) 2026  Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    fmutex.c

Abstract:

    Fast Mutex synchronization implementation.

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only.

--*/

#include "../inc/ex.h"

VOID
ExInitializeFastMutex(
    PFAST_MUTEX FastMutex
    )
{
    FastMutex->Count      = 1;
    FastMutex->Owner      = NULL;
    FastMutex->Contention = 0;
    FastMutex->OldIrql    = 0;
}

VOID
ExAcquireFastMutex(
    PFAST_MUTEX FastMutex
    )
{
    while (InterlockedDecrement(&FastMutex->Count) <= 0) {
        InterlockedIncrement(&FastMutex->Count);
        FastMutex->Contention++;
        __asm__ __volatile__("pause");
    }

    FastMutex->Owner = (PVOID)1;
}

VOID
ExReleaseFastMutex(
    PFAST_MUTEX FastMutex
    )
{
    FastMutex->Owner = NULL;
    InterlockedIncrement(&FastMutex->Count);
}

BOOLEAN
ExTryToAcquireFastMutex(
    PFAST_MUTEX FastMutex
    )
{
    if (InterlockedCompareExchange(&FastMutex->Count, 0, 1) == 1) {
        FastMutex->Owner = (PVOID)1;
        return TRUE;
    }
    return FALSE;
}
