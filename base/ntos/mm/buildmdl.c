/*++

Copyright (c) 2026  The EverywhereOS Authors. All Rights Reserved.

Module Name:

    buildmdl.c

Abstract:

    Memory Descriptor List construction and page-locking.

    MmBuildMdlForNonPagedPool - Builds an MDL for a non-paged pool buffer.
    MmProbeAndLockPages        - Locks user or kernel pages and fills the
                                 PFN array in the MDL.
    MmUnlockPages              - Releases the lock on pages described by
                                 an MDL.

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only.

--*/

#include "../inc/mm.h"
#include "mi.h"

/* -----------------------------------------------------------------------
 * MmBuildMdlForNonPagedPool
 * ----------------------------------------------------------------------- */

/*++

Routine Description:

    Builds an MDL that describes a buffer in non-paged pool.  Because
    non-paged pool is always resident and uses an identity (VA == PA) map
    in this configuration, the physical page numbers are derived directly
    from the virtual addresses.

    The caller must have pre-allocated an MDL large enough to hold the
    PFN array (use MmInitializeMdl to compute the size).

Arguments:

    VirtualAddress - Start of the non-paged pool buffer.

    Length         - Size of the buffer in bytes.

Return Value:

    Pointer to a newly allocated, populated MDL.  Returns NULL if pool
    allocation fails.

--*/
PMDL
MmBuildMdlForNonPagedPool(
    PVOID  VirtualAddress,
    SIZE_T Length
    )
{
    PMDL       Mdl;
    PFN_NUMBER PageCount;
    PFN_NUMBER i;
    PFN_NUMBER *PfnArray;
    ULONG_PTR  Va;

    PageCount = BYTES_TO_PAGES(BYTE_OFFSET(VirtualAddress) + Length);

    Mdl = (PMDL)MmAllocatePool(NonPagedPool,
                                sizeof(MDL) + PageCount * sizeof(PFN_NUMBER),
                                0x4D4D646CUL);
    if (Mdl == NULL) {
        return NULL;
    }

    MmInitializeMdl(Mdl, VirtualAddress, Length);
    Mdl->MdlFlags |= MDL_SOURCE_IS_NONPAGED_POOL | MDL_PAGES_LOCKED;

    PfnArray = (PFN_NUMBER *)(Mdl + 1);
    Va       = (ULONG_PTR)PAGE_ALIGN(VirtualAddress);

    for (i = 0; i < PageCount; i++) {
        //
        // Identity map: physical frame == VA >> PAGE_SHIFT.
        //
        PfnArray[i] = (PFN_NUMBER)(Va >> PAGE_SHIFT);
        Va += PAGE_SIZE;
    }

    return Mdl;
}

/* -----------------------------------------------------------------------
 * MmProbeAndLockPages
 * ----------------------------------------------------------------------- */

/*++

Routine Description:

    Fills the PFN array of Mdl and increments the reference count of each
    page so that the physical memory cannot be reused while the MDL is
    outstanding.

    For this implementation all memory is non-paged (identity mapped), so
    the routine simply records the physical page numbers and marks the
    pages as locked.

Arguments:

    MemoryDescriptorList - MDL to populate and lock.

    AccessMode           - 0 = kernel, 1 = user (currently unused).

    Operation            - IoReadAccess, IoWriteAccess, etc. (unused).

Return Value:

    None.

--*/
VOID
MmProbeAndLockPages(
    PMDL MemoryDescriptorList,
    ULONG AccessMode,
    ULONG Operation
    )
{
    PFN_NUMBER  PageCount;
    PFN_NUMBER *PfnArray;
    PFN_NUMBER  i;
    ULONG_PTR   Va;
    PMMPFN      Pfn1;
    ULONG       OldIrql;

    (VOID)AccessMode;
    (VOID)Operation;

    if (MemoryDescriptorList == NULL) {
        return;
    }

    PageCount = BYTES_TO_PAGES(MemoryDescriptorList->ByteOffset +
                               MemoryDescriptorList->ByteCount);
    PfnArray  = (PFN_NUMBER *)(MemoryDescriptorList + 1);
    Va        = (ULONG_PTR)MemoryDescriptorList->StartVa;

    LOCK_PFN(OldIrql);

    for (i = 0; i < PageCount; i++) {

        PfnArray[i] = (PFN_NUMBER)(Va >> PAGE_SHIFT);

        Pfn1 = MI_PFN_ELEMENT(PfnArray[i]);
        Pfn1->u2.ReferenceCount++;

        Va += PAGE_SIZE;
    }

    UNLOCK_PFN(OldIrql);

    MemoryDescriptorList->MdlFlags |= MDL_PAGES_LOCKED;
}

/* -----------------------------------------------------------------------
 * MmUnlockPages
 * ----------------------------------------------------------------------- */

/*++

Routine Description:

    Decrements the reference count of every page described by the MDL and
    clears the LockedInMemory flag.  When the reference count reaches zero
    the page is returned to the appropriate list.

Arguments:

    MemoryDescriptorList - MDL whose pages are to be unlocked.

Return Value:

    None.

--*/
VOID
MmUnlockPages(
    PMDL MemoryDescriptorList
    )
{
    PFN_NUMBER  PageCount;
    PFN_NUMBER *PfnArray;
    PFN_NUMBER  i;
    PMMPFN      Pfn1;
    ULONG       OldIrql;

    if (MemoryDescriptorList == NULL) {
        return;
    }

    if (!(MemoryDescriptorList->MdlFlags & MDL_PAGES_LOCKED)) {
        return;
    }

    PageCount = BYTES_TO_PAGES(MemoryDescriptorList->ByteOffset +
                               MemoryDescriptorList->ByteCount);
    PfnArray  = (PFN_NUMBER *)(MemoryDescriptorList + 1);

    LOCK_PFN(OldIrql);

    for (i = 0; i < PageCount; i++) {

        Pfn1 = MI_PFN_ELEMENT(PfnArray[i]);

        if (Pfn1->u2.ReferenceCount > 0) {
            Pfn1->u2.ReferenceCount--;
        }

        if (Pfn1->u2.ReferenceCount == 0) {
            MiDecrementShareCount(Pfn1, PfnArray[i]);
        }
    }

    UNLOCK_PFN(OldIrql);

    MemoryDescriptorList->MdlFlags &= ~MDL_PAGES_LOCKED;
}
