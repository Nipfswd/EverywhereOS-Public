/*++

Copyright (c) 2026  O&V Soft. All Rights Reserved.

Module Name:

    obtype.c

Abstract:

    Object type management and creation routines.

Author(s):

    Vlad Lymar <ggcc98765432110@gmail.com>

Environment:

    Kernel-mode only.

--*/

#include "../inc/ob.h"
#include "../inc/mm.h"

//
// Maximum number of registered object types in the system
//
#define MAX_OBJECT_TYPES 32

static POBJECT_TYPE ObpObjectTypes[MAX_OBJECT_TYPES];
static ULONG ObpObjectTypeCount = 0;

/*++

Routine Description:

    Creates and initializes a system object type descriptor.

Arguments:

    TypeNameString - Supplies the name of the object type.
    PoolType       - Supplies the pool type from which objects of this
                     type should be allocated.

Return Value:

    NTSTATUS - Status of the operation.

--*/
NTSTATUS
ObCreateObjectType(
    PCWSTR       TypeNameString,
    MM_POOL_TYPE PoolType
)
{
    POBJECT_TYPE ObjectType;

    if (TypeNameString == NULL || ObpObjectTypeCount >= MAX_OBJECT_TYPES) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // Allocate memory for the object type structure from non-paged pool.
    // Using tag 'pybO' (ObjT) for tracking.
    //
    ObjectType = (POBJECT_TYPE)MmAllocatePool(PoolTypeNonPaged, sizeof(OBJECT_TYPE), 0x7079624f); // 'pybO'
    
    if (ObjectType == NULL) {
        return STATUS_NO_MEMORY;
    }

    //
    // Initialize the UNICODE_STRING for the object type name
    //
    RtlInitUnicodeString(&ObjectType->TypeName, TypeNameString);

    //
    // Initialize counters and pool type
    //
    ObjectType->TotalNumberOfObjects     = 0;
    ObjectType->TotalNumberOfHandles     = 0;
    ObjectType->HighWaterNumberOfObjects = 0;
    ObjectType->HighWaterNumberOfHandles = 0;
    ObjectType->PoolType                 = (ULONG)PoolType;

    //
    // Register the type in the global array
    //
    ObpObjectTypes[ObpObjectTypeCount++] = ObjectType;

    return STATUS_SUCCESS;
}

/*++

Routine Description:

    Retrieves a system object type descriptor by its name, searching
    among the registered object types.

Arguments:

    TypeNameString - Supplies the name of the object type to look up.

Return Value:

    POBJECT_TYPE - Pointer to the object type descriptor, or NULL if not found.

--*/
POBJECT_TYPE
ObReferenceObjectTypeByName(
    PCWSTR TypeNameString
)
{
    ULONG i;
    UNICODE_STRING SearchString;

    if (TypeNameString == NULL) {
        return NULL;
    }

    //
    // Initialize a temporary unicode string for comparison
    //
    RtlInitUnicodeString(&SearchString, TypeNameString);

    //
    // Search through all registered object types
    //
    for (i = 0; i < ObpObjectTypeCount; i++) {
        POBJECT_TYPE ObjectType = ObpObjectTypes[i];
        
        //
        // Compare names case-insensitively using Rtl routines
        //
        if (RtlCompareUnicodeString(&ObjectType->TypeName, &SearchString, TRUE) == 0) {
            return ObjectType;
        }
    }

    return NULL;
}
