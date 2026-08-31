/*++

Copyright (c) 2026  Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    dirsup.c

Abstract:

    EVRYFS directory support: entry accessor, name lookup, and the
    public listing API.

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only

--*/

#include "evryfsp.h"

EVRYFS_DIRENT* EvryFsDirEntry(int index)
{
    return (EVRYFS_DIRENT*)(g_DirBuf + (unsigned)index * sizeof(EVRYFS_DIRENT));
}

int EvryFsFindEntry(const char* name)
{
    for (int i = 0; i < EVRYFS_MAX_FILES; i++) {
        EVRYFS_DIRENT* d = EvryFsDirEntry(i);
        if ((d->flags & 1) && EvryFsStrEq(d->name, name))
            return i;
    }
    return -1;
}

/* existing entry with this name wins (overwrite); else first free slot */
int EvryFsFindSlot(const char* name)
{
    int slot = -1;
    for (int i = 0; i < EVRYFS_MAX_FILES; i++) {
        EVRYFS_DIRENT* d = EvryFsDirEntry(i);
        if ((d->flags & 1) && EvryFsStrEq(d->name, name)) return i;
        if (!(d->flags & 1) && slot < 0)                  slot = i;
    }
    return slot;
}

void EvryFsList(char names[][EVRYFS_NAME_LEN], uint32_t sizes[], int* count)
{
    *count = 0;
    if (!g_FsPresent) return;

    for (int i = 0; i < EVRYFS_MAX_FILES; i++) {
        EVRYFS_DIRENT* d = EvryFsDirEntry(i);
        if (!(d->flags & 1)) continue;
        EvryFsStrCpy(names[*count], d->name, EVRYFS_NAME_LEN);
        sizes[*count] = d->size;
        (*count)++;
    }
}
