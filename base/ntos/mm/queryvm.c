/*++

Copyright (c) 2026  Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    queryvm.c

Abstract:

    Virtual memory query.

    MmQueryVirtualMemory locates the VAD that covers a given address and
    returns the region's base, size, state (MEM_FREE / MEM_RESERVE /
    MEM_COMMIT), protection, and type.

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only.

--*/

#include "../inc/mm.h"
#include "mi.h"

/* -----------------------------------------------------------------------
 * MmQueryVirtualMemory
 * ----------------------------------------------------------------------- */

/*++

Routine Description:

    Returns information about the virtual memory region that contains
    BaseAddress.

Arguments:

    BaseAddress       - Address within the region to query.

    RegionBaseAddress - Receives the base virtual address of the region.

    RegionSize        - Receives the size of the region in bytes.

    State             - Receives MEM_FREE, MEM_RESERVE, or MEM_COMMIT.

    Protect           - Receives the PAGE_* protection flags.

    Type              - Receives MEM_PRIVATE, MEM_MAPPED, or MEM_IMAGE.

Return Value:

    STATUS_SUCCESS           - Query succeeded.
    STATUS_INVALID_ADDRESS   - No VAD found; region reported as MEM_FREE.

--*/
NTSTATUS
MmQueryVirtualMemory(
    PVOID   BaseAddress,
    PVOID  *RegionBaseAddress,
    SIZE_T *RegionSize,
    PULONG  State,
    PULONG  Protect,
    PULONG  Type
    )
{
    PMMVAD     Vad;
    ULONG_PTR  Base;
    ULONG_PTR  End;
    PMMPTE     StartPte;
    PMMPTE     EndPte;
    PMMPTE     Pte;
    BOOLEAN    AnyCommitted;

    if (BaseAddress == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    Vad = MiLocateAddress(BaseAddress, MmSystemCacheWs.VadRoot);

    if (Vad == NULL) {
        //
        // No VAD: report the address as free.
        //
        if (RegionBaseAddress != NULL) {
            *RegionBaseAddress = BaseAddress;
        }
        if (RegionSize != NULL) {
            *RegionSize = PAGE_SIZE;
        }
        if (State != NULL) {
            *State = MEM_FREE;
        }
        if (Protect != NULL) {
            *Protect = PAGE_NOACCESS;
        }
        if (Type != NULL) {
            *Type = 0;
        }
        return STATUS_INVALID_ADDRESS;
    }

    Base = (ULONG_PTR)MI_VPN_TO_VA(Vad->StartingVpn);
    End  = (ULONG_PTR)MI_VPN_TO_VA_ENDING(Vad->EndingVpn);

    if (RegionBaseAddress != NULL) {
        *RegionBaseAddress = (PVOID)Base;
    }

    if (RegionSize != NULL) {
        *RegionSize = (SIZE_T)(End - Base + 1);
    }

    //
    // Determine state by checking whether any PTE in the region is
    // committed (non-zero software PTE or valid hardware PTE).
    //
    AnyCommitted = FALSE;
    StartPte     = MI_GET_PTE_ADDRESS(Base);
    EndPte       = MI_GET_PTE_ADDRESS(End);

    for (Pte = StartPte; Pte <= EndPte && !AnyCommitted; Pte++) {
        if (!MI_PTE_IS_ZERO(*Pte)) {
            AnyCommitted = TRUE;
        }
    }

    if (State != NULL) {
        *State = AnyCommitted ? MEM_COMMIT : MEM_RESERVE;
    }

    if (Protect != NULL) {
        *Protect = (ULONG)(Vad->u.VadFlags.Protection);
    }

    if (Type != NULL) {
        if (Vad->u.VadFlags.ImageMap) {
            *Type = MEM_MAPPED;
        } else {
            *Type = MEM_PRIVATE;
        }
    }

    return STATUS_SUCCESS;
}
