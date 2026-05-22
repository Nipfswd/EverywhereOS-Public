/*++

Copyright (c) 2026  Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    miglobal.c

Abstract:

    Memory Manager global variable definitions.  All exported and internal
    Mm globals live here so every other Mm source file can extern-reference
    them through mi.h and mm.h without link-time multiply-defined symbols.

    Variables are grouped by subsystem and annotated with the lock that
    protects them.  "PFN lock" means LOCK_PFN/UNLOCK_PFN (cli/sti).
    "Pool lock" means LOCK_POOL/UNLOCK_POOL (cli/sti).
    "None" means the variable is set once during MmInit and is read-only
    thereafter.

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only.  Called from MmInit before any other Mm routine.

--*/

#include "../inc/mm.h"
#include "mi.h"

/* -----------------------------------------------------------------------
 * PFN database  (protected by PFN lock)
 * ----------------------------------------------------------------------- */

//
// Pointer to the start of the PFN database array.  Set during
// MiBuildPfnDatabase; afterwards treated as read-only.
//
PMMPFN MmPfnDatabase;

//
// Highest page frame number for which a MMPFN entry exists.
//
PFN_NUMBER MmHighestPhysicalPage;

//
// Total number of physical pages described by the database.
//
PFN_NUMBER MmNumberOfPhysicalPages;

//
// Pages currently on zeroed, free, or standby lists -- available for
// immediate reuse without I/O.
//
PFN_NUMBER MmAvailablePages;

//
// Available pages less system commitments (can go negative under pressure).
//
PFN_NUMBER MmResidentAvailablePages;

/* -----------------------------------------------------------------------
 * Page list heads  (protected by PFN lock)
 * ----------------------------------------------------------------------- */

MM_PAGE_LIST_HEAD MmZeroedPageListHead;
MM_PAGE_LIST_HEAD MmFreePageListHead;
MM_PAGE_LIST_HEAD MmStandbyPageListHead;
MM_PAGE_LIST_HEAD MmModifiedPageListHead;
MM_PAGE_LIST_HEAD MmBadPageListHead;

/* -----------------------------------------------------------------------
 * Non-paged pool  (protected by pool lock)
 * ----------------------------------------------------------------------- */

//
// Total bytes in the non-paged pool region.
//
ULONG MmPoolTotalBytes;

//
// Bytes currently sitting on the free lists.
//
ULONG MmPoolFreeBytes;

//
// Base virtual address of the non-paged pool region.
//
ULONG_PTR MiNonPagedPoolStart;

//
// One byte past the end of the non-paged pool region.
//
ULONG_PTR MiNonPagedPoolEnd;

//
// Per-size free-list heads (POOL_SMALL_LISTS + 1 entries).
// Index 0 is unused; indices 1..POOL_SMALL_LISTS hold free blocks of
// exactly (index * POOL_BLOCK_SIZE) bytes; index POOL_SMALL_LISTS is
// the overflow list for blocks larger than POOL_MAX_BLOCK_SIZE.
//
LIST_ENTRY MiNonPagedPoolFreeListHead[POOL_SMALL_LISTS + 1];

/* -----------------------------------------------------------------------
 * Paged pool  (not yet implemented -- declared for link completeness)
 * ----------------------------------------------------------------------- */

ULONG_PTR MiPagedPoolStart;
ULONG_PTR MiPagedPoolEnd;

/* -----------------------------------------------------------------------
 * Hyperspace mapping  (protected by PFN lock)
 * ----------------------------------------------------------------------- */

//
// PTE used for transient single-page mappings into hyperspace.
// Hyperspace occupies the single 4 KB page at virtual address 0xC0400000.
//
PMMPTE MiHyperSpacePte;

/* -----------------------------------------------------------------------
 * System cache working set
 * ----------------------------------------------------------------------- */

PMMWSL   MmSystemCacheWorkingSetList;
MMSUPPORT MmSystemCacheWs;
