/*++

Copyright (c) 2026  The EverywhereOS Authors. All Rights Reserved.

Module Name:

    mm.h

Abstract:

    Memory Manager public interface. Declares the non-paged pool types,
    constants, and routines exported to the rest of the kernel.

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only

--*/

#ifndef _MM_H_
#define _MM_H_

#include <stdint.h>

/*
 * Pool allocation granularity in bytes. All block sizes are multiples
 * of this value. Must be a power of two.
 */
#define MM_POOL_GRANULARITY  8UL

/*
 * Magic values stored in MM_POOL_HEADER::Magic.
 * MM_POOL_TAG_FREE  - block is on the free list.
 * MM_POOL_TAG_ALLOC - block has been returned to a caller.
 * MM_POOL_POISON    - written into FreeNext/FreePrev of live allocations
 *                     so use-after-free is immediately detectable.
 */
#define MM_POOL_TAG_FREE   0x45455246UL
#define MM_POOL_TAG_ALLOC  0x414C4C4FUL
#define MM_POOL_POISON     0xDEADBEEFUL

/*
 * MM_POOL_HEADER
 *
 * Embedded immediately before every pool block (both free and allocated).
 *
 * BlockSize covers the header itself plus the usable payload; it is always
 * a multiple of MM_POOL_GRANULARITY.
 *
 * PrevBlockSize is the BlockSize of the physically preceding block in the
 * pool arena.  A value of zero marks the very first block.
 *
 * FreeNext and FreePrev link free blocks into an address-sorted doubly-
 * linked list.  In allocated blocks those fields hold MM_POOL_POISON.
 */
typedef struct _MM_POOL_HEADER {
    uint32_t Magic;
    uint32_t BlockSize;
    uint32_t PrevBlockSize;
    uint32_t Tag;
    struct _MM_POOL_HEADER *FreeNext;
    struct _MM_POOL_HEADER *FreePrev;
} MM_POOL_HEADER, *PMM_POOL_HEADER;

#define MM_POOL_HEADER_SIZE  ((uint32_t)sizeof(MM_POOL_HEADER))
#define MM_POOL_MIN_BLOCK    (MM_POOL_HEADER_SIZE + MM_POOL_GRANULARITY)

/* Bytes under management and currently unallocated -- set by mminit.c. */
extern uint32_t MmPoolTotalBytes;
extern uint32_t MmPoolFreeBytes;

/*
 * MmInit
 *
 * Must be the first Mm* call made.  Parses the Multiboot v1 information
 * block to discover available physical memory and initialises the non-paged
 * pool over the largest usable region found above 2 MB.
 *
 * MultibootInfo - Pointer to the Multiboot information structure as passed
 *                 in EBX by the bootloader and forwarded by kernelMain.
 */
void MmInit(uint32_t *MultibootInfo);

/*
 * MmAllocatePool
 *
 * Allocates NumberOfBytes bytes from the non-paged pool.
 *
 * Returns a pointer to the first usable byte (past the embedded header),
 * or NULL if the pool cannot satisfy the request.
 *
 * Tag is a four-byte caller identifier stored in the block header for
 * post-mortem debugging.
 */
void *MmAllocatePool(uint32_t NumberOfBytes, uint32_t Tag);

/*
 * MmFreePool
 *
 * Returns a block previously obtained from MmAllocatePool back to the
 * pool.  Adjacent free blocks are coalesced immediately.  Passing NULL
 * is a safe no-op.  Passing a pointer whose embedded header carries an
 * unexpected Magic value halts the processor.
 */
void MmFreePool(void *BaseAddress);

/*
 * MmQueryPoolStats
 *
 * Writes the total pool size and current free byte count into the
 * locations pointed to by TotalBytes and FreeBytes respectively.
 * Either pointer may be NULL.
 */
void MmQueryPoolStats(uint32_t *TotalBytes, uint32_t *FreeBytes);

#endif /* _MM_H_ */
