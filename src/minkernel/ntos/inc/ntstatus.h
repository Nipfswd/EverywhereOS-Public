/*++

Copyright (c) 2026  Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    ntstatus.h

Abstract:

    Constant definitions for the NTSTATUS type.

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel and User mode.

--*/

#ifndef _NTSTATUS_H_
#define _NTSTATUS_H_

#ifndef _NTSTATUS_
#define _NTSTATUS_

#define NT_SUCCESS(Status)          (((NTSTATUS)(Status)) >= 0)
#define NT_INFORMATION(Status)      ((((ULONG)(Status)) >> 30) == 1)
#define NT_WARNING(Status)          ((((ULONG)(Status)) >> 30) == 2)
#define NT_ERROR(Status)            ((((ULONG)(Status)) >> 30) == 3)

//
// Success Codes
//
#define STATUS_SUCCESS                          ((NTSTATUS)0x00000000L)
#define STATUS_WAIT_0                           ((NTSTATUS)0x00000000L)
#define STATUS_TIMEOUT                          ((NTSTATUS)0x00000102L)
#define STATUS_PENDING                          ((NTSTATUS)0x00000103L)

//
// Warning Codes
//
#define STATUS_BUFFER_OVERFLOW                  ((NTSTATUS)0x80000005L)
#define STATUS_NO_MORE_FILES                    ((NTSTATUS)0x80000006L)

//
// Error Codes
//
#define STATUS_UNSUCCESSFUL                     ((NTSTATUS)0xC0000001L)
#define STATUS_NOT_IMPLEMENTED                  ((NTSTATUS)0xC0000002L)
#define STATUS_INFO_LENGTH_MISMATCH             ((NTSTATUS)0xC0000004L)
#define STATUS_ACCESS_VIOLATION                 ((NTSTATUS)0xC0000005L)
#define STATUS_IN_PAGE_ERROR                    ((NTSTATUS)0xC0000006L)
#define STATUS_INVALID_HANDLE                   ((NTSTATUS)0xC0000008L)
#define STATUS_INVALID_PARAMETER                ((NTSTATUS)0xC000000DL)
#define STATUS_NO_MEMORY                        ((NTSTATUS)0xC0000017L)
#define STATUS_CONFLICTING_ADDRESSES            ((NTSTATUS)0xC0000018L)
#define STATUS_ACCESS_DENIED                    ((NTSTATUS)0xC0000022L)
#define STATUS_BUFFER_TOO_SMALL                 ((NTSTATUS)0xC0000023L)
#define STATUS_OBJECT_TYPE_MISMATCH             ((NTSTATUS)0xC0000024L)
#define STATUS_OBJECT_NAME_NOT_FOUND            ((NTSTATUS)0xC0000034L)
#define STATUS_OBJECT_NAME_COLLISION            ((NTSTATUS)0xC0000035L)
#define STATUS_OBJECT_PATH_INVALID              ((NTSTATUS)0xC0000039L)
#define STATUS_OBJECT_PATH_NOT_FOUND            ((NTSTATUS)0xC000003AL)
#define STATUS_OBJECT_PATH_SYNTAX_BAD           ((NTSTATUS)0xC000003BL)
#define STATUS_INSUFFICIENT_RESOURCES           ((NTSTATUS)0xC000009AL)
#define STATUS_WORKING_SET_QUOTA                ((NTSTATUS)0xC00000A1L)
#define STATUS_STACK_OVERFLOW                   ((NTSTATUS)0xC00000FDL)
#define STATUS_COMMITMENT_LIMIT                 ((NTSTATUS)0xC000012DL)
#define STATUS_INVALID_ADDRESS                  ((NTSTATUS)0xC0000141L)

#endif /* _NTSTATUS_ */

#endif /* _NTSTATUS_H_ */
