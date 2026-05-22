/*++

Copyright (c) 2026  The EverywhereOS Authors. All Rights Reserved.

Module Name:

    pagfault.c

Abstract:

    Page-fault dispatcher and per-fault-type resolution routines.

    The x86 #PF ISR pushes an error code and calls MmAccessFault with the
    faulting linear address (read from CR2) and the error code.

    MmAccessFault               - Top-level entry from the #PF handler.
    MiDispatchFault             - Routes to the per-type resolver.
    MiResolveDemandZeroFault    - Allocates a zeroed physical page.
    MiResolveTransitionFault    - Recovers a page in transition state.

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Called from the x86 #PF ISR at interrupt-level.  PFN lock must not
    be held by the caller; these routines acquire and release it internally.

--*/

#include "../inc/mm.h"
#include "mi.h"

//
// Access-fault error-code bits (x86 architecture).
//
#define PF_PRESENT  0x01UL   // fault was on a present page (protection violation)
#define PF_WRITE    0x02UL   // fault was caused by a write access
#define PF_USER     0x04UL   // fault occurred while executing in user mode

/* -----------------------------------------------------------------------
 * MiResolveDemandZeroFault
 * ----------------------------------------------------------------------- */

/*++

Routine Description:

    Handles a demand-zero fault.  Allocates one physical page, zeroes it,
    then writes the hardware PTE so the page is present and accessible.

    The Owner bit is set when the faulting address lies in the user portion
    of the virtual address space (Va < MM_SYSTEM_RANGE_START).

Arguments:

    Va         - Faulting virtual address (page-aligned by caller).

    PointerPte - Pointer to the PTE for Va.

    Protection - Five-bit internal protection encoding for the new PTE.

Return Value:

    STATUS_SUCCESS  - Page installed; the fault can be retried.

--*/
NTSTATUS
MiResolveDemandZeroFault(
    PVOID  Va,
    PMMPTE PointerPte,
    ULONG  Protection
    )
{
    PFN_NUMBER      PageFrameIndex;
    MMPTE           TempPte;
    ULONG           OldIrql;
    volatile ULONG *PageBase;
    ULONG           i;

    LOCK_PFN(OldIrql);
    PageFrameIndex = MiAllocatePfn(PointerPte);
    UNLOCK_PFN(OldIrql);

    //
    // Zero the page.  With virtual == physical (identity map), the page's
    // physical address is also its virtual address.
    //
    PageBase = (volatile ULONG *)(ULONG_PTR)(PageFrameIndex << PAGE_SHIFT);
    for (i = 0; i < PAGE_SIZE / sizeof(ULONG); i++) {
        PageBase[i] = 0;
    }

    //
    // Build the hardware PTE.  Write access is given when the protection
    // encoding includes the write bit (MM_PROTECTION_WRITE_MASK).
    //
    TempPte.Long                   = 0;
    TempPte.Hard.Valid              = 1;
    TempPte.Hard.Write              = (Protection & MM_PROTECTION_WRITE_MASK) ? 1 : 0;
    TempPte.Hard.Owner              = ((ULONG_PTR)Va < (ULONG_PTR)MM_SYSTEM_RANGE_START) ? 1 : 0;
    TempPte.Hard.PageFrameNumber    = PageFrameIndex;

    PointerPte->Long = TempPte.Long;

    return STATUS_SUCCESS;
}

/* -----------------------------------------------------------------------
 * MiResolveTransitionFault
 * ----------------------------------------------------------------------- */

/*++

Routine Description:

    Recovers a page that is in the transition state -- removed from the
    working set but not yet repurposed.  The physical frame still holds
    valid data; we remove it from its current list, update the reference
    count, and restore the hardware PTE.

Arguments:

    Va         - Faulting virtual address (page-aligned by caller).

    PointerPte - Pointer to the software PTE (Transition bit set).

Return Value:

    STATUS_SUCCESS  - Page recovered and mapped.

--*/
NTSTATUS
MiResolveTransitionFault(
    PVOID  Va,
    PMMPTE PointerPte
    )
{
    PFN_NUMBER  PageFrameIndex;
    PMMPFN      Pfn1;
    MMPTE       TempPte;
    ULONG       OldIrql;

    (VOID)Va;

    PageFrameIndex = (PFN_NUMBER)PointerPte->Trans.PageFrameNumber;
    Pfn1           = MI_PFN_ELEMENT(PageFrameIndex);

    LOCK_PFN(OldIrql);

    //
    // Unlink the page from whatever list it currently inhabits.
    //
    MiUnlinkPageFromList(PageFrameIndex);

    Pfn1->u3.e1.PageLocation   = (ULONG)ActiveAndValid;
    Pfn1->u1.ShareCount        = 1;
    Pfn1->u2.ReferenceCount    = 1;

    UNLOCK_PFN(OldIrql);

    //
    // Restore the hardware PTE.
    //
    TempPte.Long                = 0;
    TempPte.Hard.Valid           = 1;
    TempPte.Hard.Write           = PointerPte->Trans.Write;
    TempPte.Hard.Owner           = ((ULONG_PTR)Va < (ULONG_PTR)MM_SYSTEM_RANGE_START) ? 1 : 0;
    TempPte.Hard.PageFrameNumber = PageFrameIndex;
    PointerPte->Long             = TempPte.Long;

    return STATUS_SUCCESS;
}

/* -----------------------------------------------------------------------
 * MiDispatchFault
 * ----------------------------------------------------------------------- */

/*++

Routine Description:

    Examines the PTE for the faulting address and dispatches to the
    appropriate resolver.

Arguments:

    FaultStatus - x86 page-fault error code (P/W/U bits).

    Va          - Faulting virtual address (page-aligned by caller).

    PointerPte  - PTE for Va.

    TrapFrame   - Saved register context (passed through to resolvers).

Return Value:

    STATUS_SUCCESS          - Fault resolved; execution may be retried.
    STATUS_ACCESS_VIOLATION - Protection fault; unresolvable.

--*/
NTSTATUS
MiDispatchFault(
    ULONG   FaultStatus,
    PVOID   Va,
    PMMPTE  PointerPte,
    PVOID   TrapFrame
    )
{
    (VOID)TrapFrame;

    //
    // Present page: this is a protection fault.
    //
    if (FaultStatus & PF_PRESENT) {
        return STATUS_ACCESS_VIOLATION;
    }

    //
    // Completely zero PTE: demand-zero.
    //
    if (MI_PTE_IS_ZERO(*PointerPte)) {
        ULONG Protection = MM_READWRITE;
        return MiResolveDemandZeroFault(Va, PointerPte, Protection);
    }

    //
    // Transition PTE: recover the page from the standby or modified list.
    //
    if (MI_PTE_IS_TRANSITION(*PointerPte)) {
        return MiResolveTransitionFault(Va, PointerPte);
    }

    //
    // Software demand-zero PTE (protection field set, no page file offset).
    //
    if (MI_PTE_IS_DEMAND_ZERO(*PointerPte)) {
        ULONG Protection = PointerPte->Soft.Protection;
        return MiResolveDemandZeroFault(Va, PointerPte, Protection);
    }

    return STATUS_ACCESS_VIOLATION;
}

/* -----------------------------------------------------------------------
 * MmAccessFault
 * ----------------------------------------------------------------------- */

/*++

Routine Description:

    Top-level page-fault handler called from the x86 #PF ISR with
    interrupts disabled.  Looks up the PDE and PTE for the faulting
    address and calls MiDispatchFault.

    If the fault cannot be resolved in kernel mode, KeBugCheckEx is
    called with PAGE_FAULT_IN_NONPAGED_AREA.

Arguments:

    FaultStatus    - x86 page-fault error code (from the ISR error push).

    VirtualAddress - Faulting linear address (from CR2).

    PreviousMode   - Processor mode at time of fault (0 = kernel, 1 = user).

    TrapFrame      - Saved register context.

Return Value:

    STATUS_SUCCESS          - Fault resolved.
    STATUS_ACCESS_VIOLATION - User-mode protection fault.

--*/
NTSTATUS
MmAccessFault(
    ULONG  FaultStatus,
    PVOID  VirtualAddress,
    ULONG  PreviousMode,
    PVOID  TrapFrame
    )
{
    PMMPTE   PointerPde;
    PMMPTE   PointerPte;
    PVOID    AlignedVa;
    NTSTATUS Status;

    (VOID)PreviousMode;

    AlignedVa  = (PVOID)((ULONG_PTR)VirtualAddress & ~(PAGE_SIZE - 1));
    PointerPde = MI_GET_PDE_ADDRESS(AlignedVa);

    //
    // If the PDE is not present the address is completely unmapped.
    //
    if (!MI_PTE_IS_VALID(*PointerPde)) {
        if (FaultStatus & PF_USER) {
            return STATUS_ACCESS_VIOLATION;
        }
        KeBugCheckEx(PAGE_FAULT_IN_NONPAGED_AREA,
                     (ULONG_PTR)VirtualAddress,
                     (ULONG_PTR)FaultStatus,
                     0,
                     0);
    }

    PointerPte = MI_GET_PTE_ADDRESS(AlignedVa);

    Status = MiDispatchFault(FaultStatus, AlignedVa, PointerPte, TrapFrame);

    if (!NT_SUCCESS(Status)) {
        if (FaultStatus & PF_USER) {
            return Status;
        }
        KeBugCheckEx(PAGE_FAULT_IN_NONPAGED_AREA,
                     (ULONG_PTR)VirtualAddress,
                     (ULONG_PTR)FaultStatus,
                     (ULONG_PTR)Status,
                     0);
    }

    return STATUS_SUCCESS;
}
