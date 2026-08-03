/*++

Copyright (c) 2026  Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    mi.h

Abstract:

    Memory Manager private declarations.  Included only by source files
    under base/ntos/mm.  Not for use outside that directory.

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only

--*/

#ifndef _MI_H_
#define _MI_H_

#include "mm.h"

/* -----------------------------------------------------------------------
 * x86 address space constants
 * ----------------------------------------------------------------------- */

//
// The self-map occupies the last 4 MB of kernel virtual space for PDEs
// and maps the first 4 MB of that same 4 MB range as the PTE window.
//
#define MI_KERNEL_BASE              0xC0000000UL
#define MI_USER_SPACE_TOP           0xBFFFFFFFUL
#define PDE_PER_PAGE                1024UL
#define PTE_PER_PAGE                1024UL

/* -----------------------------------------------------------------------
 * PTE address computation macros (x86 32-bit, 4 KB pages, self-map)
 * ----------------------------------------------------------------------- */

//
// With the page directory self-mapped at PDE index 768 (0xC0000000),
// the PTE window begins at virtual address 0xC0000000 and the PDE
// window begins at 0xC0300000.
//
#define MI_PTE_BASE                 0xC0000000UL
#define MI_PDE_BASE                 0xC0300000UL

#define MI_GET_PTE_ADDRESS(Va) \
    ((PMMPTE)(MI_PTE_BASE + (((ULONG_PTR)(Va) >> PAGE_SHIFT) << 2)))

#define MI_GET_PDE_ADDRESS(Va) \
    ((PMMPTE)(MI_PDE_BASE + (((ULONG_PTR)(Va) >> 22) << 2)))

/* -----------------------------------------------------------------------
 * PFN database access
 * ----------------------------------------------------------------------- */

#define MI_PFN_ELEMENT(PageFrameIndex) \
    (&MmPfnDatabase[(PageFrameIndex)])

/* -----------------------------------------------------------------------
 * Virtual/physical address conversions
 * ----------------------------------------------------------------------- */

#define MI_VA_TO_VPN(Va)            ((ULONG_PTR)(Va) >> PAGE_SHIFT)
#define MI_VPN_TO_VA(Vpn)           ((PVOID)((ULONG_PTR)(Vpn) << PAGE_SHIFT))
#define MI_VPN_TO_VA_ENDING(Vpn)    ((PVOID)(((ULONG_PTR)(Vpn) << PAGE_SHIFT) | (PAGE_SIZE - 1UL)))
#define MI_VA_TO_PAGE(Va)           ((ULONG_PTR)(Va) >> PAGE_SHIFT)
#define MI_ROUND_TO_SIZE(L,A)       (((ULONG_PTR)(L) + ((A) - 1UL)) & ~((ULONG_PTR)((A) - 1UL)))

/* -----------------------------------------------------------------------
 * Physical memory layout constants
 * ----------------------------------------------------------------------- */

//
// First physical byte eligible for pool use -- keeps clear of the kernel
// image loaded at 1 MB by the bootloader.
//
#define MI_POOL_BASE_MIN            0x00200000UL
#define MI_POOL_REGION_MAX          (128UL * 1024UL * 1024UL)
#define MI_POOL_FALLBACK_SIZE       (4UL   * 1024UL * 1024UL)

//
// Physical address of the initial page directory.  Lives in the second
// 4 KB page of physical RAM (below GRUB's stage2 area).
//
#define MI_PAGE_DIRECTORY_PHYS      0x00001000UL

/* -----------------------------------------------------------------------
 * PFN lock  (single-CPU: cli/sti -- no queued spinlock required yet)
 * ----------------------------------------------------------------------- */

static inline void
MiDisableInterrupts(void)
{
    __asm__ __volatile__("cli" ::: "memory");
}

static inline void
MiEnableInterrupts(void)
{
    __asm__ __volatile__("sti" ::: "memory");
}

#define LOCK_PFN(OldIrql)   \
    (OldIrql) = 0;          \
    MiDisableInterrupts()

#define UNLOCK_PFN(OldIrql) \
    (void)(OldIrql);        \
    MiEnableInterrupts()

#define LOCK_POOL()         MiDisableInterrupts()
#define UNLOCK_POOL()       MiEnableInterrupts()

/* -----------------------------------------------------------------------
 * PTE state predicates
 * ----------------------------------------------------------------------- */

#define MI_PTE_IS_ZERO(Pte)         ((Pte).Long == 0)
#define MI_PTE_IS_VALID(Pte)        ((Pte).Hard.Valid != 0)
#define MI_PTE_IS_TRANSITION(Pte)   \
    (((Pte).Trans.Transition != 0) && ((Pte).Trans.Prototype == 0))
#define MI_PTE_IS_DEMAND_ZERO(Pte)  \
    (((Pte).Long & 0xFFFFFFFEUL) == 0)

/* -----------------------------------------------------------------------
 * Demand-zero PTE construction
 * ----------------------------------------------------------------------- */

#define MI_INITIALIZE_DEMAND_ZERO_PTE(PtePtr, Prot)     \
    {                                                   \
        (PtePtr)->Long             = 0;                 \
        (PtePtr)->Soft.Protection  = (Prot);            \
    }

/* -----------------------------------------------------------------------
 * Working set empty slot marker
 * ----------------------------------------------------------------------- */

#define WSLE_NULL_INDEX             ((WSLE_NUMBER)(~0UL))

/* -----------------------------------------------------------------------
 * ASSERT (debug builds only)
 * ----------------------------------------------------------------------- */

#ifdef DBG
#define ASSERT(exp)                                                     \
    if (!(exp)) {                                                       \
        KeBugCheckEx(0x7FUL, (ULONG_PTR)__LINE__, 0, 0, 0);            \
    }
#else
#define ASSERT(exp) ((void)(exp))
#endif

extern VOID KeBugCheckEx(ULONG BugCheckCode, ULONG_PTR P1, ULONG_PTR P2,
                         ULONG_PTR P3, ULONG_PTR P4);

/* -----------------------------------------------------------------------
 * Internal globals (defined in miglobal.c)
 * ----------------------------------------------------------------------- */

extern LIST_ENTRY   MiNonPagedPoolFreeListHead[POOL_SMALL_LISTS + 1];
extern ULONG_PTR    MiNonPagedPoolStart;
extern ULONG_PTR    MiNonPagedPoolEnd;
extern ULONG_PTR    MiPagedPoolStart;
extern ULONG_PTR    MiPagedPoolEnd;
extern PMMPTE       MiHyperSpacePte;
extern PMMWSL       MmSystemCacheWorkingSetList;
extern MMSUPPORT    MmSystemCacheWs;

/* -----------------------------------------------------------------------
 * Internal function declarations
 * ----------------------------------------------------------------------- */

//
// mminit.c
//
VOID MiEnablePaging(VOID);

//
// pfnlist.c
//
VOID        MiInsertPageInZeroedList(PFN_NUMBER PageFrameIndex);
VOID        MiInsertPageInFreeList(PFN_NUMBER PageFrameIndex);
VOID        MiInsertPageInStandbyList(PFN_NUMBER PageFrameIndex);
VOID        MiInsertPageInModifiedList(PFN_NUMBER PageFrameIndex);
VOID        MiInsertPageInBadList(PFN_NUMBER PageFrameIndex);
PFN_NUMBER  MiRemoveZeroPage(ULONG Color);
PFN_NUMBER  MiRemoveAnyPage(ULONG Color);
PFN_NUMBER  MiRemovePageFromFreeList(VOID);
VOID        MiUnlinkPageFromList(PFN_NUMBER PageFrameIndex);

//
// allocpag.c
//
VOID MiInitializePfnEntry(PFN_NUMBER PageFrameIndex, PMMPTE TargetPte, ULONG OldIrql);
VOID MiDecrementShareCount(PMMPFN Pfn1, PFN_NUMBER PageFrameIndex);

//
// pool.c
//
VOID MiInitializeNonPagedPool(ULONG_PTR PoolBase, ULONG PoolSize);

//
// addrsup.c
//
PMMVAD   MiLocateAddressInTree(ULONG_PTR Vpn, PMMVAD Root);
VOID     MiInsertNode(PMMVAD Node, PMMVAD *Root);
VOID     MiRemoveNode(PMMVAD Node, PMMVAD *Root);
PMMVAD   MiCheckForConflictingNode(ULONG_PTR StartVpn, ULONG_PTR EndVpn, PMMVAD Root);
NTSTATUS MiFindEmptyAddressRangeInTree(SIZE_T SizeOfRange, ULONG_PTR Alignment,
                                       PMMVAD Root, PULONG_PTR Base);

//
// vadtree.c
//
NTSTATUS MiInsertVad(PMMVAD Vad, PMMVAD *VadRoot);
VOID     MiRemoveVad(PMMVAD Vad, PMMVAD *VadRoot);
PMMVAD   MiLocateAddress(PVOID VirtualAddress, PMMVAD Root);

//
// pagfault.c
//
NTSTATUS MiDispatchFault(ULONG FaultStatus, PVOID VirtualAddress,
                         PMMPTE PointerPte, PVOID TrapFrame);
NTSTATUS MiResolveDemandZeroFault(PVOID VirtualAddress, PMMPTE PointerPte,
                                  ULONG Protection);
NTSTATUS MiResolveTransitionFault(PVOID VirtualAddress, PMMPTE PointerPte);

//
// wslist.c
//
VOID        MiInitializeWorkingSetList(PMMSUPPORT WsInfo);
WSLE_NUMBER MiInsertWsle(PVOID VirtualAddress, PMMSUPPORT WsInfo, PMMPFN Pfn1);
VOID        MiRemoveWsle(WSLE_NUMBER WslEntry, PMMWSL WorkingSetList);

//
// wsmanage.c
//
ULONG MiTrimWorkingSet(PMMSUPPORT WsInfo, ULONG TrimCount);

//
// sysptes.c
//
PMMPTE  MiReserveSystemPtes(ULONG NumberOfPtes);
VOID    MiReleaseSystemPtes(PMMPTE StartingPte, ULONG NumberOfPtes);

//
// hypermap.c
//
PVOID   MiMapPageInHyperSpace(PFN_NUMBER PageFrameIndex, ULONG *OldIrql);
VOID    MiUnmapPageInHyperSpace(PVOID VirtualAddress, ULONG OldIrql);

#endif /* _MI_H_ */
