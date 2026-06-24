/*++

Copyright (c) 2026  Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    time.c

Abstract:

    Kernel executive time services. Exposes KernelGetTickCount which
    returns milliseconds elapsed since boot by converting the HAL raw
    tick count to milliseconds.

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only

--*/

#include "ke.h"

/*++

Routine Description:

    Returns the number of milliseconds elapsed since the HAL clock was
    initialized.  The value is derived from the HAL raw tick counter,
    which increments at 100 Hz (10 ms per tick).  Resolution is
    therefore 10 ms; do not use for sub-10 ms measurements.

    The counter wraps at 2^32 ms (~49.7 days of uptime).  Callers that
    measure elapsed time should compare differences, not absolute values,
    to remain wrap-safe.

Arguments:

    None.

Return Value:

    Milliseconds since HAL clock initialization (uint32_t).

Environment:

    Callable at any IRQL. HalQueryTickCount is a simple volatile read.

--*/

uint32_t
KernelGetTickCount(
    void
    )
{
    //
    // The PIT is programmed at 100 Hz, so each raw tick represents
    // exactly 10 ms.  Multiplying by 10 converts ticks to milliseconds.
    //
    return HalQueryTickCount() * 10;
}
