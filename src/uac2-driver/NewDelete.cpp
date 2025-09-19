// Copyright (c) Microsoft Corporation.
// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

/*++
    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
    KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
    PURPOSE.
Module Name:
    newdelete.cpp
Abstract:
    Contains overloaded placement new and delete operators
Environment:
    Kernel mode
--*/

#include "Private.h"
#include "NewDelete.h"

/*****************************************************************************
 * ::new(POOL_FLAGS)
 *****************************************************************************
 * New function for creating objects with a specified pool flags.
 */
_Use_decl_annotations_
PVOID operator new(
    size_t size,
    POOL_FLAGS poolFlags
)
{
    PVOID result = ExAllocatePool2(poolFlags, size, 'wNwS');

    return result;
}

/*****************************************************************************
 * ::new(POOL_FLAGS, TAG)
 *****************************************************************************
 * New function for creating objects with specified pool flags and allocation tag.
 */
_Use_decl_annotations_
PVOID operator new(
    size_t size,
    POOL_FLAGS poolFlags,
    ULONG tag
)
{
    PVOID result = ExAllocatePool2(poolFlags, size, tag);

    return result;
}

_Use_decl_annotations_
void __cdecl operator delete(
                             PVOID buffer
                             )
{
    if (buffer)
    {
        ExFreePoolWithTag(buffer, 'wNwS');
    }
}

_Use_decl_annotations_
void __cdecl operator delete(
                             PVOID buffer, 
                             ULONG tag)
{
    if (buffer)
    {
        ExFreePoolWithTag(buffer, tag);
    }
}

_Use_decl_annotations_
void __cdecl operator delete(PVOID buffer, size_t /* cbSize */)
{
    if (buffer)
    {
        ExFreePool(buffer);
    }
}

_Use_decl_annotations_
void __cdecl operator delete[](PVOID buffer)
{
    if (buffer)
    {
        ExFreePool(buffer);
    }
}

_Use_decl_annotations_
void __cdecl operator delete[](PVOID buffer, size_t /* cbSize */)
{
    if (buffer)
    {
        ExFreePool(buffer);
    }
}

