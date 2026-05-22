/*++

Copyright (c) 2026  The EverywhereOS Authors. All Rights Reserved.

Module Name:

    protect.c

Abstract:

    Virtual memory protection change.

    MmProtectVirtualMemory walks all PTEs in the given range and updates
    their protection bits.  Present (valid) PTEs are updated in place.
    Software (demand-zero or transition) PTEs have their Protection field
    updated.

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only.

--*/

#include "../inc/mm.h"
#include "mi.h"

/* -----------------------------------------------------------------------
 * MmProtectVirtualMemory
 * ----------------------------------------------------------------------- */

/*++

Routine Description:

    Changes the protection on a region of committed virtual memory.

Arguments:

    BaseAddress - On entry, the start of the range.  On exit, the
                  page-aligned base of the actual region modified.

    RegionSize  - On entry, the size in bytes.  On exit, the actual
                  (page-rounded) size.

    NewProtect  - New PAGE_* protection to apply.

    OldProtect  - Receives the previous protection of the first page
                  in the range.

Return Value:

    STATUS_SUCCESS           - Protection changed.
    STATUS_INVALID_PARAMETER - Bad arguments or zero size.
    STATUS_INVALID_ADDRESS   - No VAD covers the range.

--*/
NTSTATUS
MmProtectVirtualMemory(
    PVOID  *BaseAddress,
    SIZE_T *RegionSize,
    ULONG   NewProtect,
    PULONG  OldProtect
    )
{
    ULONG_PTR  Base;
    SIZE_T     Size;
    PMMPTE     Pte;
    PMMPTE     StartPte;
    PMMPTE     EndPte;
    ULONG      OldProt;
    BOOLEAN    FirstPage;

    if (BaseAddress == NULL || *BaseAddress == NULL ||
        RegionSize  == NULL || *RegionSize  == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    Base = (ULONG_PTR)*BaseAddress & PAGE_MASK;
    Size = ROUND_TO_PAGES(*RegionSize);

    //
    // Verify the range falls within a known VAD.
    //
    if (MiLocateAddress((PVOID)Base, MmSystemCacheWs.VadRoot) == NULL) {
        return STATUS_INVALID_ADDRESS;
    }

    StartPte  = MI_GET_PTE_ADDRESS(Base);
    EndPte    = MI_GET_PTE_ADDRESS(Base + Size - PAGE_SIZE);
    OldProt   = 0;
    FirstPage = TRUE;

    for (Pte = StartPte; Pte <= EndPte; Pte++) {

        if (MI_PTE_IS_VALID(*Pte)) {

            if (FirstPage) {
                //
                // Recover the old protection from the valid PTE.
                //
                OldProt   = Pte->Hard.Write ? PAGE_READWRITE : PAGE_READONLY;
                FirstPage = FALSE;
            }

            Pte->Hard.Write  = (NewProtect & (PAGE_READWRITE |
                                              PAGE_EXECUTE_READWRITE)) ? 1 : 0;

        } else if (!MI_PTE_IS_ZERO(*Pte)) {

            //
            // Demand-zero or transition software PTE.
            //
            if (FirstPage) {
                OldProt   = Pte->Soft.Protection;
                FirstPage = FALSE;
            }

            if (MI_PTE_IS_TRANSITION(*Pte)) {
                Pte->Trans.Protection = (ULONG)NewProtect & 0x1F;
            } else {
                Pte->Soft.Protection  = (ULONG)NewProtect & 0x1F;
            }
        }
    }

    *BaseAddress = (PVOID)Base;
    *RegionSize  = Size;

    if (OldProtect != NULL) {
        *OldProtect = OldProt;
    }

    return STATUS_SUCCESS;
}
