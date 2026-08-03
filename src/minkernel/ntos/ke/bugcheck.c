/*++

Copyright (c) 2026  Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    bugcheck.c

Abstract:

    Kernel bug-check support.  Provides KeBugCheck, KeBugCheckEx, and
    the internal KeBugCheck2 worker.  When a bugcheck is triggered the
    worker disables interrupts, formats a STOP message to the COM1
    debug transport, and halts the processor.

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only.  Must not call malloc, printf, or any libc
    function.

--*/

#include "ke.h"

/*
 * KiBugCheckData
 *
 * Records the most recent bugcheck parameters so the fault can be
 * examined after the fact.
 *
 * [0] - BugCheckCode
 * [1] - Parameter1
 * [2] - Parameter2
 * [3] - Parameter3
 * [4] - Parameter4
 */
uint32_t KiBugCheckData[5] = { 0, 0, 0, 0, 0 };

/*
 * KeBugCheckCount
 *
 * Incremented by the first caller that enters KeBugCheck2.  A second
 * concurrent or nested entry spins on this value and does not re-enter
 * the output path to avoid corrupting a partially-written STOP line.
 */
static volatile uint32_t KeBugCheckCount = 0;

/*++

Routine Description:

    Converts Value to its 8-character upper-case hexadecimal
    representation and writes it to the COM1 debug transport.
    No heap allocation is performed.

Arguments:

    Value - 32-bit unsigned integer to format.

Return Value:

    None.

--*/

static void
KiBugCheckPrintHex(
    uint32_t Value
    )
{
    static const char HexDigits[16] = {
        '0','1','2','3','4','5','6','7',
        '8','9','A','B','C','D','E','F'
    };

    char Buf[9];
    int  i;

    Buf[8] = '\0';
    for (i = 7; i >= 0; i--) {
        Buf[i] = HexDigits[Value & 0xFu];
        Value >>= 4;
    }

    KdComPortWriteString(Buf);
}

/*++

Routine Description:

    Core bugcheck worker.  Saves all parameters to KiBugCheckData,
    ensures COM1 is ready, emits the STOP line, then halts.

    The output format is:

        *** STOP: 0xCODE (0xP1, 0xP2, 0xP3, 0xP4)
        *** Kernel halted.

Arguments:

    BugCheckCode - Identifies the class of failure.

    Parameter1   - Supplemental parameter 1.
    Parameter2   - Supplemental parameter 2.
    Parameter3   - Supplemental parameter 3.
    Parameter4   - Supplemental parameter 4.

Return Value:

    Does not return.

--*/

static void
KeBugCheck2(
    uint32_t BugCheckCode,
    uint32_t Parameter1,
    uint32_t Parameter2,
    uint32_t Parameter3,
    uint32_t Parameter4
    )
{
    uint32_t OldCount;

    //
    // Save the bugcheck parameters for post-mortem inspection.
    //
    KiBugCheckData[0] = BugCheckCode;
    KiBugCheckData[1] = Parameter1;
    KiBugCheckData[2] = Parameter2;
    KiBugCheckData[3] = Parameter3;
    KiBugCheckData[4] = Parameter4;

    //
    // Only the first caller emits output; subsequent nested or concurrent
    // entries fall straight through to the halt loop.
    //
    OldCount = KeBugCheckCount;
    KeBugCheckCount++;

    if (OldCount == 0) {
        //
        // Guarantee the debug transport is live.  If kernelMain has
        // already called KdComPortInitialize the port address will be
        // non-zero and CpInitialize will simply reinitialise the UART.
        // If the bugcheck fires before that call we force it here.
        //
        KdComPortInitialize();

        KdComPortWriteString("\r\n*** STOP: 0x");
        KiBugCheckPrintHex(BugCheckCode);
        KdComPortWriteString(" (0x");
        KiBugCheckPrintHex(Parameter1);
        KdComPortWriteString(", 0x");
        KiBugCheckPrintHex(Parameter2);
        KdComPortWriteString(", 0x");
        KiBugCheckPrintHex(Parameter3);
        KdComPortWriteString(", 0x");
        KiBugCheckPrintHex(Parameter4);
        KdComPortWriteString(")\r\n*** Kernel halted.\r\n");
    }

    //
    // Disable interrupts and halt.  The loop re-executes HLT on every
    // wakeup (NMI, etc.) so the processor never returns to torn state.
    //
    __asm__ __volatile__("cli");
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

/*++

Routine Description:

    Issues a kernel bug check with no supplemental parameters.

Arguments:

    BugCheckCode - Identifies the type of failure that occurred.

Return Value:

    Does not return.

--*/

void
KeBugCheck(
    uint32_t BugCheckCode
    )
{
    KeBugCheck2(BugCheckCode, 0, 0, 0, 0);
}

/*++

Routine Description:

    Issues a kernel bug check with four supplemental parameters.

    Standard codes used by this kernel:
      0x19  BAD_POOL_HEADER        -- pool sentinel mismatch
      0x7F  UNEXPECTED_KERNEL_MODE_TRAP -- ASSERT failure
      0xC2  BAD_POOL_CALLER       -- invalid pool operation

Arguments:

    BugCheckCode - Identifies the type of failure.
    Parameter1   - Supplemental parameter; meaning is code-specific.
    Parameter2   - Supplemental parameter.
    Parameter3   - Supplemental parameter.
    Parameter4   - Supplemental parameter.

Return Value:

    Does not return.

--*/

void
KeBugCheckEx(
    uint32_t BugCheckCode,
    uint32_t Parameter1,
    uint32_t Parameter2,
    uint32_t Parameter3,
    uint32_t Parameter4
    )
{
    KeBugCheck2(BugCheckCode, Parameter1, Parameter2, Parameter3, Parameter4);
}
