// Copyright (c) Yamaha Corporation.
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

/*++

Module Name:

    ContiguousMemory.cpp

Abstract:

    Implements a class that manages contiguous memory.


Environment:

    Kernel-mode Driver Framework

--*/

#include "Driver.h"
#include "Device.h"
#include "Public.h"
#include "Common.h"
#include "USBAudio.h"
#include "ContiguousMemory.h"
#include "USBAudioConfiguration.h"

#ifndef __INTELLISENSE__
#include "ContiguousMemory.tmh"
#endif

_Use_decl_annotations_
PAGED_CODE_SEG
ContiguousMemory *
ContiguousMemory::Create(
    const ULONG maxIrpNumber
)
{
    PAGED_CODE();
    return new (POOL_FLAG_NON_PAGED, DRIVER_TAG) ContiguousMemory(maxIrpNumber);
}

_Use_decl_annotations_
PAGED_CODE_SEG
ContiguousMemory::ContiguousMemory(
    const ULONG maxIrpNumber
)
    : c_maxIrpNumber(maxIrpNumber)
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    ASSERT(c_maxIrpNumber <= UAC_MAX_IRP_NUMBER);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
ContiguousMemory::~ContiguousMemory()
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");
    Free();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
/*++

Routine Description:

    Allocates contiguous memory for use in isochronous transfers. The optimal
    size of this area is calculated based on the contents of the USB Audio
    configuration.

Arguments:

    usbAudioStreamInterfaceGroup - USB Audio Stream Interface Group class

    maxBurstOverride -

    maxClassicFramesPerIrp -

    framesPerMs -

Return Value:

    NTSTATUS - NT status value

--*/
ContiguousMemory::Allocate(
    USBAudioStreamInterfaceGroup * usbAudioStreamInterfaceGroup,
    ULONG                          maxBurstOverride,
    ULONG                          maxClassicFramesPerIrp,
    ULONG                          framesPerMs
)
{
    NTSTATUS         status = STATUS_SUCCESS;
    PHYSICAL_ADDRESS lowestAcceptableAddress;
    PHYSICAL_ADDRESS highestAcceptableAddress;
    PHYSICAL_ADDRESS boundaryAddressMultiple;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    lowestAcceptableAddress.QuadPart = 0;
    boundaryAddressMultiple.QuadPart = 0;
    highestAcceptableAddress.QuadPart = 0xffffffff;

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - this, m_contiguousMemory, %p, %p", this, m_contiguousMemory);
    for (ULONG direction = 0; direction < toULONG(IsoDirection::NumOfIsoDirection); ++direction)
    {
        if ((static_cast<IsoDirection>(direction) == IsoDirection::In) && !usbAudioStreamInterfaceGroup->HasInputIsochronousInterface())
        {
            continue;
        }
        if ((static_cast<IsoDirection>(direction) == IsoDirection::Out) && !usbAudioStreamInterfaceGroup->HasOutputIsochronousInterface())
        {
            continue;
        }
        if ((static_cast<IsoDirection>(direction) == IsoDirection::Feedback) && !usbAudioStreamInterfaceGroup->HasFeedbackIsochronousInterface())
        {
            continue;
        }

        ULONG maxPacketSize = GetMaxPacketSize(usbAudioStreamInterfaceGroup, static_cast<IsoDirection>(direction));
        if (maxPacketSize == 0)
        {
            return STATUS_UNSUCCESSFUL;
        }
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - maxPaccketSize %s = %d", GetDirectionString((IsoDirection)direction), maxPacketSize);

        // >>comment-001<<
        m_contiguousMemorySize[direction] = maxPacketSize * maxBurstOverride * maxClassicFramesPerIrp * framesPerMs;
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - Max Contiguous Memory Size = %d", m_contiguousMemorySize[direction]);

        for (ULONG index = 0; index < c_maxIrpNumber; ++index)
        {
            m_contiguousMemory[direction][index] = (PUCHAR)MmAllocateContiguousMemorySpecifyCache(m_contiguousMemorySize[direction], lowestAcceptableAddress, highestAcceptableAddress, boundaryAddressMultiple, MmNonCached);
            if (m_contiguousMemory[direction][index] == nullptr)
            {
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "[%s][%d] = %p", GetDirectionString((IsoDirection)direction), index, m_contiguousMemory[direction][index]);

            RtlZeroMemory(m_contiguousMemory[direction][index], m_contiguousMemorySize[direction]);
        }
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - m_contiguousMemory[%d], %p", direction, m_contiguousMemory[direction]);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
ContiguousMemory::Free()
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    for (ULONG direction = 0; direction < toULONG(IsoDirection::NumOfIsoDirection); ++direction)
    {
        for (ULONG index = 0; index < c_maxIrpNumber; ++index)
        {
            if (m_contiguousMemory[direction][index] != nullptr)
            {
                MmFreeContiguousMemory(m_contiguousMemory[direction][index]);
                m_contiguousMemory[direction][index] = nullptr;
            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
ContiguousMemory::Clear()
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    for (ULONG direction = 0; direction < toULONG(IsoDirection::NumOfIsoDirection); ++direction)
    {
        for (ULONG index = 0; index < c_maxIrpNumber; ++index)
        {
            if ((m_contiguousMemory[direction][index] != nullptr) && (m_contiguousMemorySize[direction] != 0))
            {
                RtlZeroMemory(m_contiguousMemory[direction][index], m_contiguousMemorySize[direction]);
            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
bool ContiguousMemory::IsValid(
    LONG         index,
    IsoDirection direction
)
{
    bool result = false;

    ASSERT(this != nullptr);
    ASSERT(m_contiguousMemory[static_cast<ULONG>(direction)][index] != nullptr);

    if ((static_cast<ULONG>(direction) < toULONG(IsoDirection::NumOfIsoDirection)) && ((ULONG)index < c_maxIrpNumber))
    {
        if (m_contiguousMemory[static_cast<ULONG>(direction)][index] != nullptr)
        {
            result = true;
        }
    }

    return result;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
PUCHAR
ContiguousMemory::GetDataBuffer(
    LONG         index,
    IsoDirection direction
)
{
    PUCHAR dataBuffer = nullptr;

    if (IsValid(index, direction))
    {
        dataBuffer = m_contiguousMemory[static_cast<ULONG>(direction)][index];
    }

    return dataBuffer;
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG
ContiguousMemory::GetSize(
    IsoDirection direction
)
{
    ULONG size = 0;

    PAGED_CODE();

    if (IsValid(0, direction))
    {
        size = m_contiguousMemorySize[static_cast<ULONG>(direction)];
    }

    return size;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
ULONG
ContiguousMemory::GetTotalSize(
    IsoDirection direction
)
{
    ULONG size = 0;

    if (IsValid(0, direction))
    {
        size = m_contiguousMemorySize[static_cast<ULONG>(direction)] * c_maxIrpNumber;
    }

    return size;
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG
ContiguousMemory::GetMaxPacketSize(
    USBAudioStreamInterfaceGroup * usbAudioStreamInterfaceGroup,
    IsoDirection                   direction
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    if (usbAudioStreamInterfaceGroup == nullptr)
    {
        return 0;
    }

    ULONG maxPacketSize = usbAudioStreamInterfaceGroup->GetMaxPacketSize(direction);

    if (maxPacketSize < UAC_DEFAULT_MAX_PACKET_SIZE)
    {
        maxPacketSize = UAC_DEFAULT_MAX_PACKET_SIZE;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");

    return maxPacketSize;
}
