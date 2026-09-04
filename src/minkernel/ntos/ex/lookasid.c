/*++

Copyright (c) 2026  Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    lookasid.c

Abstract:

    Executive Lookaside Lists and Interlocked S-List implementation.

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only.

--*/

#include "../inc/ex.h"

VOID
InitializeSListHead(
    PSLIST_HEADER SListHead
    )
{
    SListHead->Next     = NULL;
    SListHead->Depth    = 0;
    SListHead->Sequence = 0;
}

PSLIST_ENTRY
InterlockedPushEntrySList(
    PSLIST_HEADER SListHead,
    PSLIST_ENTRY  ListEntry
    )
{
    PSLIST_ENTRY FirstEntry;

    do {
        FirstEntry = SListHead->Next;
        ListEntry->Next = FirstEntry;
    } while (!__sync_bool_compare_and_swap(&SListHead->Next, FirstEntry, ListEntry));

    InterlockedIncrement((LONG volatile *)&SListHead->Depth);
    return FirstEntry;
}

PSLIST_ENTRY
InterlockedPopEntrySList(
    PSLIST_HEADER SListHead
    )
{
    PSLIST_ENTRY FirstEntry;
    PSLIST_ENTRY NextEntry;

    do {
        FirstEntry = SListHead->Next;
        if (FirstEntry == NULL) {
            return NULL;
        }
        NextEntry = FirstEntry->Next;
    } while (!__sync_bool_compare_and_swap(&SListHead->Next, FirstEntry, NextEntry));

    InterlockedDecrement((LONG volatile *)&SListHead->Depth);
    return FirstEntry;
}

static PVOID
ExpDefaultAllocate(
    MM_POOL_TYPE PoolType,
    SIZE_T       NumberOfBytes,
    ULONG        Tag
    )
{
    return MmAllocatePool(PoolType, (ULONG)NumberOfBytes, Tag);
}

static VOID
ExpDefaultFree(
    PVOID Buffer
    )
{
    MmFreePool(Buffer, 0);
}

VOID
ExInitializeNPagedLookasideList(
    PNPAGED_LOOKASIDE_LIST Lookaside,
    PALLOCATE_FUNCTION     Allocate,
    PFREE_FUNCTION         Free,
    ULONG                  Flags,
    SIZE_T                 Size,
    ULONG                  Tag,
    USHORT                 Depth
    )
{
    (VOID)Flags;

    RtlZeroMemory(Lookaside, sizeof(NPAGED_LOOKASIDE_LIST));

    InitializeSListHead(&Lookaside->L.ListHead);

    Lookaside->L.Depth          = 0;
    Lookaside->L.MaximumDepth   = (Depth != 0) ? Depth : 256;
    Lookaside->L.TotalAllocates = 0;
    Lookaside->L.AllocateMisses = 0;
    Lookaside->L.TotalFrees     = 0;
    Lookaside->L.FreeMisses     = 0;
    Lookaside->L.Type           = NonPagedPool;
    Lookaside->L.Tag            = Tag;
    Lookaside->L.Size           = (ULONG)Size;

    Lookaside->L.Allocate       = (Allocate != NULL) ? Allocate : ExpDefaultAllocate;
    Lookaside->L.Free           = (Free != NULL) ? Free : ExpDefaultFree;
}

PVOID
ExAllocateFromNPagedLookasideList(
    PNPAGED_LOOKASIDE_LIST Lookaside
    )
{
    PVOID Entry;

    Lookaside->L.TotalAllocates++;
    Entry = (PVOID)InterlockedPopEntrySList(&Lookaside->L.ListHead);

    if (Entry == NULL) {
        Lookaside->L.AllocateMisses++;
        Entry = Lookaside->L.Allocate(Lookaside->L.Type,
                                      Lookaside->L.Size,
                                      Lookaside->L.Tag);
    }

    return Entry;
}

VOID
ExFreeToNPagedLookasideList(
    PNPAGED_LOOKASIDE_LIST Lookaside,
    PVOID                  Entry
    )
{
    Lookaside->L.TotalFrees++;

    if (Lookaside->L.ListHead.Depth < Lookaside->L.MaximumDepth) {
        InterlockedPushEntrySList(&Lookaside->L.ListHead, (PSLIST_ENTRY)Entry);
    } else {
        Lookaside->L.FreeMisses++;
        Lookaside->L.Free(Entry);
    }
}

VOID
ExDeleteNPagedLookasideList(
    PNPAGED_LOOKASIDE_LIST Lookaside
    )
{
    PVOID Entry;

    while ((Entry = (PVOID)InterlockedPopEntrySList(&Lookaside->L.ListHead)) != NULL) {
        Lookaside->L.Free(Entry);
    }
}
