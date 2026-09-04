/*++

Copyright (c) 2026  Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    luid.c

Abstract:

    Locally Unique Identifier (LUID) allocator.

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only.

--*/

#include "../inc/ex.h"

LARGE_INTEGER ExpLuid = { .QuadPart = 1000 };

VOID
ExAllocateLocallyUniqueId(
    PLUID Luid
    )
{
    LONGLONG Current;

    do {
        Current = ExpLuid.QuadPart;
    } while (!__sync_bool_compare_and_swap(&ExpLuid.QuadPart, Current, Current + 1));

    Luid->LowPart  = (ULONG)(Current & 0xFFFFFFFFUL);
    Luid->HighPart = (LONG)(Current >> 32);
}
