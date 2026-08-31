/*++

Copyright (c) 2026  Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    strsup.c

Abstract:

    Freestanding string helpers (no libc in kernel mode).

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only

--*/

#include "evryfsp.h"

int EvryFsStrEq(const char* a, const char* b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return (*a == 0 && *b == 0);
}

void EvryFsStrCpy(char* dst, const char* src, int n)
{
    int i = 0;
    while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}
