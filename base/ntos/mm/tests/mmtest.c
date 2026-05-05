/*++

Copyright (c) 2026  The EverywhereOS Authors. All Rights Reserved.

Module Name:

    mmtest.c

Abstract:

    Non-paged pool regression test suite.

    This module is the sole C source of a standalone bare-metal test kernel
    whose only purpose is to exercise MmAllocatePool and MmFreePool and
    report results over the COM1 serial port.  It is compiled and linked
    against only the MM sources (mminit.c, allocpag.c); no graphics, HAL,
    or shell code is present.

    The test kernel is built by the 'make test' target and run under
    QEMU with the -nographic and -serial stdio flags, directing all serial
    output to the host terminal.  Each test case prints either

        [ PASS ]  <TestName>

    or

        [ FAIL ]  <TestName>

    A final summary banner prints PASS (all cases passed) or FAIL (one or
    more cases failed).

    Test cases are organised into the following groups:

      Group A -- Allocation fundamentals
        A1  MmTestCaseNullOnZeroBytes
        A2  MmTestCaseBasicAllocNonNull
        A3  MmTestCaseHeaderMagicOnAlloc
        A4  MmTestCaseHeaderPoisonOnAlloc
        A5  MmTestCaseTagStoredInHeader
        A6  MmTestCaseSizeRounding
        A7  MmTestCasePayloadIsWritable

      Group B -- Pool statistics
        B1  MmTestCaseFreeDecreasesUsage
        B2  MmTestCaseStatsRestoredAfterFree
        B3  MmTestCaseBlockSizeAccounting

      Group C -- Free and coalescing
        C1  MmTestCaseNullFreeIsNoOp
        C2  MmTestCaseForwardCoalesce
        C3  MmTestCaseBackwardCoalesce
        C4  MmTestCaseFullCoalesceRestoresPool
        C5  MmTestCaseMultipleAllocsAndFree

      Group D -- Structural invariants
        D1  MmTestCasePrevBlockSizeChain
        D2  MmTestCaseFreeListSentinelMagic
        D3  MmTestCaseSizeVariants

      Group E -- Exhaustion and recovery
        E1  MmTestCaseExhaustionReturnsNull
        E2  MmTestCasePoolRecoveryAfterExhaustion

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only (bare-metal test binary, no OS services)

--*/

#include "mi.h"

#ifndef NULL
#define NULL ((void *)0)
#endif


/* =========================================================================
 * COM1 serial port driver
 *
 * The test kernel has no VGA support.  All output is written to COM1
 * (I/O base 0x3F8) at 115200 8N1.  The driver is entirely self-contained
 * within this file; no external headers are required.
 * ========================================================================= */

#define SERIAL_COM1_BASE        0x3F8U

/*
 * UART register offsets from the base I/O address.
 * When the Divisor Latch Access Bit (DLAB, LCR bit 7) is clear:
 *   +0  Receiver Buffer (read) / Transmitter Holding (write)
 *   +1  Interrupt Enable Register
 * When DLAB is set:
 *   +0  Divisor Latch LSB
 *   +1  Divisor Latch MSB
 * Always:
 *   +2  FIFO Control Register (write) / Interrupt Identification (read)
 *   +3  Line Control Register
 *   +4  Modem Control Register
 *   +5  Line Status Register
 */
#define SERIAL_REG_DATA         (SERIAL_COM1_BASE + 0U)
#define SERIAL_REG_IER          (SERIAL_COM1_BASE + 1U)
#define SERIAL_REG_FCR          (SERIAL_COM1_BASE + 2U)
#define SERIAL_REG_LCR          (SERIAL_COM1_BASE + 3U)
#define SERIAL_REG_MCR          (SERIAL_COM1_BASE + 4U)
#define SERIAL_REG_LSR          (SERIAL_COM1_BASE + 5U)

/*
 * Divisor latch registers -- only accessible when DLAB is set.
 */
#define SERIAL_REG_DLL          (SERIAL_COM1_BASE + 0U)
#define SERIAL_REG_DLH          (SERIAL_COM1_BASE + 1U)

/*
 * Line Control Register bits.
 */
#define SERIAL_LCR_DLAB         0x80U   /* divisor latch access */
#define SERIAL_LCR_8N1          0x03U   /* 8 data bits, no parity, 1 stop */

/*
 * Line Status Register bits.
 */
#define SERIAL_LSR_THRE         0x20U   /* transmit holding register empty */

/*
 * FIFO Control Register value used during init:
 *   bit 0    - enable FIFO
 *   bit 1    - clear Rx FIFO
 *   bit 2    - clear Tx FIFO
 *   bits 6-7 - 14-byte interrupt threshold
 */
#define SERIAL_FCR_INIT         0xC7U

/*
 * Modem Control Register value:
 *   bit 0 - DTR
 *   bit 1 - RTS
 *   bit 3 - OUT2 (required to enable interrupts, harmless without them)
 */
#define SERIAL_MCR_INIT         0x0BU

/*
 * Baud rate 115200: base clock 1.8432 MHz / 16 = 115200 Hz.
 * Divisor = 1.
 */
#define SERIAL_DIVISOR_LO       0x01U
#define SERIAL_DIVISOR_HI       0x00U

/*
 * Hex digit lookup table for MmTestSerialWriteHex32.
 */
static const char MmTestHexDigits[16] = {
    '0','1','2','3','4','5','6','7',
    '8','9','A','B','C','D','E','F'
};


/*++

Routine Description:

    Emits one byte to the CPU I/O bus.  This is a local copy of the outb
    inline in ke.h; it is duplicated here so that mmtest.c does not need
    to include the full kernel executive header with all its window-manager
    and graphics declarations.

Arguments:

    Port  - 16-bit I/O port number.
    Value - Byte to write.

Return Value:

    None.

--*/

static inline void
MmTestOutb(
    uint16_t Port,
    uint8_t  Value
    )
{
    __asm__ __volatile__("outb %0, %1" : : "a"(Value), "Nd"(Port));
}


/*++

Routine Description:

    Reads one byte from the CPU I/O bus.

Arguments:

    Port - 16-bit I/O port number.

Return Value:

    Byte read from Port.

--*/

static inline uint8_t
MmTestInb(
    uint16_t Port
    )
{
    uint8_t Ret;
    __asm__ __volatile__("inb %1, %0" : "=a"(Ret) : "Nd"(Port));
    return Ret;
}


/*++

Routine Description:

    Initialises COM1 for 115200 8N1 polled output.

    Sequence:
      1. Disable all UART interrupts.
      2. Set DLAB, program the 115200 baud divisor, clear DLAB.
      3. Set 8N1 line parameters.
      4. Enable and flush FIFOs.
      5. Assert DTR + RTS + OUT2 on the modem control register.

    No attempt is made to test the loopback mode; this routine assumes the
    hardware (or QEMU's emulated 16550) is functional.

Arguments:

    None.

Return Value:

    None.

--*/

static void
MmTestSerialInit(
    void
    )
{
    MmTestOutb(SERIAL_REG_IER, 0x00);

    MmTestOutb(SERIAL_REG_LCR, SERIAL_LCR_DLAB);
    MmTestOutb(SERIAL_REG_DLL, SERIAL_DIVISOR_LO);
    MmTestOutb(SERIAL_REG_DLH, SERIAL_DIVISOR_HI);

    MmTestOutb(SERIAL_REG_LCR, SERIAL_LCR_8N1);
    MmTestOutb(SERIAL_REG_FCR, SERIAL_FCR_INIT);
    MmTestOutb(SERIAL_REG_MCR, SERIAL_MCR_INIT);
}


/*++

Routine Description:

    Transmits one byte over COM1, blocking until the transmit holding
    register is empty before writing.

    Busy-waiting is intentional: the test kernel has no interrupt
    infrastructure, and the serial FIFO depth (16 bytes for the 16550A)
    is too small to buffer long strings without flow control.  At 115200
    baud each character takes approximately 87 microseconds, so the wait
    is negligible compared to the pool operations under test.

Arguments:

    Byte - Octet to transmit.

Return Value:

    None.

--*/

static void
MmTestSerialWriteByte(
    uint8_t Byte
    )
{
    while ((MmTestInb(SERIAL_REG_LSR) & SERIAL_LSR_THRE) == 0) {
    }
    MmTestOutb(SERIAL_REG_DATA, Byte);
}


/*++

Routine Description:

    Writes a null-terminated ASCII string to COM1 one byte at a time.
    A NULL pointer is silently ignored.

Arguments:

    String - Pointer to the null-terminated string to write.

Return Value:

    None.

--*/

static void
MmTestSerialWriteString(
    const char *String
    )
{
    if (!String) {
        return;
    }
    while (*String) {
        MmTestSerialWriteByte((uint8_t)*String);
        String++;
    }
}


/*++

Routine Description:

    Writes a 32-bit unsigned integer to COM1 as an 8-digit uppercase
    hexadecimal string prefixed with "0x".  Useful for printing pool
    addresses and block sizes in failure diagnostics.

Arguments:

    Value - 32-bit value to format.

Return Value:

    None.

--*/

static void
MmTestSerialWriteHex32(
    uint32_t Value
    )
{
    int i;

    MmTestSerialWriteByte('0');
    MmTestSerialWriteByte('x');
    for (i = 28; i >= 0; i -= 4) {
        MmTestSerialWriteByte((uint8_t)MmTestHexDigits[(Value >> i) & 0xFU]);
    }
}


/*++

Routine Description:

    Writes a 32-bit unsigned integer to COM1 in decimal notation.
    Leading zeroes are suppressed; the value 0 prints as "0".

Arguments:

    Value - Value to print.

Return Value:

    None.

--*/

static void
MmTestSerialWriteUint32(
    uint32_t Value
    )
{
    char     Buf[11];
    int      Pos;

    if (Value == 0) {
        MmTestSerialWriteByte('0');
        return;
    }

    Pos = 10;
    Buf[Pos] = '\0';
    while (Value != 0 && Pos > 0) {
        Pos--;
        Buf[Pos] = (char)('0' + (Value % 10U));
        Value   /= 10U;
    }
    MmTestSerialWriteString(Buf + Pos);
}


/*++

Routine Description:

    Writes a CR-LF pair to COM1, ending the current output line in a
    manner compatible with both Unix terminals and Windows HyperTerminal.

Arguments:

    None.

Return Value:

    None.

--*/

static void
MmTestSerialWriteCrLf(
    void
    )
{
    MmTestSerialWriteByte('\r');
    MmTestSerialWriteByte('\n');
}


/* =========================================================================
 * Test framework
 *
 * Each test function returns non-zero (true) on success and 0 on failure.
 * MmTestRun prints the result line and updates the global pass/fail counts.
 * MmTestPrintSummary prints the totals and the final PASS or FAIL banner.
 * ========================================================================= */

typedef int (*MM_TEST_FUNCTION)(void);

static uint32_t MmTestPassCount;
static uint32_t MmTestFailCount;


/*++

Routine Description:

    Runs one named test case.

    Invokes Function and prints a result line of the form:

        [ PASS ]  <Name>
    or
        [ FAIL ]  <Name>

    The global MmTestPassCount or MmTestFailCount is incremented according
    to the outcome.  The test is always run regardless of prior failures so
    that the full picture of the test suite state is visible in the output.

Arguments:

    Name     - Human-readable name for the test case.
    Function - Test function to invoke.  Returns non-zero for pass, 0 for fail.

Return Value:

    None.

--*/

static void
MmTestRun(
    const char         *Name,
    MM_TEST_FUNCTION    Function
    )
{
    int Result;

    Result = Function();

    if (Result) {
        MmTestSerialWriteString("[ PASS ]  ");
        MmTestPassCount++;
    } else {
        MmTestSerialWriteString("[ FAIL ]  ");
        MmTestFailCount++;
    }
    MmTestSerialWriteString(Name);
    MmTestSerialWriteCrLf();
}


/*++

Routine Description:

    Prints the final test summary after all cases have run.

    Format:
        Tests run: <N>  Passed: <P>  Failed: <F>
        <PASS|FAIL>

    The final banner is "PASS" only if MmTestFailCount is zero.

Arguments:

    None.

Return Value:

    None.

--*/

static void
MmTestPrintSummary(
    void
    )
{
    MmTestSerialWriteCrLf();
    MmTestSerialWriteString("Tests run: ");
    MmTestSerialWriteUint32(MmTestPassCount + MmTestFailCount);
    MmTestSerialWriteString("  Passed: ");
    MmTestSerialWriteUint32(MmTestPassCount);
    MmTestSerialWriteString("  Failed: ");
    MmTestSerialWriteUint32(MmTestFailCount);
    MmTestSerialWriteCrLf();

    if (MmTestFailCount == 0) {
        MmTestSerialWriteString("PASS");
    } else {
        MmTestSerialWriteString("FAIL");
    }
    MmTestSerialWriteCrLf();
}


/* =========================================================================
 * Shared helpers used by multiple test cases.
 * ========================================================================= */


/*++

Routine Description:

    Returns a pointer to the MM_POOL_HEADER that immediately precedes the
    caller-visible payload at Ptr.

    The test suite uses this to inspect Magic, BlockSize, PrevBlockSize,
    Tag, FreeNext, and FreePrev directly -- without changing allocpag.c
    to expose internals that have no business being in the public interface.

    Callers must ensure Ptr was obtained from MmAllocatePool and has not
    yet been passed to MmFreePool.

Arguments:

    Ptr - Pointer returned by MmAllocatePool.

Return Value:

    Pointer to the embedded MM_POOL_HEADER for that allocation.

--*/

static PMM_POOL_HEADER
MmTestGetHeader(
    void *Ptr
    )
{
    return (PMM_POOL_HEADER)((uint8_t *)Ptr - MM_POOL_HEADER_SIZE);
}


/*++

Routine Description:

    Fills Count bytes starting at Dst with the repeating byte pattern
    derived from Value.  Used to write and later verify known content in
    pool payloads without relying on any standard library.

Arguments:

    Dst   - Destination buffer.
    Value - Pattern byte.
    Count - Number of bytes to write.

Return Value:

    None.

--*/

static void
MmTestMemSet(
    void     *Dst,
    uint8_t   Value,
    uint32_t  Count
    )
{
    uint8_t  *p = (uint8_t *)Dst;
    uint32_t  i;

    for (i = 0; i < Count; i++) {
        p[i] = Value;
    }
}


/*++

Routine Description:

    Verifies that every byte in Buf equals Value.  Returns 1 if the check
    passes for all Count bytes, 0 on the first mismatch.

Arguments:

    Buf   - Buffer to inspect.
    Value - Expected byte value at each position.
    Count - Length of Buf in bytes.

Return Value:

    Non-zero if all bytes equal Value, 0 otherwise.

--*/

static int
MmTestMemCheck(
    const void *Buf,
    uint8_t     Value,
    uint32_t    Count
    )
{
    const uint8_t *p = (const uint8_t *)Buf;
    uint32_t       i;

    for (i = 0; i < Count; i++) {
        if (p[i] != Value) {
            return 0;
        }
    }
    return 1;
}


/* =========================================================================
 * Group A -- Allocation fundamentals
 * ========================================================================= */


/*++

Routine Description:

    A1 -- MmTestCaseNullOnZeroBytes

    Verifies that MmAllocatePool returns NULL when NumberOfBytes is zero.
    allocpag.c has an explicit early-exit for this case:

        if (NumberOfBytes == 0) { return (void *)0; }

    A NULL return is the only sane behaviour -- a zero-byte allocation
    cannot give the caller a uniquely-addressable payload, so the sentinel
    return convention from C (malloc(0) may return NULL or a unique pointer)
    is tightened to always-NULL here to keep the allocator simple.

    Pool statistics must not change as a side effect of this call.

Arguments:

    None.

Return Value:

    Non-zero if the test passes, 0 if it fails.

--*/

static int
MmTestCaseNullOnZeroBytes(
    void
    )
{
    uint32_t FreeBefore;
    uint32_t FreeAfter;
    void    *Ptr;

    MmQueryPoolStats(NULL, &FreeBefore);
    Ptr = MmAllocatePool(0, 0x54455354);
    MmQueryPoolStats(NULL, &FreeAfter);

    if (Ptr != (void *)0) {
        return 0;
    }
    if (FreeAfter != FreeBefore) {
        return 0;
    }
    return 1;
}


/*++

Routine Description:

    A2 -- MmTestCaseBasicAllocNonNull

    Verifies that a straightforward MmAllocatePool(16, Tag) call returns a
    non-NULL pointer.  The returned pointer is freed before returning so that
    subsequent tests begin from a clean pool state.

Arguments:

    None.

Return Value:

    Non-zero if the test passes, 0 if it fails.

--*/

static int
MmTestCaseBasicAllocNonNull(
    void
    )
{
    void *Ptr;

    Ptr = MmAllocatePool(16, 0x54455354);
    if (!Ptr) {
        return 0;
    }
    MmFreePool(Ptr);
    return 1;
}


/*++

Routine Description:

    A3 -- MmTestCaseHeaderMagicOnAlloc

    After a successful allocation the Magic field of the embedded
    MM_POOL_HEADER must equal MM_POOL_TAG_ALLOC (0x414C4C4F, "ALLO" in
    little-endian ASCII).  The allocator sets this field just before
    returning the payload pointer:

        block->Magic = MM_POOL_TAG_ALLOC;

    This test reads the header field directly using MmTestGetHeader and
    compares it to the expected constant.

Arguments:

    None.

Return Value:

    Non-zero if the test passes, 0 if it fails.

--*/

static int
MmTestCaseHeaderMagicOnAlloc(
    void
    )
{
    void            *Ptr;
    PMM_POOL_HEADER  Hdr;

    Ptr = MmAllocatePool(32, 0x54455354);
    if (!Ptr) {
        return 0;
    }
    Hdr = MmTestGetHeader(Ptr);

    if (Hdr->Magic != MM_POOL_TAG_ALLOC) {
        MmFreePool(Ptr);
        return 0;
    }
    MmFreePool(Ptr);
    return 1;
}


/*++

Routine Description:

    A4 -- MmTestCaseHeaderPoisonOnAlloc

    After allocation the FreeNext and FreePrev fields of the header must
    both hold MM_POOL_POISON (0xDEADBEEF).  The allocator calls
    MiRemoveFreeBlock before returning, which writes the poison value into
    both link fields:

        Block->FreeNext = (PMM_POOL_HEADER)(uintptr_t)MM_POOL_POISON;
        Block->FreePrev = (PMM_POOL_HEADER)(uintptr_t)MM_POOL_POISON;

    Any write through a stale pointer into these fields after the block is
    allocated will therefore overwrite the poison pattern, making the
    corruption detectable at the next MmFreePool call (the Magic check will
    fail because the double-free guard examines Magic, not the link fields --
    but a write into FreeNext could also corrupt a neighbouring header,
    which the PrevBlockSize chain test would catch).

Arguments:

    None.

Return Value:

    Non-zero if the test passes, 0 if it fails.

--*/

static int
MmTestCaseHeaderPoisonOnAlloc(
    void
    )
{
    void            *Ptr;
    PMM_POOL_HEADER  Hdr;
    int              Ok;

    Ptr = MmAllocatePool(32, 0x54455354);
    if (!Ptr) {
        return 0;
    }
    Hdr = MmTestGetHeader(Ptr);

    Ok = ((uintptr_t)Hdr->FreeNext == MM_POOL_POISON &&
          (uintptr_t)Hdr->FreePrev == MM_POOL_POISON);

    MmFreePool(Ptr);
    return Ok;
}


/*++

Routine Description:

    A5 -- MmTestCaseTagStoredInHeader

    Verifies that the Tag field in the MM_POOL_HEADER reflects the value
    passed to MmAllocatePool.  The allocator assigns it unconditionally:

        block->Tag = Tag;

    The test allocates with the tag 0x54455354 ('TEST' in ASCII) and reads
    the header field back, expecting an exact match.

Arguments:

    None.

Return Value:

    Non-zero if the test passes, 0 if it fails.

--*/

static int
MmTestCaseTagStoredInHeader(
    void
    )
{
    static const uint32_t TestTag = 0x54455354UL;
    void                 *Ptr;
    PMM_POOL_HEADER       Hdr;
    int                   Ok;

    Ptr = MmAllocatePool(32, TestTag);
    if (!Ptr) {
        return 0;
    }
    Hdr = MmTestGetHeader(Ptr);
    Ok  = (Hdr->Tag == TestTag);
    MmFreePool(Ptr);
    return Ok;
}


/*++

Routine Description:

    A6 -- MmTestCaseSizeRounding

    Verifies that the allocator correctly rounds up the requested size to
    the nearest MM_POOL_GRANULARITY (8) byte boundary.

    For a 1-byte request:
        payload = MiAlignUp(1, 8) = 8
        block_size = MM_POOL_HEADER_SIZE + 8 = MM_POOL_MIN_BLOCK

    For a 9-byte request (one byte over a granularity boundary):
        payload = MiAlignUp(9, 8) = 16
        block_size = MM_POOL_HEADER_SIZE + 16

    This test allocates both sizes, reads the BlockSize field from each
    header, and checks that both are exact multiples of MM_POOL_GRANULARITY
    and at least MM_POOL_HEADER_SIZE + MM_POOL_GRANULARITY.

Arguments:

    None.

Return Value:

    Non-zero if the test passes, 0 if it fails.

--*/

static int
MmTestCaseSizeRounding(
    void
    )
{
    void            *Ptr1;
    void            *Ptr9;
    PMM_POOL_HEADER  Hdr1;
    PMM_POOL_HEADER  Hdr9;
    int              Ok;

    Ptr1 = MmAllocatePool(1, 0x54455354);
    Ptr9 = MmAllocatePool(9, 0x54455354);

    if (!Ptr1 || !Ptr9) {
        MmFreePool(Ptr1);
        MmFreePool(Ptr9);
        return 0;
    }

    Hdr1 = MmTestGetHeader(Ptr1);
    Hdr9 = MmTestGetHeader(Ptr9);

    Ok = (Hdr1->BlockSize >= MM_POOL_MIN_BLOCK                   &&
          (Hdr1->BlockSize % MM_POOL_GRANULARITY) == 0           &&
          Hdr9->BlockSize  >= (MM_POOL_HEADER_SIZE + 16U)        &&
          (Hdr9->BlockSize % MM_POOL_GRANULARITY) == 0);

    MmFreePool(Ptr1);
    MmFreePool(Ptr9);
    return Ok;
}


/*++

Routine Description:

    A7 -- MmTestCasePayloadIsWritable

    Verifies that the payload region returned by MmAllocatePool is fully
    writable and that no reads back through a live allocation corrupt
    the header.

    The test writes a known byte pattern (0xA5, the classic memory test
    pattern) to every byte of a 256-byte allocation, then reads them back.
    Success requires all 256 bytes to equal 0xA5.

    This test also acts as a guard against scenarios where the allocator
    accidentally returns a pointer into the pool header or beyond the end
    of the pool arena.

Arguments:

    None.

Return Value:

    Non-zero if the test passes, 0 if it fails.

--*/

static int
MmTestCasePayloadIsWritable(
    void
    )
{
    static const uint32_t PayloadSize = 256;
    void                 *Ptr;
    int                   Ok;

    Ptr = MmAllocatePool(PayloadSize, 0x54455354);
    if (!Ptr) {
        return 0;
    }

    MmTestMemSet(Ptr, 0xA5, PayloadSize);
    Ok = MmTestMemCheck(Ptr, 0xA5, PayloadSize);

    MmFreePool(Ptr);
    return Ok;
}


/* =========================================================================
 * Group B -- Pool statistics
 * ========================================================================= */


/*++

Routine Description:

    B1 -- MmTestCaseFreeDecreasesUsage

    Verifies that MmPoolFreeBytes decreases after a successful allocation
    and that the decrease is non-zero.

    After MmAllocatePool the allocator executes:

        MmPoolFreeBytes -= block->BlockSize;

    The test snapshots MmPoolFreeBytes before and after the allocation and
    checks that the post-allocation value is strictly less than the
    pre-allocation value.

Arguments:

    None.

Return Value:

    Non-zero if the test passes, 0 if it fails.

--*/

static int
MmTestCaseFreeDecreasesUsage(
    void
    )
{
    uint32_t FreeBefore;
    uint32_t FreeAfter;
    void    *Ptr;

    MmQueryPoolStats(NULL, &FreeBefore);
    Ptr = MmAllocatePool(128, 0x54455354);
    if (!Ptr) {
        return 0;
    }
    MmQueryPoolStats(NULL, &FreeAfter);

    MmFreePool(Ptr);

    return (FreeAfter < FreeBefore);
}


/*++

Routine Description:

    B2 -- MmTestCaseStatsRestoredAfterFree

    Verifies that MmPoolFreeBytes returns to its value prior to an
    allocation once MmFreePool is called for that allocation.

    This is the end-to-end round-trip check: allocate, snapshot, free,
    re-snapshot.  If the values differ the coalescing logic or the
    MmPoolFreeBytes accounting is broken.

Arguments:

    None.

Return Value:

    Non-zero if the test passes, 0 if it fails.

--*/

static int
MmTestCaseStatsRestoredAfterFree(
    void
    )
{
    uint32_t FreeBefore;
    uint32_t FreeAfter;
    void    *Ptr;

    MmQueryPoolStats(NULL, &FreeBefore);
    Ptr = MmAllocatePool(128, 0x54455354);
    if (!Ptr) {
        return 0;
    }
    MmFreePool(Ptr);
    MmQueryPoolStats(NULL, &FreeAfter);

    return (FreeAfter == FreeBefore);
}


/*++

Routine Description:

    B3 -- MmTestCaseBlockSizeAccounting

    Verifies that the decrease in MmPoolFreeBytes after an allocation
    equals exactly the BlockSize stored in the header for that block.

    The allocator computes the total block size (header + rounded payload)
    and subtracts it from MmPoolFreeBytes.  If a split occurs the split
    tail is added back as a free block, so only the chosen block's
    BlockSize is debited.  This test captures the before/after snapshot
    and compares the delta to Hdr->BlockSize.

Arguments:

    None.

Return Value:

    Non-zero if the test passes, 0 if it fails.

--*/

static int
MmTestCaseBlockSizeAccounting(
    void
    )
{
    uint32_t         FreeBefore;
    uint32_t         FreeAfter;
    uint32_t         Delta;
    void            *Ptr;
    PMM_POOL_HEADER  Hdr;
    int              Ok;

    MmQueryPoolStats(NULL, &FreeBefore);
    Ptr = MmAllocatePool(64, 0x54455354);
    if (!Ptr) {
        return 0;
    }
    MmQueryPoolStats(NULL, &FreeAfter);

    Hdr   = MmTestGetHeader(Ptr);
    Delta = FreeBefore - FreeAfter;
    Ok    = (Delta == Hdr->BlockSize);

    MmFreePool(Ptr);
    return Ok;
}


/* =========================================================================
 * Group C -- Free and coalescing
 * ========================================================================= */


/*++

Routine Description:

    C1 -- MmTestCaseNullFreeIsNoOp

    Verifies that MmFreePool(NULL) is a safe no-op: it does not crash, does
    not alter pool statistics, and does not corrupt the free list.

    allocpag.c has the guard:

        if (!BaseAddress) { return; }

    This test calls MmFreePool(NULL) and checks that MmPoolFreeBytes is
    unchanged before and after the call.

Arguments:

    None.

Return Value:

    Non-zero if the test passes, 0 if it fails.

--*/

static int
MmTestCaseNullFreeIsNoOp(
    void
    )
{
    uint32_t FreeBefore;
    uint32_t FreeAfter;

    MmQueryPoolStats(NULL, &FreeBefore);
    MmFreePool((void *)0);
    MmQueryPoolStats(NULL, &FreeAfter);

    return (FreeAfter == FreeBefore);
}


/*++

Routine Description:

    C2 -- MmTestCaseForwardCoalesce

    Verifies the forward coalescing path in MmFreePool.

    Physical pool layout after allocating A (128 bytes) then B (128 bytes):

        [hdr_A|payload_A][hdr_B|payload_B][hdr_tail(FREE)|...]

    When A is freed:
      - A is marked FREE and inserted into the free list.
      - No forward coalesce occurs (B is still ALLOC).
      - No backward coalesce occurs (A is the first block, PrevBlockSize=0).

    When B is freed immediately after A:
      - B forward-coalesces with hdr_tail (which is FREE): B absorbs tail.
      - B backward-coalesces with A (which is FREE): A absorbs B+tail.
      - The pool collapses back to a single large free block.

    The test verifies that MmPoolFreeBytes after freeing both A and B equals
    the initial free count captured before either allocation.

Arguments:

    None.

Return Value:

    Non-zero if the test passes, 0 if it fails.

--*/

static int
MmTestCaseForwardCoalesce(
    void
    )
{
    uint32_t Initial;
    uint32_t Final;
    void    *A;
    void    *B;

    MmQueryPoolStats(NULL, &Initial);

    A = MmAllocatePool(128, 0x54455354);
    B = MmAllocatePool(128, 0x54455354);
    if (!A || !B) {
        MmFreePool(A);
        MmFreePool(B);
        return 0;
    }

    MmFreePool(A);
    MmFreePool(B);

    MmQueryPoolStats(NULL, &Final);
    return (Final == Initial);
}


/*++

Routine Description:

    C3 -- MmTestCaseBackwardCoalesce

    Verifies the backward coalescing path in MmFreePool.

    Physical pool layout after allocating A then B (same as C2):

        [hdr_A|payload_A][hdr_B|payload_B][hdr_tail(FREE)|...]

    When B is freed first:
      - B forward-coalesces with hdr_tail (FREE): B absorbs tail.
      - No backward coalesce occurs (A is still ALLOC).
      - B+tail is inserted into the free list.

    When A is freed:
      - A forward-coalesces with B+tail (which is FREE): A absorbs B+tail.
      - No backward coalesce (A is first block, PrevBlockSize=0).
      - A+B+tail is re-inserted: pool collapses to a single free block.

    This exercises the forward coalesce inside the second MmFreePool call,
    complementing C2 which exercises the backward coalesce path.  The final
    MmPoolFreeBytes must again equal the initial count.

Arguments:

    None.

Return Value:

    Non-zero if the test passes, 0 if it fails.

--*/

static int
MmTestCaseBackwardCoalesce(
    void
    )
{
    uint32_t Initial;
    uint32_t Final;
    void    *A;
    void    *B;

    MmQueryPoolStats(NULL, &Initial);

    A = MmAllocatePool(128, 0x54455354);
    B = MmAllocatePool(128, 0x54455354);
    if (!A || !B) {
        MmFreePool(A);
        MmFreePool(B);
        return 0;
    }

    MmFreePool(B);
    MmFreePool(A);

    MmQueryPoolStats(NULL, &Final);
    return (Final == Initial);
}


/*++

Routine Description:

    C4 -- MmTestCaseFullCoalesceRestoresPool

    Stress-tests coalescing by allocating a chain of five adjacent blocks
    and freeing them in a non-sequential order that exercises multiple
    coalesce paths in a single test:

        Allocate A, B, C, D, E (in order -- each abuts the next).
        Free in order: C, A, E, B, D.

    The interleaved free order guarantees:
      - Free C: isolated island; no coalesce possible (B and D are ALLOC).
      - Free A: A forward-checks B (ALLOC); no forward coalesce.
                A backward-checks nothing (first block).
      - Free E: E forward-checks tail free block; coalesces forward.
                E backward-checks D (ALLOC); no backward coalesce.
      - Free B: B forward-checks C (FREE); coalesces forward -> B+C.
                B backward-checks A (FREE); coalesces backward -> A+B+C.
      - Free D: D forward-checks E+tail (FREE); coalesces forward -> D+E+tail.
                D backward-checks C (now part of A+B+C, which is FREE);
                coalesces backward -> A+B+C+D+E+tail = full pool.

    After all five frees MmPoolFreeBytes must equal the original pool total
    captured before any allocation.

Arguments:

    None.

Return Value:

    Non-zero if the test passes, 0 if it fails.

--*/

static int
MmTestCaseFullCoalesceRestoresPool(
    void
    )
{
    uint32_t Initial;
    uint32_t Final;
    void    *A, *B, *C, *D, *E;

    MmQueryPoolStats(NULL, &Initial);

    A = MmAllocatePool(64, 0x54455354);
    B = MmAllocatePool(64, 0x54455354);
    C = MmAllocatePool(64, 0x54455354);
    D = MmAllocatePool(64, 0x54455354);
    E = MmAllocatePool(64, 0x54455354);

    if (!A || !B || !C || !D || !E) {
        MmFreePool(A);
        MmFreePool(B);
        MmFreePool(C);
        MmFreePool(D);
        MmFreePool(E);
        return 0;
    }

    MmFreePool(C);
    MmFreePool(A);
    MmFreePool(E);
    MmFreePool(B);
    MmFreePool(D);

    MmQueryPoolStats(NULL, &Final);
    return (Final == Initial);
}


/*++

Routine Description:

    C5 -- MmTestCaseMultipleAllocsAndFree

    Allocates 128 blocks of 32 bytes each using a static pointer table,
    then frees them all in order, and verifies that MmPoolFreeBytes
    returns to the value measured before any allocation.

    The allocation count (128) is chosen to be significantly larger than
    the pool header size ratio so that block splitting is exercised many
    times, while still fitting easily within the 4 MB fallback pool.

    Total memory consumed: 128 * (MM_POOL_HEADER_SIZE + 32) = 128 * 56 = 7168
    bytes, well within any QEMU memory configuration.

Arguments:

    None.

Return Value:

    Non-zero if the test passes, 0 if it fails.

--*/

#define MMTEST_MULTI_COUNT  128U

static void *MmTestMultiPtrs[MMTEST_MULTI_COUNT];

static int
MmTestCaseMultipleAllocsAndFree(
    void
    )
{
    uint32_t Initial;
    uint32_t Final;
    uint32_t i;

    MmQueryPoolStats(NULL, &Initial);

    for (i = 0; i < MMTEST_MULTI_COUNT; i++) {
        MmTestMultiPtrs[i] = MmAllocatePool(32, 0x54455354);
        if (!MmTestMultiPtrs[i]) {
            while (i-- > 0) {
                MmFreePool(MmTestMultiPtrs[i]);
            }
            return 0;
        }
    }

    for (i = 0; i < MMTEST_MULTI_COUNT; i++) {
        MmFreePool(MmTestMultiPtrs[i]);
    }

    MmQueryPoolStats(NULL, &Final);
    return (Final == Initial);
}


/* =========================================================================
 * Group D -- Structural invariants
 * ========================================================================= */


/*++

Routine Description:

    D1 -- MmTestCasePrevBlockSizeChain

    Walks the physical pool arena from MiPoolBase to MiPoolEnd and verifies
    that the PrevBlockSize field in every block (after the first) equals the
    BlockSize field of the physically preceding block.

    The allocator maintains PrevBlockSize in three places:
      1. When a block is split during allocation, the tail block's
         PrevBlockSize is set to the allocated block's (new) BlockSize, and
         the PrevBlockSize of the block after the tail is also updated.
      2. During forward coalescing in MmFreePool, the PrevBlockSize of the
         block immediately following the merged region is updated.
      3. During backward coalescing in MmFreePool, the PrevBlockSize of the
         block following the extended predecessor is updated.

    This test allocates four blocks of different sizes to ensure at least
    four splits have occurred, then walks the full physical chain checking
    each PrevBlockSize.  The four blocks are freed before returning.

Arguments:

    None.

Return Value:

    Non-zero if the test passes, 0 if it fails.

--*/

static int
MmTestCasePrevBlockSizeChain(
    void
    )
{
    void            *Ptrs[4];
    PMM_POOL_HEADER  Cur;
    PMM_POOL_HEADER  Prev;
    uint32_t         i;
    int              Ok;

    static const uint32_t Sizes[4] = { 16, 48, 24, 64 };

    for (i = 0; i < 4; i++) {
        Ptrs[i] = MmAllocatePool(Sizes[i], 0x54455354);
        if (!Ptrs[i]) {
            while (i-- > 0) {
                MmFreePool(Ptrs[i]);
            }
            return 0;
        }
    }

    Ok   = 1;
    Prev = (PMM_POOL_HEADER)MiPoolBase;
    Cur  = (PMM_POOL_HEADER)((uint8_t *)Prev + Prev->BlockSize);

    while ((uint8_t *)Cur < MiPoolEnd) {
        if (Cur->PrevBlockSize != Prev->BlockSize) {
            Ok = 0;
            break;
        }
        Prev = Cur;
        Cur  = (PMM_POOL_HEADER)((uint8_t *)Cur + Cur->BlockSize);
    }

    for (i = 0; i < 4; i++) {
        MmFreePool(Ptrs[i]);
    }
    return Ok;
}


/*++

Routine Description:

    D2 -- MmTestCaseFreeListSentinelMagic

    Verifies that the MiPoolFreeListHead sentinel node retains Magic == 0
    throughout normal allocation and deallocation activity.

    MiInitPool sets the sentinel's Magic to 0 explicitly:

        MiPoolFreeListHead.Magic = 0;

    The allocator and free routines never write to the sentinel's Magic
    field -- they only update its FreeNext and FreePrev link fields.  If
    any code accidentally treats the sentinel as a real block and calls
    MiRemoveFreeBlock or MiInsertFreeBlock on it, the Magic field would
    likely be overwritten with a non-zero value.

    This test performs an alloc/free cycle and then reads MiPoolFreeListHead.
    Magic directly.  A value of 0 confirms the sentinel was not corrupted.

Arguments:

    None.

Return Value:

    Non-zero if the test passes, 0 if it fails.

--*/

static int
MmTestCaseFreeListSentinelMagic(
    void
    )
{
    void *Ptr;

    if (MiPoolFreeListHead.Magic != 0) {
        return 0;
    }

    Ptr = MmAllocatePool(32, 0x54455354);
    if (!Ptr) {
        return 0;
    }
    MmFreePool(Ptr);

    return (MiPoolFreeListHead.Magic == 0);
}


/*++

Routine Description:

    D3 -- MmTestCaseSizeVariants

    Allocates eight blocks spanning a range of payload sizes that cover
    sub-granularity, at-granularity, over-granularity, and page-scale
    requests.  For each allocation the test verifies:

        1. The returned pointer is non-NULL.
        2. The header Magic equals MM_POOL_TAG_ALLOC.
        3. The header BlockSize is a multiple of MM_POOL_GRANULARITY.
        4. The header BlockSize is at least MM_POOL_HEADER_SIZE + the
           rounded-up payload (i.e., the allocator did not give back a
           block that is too small for the request).

    All blocks are freed before returning, and MmPoolFreeBytes is checked
    against the initial value to detect any accounting leak.

    Sizes tested:
        1     (sub-granularity, rounds up to 8)
        7     (one below granularity boundary, rounds up to 8)
        8     (exact granularity)
        9     (one above granularity boundary, rounds up to 16)
        63    (rounds up to 64)
        64    (exact; common allocation size in practice)
        1023  (rounds up to 1024)
        4096  (full page, common slab size)

Arguments:

    None.

Return Value:

    Non-zero if the test passes, 0 if it fails.

--*/

#define MMTEST_VARIANTS_COUNT  8U

static void *MmTestVariantPtrs[MMTEST_VARIANTS_COUNT];

static int
MmTestCaseSizeVariants(
    void
    )
{
    static const uint32_t ReqSizes[MMTEST_VARIANTS_COUNT] = {
        1, 7, 8, 9, 63, 64, 1023, 4096
    };
    uint32_t         Initial;
    uint32_t         Final;
    uint32_t         i;
    PMM_POOL_HEADER  Hdr;
    uint32_t         RoundedPayload;
    int              Ok;

    MmQueryPoolStats(NULL, &Initial);
    Ok = 1;

    for (i = 0; i < MMTEST_VARIANTS_COUNT; i++) {
        MmTestVariantPtrs[i] = MmAllocatePool(ReqSizes[i], 0x54455354);
        if (!MmTestVariantPtrs[i]) {
            Ok = 0;
            break;
        }

        Hdr            = MmTestGetHeader(MmTestVariantPtrs[i]);
        RoundedPayload = (ReqSizes[i] + MM_POOL_GRANULARITY - 1U) &
                         ~(MM_POOL_GRANULARITY - 1U);

        if (Hdr->Magic != MM_POOL_TAG_ALLOC) {
            Ok = 0;
        }
        if ((Hdr->BlockSize % MM_POOL_GRANULARITY) != 0) {
            Ok = 0;
        }
        if (Hdr->BlockSize < MM_POOL_HEADER_SIZE + RoundedPayload) {
            Ok = 0;
        }
    }

    for (i = 0; i < MMTEST_VARIANTS_COUNT; i++) {
        MmFreePool(MmTestVariantPtrs[i]);
    }

    MmQueryPoolStats(NULL, &Final);
    if (Final != Initial) {
        Ok = 0;
    }
    return Ok;
}


/* =========================================================================
 * Group E -- Exhaustion and recovery
 * ========================================================================= */

/*
 * Maximum number of fixed-size blocks the exhaustion tests will attempt
 * to allocate.  With a pool up to 128 MB and 4 KB per block we need up
 * to 128*1024*1024 / (4096 + 24) = ~32,500 slots to guarantee exhaustion
 * on the largest possible pool.  32768 entries (128 KB of BSS on 32-bit)
 * is safe headroom; the array is in .bss so it does not bloat the ELF.
 */
#define MMTEST_EXHAUST_BLOCK_SIZE   4096U
#define MMTEST_EXHAUST_ARRAY_SIZE   32768U

static void *MmTestExhaustPtrs[MMTEST_EXHAUST_ARRAY_SIZE];


/*++

Routine Description:

    E1 -- MmTestCaseExhaustionReturnsNull

    Verifies that MmAllocatePool returns NULL once the pool has no
    remaining free block large enough to satisfy a request.

    The test allocates MMTEST_EXHAUST_BLOCK_SIZE (4096) byte blocks in a
    loop until either the allocation returns NULL or the pointer table
    overflows (which would indicate the pool is much larger than expected).

    Success requires that at least one NULL was returned before the pointer
    table filled up.  All successfully allocated blocks are freed by E2
    (MmTestCasePoolRecoveryAfterExhaustion), which must run immediately
    after this test in the suite array.

    A static counter MmTestExhaustCount records the number of successful
    allocations for E2 to free.

Arguments:

    None.

Return Value:

    Non-zero if the test passes (NULL was returned), 0 if it fails.

--*/

static uint32_t MmTestExhaustCount;

static int
MmTestCaseExhaustionReturnsNull(
    void
    )
{
    uint32_t i;
    void    *Ptr;

    MmTestExhaustCount = 0;

    for (i = 0; i < MMTEST_EXHAUST_ARRAY_SIZE; i++) {
        Ptr = MmAllocatePool(MMTEST_EXHAUST_BLOCK_SIZE, 0x54455354);
        if (!Ptr) {
            MmTestExhaustCount = i;
            return 1;
        }
        MmTestExhaustPtrs[i] = Ptr;
    }

    /*
     * Pointer table filled without getting a NULL -- pool is larger than
     * expected.  Free everything and report failure so the test author
     * knows MMTEST_EXHAUST_ARRAY_SIZE needs to be increased.
     */
    for (i = 0; i < MMTEST_EXHAUST_ARRAY_SIZE; i++) {
        MmFreePool(MmTestExhaustPtrs[i]);
    }
    return 0;
}


/*++

Routine Description:

    E2 -- MmTestCasePoolRecoveryAfterExhaustion

    Verifies that the pool is fully usable again after all blocks
    allocated during exhaustion are returned via MmFreePool.

    This test must run immediately after E1.  It frees the
    MmTestExhaustCount blocks saved in MmTestExhaustPtrs by E1, then
    performs a fresh small allocation to prove the pool responds normally.

    Specifically:
      1. Record initial MmPoolFreeBytes (post-E1, pool is exhausted).
      2. Free all MmTestExhaustCount blocks -- the entire arena should
         coalesce back to a single free block.
      3. Attempt MmAllocatePool(64, tag) -- must succeed.
      4. Free the new block.
      5. Verify MmPoolFreeBytes equals MmPoolTotalBytes (pool fully free).

Arguments:

    None.

Return Value:

    Non-zero if the test passes, 0 if it fails.

--*/

static int
MmTestCasePoolRecoveryAfterExhaustion(
    void
    )
{
    uint32_t Total;
    uint32_t FreeAfterRecovery;
    uint32_t i;
    void    *Ptr;

    for (i = 0; i < MmTestExhaustCount; i++) {
        MmFreePool(MmTestExhaustPtrs[i]);
    }

    Ptr = MmAllocatePool(64, 0x54455354);
    if (!Ptr) {
        return 0;
    }
    MmFreePool(Ptr);

    MmQueryPoolStats(&Total, &FreeAfterRecovery);
    return (FreeAfterRecovery == Total);
}


/* =========================================================================
 * Test case table
 * ========================================================================= */

typedef struct _MM_TEST_ENTRY {
    const char         *Name;
    MM_TEST_FUNCTION    Function;
} MM_TEST_ENTRY;

static const MM_TEST_ENTRY MmTestTable[] = {
    { "A1  NullOnZeroBytes",            MmTestCaseNullOnZeroBytes          },
    { "A2  BasicAllocNonNull",          MmTestCaseBasicAllocNonNull        },
    { "A3  HeaderMagicOnAlloc",         MmTestCaseHeaderMagicOnAlloc       },
    { "A4  HeaderPoisonOnAlloc",        MmTestCaseHeaderPoisonOnAlloc      },
    { "A5  TagStoredInHeader",          MmTestCaseTagStoredInHeader        },
    { "A6  SizeRounding",               MmTestCaseSizeRounding             },
    { "A7  PayloadIsWritable",          MmTestCasePayloadIsWritable        },
    { "B1  FreeDecreasesUsage",         MmTestCaseFreeDecreasesUsage       },
    { "B2  StatsRestoredAfterFree",     MmTestCaseStatsRestoredAfterFree   },
    { "B3  BlockSizeAccounting",        MmTestCaseBlockSizeAccounting      },
    { "C1  NullFreeIsNoOp",             MmTestCaseNullFreeIsNoOp           },
    { "C2  ForwardCoalesce",            MmTestCaseForwardCoalesce          },
    { "C3  BackwardCoalesce",           MmTestCaseBackwardCoalesce         },
    { "C4  FullCoalesceRestoresPool",   MmTestCaseFullCoalesceRestoresPool },
    { "C5  MultipleAllocsAndFree",      MmTestCaseMultipleAllocsAndFree    },
    { "D1  PrevBlockSizeChain",         MmTestCasePrevBlockSizeChain       },
    { "D2  FreeListSentinelMagic",      MmTestCaseFreeListSentinelMagic    },
    { "D3  SizeVariants",               MmTestCaseSizeVariants             },
    { "E1  ExhaustionReturnsNull",      MmTestCaseExhaustionReturnsNull    },
    { "E2  PoolRecoveryAfterExhaust",   MmTestCasePoolRecoveryAfterExhaustion },
};

#define MMTEST_CASE_COUNT  (sizeof(MmTestTable) / sizeof(MmTestTable[0]))


/* =========================================================================
 * Test kernel entry point
 * ========================================================================= */


/*++

Routine Description:

    testMain -- MM regression test kernel entry point.

    Called from entry.asm after the GDT is loaded and the stack is
    established.  Performs the following sequence:

        1. Initialise COM1 at 115200 8N1 for serial output.
        2. Print the suite header banner.
        3. Call MmInit to parse the Multiboot memory map and establish
           the non-paged pool.
        4. Print the pool size discovered by MmInit.
        5. Run every test case in MmTestTable in order.
        6. Print the summary banner.
        7. Halt the processor.

    The function never returns.

Arguments:

    MultibootInfo - Pointer to the Multiboot v1 information block as
                    passed in EBX by the bootloader and pushed by entry.asm.

Return Value:

    None (does not return).

--*/

void
testMain(
    uint32_t *MultibootInfo
    )
{
    uint32_t Total;
    uint32_t Free;
    uint32_t i;

    MmTestSerialInit();

    MmTestSerialWriteString("EverywhereOS MM Regression Test Suite");
    MmTestSerialWriteCrLf();
    MmTestSerialWriteString("--------------------------------------");
    MmTestSerialWriteCrLf();

    MmInit(MultibootInfo);

    MmQueryPoolStats(&Total, &Free);

    MmTestSerialWriteString("Pool base : ");
    MmTestSerialWriteHex32((uint32_t)(uintptr_t)MiPoolBase);
    MmTestSerialWriteCrLf();
    MmTestSerialWriteString("Pool total: ");
    MmTestSerialWriteUint32(Total);
    MmTestSerialWriteString(" bytes (");
    MmTestSerialWriteUint32(Total / (1024U * 1024U));
    MmTestSerialWriteString(" MB)");
    MmTestSerialWriteCrLf();
    MmTestSerialWriteString("Pool free : ");
    MmTestSerialWriteUint32(Free);
    MmTestSerialWriteString(" bytes");
    MmTestSerialWriteCrLf();
    MmTestSerialWriteCrLf();

    MmTestPassCount = 0;
    MmTestFailCount = 0;

    for (i = 0; i < MMTEST_CASE_COUNT; i++) {
        MmTestRun(MmTestTable[i].Name, MmTestTable[i].Function);
    }

    MmTestPrintSummary();

    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }
}
