/*++

Copyright (c) 2026  The EverywhereOS Authors. All Rights Reserved.

Module Name:

    mminit.c

Abstract:

    Memory Manager initialisation.  Parses the Multiboot v1 memory map
    supplied by the bootloader to locate the largest usable physical region
    at or above 2 MB, aligns its base to the pool granularity, caps its
    length at 128 MB, and calls MiInitPool to establish the non-paged pool
    over that region.

    If no Multiboot memory map is present (flag bit 6 is clear) a
    conservative fallback of 4 MB starting at 2 MB is assumed, which covers
    standard QEMU default configurations.

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only

--*/

#include "mi.h"

/*
 * Multiboot v1 information structure field offsets (bytes from base).
 * Only the fields consumed here are listed.
 */
#define MBINFO_OFF_FLAGS        0
#define MBINFO_OFF_MMAP_LENGTH  44
#define MBINFO_OFF_MMAP_ADDR    48

#define MBINFO_FLAG_MMAP        (1UL << 6)

/*
 * Multiboot memory-map entry field offsets.
 * Each entry starts with a 4-byte `size' that records the byte count of
 * everything that follows, so the stride between entries is (size + 4).
 */
#define MMAPE_OFF_SIZE      0
#define MMAPE_OFF_BASE_LO   4
#define MMAPE_OFF_BASE_HI   8
#define MMAPE_OFF_LEN_LO    12
#define MMAPE_OFF_LEN_HI    16
#define MMAPE_OFF_TYPE      20
#define MMAPE_TYPE_USABLE   1UL

/*
 * Lowest address eligible for pool use.  Keeps the pool well clear of the
 * kernel image (loaded at 1 MB) plus any GRUB modules or data structures
 * that may follow it.
 */
#define MI_POOL_BASE_MIN    0x00200000UL

/* Hard ceiling accepted from any single memory-map entry. */
#define MI_POOL_SIZE_MAX    (128UL * 1024UL * 1024UL)

/*
 * Fallback pool size used when the bootloader does not supply a memory
 * map.  4 MB is safe on any x86 machine with at least 8 MB of RAM.
 */
#define MI_POOL_FALLBACK_SIZE  (4UL * 1024UL * 1024UL)

uint8_t       *MiPoolBase;
uint8_t       *MiPoolEnd;
uint32_t       MmPoolTotalBytes;
uint32_t       MmPoolFreeBytes;
MM_POOL_HEADER MiPoolFreeListHead;

/*++

Routine Description:

    Reads a 32-bit little-endian unsigned integer from an unaligned byte
    pointer at a fixed byte offset.  Uses explicit byte assembly to avoid
    undefined behaviour on platforms that fault on unaligned loads.

Arguments:

    Ptr    - Base pointer.
    Offset - Byte offset from Ptr to read.

Return Value:

    The uint32_t value at Ptr + Offset.

--*/

static uint32_t
MiReadU32(
    const uint8_t *Ptr,
    uint32_t       Offset
    )
{
    const uint8_t *p = Ptr + Offset;
    uint32_t       v;

    v  = (uint32_t)p[0];
    v |= (uint32_t)p[1] << 8;
    v |= (uint32_t)p[2] << 16;
    v |= (uint32_t)p[3] << 24;

    return v;
}

/*++

Routine Description:

    Establishes the non-paged pool over the physical region
    [Base, Base + Size).

    The pool is initialised as a single free block covering the entire
    region.  MiPoolFreeListHead is set up as the sentinel for the
    address-sorted free list.

Arguments:

    Base - Starting address of the pool region.  Must be aligned to
           MM_POOL_GRANULARITY.

    Size - Size in bytes.  Must be >= MM_POOL_MIN_BLOCK.

Return Value:

    None.

--*/

void
MiInitPool(
    uint8_t  *Base,
    uint32_t  Size
    )
{
    PMM_POOL_HEADER initial;

    MiPoolBase       = Base;
    MiPoolEnd        = Base + Size;
    MmPoolTotalBytes = Size;
    MmPoolFreeBytes  = Size;

    MiPoolFreeListHead.Magic         = 0;
    MiPoolFreeListHead.BlockSize     = 0;
    MiPoolFreeListHead.PrevBlockSize = 0;
    MiPoolFreeListHead.Tag           = 0;
    MiPoolFreeListHead.FreeNext      = &MiPoolFreeListHead;
    MiPoolFreeListHead.FreePrev      = &MiPoolFreeListHead;

    initial                = (PMM_POOL_HEADER)Base;
    initial->Magic         = MM_POOL_TAG_FREE;
    initial->BlockSize     = Size;
    initial->PrevBlockSize = 0;
    initial->Tag           = 0;
    initial->FreeNext      = &MiPoolFreeListHead;
    initial->FreePrev      = &MiPoolFreeListHead;

    MiPoolFreeListHead.FreeNext = initial;
    MiPoolFreeListHead.FreePrev = initial;
}

/*++

Routine Description:

    Initialises the Memory Manager.

    Reads the Multiboot v1 information structure at MultibootInfo to obtain
    the physical memory map.  Scans every usable entry (type == 1) whose
    base address fits within 32 bits, clips or skips entries that lie
    entirely below MI_POOL_BASE_MIN, and tracks the entry with the largest
    usable span above that threshold.

    The selected region's base is aligned up to MM_POOL_GRANULARITY and
    its length is capped at MI_POOL_SIZE_MAX.  MiInitPool is then called
    to construct the pool.

    If the Multiboot flags do not indicate a valid memory map, a safe
    fallback region is used instead.

Arguments:

    MultibootInfo - Pointer to the Multiboot v1 information structure as
                    passed in EBX by the bootloader and forwarded from
                    kernelMain.

Return Value:

    None.

--*/

void
MmInit(
    uint32_t *MultibootInfo
    )
{
    uint8_t  *mbi          = (uint8_t *)MultibootInfo;
    uint32_t  flags        = MiReadU32(mbi, MBINFO_OFF_FLAGS);
    uint8_t  *entry;
    uint8_t  *mmap_end;
    uint32_t  mmap_length;
    uint32_t  mmap_addr;
    uint32_t  best_base    = 0;
    uint32_t  best_len     = 0;
    uint32_t  stride;
    uint32_t  base_lo;
    uint32_t  base_hi;
    uint32_t  len_lo;
    uint32_t  len_hi;
    uint32_t  type;
    uint32_t  base;
    uint32_t  len;
    uint32_t  align_adj;

    if (!(flags & MBINFO_FLAG_MMAP)) {
        MiInitPool((uint8_t *)MI_POOL_BASE_MIN, MI_POOL_FALLBACK_SIZE);
        return;
    }

    mmap_length = MiReadU32(mbi, MBINFO_OFF_MMAP_LENGTH);
    mmap_addr   = MiReadU32(mbi, MBINFO_OFF_MMAP_ADDR);

    entry    = (uint8_t *)mmap_addr;
    mmap_end = entry + mmap_length;

    while (entry < mmap_end) {
        stride  = MiReadU32(entry, MMAPE_OFF_SIZE);
        base_lo = MiReadU32(entry, MMAPE_OFF_BASE_LO);
        base_hi = MiReadU32(entry, MMAPE_OFF_BASE_HI);
        len_lo  = MiReadU32(entry, MMAPE_OFF_LEN_LO);
        len_hi  = MiReadU32(entry, MMAPE_OFF_LEN_HI);
        type    = MiReadU32(entry, MMAPE_OFF_TYPE);

        if (type == MMAPE_TYPE_USABLE && base_hi == 0 && len_hi == 0 && len_lo > 0) {
            base = base_lo;
            len  = len_lo;

            /* Clip region so it begins at or above MI_POOL_BASE_MIN. */
            if (base < MI_POOL_BASE_MIN) {
                uint32_t clip = MI_POOL_BASE_MIN - base;
                if (clip >= len) {
                    entry += stride + 4;
                    continue;
                }
                len  -= clip;
                base  = MI_POOL_BASE_MIN;
            }

            /* Align base up to pool granularity. */
            align_adj = (MM_POOL_GRANULARITY - (base & (MM_POOL_GRANULARITY - 1)))
                        & (MM_POOL_GRANULARITY - 1);
            if (align_adj >= len) {
                entry += stride + 4;
                continue;
            }
            base += align_adj;
            len  -= align_adj;

            if (len > best_len) {
                best_base = base;
                best_len  = len;
            }
        }

        entry += stride + 4;
    }

    if (best_len == 0) {
        MiInitPool((uint8_t *)MI_POOL_BASE_MIN, MI_POOL_FALLBACK_SIZE);
        return;
    }

    if (best_len > (uint32_t)MI_POOL_SIZE_MAX) {
        best_len = (uint32_t)MI_POOL_SIZE_MAX;
    }

    MiInitPool((uint8_t *)best_base, best_len);
}
