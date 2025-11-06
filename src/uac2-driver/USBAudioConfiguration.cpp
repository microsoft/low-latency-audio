// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

/*++

Module Name:

    USBAudioConfiguration.cpp

Abstract:

    Implement classes that parses and manages the USB device descriptor.

Environment:

    Kernel-mode Driver Framework

--*/

#include "Driver.h"
#include "Device.h"
#include "Public.h"
#include "Private.h"
#include "Common.h"
#include "DeviceControl.h"
#include "ErrorStatistics.h"
#include "USBAudioConfiguration.h"

#ifndef __INTELLISENSE__
#include "USBAudioConfiguration.tmh"
#endif

#define ConvertBmaControls(bmControls) (((ULONG)bmControls[0]) | (((ULONG)bmControls[1]) << 8) | (((ULONG)bmControls[2]) << 16) | (((ULONG)bmControls[3]) << 24))
#define LANGID_EN_US                   0x0409

// ======================================================================

template <class T, ULONG I>
PAGED_CODE_SEG
_Use_decl_annotations_
VariableArray<T, I>::VariableArray()
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");
}

template <class T, ULONG I>
_Use_decl_annotations_
PAGED_CODE_SEG
VariableArray<T, I>::~VariableArray()
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");
    if (m_memory != nullptr)
    {
        WdfObjectDelete(m_memory);
        m_memory = nullptr;
        m_array = nullptr;
    }
    m_sizeOfArray = 0;
    m_numOfArray = 0;
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");
}

template <class T, ULONG I>
_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
VariableArray<T, I>::Set(
    WDFOBJECT parentObject,
    ULONG     index,
    T         data
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    if (index >= m_sizeOfArray)
    {
        WDFMEMORY memoryOld = m_memory;
        T *       arrayOld = m_array;
        ULONG     sizeOfArrayOld = m_sizeOfArray;
        m_array = nullptr;
        if (index < I)
        {
            status = Allocate(parentObject, I);
        }
        else
        {
            status = Allocate(parentObject, index + I);
        }
        if (NT_SUCCESS(status))
        {
            m_numOfArray++;
            if ((arrayOld != nullptr))
            {
                if (m_array != nullptr)
                {
                    RtlCopyMemory(m_array, arrayOld, sizeof(T) * sizeOfArrayOld);
                }
                if (memoryOld != nullptr)
                {
                    WdfObjectDelete(memoryOld);
                    memoryOld = nullptr;
                    arrayOld = nullptr;
                }
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, "delete arrayOld");
            }
            m_array[index] = data;
        }
    }
    else
    {
        m_array[index] = data;
        m_numOfArray++;
    }
    return status;
}

template <class T, ULONG I>
_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
VariableArray<T, I>::Get(ULONG index, T & data) const
{
    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(index >= m_numOfArray, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE(m_array == nullptr, STATUS_UNSUCCESSFUL);

    data = m_array[index];

    return STATUS_SUCCESS;
}

template <class T, ULONG I>
_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
VariableArray<T, I>::Append(
    WDFOBJECT parentObject,
    T         data
)
{
    PAGED_CODE();

    return Set(parentObject, m_numOfArray, data);
}

template <class T, ULONG I>
_Use_decl_annotations_
PAGED_CODE_SEG
ULONG VariableArray<T, I>::GetNumOfArray() const
{
    PAGED_CODE();

    return m_numOfArray;
}

template <class T, ULONG I>
_Use_decl_annotations_
PAGED_CODE_SEG
void VariableArray<T, I>::Report() const
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - m_sizeOfArray = %d", m_sizeOfArray);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - m_numOfArray  = %d", m_numOfArray);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - m_array       = %p", m_array);

    if (m_array != nullptr)
    {
        for (ULONG index = 0; index < m_sizeOfArray; index++)
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - m_array[%d] = %u", index, (ULONG)m_array[index]);
        }
    }
}

template <class T, ULONG I>
_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
VariableArray<T, I>::Allocate(
    WDFOBJECT parentObject,
    ULONG     sizeOfArray
)
{
    WDF_OBJECT_ATTRIBUTES attributes;

    PAGED_CODE();

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = parentObject;

    RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(T) * sizeOfArray, &m_memory, (PVOID *)&m_array));

    RtlZeroMemory(m_array, sizeof(T) * sizeOfArray);

    m_sizeOfArray = sizeOfArray;

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, "Allocate(%d) ", sizeOfArray);

    return STATUS_SUCCESS;
}

// ======================================================================

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioEndpoint::USBAudioEndpoint(
    WDFOBJECT                parentObject,
    PUSB_ENDPOINT_DESCRIPTOR endpoint
)
    : m_parentObject(parentObject), m_endpointDescriptor(endpoint)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioEndpoint::~USBAudioEndpoint()
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioEndpoint * USBAudioEndpoint::Create(
    WDFOBJECT                      parentObject,
    const PUSB_ENDPOINT_DESCRIPTOR descriptor
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    USBAudioEndpoint * endpoint = new (POOL_FLAG_NON_PAGED, DRIVER_TAG) USBAudioEndpoint(parentObject, descriptor);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");

    return endpoint;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudioEndpoint::GetEndpointAddress() const
{
    PAGED_CODE();

    return m_endpointDescriptor->bEndpointAddress;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudioEndpoint::GetEndpointAttribute() const
{
    PAGED_CODE();

    return m_endpointDescriptor->bmAttributes;
}

_Use_decl_annotations_
PAGED_CODE_SEG
IsoDirection USBAudioEndpoint::GetDirection() const
{
    IsoDirection direction;
    PAGED_CODE();

    if (USB_ENDPOINT_DIRECTION_IN(m_endpointDescriptor->bEndpointAddress))
    {
        if (USB_ENDPOINT_TYPE_ISOCHRONOUS_USAGE(m_endpointDescriptor->bmAttributes) == USB_ENDPOINT_TYPE_ISOCHRONOUS_USAGE_FEEDBACK_ENDPOINT)
        {
            direction = IsoDirection::Feedback;
        }
        else
        {
            direction = IsoDirection::In;
        }
    }
    else
    {
        direction = IsoDirection::Out;
    }

    return direction;
}

_Use_decl_annotations_
PAGED_CODE_SEG
USHORT USBAudioEndpoint::GetMaxPacketSize() const
{
    PAGED_CODE();

    return m_endpointDescriptor->wMaxPacketSize;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudioEndpoint::GetInterval() const
{
    PAGED_CODE();

    return m_endpointDescriptor->bInterval;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudioEndpoint::GetAttributes() const
{
    PAGED_CODE();

    return m_endpointDescriptor->bmAttributes;
}

// ======================================================================

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioEndpointCompanion::USBAudioEndpointCompanion(
    WDFOBJECT                                     parentObject,
    PUSB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR endpoint
)
    : m_parentObject(parentObject), m_endpointCompanionDescriptor(endpoint)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioEndpointCompanion::~USBAudioEndpointCompanion()
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioEndpointCompanion * USBAudioEndpointCompanion::Create(
    WDFOBJECT                                           parentObject,
    const PUSB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR descriptor
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    USBAudioEndpointCompanion * endpoint = new (POOL_FLAG_NON_PAGED, DRIVER_TAG) USBAudioEndpointCompanion(parentObject, descriptor);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");
    return endpoint;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudioEndpointCompanion::GetMaxBurst() const
{
    PAGED_CODE();

    return m_endpointCompanionDescriptor->bMaxBurst;
}

_Use_decl_annotations_
PAGED_CODE_SEG
USHORT USBAudioEndpointCompanion::GetBytesPerInterval() const
{
    PAGED_CODE();

    return m_endpointCompanionDescriptor->wBytesPerInterval;
}

// ======================================================================

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioInterface::USBAudioInterface(
    WDFOBJECT                 parentObject,
    PUSB_INTERFACE_DESCRIPTOR descriptor
)
    : m_parentObject(parentObject), m_interfaceDescriptor(descriptor)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioInterface::~USBAudioInterface()
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    if (m_usbAudioEndpoints != nullptr)
    {
        for (ULONG index = 0; index < GetNumEndpoints(); index++)
        {
            if (m_usbAudioEndpoints[index] != nullptr)
            {
                delete m_usbAudioEndpoints[index];
                m_usbAudioEndpoints[index] = nullptr;
            }
        }
        m_usbAudioEndpoints = nullptr;
    }

    if (m_usbAudioEndpointsMemory != nullptr)
    {
        WdfObjectDelete(m_usbAudioEndpointsMemory);
        m_usbAudioEndpointsMemory = nullptr;
    }

    if (m_usbAudioEndpointCompanions != nullptr)
    {
        for (ULONG index = 0; index < GetNumEndpoints(); index++)
        {
            if (m_usbAudioEndpointCompanions[index] != nullptr)
            {
                delete m_usbAudioEndpointCompanions[index];
                m_usbAudioEndpointCompanions[index] = nullptr;
            }
        }
        m_usbAudioEndpointCompanions = nullptr;
    }

    if (m_usbAudioEndpointCompanionsMemory != nullptr)
    {
        WdfObjectDelete(m_usbAudioEndpointCompanionsMemory);
        m_usbAudioEndpointCompanionsMemory = nullptr;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudioInterface::SetEndpoint(const PUSB_ENDPOINT_DESCRIPTOR endpoint)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(GetNumEndpoints() == 0, STATUS_INVALID_PARAMETER);

    if (m_usbAudioEndpoints == nullptr)
    {
        WDF_OBJECT_ATTRIBUTES attributes;

        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = m_parentObject;

        RETURN_NTSTATUS_IF_TRUE(m_numOfEndpoint != 0, STATUS_UNSUCCESSFUL);

        RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(USBAudioEndpoint *) * GetNumEndpoints(), &m_usbAudioEndpointsMemory, (PVOID *)&m_usbAudioEndpoints));
        RtlZeroMemory(m_usbAudioEndpoints, sizeof(USBAudioEndpoint *) * GetNumEndpoints());
    }

    m_usbAudioEndpoints[m_numOfEndpoint] = USBAudioEndpoint::Create(m_parentObject, endpoint);
    RETURN_NTSTATUS_IF_TRUE(m_usbAudioEndpoints[m_numOfEndpoint] == nullptr, STATUS_INSUFFICIENT_RESOURCES);
    m_numOfEndpoint++;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudioInterface::GetLength() const
{
    PAGED_CODE();

    return m_interfaceDescriptor->bLength;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudioInterface::GetDescriptorType() const
{
    PAGED_CODE();

    return m_interfaceDescriptor->bDescriptorType;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudioInterface::GetInterfaceNumber() const
{
    PAGED_CODE();

    return m_interfaceDescriptor->bInterfaceNumber;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudioInterface::GetAlternateSetting() const
{
    PAGED_CODE();

    return m_interfaceDescriptor->bAlternateSetting;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudioInterface::GetNumEndpoints() const
{
    PAGED_CODE();

    return m_interfaceDescriptor->bNumEndpoints;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudioInterface::GetInterfaceClass() const
{
    PAGED_CODE();

    return m_interfaceDescriptor->bInterfaceClass;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudioInterface::GetInterfaceSubClass() const
{
    PAGED_CODE();

    return m_interfaceDescriptor->bInterfaceSubClass;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudioInterface::GetInterfaceProtocol() const
{
    PAGED_CODE();

    return m_interfaceDescriptor->bInterfaceProtocol;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudioInterface::GetInterface() const
{
    PAGED_CODE();

    return m_interfaceDescriptor->iInterface;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioInterface::GetEndpointAddress(ULONG index, UCHAR & bEndpointAddress) const
{
    bool result = false;

    PAGED_CODE();

    if ((m_usbAudioEndpoints != nullptr) && (index < GetNumEndpoints()) && (m_usbAudioEndpoints[index] != nullptr))
    {
        bEndpointAddress = m_usbAudioEndpoints[index]->GetEndpointAddress();
        result = true;
    }

    return result;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudioInterface::GetEndpointAddress()
{
    UCHAR endpointAddress = 0;

    PAGED_CODE();

    if (m_usbAudioEndpoints != nullptr)
    {
        for (ULONG index = 0; index < GetNumEndpoints(); index++)
        {
            if ((m_usbAudioEndpoints[index] != nullptr) && ((m_usbAudioEndpoints[index]->GetDirection() == IsoDirection::In) || (m_usbAudioEndpoints[index]->GetDirection() == IsoDirection::Out)))
            {
                if (GetEndpointAddress(0, endpointAddress))
                {
                    return endpointAddress;
                }
            }
        }
    }
    return endpointAddress;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioInterface::GetEndpointAttribute(ULONG index, UCHAR & endpointAttribute) const
{
    bool result = false;

    PAGED_CODE();

    if ((m_usbAudioEndpoints != nullptr) && (index < GetNumEndpoints()) && (m_usbAudioEndpoints[index] != nullptr))
    {
        endpointAttribute = m_usbAudioEndpoints[index]->GetEndpointAttribute();
        result = true;
    }

    return result;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudioInterface::GetEndpointAttribute()
{
    UCHAR endpointAttribute = 0;

    PAGED_CODE();

    if (m_usbAudioEndpoints != nullptr)
    {
        for (ULONG index = 0; index < GetNumEndpoints(); index++)
        {
            if ((m_usbAudioEndpoints[index] != nullptr) && ((m_usbAudioEndpoints[index]->GetDirection() == IsoDirection::In) || (m_usbAudioEndpoints[index]->GetDirection() == IsoDirection::Out)))
            {
                if (GetEndpointAttribute(0, endpointAttribute))
                {
                    return endpointAttribute;
                }
            }
        }
    }
    return endpointAttribute;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioInterface::GetMaxPacketSize(
    IsoDirection direction,
    USHORT &     maxPacketSize
) const
{
    bool   result = false;
    USHORT currentMaxPacketSize = 0;

    PAGED_CODE();

    if (m_usbAudioEndpoints != nullptr)
    {
        for (ULONG index = 0; index < GetNumEndpoints(); index++)
        {
            if ((m_usbAudioEndpoints[index] != nullptr) && (m_usbAudioEndpoints[index]->GetDirection() == direction))
            {
                if (m_usbAudioEndpoints[index]->GetMaxPacketSize() > currentMaxPacketSize)
                {
                    currentMaxPacketSize = m_usbAudioEndpoints[index]->GetMaxPacketSize();
                }

                if ((m_usbAudioEndpointCompanions != nullptr) && (m_usbAudioEndpointCompanions[index] != nullptr) && (m_usbAudioEndpointCompanions[index]->GetMaxBurst() != 0))
                {
                    if (m_usbAudioEndpointCompanions[index]->GetBytesPerInterval() > currentMaxPacketSize)
                    {
                        currentMaxPacketSize = m_usbAudioEndpointCompanions[index]->GetBytesPerInterval();

                        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "MaxPacketSize updated by endpoint companion descriptor, direction %s, size %u", GetDirectionString(direction), currentMaxPacketSize);
                    }
                }
                result = true;
            }
        }
    }

    if (result)
    {
        maxPacketSize = currentMaxPacketSize;
    }

    return result;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioInterface::GetMaxPacketSize(
    ULONG    index,
    USHORT & maxPacketSize
) const
{
    bool result = false;

    PAGED_CODE();

    if ((m_usbAudioEndpoints != nullptr) && (index < GetNumEndpoints()) && (m_usbAudioEndpoints[index] != nullptr))
    {
        maxPacketSize = m_usbAudioEndpoints[index]->GetMaxPacketSize();
        result = true;
    }

    return result;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioInterface::GetInterval(ULONG index, UCHAR & bInterval) const
{
    bool result = false;

    PAGED_CODE();

    if ((m_usbAudioEndpoints != nullptr) && (index < GetNumEndpoints()) && (m_usbAudioEndpoints[index] != nullptr))
    {
        bInterval = m_usbAudioEndpoints[index]->GetInterval();
        result = true;
    }

    return result;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioInterface::GetAttributes(ULONG index, UCHAR & bmAttributes) const
{
    bool result = false;

    PAGED_CODE();

    if ((m_usbAudioEndpoints != nullptr) && (index < GetNumEndpoints()) && (m_usbAudioEndpoints[index] != nullptr))
    {
        bmAttributes = m_usbAudioEndpoints[index]->GetAttributes();
        result = true;
    }
    return result;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudioInterface::SetEndpointCompanion(const PUSB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR endpoint)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(GetNumEndpoints() == 0, STATUS_INVALID_PARAMETER);

    if (m_usbAudioEndpointCompanions == nullptr)
    {
        WDF_OBJECT_ATTRIBUTES attributes;

        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = m_parentObject;

        RETURN_NTSTATUS_IF_TRUE(m_numOfEndpointCompanion != 0, STATUS_UNSUCCESSFUL);

        RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(USBAudioEndpointCompanion *) * GetNumEndpoints(), &m_usbAudioEndpointCompanionsMemory, (PVOID *)&m_usbAudioEndpointCompanions));
        RtlZeroMemory(m_usbAudioEndpointCompanions, sizeof(USBAudioEndpointCompanion *) * GetNumEndpoints());
    }

    m_usbAudioEndpointCompanions[m_numOfEndpointCompanion] = USBAudioEndpointCompanion::Create(m_parentObject, endpoint);
    RETURN_NTSTATUS_IF_TRUE(m_usbAudioEndpointCompanions[m_numOfEndpointCompanion] == nullptr, STATUS_INSUFFICIENT_RESOURCES);
    m_numOfEndpointCompanion++;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioInterface::GetBytesPerInterval(ULONG index, USHORT & wBytesPerInterval) const
{
    bool result = false;

    PAGED_CODE();

    if ((m_usbAudioEndpointCompanions != nullptr) && (index < GetNumEndpoints()) && (m_usbAudioEndpointCompanions[index] != nullptr))
    {
        wBytesPerInterval = m_usbAudioEndpointCompanions[index]->GetBytesPerInterval();
        result = true;
    }

    return result;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioInterface::IsEndpointTypeSupported(
    UCHAR endpointType
)
{
    PAGED_CODE();

    for (ULONG index = 0; index < GetNumEndpoints(); index++)
    {
        UCHAR endpointAttribute = 0;
        if (GetEndpointAttribute(index, endpointAttribute))
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - index %u, endpointAttribute 0x%x, 0x%x", index, endpointAttribute, endpointType);
            if ((endpointAttribute & USB_ENDPOINT_TYPE_MASK) == endpointType)
            {
                return true;
            }
        }
    }
    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioInterface::IsEndpointTypeIsochronousSynchronizationSupported(
    UCHAR synchronizationType
)
{
    PAGED_CODE();

    for (ULONG index = 0; index < GetNumEndpoints(); index++)
    {
        UCHAR endpointAttribute = 0;
        if (GetEndpointAttribute(index, endpointAttribute))
        {
            if ((endpointAttribute & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_TYPE_ISOCHRONOUS)
            {
                if ((endpointAttribute & USB_ENDPOINT_TYPE_ISOCHRONOUS_SYNCHRONIZATION_MASK) == synchronizationType)
                {
                    return true;
                }
            }
        }
    }
    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioInterface::IsSupportDirection(
    bool isInput
)
{
    PAGED_CODE();

    for (ULONG index = 0; index < GetNumEndpoints(); index++)
    {
        UCHAR endpointAddress = 0;
        UCHAR endpointAttribute = 0;
        if (GetEndpointAddress(index, endpointAddress) && GetEndpointAttribute(index, endpointAttribute))
        {
            if ((endpointAttribute & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_TYPE_ISOCHRONOUS)
            {
                bool result = false;
                if (isInput)
                {
                    result = USB_ENDPOINT_DIRECTION_IN(endpointAddress) ? true : false;
                }
                else
                {
                    result = USB_ENDPOINT_DIRECTION_OUT(endpointAddress) ? true : false;
                }
                return result;
            }
        }
    }
    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
PUSB_INTERFACE_DESCRIPTOR & USBAudioInterface::GetInterfaceDescriptor()
{
    PAGED_CODE();

    return m_interfaceDescriptor;
}

// ======================================================================

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioControlInterface::USBAudioControlInterface(
    WDFOBJECT                 parentObject,
    PUSB_INTERFACE_DESCRIPTOR descriptor
)
    : USBAudioInterface(parentObject, descriptor)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioControlInterface::~USBAudioControlInterface()
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioControlInterface::IsStreamInterface()
{
    PAGED_CODE();

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioControlInterface::IsControlInterface()
{
    PAGED_CODE();

    return true;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudioControlInterface::
    SetGenericAudioDescriptor(
        const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    )
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(descriptor == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE((descriptor->bDescriptorType != NS_USBAudio0200::CS_INTERFACE), STATUS_INVALID_PARAMETER);

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bLength            = 0x%02x", descriptor->bLength);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bDescriptorType    = 0x%02x", descriptor->bDescriptorType);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bDescriptorSubtype = 0x%02x", descriptor->bDescriptorSubtype);
    status = m_genericAudioDescriptorInfo.Append(m_parentObject, descriptor);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

// ======================================================================
// ======================================================================

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioStreamInterface::USBAudioStreamInterface(
    WDFOBJECT                 parentObject,
    PUSB_INTERFACE_DESCRIPTOR descriptor
)
    : USBAudioInterface(parentObject, descriptor)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioStreamInterface::~USBAudioStreamInterface()
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioStreamInterface::IsStreamInterface()
{
    PAGED_CODE();

    return true;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioStreamInterface::IsControlInterface()
{
    PAGED_CODE();

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG USBAudioStreamInterface::GetLockDelay()
{
    PAGED_CODE();

    return m_lockDelay;
}

// ======================================================================
_Use_decl_annotations_
PAGED_CODE_SEG
USBAudio1ControlInterface::USBAudio1ControlInterface(
    WDFOBJECT                 parentObject,
    PUSB_INTERFACE_DESCRIPTOR descriptor
)
    : USBAudioControlInterface(parentObject, descriptor)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudio1ControlInterface::~USBAudio1ControlInterface()
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudio1ControlInterface * USBAudio1ControlInterface::Create(
    WDFOBJECT                       parentObject,
    const PUSB_INTERFACE_DESCRIPTOR descriptor
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    USBAudio1ControlInterface * controlInterface = new (POOL_FLAG_NON_PAGED, DRIVER_TAG) USBAudio1ControlInterface(parentObject, descriptor);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");

    return controlInterface;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::SetClockSource(const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR /* descriptor */)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::SetInputTerminal(const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR /* descriptor */)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::SetOutputTerminal(const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR /* descriptor */)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::SetMixerUnit(const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR /* descriptor */)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::SetSelectorUnit(const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR /* descriptor */)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::SetFeatureUnit(const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR /* descriptor */)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::SetProcesingUnit(const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR /* descriptor */)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::SetExtensionUnit(const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR /* descriptor */)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::SetClockSelector(const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR /* descriptor */)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::SetClockMultiplier(const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR /* descriptor */)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::SetSampleRateConverter(const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR /* descriptor */)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::QueryCurrentAttributeAll(
    PDEVICE_CONTEXT /* deviceContext */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::QueryRangeAttributeAll(
    PDEVICE_CONTEXT /* deviceContext */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::SearchOutputTerminalFromInputTerminal(
    UCHAR /* terminalLink */,
    UCHAR & /* numOfChannels */,
    USHORT & /* terminalType */,
    UCHAR & /* volumeUnitID */,
    UCHAR & /* muteUnitID */
)
{

    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::SearchInputTerminalFromOutputTerminal(
    UCHAR /* terminalLink */,
    UCHAR & /* numOfChannels */,
    USHORT & /* terminalType */,
    UCHAR & /* volumeUnitID */,
    UCHAR & /* muteUnitID */
)
{

    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::SetCurrentSampleFrequency(
    PDEVICE_CONTEXT /* deviceContext */,
    ULONG /* desiredSampleRate */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::GetCurrentSampleFrequency(
    PDEVICE_CONTEXT /* deviceContext */,
    ULONG & /* sampleRate */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio1ControlInterface::CanSetSampleFrequency(
    bool /* isInput */
)
{
    PAGED_CODE();

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::GetCurrentSupportedSampleFrequency(
    PDEVICE_CONTEXT /* deviceContext */,
    ULONG & /* supportedSampleRate */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

// ======================================================================
_Use_decl_annotations_
PAGED_CODE_SEG
USBAudio1StreamInterface::USBAudio1StreamInterface(
    WDFOBJECT                 parentObject,
    PUSB_INTERFACE_DESCRIPTOR descriptor
)
    : USBAudioStreamInterface(parentObject, descriptor)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudio1StreamInterface::~USBAudio1StreamInterface()
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudio1StreamInterface * USBAudio1StreamInterface::Create(
    WDFOBJECT                       parentObject,
    const PUSB_INTERFACE_DESCRIPTOR descriptor
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    USBAudio1StreamInterface * streamInterface = new (POOL_FLAG_NON_PAGED, DRIVER_TAG) USBAudio1StreamInterface(parentObject, descriptor);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");

    return streamInterface;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio1StreamInterface::IsInterfaceSupportingFormats()
{
    PAGED_CODE();

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1StreamInterface::CheckInterfaceConfiguration(
    PDEVICE_CONTEXT /* deviceContext */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1StreamInterface::SetFormatType(
    const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR /* descriptor */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1StreamInterface::SetGeneral(const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR /* descriptor */)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1StreamInterface::SetIsochronousAudioDataEndpoint(
    const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(descriptor == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE((descriptor->bDescriptorType != NS_USBAudio0100::CS_ENDPOINT) || (descriptor->bDescriptorSubtype != NS_USBAudio0100::EP_GENERAL), STATUS_INVALID_PARAMETER);

    if (m_isochronousAudioDataEndpointDescriptor != nullptr)
    {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_DESCRIPTOR, "CS isochronous audio data endpoint descriptor is already set.");
    }

    NS_USBAudio0100::PCS_AS_ISOCHRONOUS_AUDIO_DATA_ENDPOINT_DESCRIPTOR isochronousAudioDataEndpointDescriptor = (NS_USBAudio0100::PCS_AS_ISOCHRONOUS_AUDIO_DATA_ENDPOINT_DESCRIPTOR)descriptor;

    if (isochronousAudioDataEndpointDescriptor->bLockDelayUnits == NS_USBAudio0100::LOCK_DELAY_UNIT_MILLISECONDS)
    {
        m_lockDelay = isochronousAudioDataEndpointDescriptor->wLockDelay;
    }
    m_isochronousAudioDataEndpointDescriptor = isochronousAudioDataEndpointDescriptor;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudio1StreamInterface::GetCurrentTerminalLink()
{
    PAGED_CODE();

    return (m_csAsInterfaceDescriptor != nullptr) ? m_csAsInterfaceDescriptor->bTerminalLink : USBAudioConfiguration::InvalidID;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudio1StreamInterface::GetCurrentBmControls()
{
    PAGED_CODE();

    return 0;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudio1StreamInterface::GetCurrentChannels()
{
    PAGED_CODE();

    return 0; // TBD
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudio1StreamInterface::GetCurrentChannelNames()
{
    PAGED_CODE();

    return 0; // TBD
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG
USBAudio1StreamInterface::GetMaxSupportedBytesPerSample()
{
    PAGED_CODE();

    return 0; // TBD
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG USBAudio1StreamInterface::GetMaxSupportedValidBitsPerSample()
{
    PAGED_CODE();

    return 0; // TBD
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudio1StreamInterface::GetCurrentActiveAlternateSetting()
{
    PAGED_CODE();

    return 0;
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG USBAudio1StreamInterface::GetCurrentValidAlternateSettingMap()
{
    PAGED_CODE();

    return 0;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudio1StreamInterface::GetValidBitsPerSample()
{
    PAGED_CODE();

    return 0; // TBD
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudio1StreamInterface::GetBytesPerSample()
{
    PAGED_CODE();

    return 0; // TBD
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio1StreamInterface::HasInputIsochronousEndpoint()
{
    PAGED_CODE();

    return false; // TBD
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio1StreamInterface::HasOutputIsochronousEndpoint()
{
    PAGED_CODE();

    return false; // TBD
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio1StreamInterface::HasFeedbackEndpoint()
{
    PAGED_CODE();

    return false; // TBD
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudio1StreamInterface::GetFeedbackEndpointAddress()
{
    PAGED_CODE();

    return 0; // TBD
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudio1StreamInterface::GetFeedbackInterval()
{
    PAGED_CODE();

    return 0; // TBD
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio1StreamInterface::IsValidAudioDataFormat(
    ULONG /* formatType */,
    ULONG /* audioDataFormat */
)
{
    PAGED_CODE();

    // NOT_SUPPORTED

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1StreamInterface::QueryCurrentAttributeAll(
    PDEVICE_CONTEXT /* deviceContext */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1StreamInterface::RegisterUSBAudioDataFormatManager(
    USBAudioDataFormatManager & /* usbAudioDataFormatManagerIn */,
    USBAudioDataFormatManager & /* usbAudioDataFormatManagerOut */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

// ======================================================================
_Use_decl_annotations_
PAGED_CODE_SEG
USBAudio2ControlInterface::USBAudio2ControlInterface(
    WDFOBJECT                 parentObject,
    PUSB_INTERFACE_DESCRIPTOR descriptor
)
    : USBAudioControlInterface(parentObject, descriptor)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudio2ControlInterface::~USBAudio2ControlInterface()
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudio2ControlInterface * USBAudio2ControlInterface::Create(
    WDFOBJECT                       parentObject,
    const PUSB_INTERFACE_DESCRIPTOR descriptor
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    USBAudio2ControlInterface * controlInterface = new (POOL_FLAG_NON_PAGED, DRIVER_TAG) USBAudio2ControlInterface(parentObject, descriptor);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");

    return controlInterface;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SetClockSource(const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor)
{
    NTSTATUS                                        status = STATUS_SUCCESS;
    NS_USBAudio0200::PCS_AC_CLOCK_SOURCE_DESCRIPTOR clockSourceDescriptor = nullptr;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(descriptor == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE((descriptor->bDescriptorType != NS_USBAudio0200::CS_INTERFACE) || (descriptor->bDescriptorSubtype != NS_USBAudio0200::CLOCK_SOURCE), STATUS_INVALID_PARAMETER);

    clockSourceDescriptor = (NS_USBAudio0200::PCS_AC_CLOCK_SOURCE_DESCRIPTOR)descriptor;

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bClockID       = 0x%02x", clockSourceDescriptor->bClockID);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bmAttributes   = 0x%02x", clockSourceDescriptor->bmAttributes);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bmControls     = 0x%02x", clockSourceDescriptor->bmControls);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bAssocTerminal = 0x%02x", clockSourceDescriptor->bAssocTerminal);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - iClockSource   = 0x%02x", clockSourceDescriptor->iClockSource);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - AC Clock Source : no. %u, clock ID %02x", m_acClockSourceInfo.GetNumOfArray(), clockSourceDescriptor->bClockID);
    status = m_acClockSourceInfo.Append(m_parentObject, clockSourceDescriptor);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SetInputTerminal(const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor)
{
    NTSTATUS                                          status = STATUS_SUCCESS;
    NS_USBAudio0200::PCS_AC_INPUT_TERMINAL_DESCRIPTOR inputTerminalDescriptor = nullptr;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(descriptor == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE((descriptor->bDescriptorType != NS_USBAudio0200::CS_INTERFACE) || (descriptor->bDescriptorSubtype != NS_USBAudio0200::INPUT_TERMINAL), STATUS_INVALID_PARAMETER);

    inputTerminalDescriptor = (NS_USBAudio0200::PCS_AC_INPUT_TERMINAL_DESCRIPTOR)descriptor;

    if (descriptor->bLength >= sizeof(NS_USBAudio0200::CS_AC_INPUT_TERMINAL_DESCRIPTOR))
    {
        status = m_acInputTerminalInfo.Append(m_parentObject, inputTerminalDescriptor);

        ULONG outTerminalId = inputTerminalDescriptor->bTerminalID;
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - AC Input Terminal : terminal ID %02x", outTerminalId);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SetOutputTerminal(const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor)
{
    NTSTATUS                                           status = STATUS_SUCCESS;
    NS_USBAudio0200::PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR outputTerminalDescriptor = nullptr;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(descriptor == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE((descriptor->bDescriptorType != NS_USBAudio0200::CS_INTERFACE) || (descriptor->bDescriptorSubtype != NS_USBAudio0200::OUTPUT_TERMINAL), STATUS_INVALID_PARAMETER);

    outputTerminalDescriptor = (NS_USBAudio0200::PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR)descriptor;

    if (descriptor->bLength >= sizeof(NS_USBAudio0200::CS_AC_OUTPUT_TERMINAL_DESCRIPTOR))
    {
        status = m_acOutputTerminalInfo.Append(m_parentObject, outputTerminalDescriptor);

        ULONG inSourceUnitId = outputTerminalDescriptor->bSourceID;
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - AC Output Terminal : source ID %02x", inSourceUnitId);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SetMixerUnit(const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(descriptor == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE((descriptor->bDescriptorType != NS_USBAudio0200::CS_INTERFACE) || (descriptor->bDescriptorSubtype != NS_USBAudio0200::MIXER_UNIT), STATUS_INVALID_PARAMETER);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SetSelectorUnit(const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(descriptor == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE((descriptor->bDescriptorType != NS_USBAudio0200::CS_INTERFACE) || (descriptor->bDescriptorSubtype != NS_USBAudio0200::SELECTOR_UNIT), STATUS_INVALID_PARAMETER);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SetFeatureUnit(const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor)
{
    NTSTATUS                                        status = STATUS_SUCCESS;
    NS_USBAudio0200::PCS_AC_FEATURE_UNIT_DESCRIPTOR featureUnitDescriptor = nullptr;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(descriptor == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE((descriptor->bDescriptorType != NS_USBAudio0200::CS_INTERFACE) || (descriptor->bDescriptorSubtype != NS_USBAudio0200::FEATURE_UNIT), STATUS_INVALID_PARAMETER);

    featureUnitDescriptor = (NS_USBAudio0200::PCS_AC_FEATURE_UNIT_DESCRIPTOR)descriptor;

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - AC Feature Unit : unit ID %02x, source ID %02x", featureUnitDescriptor->bUnitID, featureUnitDescriptor->bSourceID);

    status = m_acFeatureUnitInfo.Append(m_parentObject, featureUnitDescriptor);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SetProcesingUnit(const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(descriptor == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE((descriptor->bDescriptorType != NS_USBAudio0200::CS_INTERFACE) || (descriptor->bDescriptorSubtype != NS_USBAudio0200::PROCESSING_UNIT), STATUS_INVALID_PARAMETER);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SetExtensionUnit(const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(descriptor == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE((descriptor->bDescriptorType != NS_USBAudio0200::CS_INTERFACE) || (descriptor->bDescriptorSubtype != NS_USBAudio0200::EXTENSION_UNIT), STATUS_INVALID_PARAMETER);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SetClockSelector(const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(descriptor == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE((descriptor->bDescriptorType != NS_USBAudio0200::CS_INTERFACE) || (descriptor->bDescriptorSubtype != NS_USBAudio0200::CLOCK_SELECTOR), STATUS_INVALID_PARAMETER);

    if ((descriptor->bLength >= sizeof(NS_USBAudio0200::CS_AC_CLOCK_SELECTOR_DESCRIPTOR)) && (descriptor->bDescriptorSubtype == NS_USBAudio0200::CLOCK_SELECTOR))
    {
        m_clockSelectorDescriptor = (NS_USBAudio0200::PCS_AC_CLOCK_SELECTOR_DESCRIPTOR)descriptor;

        // deviceContext->ClockSelectorId = clockSelectorDescriptor->bClockID;
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - AC Clock Selector : clock ID %02x", m_clockSelectorDescriptor->bClockID);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SetClockMultiplier(const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(descriptor == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE((descriptor->bDescriptorType != NS_USBAudio0200::CS_INTERFACE) || (descriptor->bDescriptorSubtype != NS_USBAudio0200::CLOCK_MULTIPLIER), STATUS_INVALID_PARAMETER);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SetSampleRateConverter(const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(descriptor == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE((descriptor->bDescriptorType != NS_USBAudio0200::CS_INTERFACE) || (descriptor->bDescriptorSubtype != NS_USBAudio0200::SAMPLE_RATE_CONVERTER), STATUS_INVALID_PARAMETER);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::QuerySampleFrequencyControls(
    UCHAR   clockSourceID,
    UCHAR & controls
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    controls = 0;

    ULONG numOfAcClockSourceInfo = m_acClockSourceInfo.GetNumOfArray();

    for (ULONG index = 0; index < numOfAcClockSourceInfo; index++)
    {
        NS_USBAudio0200::PCS_AC_CLOCK_SOURCE_DESCRIPTOR clockSourceDescriptor = nullptr;
        if (NT_SUCCESS(m_acClockSourceInfo.Get(index, clockSourceDescriptor)))
        {
            if (clockSourceDescriptor->bClockID == clockSourceID)
            {
                controls = clockSourceDescriptor->bmControls;
                return status;
            }
        }
    }

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::QueryCurrentSampleFrequency(
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;
    UCHAR    inputClockSourceID = USBAudioConfiguration::InvalidID, outputClockSourceID = USBAudioConfiguration::InvalidID;
    ULONG    sampleRate = 0;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    //
    // This driver does not support selection of the user's clock source.
    // Therefore, the internal programmable clock source will be selected as the default.
    //
    RETURN_NTSTATUS_IF_FAILED(SetCurrentClockSourceInternal(deviceContext));

    RETURN_NTSTATUS_IF_FAILED(GetCurrentClockSourceID(deviceContext, inputClockSourceID, outputClockSourceID));

    RETURN_NTSTATUS_IF_FAILED(QuerySampleFrequencyControls(inputClockSourceID, m_inputSampleFrequencyControls));

    RETURN_NTSTATUS_IF_FAILED(QuerySampleFrequencyControls(outputClockSourceID, m_outputSampleFrequencyControls));

    if (inputClockSourceID == outputClockSourceID)
    {
        if (inputClockSourceID != USBAudioConfiguration::InvalidID)
        {
            status = ControlRequestGetSampleFrequency(deviceContext, GetInterfaceNumber(), inputClockSourceID, sampleRate);
            if (NT_SUCCESS(status))
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, clock id %u, sample frequency %u, input and output have the same clock source", GetInterfaceNumber(), inputClockSourceID, sampleRate);
                m_outputCurrentSampleRate = m_inputCurrentSampleRate = sampleRate;
            }
        }
        else
        {
            // For device that do not have a clock source descriptor.
        }
    }
    else
    {
        if (inputClockSourceID != USBAudioConfiguration::InvalidID)
        {
            status = ControlRequestGetSampleFrequency(deviceContext, GetInterfaceNumber(), inputClockSourceID, sampleRate);
            if (NT_SUCCESS(status))
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, clock id %u, input sample frequency %u, ", GetInterfaceNumber(), inputClockSourceID, sampleRate);
                m_inputCurrentSampleRate = sampleRate;
            }
        }

        if (outputClockSourceID != USBAudioConfiguration::InvalidID)
        {
            status = ControlRequestGetSampleFrequency(deviceContext, GetInterfaceNumber(), outputClockSourceID, sampleRate);
            if (NT_SUCCESS(status))
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, clock id %u, output sample frequency %u, ", GetInterfaceNumber(), outputClockSourceID, sampleRate);
                m_outputCurrentSampleRate = sampleRate;
            }
        }

        if ((inputClockSourceID != USBAudioConfiguration::InvalidID) && (outputClockSourceID == USBAudioConfiguration::InvalidID))
        {
            m_outputCurrentSampleRate = m_inputCurrentSampleRate;
        }
        if ((inputClockSourceID == USBAudioConfiguration::InvalidID) && (outputClockSourceID != USBAudioConfiguration::InvalidID))
        {
            m_inputCurrentSampleRate = m_outputCurrentSampleRate;
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SetCurrentSampleFrequency(
    PDEVICE_CONTEXT deviceContext,
    ULONG           desiredSampleRate
)
{
    NTSTATUS status = STATUS_SUCCESS;
    UCHAR    inputClockSourceID = USBAudioConfiguration::InvalidID;
    UCHAR    outputClockSourceID = USBAudioConfiguration::InvalidID;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry, %u", desiredSampleRate);

    RETURN_NTSTATUS_IF_FAILED(GetCurrentClockSourceID(deviceContext, inputClockSourceID, outputClockSourceID));

    if (inputClockSourceID == outputClockSourceID)
    {
        if (inputClockSourceID != USBAudioConfiguration::InvalidID)
        {
            status = ControlRequestSetSampleFrequency(deviceContext, GetInterfaceNumber(), inputClockSourceID, desiredSampleRate);
            if (NT_SUCCESS(status))
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, clock id %u, sample frequency %u, input and output have the same clock source", GetInterfaceNumber(), inputClockSourceID, desiredSampleRate);
                m_outputCurrentSampleRate = m_inputCurrentSampleRate = desiredSampleRate;
            }
        }
    }
    else
    {
        if (inputClockSourceID != USBAudioConfiguration::InvalidID)
        {
            status = ControlRequestSetSampleFrequency(deviceContext, GetInterfaceNumber(), inputClockSourceID, desiredSampleRate);
            if (NT_SUCCESS(status))
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, clock id %u, sample frequency %u", GetInterfaceNumber(), inputClockSourceID, desiredSampleRate);
                m_inputCurrentSampleRate = desiredSampleRate;
            }
        }
        if (outputClockSourceID != USBAudioConfiguration::InvalidID)
        {
            status = ControlRequestSetSampleFrequency(deviceContext, GetInterfaceNumber(), outputClockSourceID, desiredSampleRate);
            if (NT_SUCCESS(status))
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, clock id %u, sample frequency %u", GetInterfaceNumber(), outputClockSourceID, desiredSampleRate);
                m_outputCurrentSampleRate = desiredSampleRate;
            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::GetCurrentSampleFrequency(
    PDEVICE_CONTEXT deviceContext,
    ULONG &         sampleRate
)
{
    NTSTATUS status = STATUS_SUCCESS;
    sampleRate = 0;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    status = QueryCurrentSampleFrequency(deviceContext);
    if (NT_SUCCESS(status))
    {
        if (m_inputCurrentSampleRate != 0)
        {
            sampleRate = m_inputCurrentSampleRate;
        }
        else
        {
            sampleRate = m_outputCurrentSampleRate;
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!, %u", status, sampleRate);
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio2ControlInterface::CanSetSampleFrequency(
    bool isInput
)
{
    bool canSetSampleFrequency = false;
    PAGED_CODE();

    if (isInput)
    {
        canSetSampleFrequency = ((m_inputSampleFrequencyControls & NS_USBAudio0200::CLOCK_FREQUENCY_CONTROL_MASK) == NS_USBAudio0200::CLOCK_FREQUENCY_CONTROL_READ_WRITE);
    }
    else
    {
        canSetSampleFrequency = ((m_outputSampleFrequencyControls & NS_USBAudio0200::CLOCK_FREQUENCY_CONTROL_MASK) == NS_USBAudio0200::CLOCK_FREQUENCY_CONTROL_READ_WRITE);
    }

    return canSetSampleFrequency;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::GetCurrentSupportedSampleFrequency(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           clockSourceID,
    ULONG &         supportedSampleRate
)
{
    NTSTATUS                                                status = STATUS_SUCCESS;
    WDFMEMORY                                               memory = nullptr;
    ULONG                                                   sampleRate = 0;
    NS_USBAudio0200::PCONTROL_RANGE_PARAMETER_BLOCK_LAYOUT3 parameterBlock = nullptr;
    UCHAR                                                   clockFrequencyControl = 0;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    supportedSampleRate = 0;

    RETURN_NTSTATUS_IF_FAILED(QuerySampleFrequencyControls(clockSourceID, clockFrequencyControl));

    if ((clockFrequencyControl & NS_USBAudio0200::CLOCK_FREQUENCY_CONTROL_MASK) == NS_USBAudio0200::CLOCK_FREQUENCY_CONTROL_READ)
    {
        RETURN_NTSTATUS_IF_FAILED(GetCurrentSampleFrequency(deviceContext, sampleRate));
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, clock id %u, sample frequency control is read only. sample frequency %u", GetInterfaceNumber(), clockSourceID, sampleRate);
    }

    status = ControlRequestGetSampleFrequencyRange(deviceContext, GetInterfaceNumber(), clockSourceID, memory, parameterBlock);
    if (NT_SUCCESS(status))
    {
        ASSERT(memory != nullptr);
        ASSERT(parameterBlock != nullptr);
        for (ULONG rangeIndex = 0; rangeIndex < parameterBlock->wNumSubRanges; rangeIndex++)
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, clock id %u, sample frequency range [%u] min %u, max %u,  res %u", GetInterfaceNumber(), clockSourceID, rangeIndex, parameterBlock->subrange[rangeIndex].dMIN, parameterBlock->subrange[rangeIndex].dMAX, parameterBlock->subrange[rangeIndex].dRES);
            for (ULONG sampleRateListIndex = 0; sampleRateListIndex < c_SampleRateCount; ++sampleRateListIndex)
            {
                if ((c_SampleRateList[sampleRateListIndex] >= parameterBlock->subrange[rangeIndex].dMIN) && (c_SampleRateList[sampleRateListIndex] <= parameterBlock->subrange[rangeIndex].dMAX) && ((sampleRate == 0) || (sampleRate == c_SampleRateList[sampleRateListIndex])))
                {
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " <PID %04x>", deviceContext->AudioProperty.ProductId);
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - supporting %u Hz", c_SampleRateList[sampleRateListIndex]);

                    supportedSampleRate |= 1 << sampleRateListIndex;
                }
            }
        }
        WdfObjectDelete(memory);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::GetCurrentSupportedSampleFrequency(
    PDEVICE_CONTEXT deviceContext,
    ULONG &         supportedSampleRate
)
{
    NTSTATUS status = STATUS_SUCCESS;
    UCHAR    inputClockSourceID = USBAudioConfiguration::InvalidID;
    UCHAR    outputClockSourceID = USBAudioConfiguration::InvalidID;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_FAILED(GetCurrentClockSourceID(deviceContext, inputClockSourceID, outputClockSourceID));

    m_inputSupportedSampleRate = 0;
    m_outputSupportedSampleRate = 0;
    supportedSampleRate = 0;

    if ((inputClockSourceID == outputClockSourceID) && (inputClockSourceID != USBAudioConfiguration::InvalidID))
    {
        RETURN_NTSTATUS_IF_FAILED(GetCurrentSupportedSampleFrequency(deviceContext, inputClockSourceID, m_inputSupportedSampleRate));
        supportedSampleRate = m_inputSupportedSampleRate;
    }
    else
    {
        if (inputClockSourceID != USBAudioConfiguration::InvalidID)
        {
            RETURN_NTSTATUS_IF_FAILED(GetCurrentSupportedSampleFrequency(deviceContext, inputClockSourceID, m_inputSupportedSampleRate));
        }
        if (outputClockSourceID != USBAudioConfiguration::InvalidID)
        {
            RETURN_NTSTATUS_IF_FAILED(GetCurrentSupportedSampleFrequency(deviceContext, outputClockSourceID, m_outputSupportedSampleRate));
        }

        if ((inputClockSourceID != USBAudioConfiguration::InvalidID) && (outputClockSourceID != USBAudioConfiguration::InvalidID))
        {
            supportedSampleRate = m_inputSupportedSampleRate & m_outputSupportedSampleRate;
        }
        else if ((inputClockSourceID != USBAudioConfiguration::InvalidID))
        {
            supportedSampleRate = m_inputSupportedSampleRate;
        }
        else if (outputClockSourceID != USBAudioConfiguration::InvalidID)
        {
            supportedSampleRate = m_outputSupportedSampleRate;
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::GetCurrentFeatureUnit(
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    ULONG numOfAcFeatureUnitInfo = m_acFeatureUnitInfo.GetNumOfArray();

    //  FU_VOLUME_CONTROL current
    for (ULONG index = 0; index < numOfAcFeatureUnitInfo; index++)
    {
        NS_USBAudio0200::PCS_AC_FEATURE_UNIT_DESCRIPTOR featureUnitDescriptor = nullptr;
        if (NT_SUCCESS(m_acFeatureUnitInfo.Get(index, featureUnitDescriptor)))
        {
            UCHAR numOfChannels = (featureUnitDescriptor->bLength - offsetof(NS_USBAudio0200::CS_AC_FEATURE_UNIT_DESCRIPTOR, ch)) / (sizeof(NS_USBAudio0200::CS_AC_FEATURE_UNIT_DESCRIPTOR::ch[0]));
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - feature unit numOfChannels %u", numOfChannels);
            for (UCHAR ch = 0; ch < numOfChannels; ch++)
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - feature unit ch %u, bmControls 0x%02u%02u%02u%02u  0x%08x", ch, featureUnitDescriptor->ch[ch].bmaControls[3], featureUnitDescriptor->ch[ch].bmaControls[2], featureUnitDescriptor->ch[ch].bmaControls[1], featureUnitDescriptor->ch[ch].bmaControls[0], ConvertBmaControls(featureUnitDescriptor->ch[ch].bmaControls));
                if (ConvertBmaControls(featureUnitDescriptor->ch[ch].bmaControls) & NS_USBAudio0200::FEATURE_UNIT_BMA_MUTE_CONTROL_MASK)
                {
                    bool mute = false;
                    status = ControlRequestGetMute(deviceContext, GetInterfaceNumber(), featureUnitDescriptor->bUnitID, ch, mute);
                    if (NT_SUCCESS(status))
                    {
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - mute channel is %d, current %u", ch, (UCHAR)mute);
                    }
                }
                if (ConvertBmaControls(featureUnitDescriptor->ch[ch].bmaControls) & NS_USBAudio0200::FEATURE_UNIT_BMA_VOLUME_CONTROL_MASK)
                {
                    USHORT volume = 0;
                    status = ControlRequestGetVolume(deviceContext, GetInterfaceNumber(), featureUnitDescriptor->bUnitID, ch, volume);
                    if (NT_SUCCESS(status))
                    {
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - volume channel is %d, current %u", ch, volume);
                    }
                }
            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::GetRangeSampleFrequency(
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG    supportedSampleRate = 0;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_FAILED(GetCurrentSupportedSampleFrequency(deviceContext, supportedSampleRate));
    deviceContext->AudioProperty.SupportedSampleRate = supportedSampleRate;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::GetRangeFeatureUnit(
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    ULONG numOfAcFeatureUnitInfo = m_acFeatureUnitInfo.GetNumOfArray();

    // FU_VOLUME_CONTROL ranges
    for (ULONG index = 0; index < numOfAcFeatureUnitInfo; index++)
    {
        NS_USBAudio0200::PCS_AC_FEATURE_UNIT_DESCRIPTOR featureUnitDescriptor = nullptr;
        if (NT_SUCCESS(m_acFeatureUnitInfo.Get(index, featureUnitDescriptor)))
        {
            UCHAR numOfChannels = (featureUnitDescriptor->bLength - offsetof(NS_USBAudio0200::CS_AC_FEATURE_UNIT_DESCRIPTOR, ch)) / (sizeof(NS_USBAudio0200::CS_AC_FEATURE_UNIT_DESCRIPTOR::ch[0]));
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - feature unit numOfChannels %u", numOfChannels);
            for (UCHAR ch = 0; ch < numOfChannels; ch++)
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - feature unit ch %u, bmControls 0x%02u%02u%02u%02u  0x%08x", ch, featureUnitDescriptor->ch[ch].bmaControls[3], featureUnitDescriptor->ch[ch].bmaControls[2], featureUnitDescriptor->ch[ch].bmaControls[1], featureUnitDescriptor->ch[ch].bmaControls[0], ConvertBmaControls(featureUnitDescriptor->ch[ch].bmaControls));
                if (ConvertBmaControls(featureUnitDescriptor->ch[ch].bmaControls) & NS_USBAudio0200::FEATURE_UNIT_BMA_VOLUME_CONTROL_MASK)
                {
                    WDFMEMORY                                               memory = nullptr;
                    NS_USBAudio0200::PCONTROL_RANGE_PARAMETER_BLOCK_LAYOUT2 parameterBlock = nullptr;

                    status = ControlRequestGetVolumeRange(deviceContext, GetInterfaceNumber(), featureUnitDescriptor->bUnitID, ch, memory, parameterBlock);
                    if (NT_SUCCESS(status))
                    {
                        ASSERT(memory != nullptr);
                        ASSERT(parameterBlock != nullptr);
                        for (ULONG rangeIndex = 0; rangeIndex < parameterBlock->wNumSubRanges; rangeIndex++)
                        {
                            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, ch %u, unit id %u, volume range [%u] min %u, max %u,  res %u", GetInterfaceNumber(), ch, featureUnitDescriptor->bUnitID, rangeIndex, parameterBlock->subrange[rangeIndex].wMIN, parameterBlock->subrange[rangeIndex].wMAX, parameterBlock->subrange[rangeIndex].wRES);
                        }
                        WdfObjectDelete(memory);
                    }
                }
            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::GetCurrentClockSourceID(
    PDEVICE_CONTEXT deviceContext,
    UCHAR &         clockSourceID
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    clockSourceID = USBAudioConfiguration::InvalidID;

    //
    // If a clock selector exists, get the clock source selected by the
    // current clock selector.
    //
    if (m_clockSelectorDescriptor != nullptr)
    {
        UCHAR clockSelectorIndex = 0; // 1 origin
        if (m_clockSelectorDescriptor->bNrInPins > 1)
        {
            // Get only if multiple pins are found.
            RETURN_NTSTATUS_IF_FAILED(ControlRequestGetClockSelector(deviceContext, GetInterfaceNumber(), m_clockSelectorDescriptor->bClockID, clockSelectorIndex));
        }
        else
        {
            //
        }
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bNrInPins %u, clockSelecotrIndex %u", m_clockSelectorDescriptor->bNrInPins, clockSelectorIndex);
        if ((clockSelectorIndex > 0) && (clockSelectorIndex <= m_clockSelectorDescriptor->bNrInPins))
        {
            clockSourceID = m_clockSelectorDescriptor->baCSourceID[clockSelectorIndex - 1];
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - clockSourceID %u", clockSourceID);
        }
    }

    //
    // If clock selector is not present, the first clock source is used.
    //
    if (clockSourceID == USBAudioConfiguration::InvalidID)
    {
        NS_USBAudio0200::PCS_AC_CLOCK_SOURCE_DESCRIPTOR clockSourceDescriptor = nullptr;
        ULONG                                           numOfAcClockSourceInfo = m_acClockSourceInfo.GetNumOfArray();

        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - numOfAcClockSourceInfo %u", numOfAcClockSourceInfo);
        if (numOfAcClockSourceInfo == 0)
        {
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_DESCRIPTOR, "Clock Source Descriptor is missing.");
            m_inputCurrentSampleRate = 0;
            m_outputCurrentSampleRate = 0;
            return STATUS_SUCCESS;
        }
        RETURN_NTSTATUS_IF_FAILED(m_acClockSourceInfo.Get(0, clockSourceDescriptor));
        clockSourceID = clockSourceDescriptor->bClockID;
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - clockSourceID %u", clockSourceID);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::GetCurrentClockSourceID(
    PDEVICE_CONTEXT deviceContext,
    bool            isInput,
    UCHAR &         clockSourceID
)
{
    NTSTATUS status = STATUS_SUCCESS;
    UCHAR    terminalLink;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    clockSourceID = USBAudioConfiguration::InvalidID;

    RETURN_NTSTATUS_IF_FAILED(deviceContext->UsbAudioConfiguration->GetCurrentTerminalLink(isInput, terminalLink));

    if (terminalLink != USBAudioConfiguration::InvalidID)
    {
        if (isInput)
        {
            ULONG numOfAcOutputTerminalInfo = m_acOutputTerminalInfo.GetNumOfArray();

            for (ULONG index = 0; index < numOfAcOutputTerminalInfo; index++)
            {
                NS_USBAudio0200::PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR outputTerminalDescriptor = nullptr;
                if (NT_SUCCESS(m_acOutputTerminalInfo.Get(index, outputTerminalDescriptor)))
                {
                    if (outputTerminalDescriptor->bTerminalID == terminalLink)
                    {
                        clockSourceID = outputTerminalDescriptor->bCSourceID;
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - output terminal id %u, terminal type %u, bCSourceID %u", outputTerminalDescriptor->bTerminalID, outputTerminalDescriptor->wTerminalType, clockSourceID);
                        break;
                    }
                }
            }
        }
        else
        {
            ULONG numOfAcInputTerminalInfo = m_acInputTerminalInfo.GetNumOfArray();

            for (ULONG index = 0; index < numOfAcInputTerminalInfo; index++)
            {
                NS_USBAudio0200::PCS_AC_INPUT_TERMINAL_DESCRIPTOR inputTerminalDescriptor = nullptr;
                if (NT_SUCCESS(m_acInputTerminalInfo.Get(index, inputTerminalDescriptor)))
                {
                    if (inputTerminalDescriptor->bTerminalID == terminalLink)
                    {
                        clockSourceID = inputTerminalDescriptor->bCSourceID;
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - input terminal id %u, terminal type %u, bCSourceID %u", inputTerminalDescriptor->bTerminalID, inputTerminalDescriptor->wTerminalType, clockSourceID);
                        break;
                    }
                }
            }
        }
    }

    if (clockSourceID == USBAudioConfiguration::InvalidID)
    {
        if (isInput)
        {
            ULONG numOfAcOutputTerminalInfo = m_acOutputTerminalInfo.GetNumOfArray();

            for (ULONG index = 0; index < numOfAcOutputTerminalInfo; index++)
            {
                NS_USBAudio0200::PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR outputTerminalDescriptor = nullptr;
                if (NT_SUCCESS(m_acOutputTerminalInfo.Get(index, outputTerminalDescriptor)))
                {
                    if (outputTerminalDescriptor->wTerminalType == NS_USBAudio0200::USB_STREAMING)
                    {
                        clockSourceID = outputTerminalDescriptor->bCSourceID;
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - output terminal id %u, terminal type %u, bCSourceID %u", outputTerminalDescriptor->bTerminalID, outputTerminalDescriptor->wTerminalType, clockSourceID);
                        break;
                    }
                }
            }
        }
        else
        {
            ULONG numOfAcInputTerminalInfo = m_acInputTerminalInfo.GetNumOfArray();

            for (ULONG index = 0; index < numOfAcInputTerminalInfo; index++)
            {
                NS_USBAudio0200::PCS_AC_INPUT_TERMINAL_DESCRIPTOR inputTerminalDescriptor = nullptr;
                if (NT_SUCCESS(m_acInputTerminalInfo.Get(index, inputTerminalDescriptor)))
                {
                    if (inputTerminalDescriptor->wTerminalType == NS_USBAudio0200::USB_STREAMING)
                    {
                        clockSourceID = inputTerminalDescriptor->bCSourceID;
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - input terminal id %u, terminal type %u, bCSourceID %u", inputTerminalDescriptor->bTerminalID, inputTerminalDescriptor->wTerminalType, clockSourceID);
                        break;
                    }
                }
            }
        }
    }

    if ((m_clockSelectorDescriptor != nullptr) && (m_clockSelectorDescriptor->bClockID == clockSourceID))
    {
        RETURN_NTSTATUS_IF_FAILED(GetCurrentClockSourceID(deviceContext, clockSourceID));
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::GetCurrentClockSourceID(
    PDEVICE_CONTEXT deviceContext,
    UCHAR &         inputClockSourceID,
    UCHAR &         outputClockSourceID
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    inputClockSourceID = USBAudioConfiguration::InvalidID;
    outputClockSourceID = USBAudioConfiguration::InvalidID;

    RETURN_NTSTATUS_IF_FAILED(GetCurrentClockSourceID(deviceContext, true, inputClockSourceID));
    RETURN_NTSTATUS_IF_FAILED(GetCurrentClockSourceID(deviceContext, false, outputClockSourceID));

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SetCurrentClockSourceInternal(
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    if (m_clockSelectorDescriptor != nullptr)
    {
        if (m_clockSelectorDescriptor->bNrInPins > 1)
        {
            UCHAR currentClockSelectorIndex = 0; // 1 origin
            UCHAR targetClockSelectorIndex = 0;
            UCHAR targetClockID = USBAudioConfiguration::InvalidID;

            // Get only if multiple pins are found.
            RETURN_NTSTATUS_IF_FAILED(ControlRequestGetClockSelector(deviceContext, GetInterfaceNumber(), m_clockSelectorDescriptor->bClockID, currentClockSelectorIndex));
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bNrInPins %u, clockSelecotrIndex %u", m_clockSelectorDescriptor->bNrInPins, currentClockSelectorIndex);

            ULONG numOfAcClockSourceInfo = m_acClockSourceInfo.GetNumOfArray();

            // Finding an internal, programmable clock source
            for (ULONG index = 0; index < numOfAcClockSourceInfo; index++)
            {
                NS_USBAudio0200::PCS_AC_CLOCK_SOURCE_DESCRIPTOR clockSourceDescriptor = nullptr;
                if (NT_SUCCESS(m_acClockSourceInfo.Get(index, clockSourceDescriptor)))
                {
                    if ((clockSourceDescriptor->bmAttributes & NS_USBAudio0200::CLOCK_TYPE_MASK) == NS_USBAudio0200::CLOCK_TYPE_INTERNAL_PROGRAMMABLE_CLOCK)
                    {
                        targetClockID = clockSourceDescriptor->bClockID;
                        break;
                    }
                }
            }

            // Find the next preferred internal, variable clock source.
            if (targetClockID == USBAudioConfiguration::InvalidID)
            {
                for (ULONG index = 0; index < numOfAcClockSourceInfo; index++)
                {
                    NS_USBAudio0200::PCS_AC_CLOCK_SOURCE_DESCRIPTOR clockSourceDescriptor = nullptr;
                    if (NT_SUCCESS(m_acClockSourceInfo.Get(index, clockSourceDescriptor)))
                    {
                        if ((clockSourceDescriptor->bmAttributes & NS_USBAudio0200::CLOCK_TYPE_MASK) == NS_USBAudio0200::CLOCK_TYPE_INTERNAL_VARIABLE_CLOCK)
                        {
                            targetClockID = clockSourceDescriptor->bClockID;
                            break;
                        }
                    }
                }
            }

            // Find the next preferred internal, fixed clock source.
            if (targetClockID == USBAudioConfiguration::InvalidID)
            {
                for (ULONG index = 0; index < numOfAcClockSourceInfo; index++)
                {
                    NS_USBAudio0200::PCS_AC_CLOCK_SOURCE_DESCRIPTOR clockSourceDescriptor = nullptr;
                    if (NT_SUCCESS(m_acClockSourceInfo.Get(index, clockSourceDescriptor)))
                    {
                        if ((clockSourceDescriptor->bmAttributes & NS_USBAudio0200::CLOCK_TYPE_MASK) == NS_USBAudio0200::CLOCK_TYPE_INTERNAL_FIXED_CLOCK)
                        {
                            targetClockID = clockSourceDescriptor->bClockID;
                            break;
                        }
                    }
                }
            }

            if (targetClockID == USBAudioConfiguration::InvalidID)
            {
                targetClockID = m_clockSelectorDescriptor->baCSourceID[0];
            }

            for (UCHAR clockSelectorIndex = 0; clockSelectorIndex < m_clockSelectorDescriptor->bNrInPins; clockSelectorIndex++)
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - clockSourceID %u, target clockSourceID %u", m_clockSelectorDescriptor->baCSourceID[clockSelectorIndex], targetClockID);
                if (targetClockID == m_clockSelectorDescriptor->baCSourceID[clockSelectorIndex])
                {
                    targetClockSelectorIndex = clockSelectorIndex + 1; // convert to 1 origin
                    break;
                }
            }

            if (targetClockSelectorIndex != currentClockSelectorIndex)
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, clock id %u, clockSelectorIndex %u", GetInterfaceNumber(), m_clockSelectorDescriptor->bClockID, targetClockSelectorIndex);
                RETURN_NTSTATUS_IF_FAILED(ControlRequestSetClockSelector(deviceContext, GetInterfaceNumber(), m_clockSelectorDescriptor->bClockID, targetClockSelectorIndex));
            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::QueryCurrentAttributeAll(
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_FAILED(QueryCurrentSampleFrequency(deviceContext));

    // NS_USBAudio0200::CLOCK_SELECTOR_CONTROL current
    // ControlRequestGetClockSelector TBD

    // NS_USBAudio0200::CLOCK_MULTIPLIER current
    // TBD

    // terminal current
    // TBD

    // mixer unit current
    // TBD

    // selector unit current
    // TBD

    // feature unit current
    //  NS_USBAudio0200::FU_VOLUME_CONTROL
    //  NS_USBAudio0200::FU_MUTE_CONTROL current
    RETURN_NTSTATUS_IF_FAILED(GetCurrentFeatureUnit(deviceContext));

    // NS_USBAudio0200::AS_AUDIO_DATA_FORMAT_CONTROL
    // ControlRequestGetAudioDataFormat

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::QueryRangeAttributeAll(
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    //  CS_SAM_FREQ_CONTROL ranges
    RETURN_NTSTATUS_IF_FAILED(GetRangeSampleFrequency(deviceContext));

    // mixer unit current
    // TBD

    // FU_VOLUME_CONTROL ranges
    RETURN_NTSTATUS_IF_FAILED(GetRangeFeatureUnit(deviceContext));

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SearchOutputTerminal(
    UCHAR &  sourceID,
    UCHAR &  numOfChannels,
    USHORT & terminalType,
    UCHAR &  volumeUnitID,
    UCHAR &  muteUnitID,
    SCHAR    recursionCount
)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC!  recursionCount = %d", recursionCount);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - souceID id %u", sourceID);

    recursionCount--;

    ULONG numOfGenericAudioDescriptorInfo = m_genericAudioDescriptorInfo.GetNumOfArray();

    for (ULONG index = 0; index < numOfGenericAudioDescriptorInfo; index++)
    {
        NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR genericAudioDescriptor = nullptr;
        if (NT_SUCCESS(m_genericAudioDescriptorInfo.Get(index, genericAudioDescriptor)))
        {
            switch (genericAudioDescriptor->bDescriptorSubtype)
            {
            case NS_USBAudio0200::OUTPUT_TERMINAL:
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - output terminal bTerminalID %u, bSourceID %u, bCSSourceID %u", ((NS_USBAudio0200::PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor)->bTerminalID, ((NS_USBAudio0200::PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor)->bSourceID, ((NS_USBAudio0200::PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor)->bCSourceID);
                if (((NS_USBAudio0200::PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor)->bSourceID == sourceID)
                {
                    terminalType = ((NS_USBAudio0200::PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor)->wTerminalType;
                    return STATUS_SUCCESS;
                }
                break;

            case NS_USBAudio0200::FEATURE_UNIT: {
                NS_USBAudio0200::PCS_AC_FEATURE_UNIT_DESCRIPTOR featureUnitDescriptor = (NS_USBAudio0200::PCS_AC_FEATURE_UNIT_DESCRIPTOR)genericAudioDescriptor;
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - feature unit bSourceID %u", featureUnitDescriptor->bSourceID);
                if (featureUnitDescriptor->bSourceID == sourceID)
                {
                    UCHAR size = 4; // CS_AC_FEATURE_UNIT_DESCRIPTOR::bmaControls
                    UCHAR channels = (featureUnitDescriptor->bLength - offsetof(NS_USBAudio0200::CS_AC_FEATURE_UNIT_DESCRIPTOR, ch)) / size;
                    for (UCHAR ch = 0; ch < channels; ++ch)
                    {
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - feature unit ch[%u] bmaControls %02x %02x %02x %02x", ch, featureUnitDescriptor->ch[ch].bmaControls[3], featureUnitDescriptor->ch[ch].bmaControls[2], featureUnitDescriptor->ch[ch].bmaControls[1], featureUnitDescriptor->ch[ch].bmaControls[0]);
                        if (featureUnitDescriptor->ch[ch].bmaControls[0] & NS_USBAudio0200::FEATURE_UNIT_BMA_MUTE_CONTROL_MASK)
                        {
                            muteUnitID = featureUnitDescriptor->bUnitID;
                        }
                        if (featureUnitDescriptor->ch[ch].bmaControls[0] & NS_USBAudio0200::FEATURE_UNIT_BMA_VOLUME_CONTROL_MASK)
                        {
                            volumeUnitID = featureUnitDescriptor->bUnitID;
                        }
                    }
                    sourceID = featureUnitDescriptor->bUnitID;
                    break;
                }
            }
            break;
            case NS_USBAudio0200::MIXER_UNIT:
                if (recursionCount >= 0)
                {
                    NS_USBAudio0200::PCS_AC_MIXER_UNIT_DESCRIPTOR_COMMON mixerUnitDescriptor = (NS_USBAudio0200::PCS_AC_MIXER_UNIT_DESCRIPTOR_COMMON)genericAudioDescriptor;
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - mixer unit bUnitID %u", mixerUnitDescriptor->bUnitID);
                    if (mixerUnitDescriptor->bNrInPins != 0)
                    {
                        ULONG sizeOfPin = (mixerUnitDescriptor->bLength - sizeof(NS_USBAudio0200::CS_AC_MIXER_UNIT_DESCRIPTOR_COMMON)) / mixerUnitDescriptor->bNrInPins;
                        for (UCHAR pin = 0; pin < mixerUnitDescriptor->bNrInPins; ++pin)
                        {
                            UCHAR baSourceID = *(((UCHAR *)mixerUnitDescriptor) + sizeof(NS_USBAudio0200::CS_AC_MIXER_UNIT_DESCRIPTOR_COMMON) + sizeOfPin * pin);
                            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - mixer unit pin[%u] baSourceID %02x", pin, baSourceID);
                            if (baSourceID == sourceID)
                            {
                                UCHAR sourceIDBackup = sourceID;
                                sourceID = mixerUnitDescriptor->bUnitID;
                                status = SearchOutputTerminal(sourceID, numOfChannels, terminalType, volumeUnitID, muteUnitID, recursionCount);
                                if (NT_SUCCESS(status))
                                {
                                    return status;
                                }
                                sourceID = sourceIDBackup;
                            }
                        }
                    }
                }
                break;
            default:
            case NS_USBAudio0200::CLOCK_MULTIPLIER:
            case NS_USBAudio0200::CLOCK_SELECTOR:
            case NS_USBAudio0200::CLOCK_SOURCE:
            case NS_USBAudio0200::EXTENSION_UNIT:
            case NS_USBAudio0200::INPUT_TERMINAL:
            case NS_USBAudio0200::PROCESSING_UNIT:
            case NS_USBAudio0200::SAMPLE_RATE_CONVERTER:
            case NS_USBAudio0200::SELECTOR_UNIT:
                break;
            }
        }
    }

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SearchOutputTerminalFromInputTerminal(
    UCHAR    terminalLink,
    UCHAR &  numOfChannels,
    USHORT & terminalType,
    UCHAR &  volumeUnitID,
    UCHAR &  muteUnitID
)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    enum
    {
        MAX_OF_UNITS = 10
    };

    PAGED_CODE();

    ULONG numOfAcInputTerminalInfo = m_acInputTerminalInfo.GetNumOfArray();
    UCHAR sourceID = USBAudioConfiguration::InvalidID;

    numOfChannels = 0;
    terminalType = NS_USBAudio0200::LINE_CONNECTOR;
    volumeUnitID = USBAudioConfiguration::InvalidID;
    muteUnitID = USBAudioConfiguration::InvalidID;

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - num of ac input terminal info %u", numOfAcInputTerminalInfo);
    for (ULONG index = 0; index < numOfAcInputTerminalInfo; index++)
    {
        NS_USBAudio0200::PCS_AC_INPUT_TERMINAL_DESCRIPTOR inputTerminalDescriptor = nullptr;
        if (NT_SUCCESS(m_acInputTerminalInfo.Get(index, inputTerminalDescriptor)))
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - terminal id %u, terminal link %u", inputTerminalDescriptor->bTerminalID, terminalLink);
            if (inputTerminalDescriptor->bTerminalID == terminalLink)
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - terminal id %u, channels %u", inputTerminalDescriptor->bTerminalID, inputTerminalDescriptor->bNrChannels);
                sourceID = inputTerminalDescriptor->bTerminalID;
                numOfChannels = inputTerminalDescriptor->bNrChannels;
                break;
            }
        }
    }

    for (ULONG units = 0; units < MAX_OF_UNITS; units++)
    {
        enum
        {
            MAX_CHAINED_MIXER_UNITS = 1
        };
        UCHAR sourceIDBackup = sourceID;

        status = SearchOutputTerminal(sourceID, numOfChannels, terminalType, volumeUnitID, muteUnitID, MAX_CHAINED_MIXER_UNITS);
        if (NT_SUCCESS(status))
        {
            return status;
        }
        if (sourceIDBackup == sourceID)
        {
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_DESCRIPTOR, "The topology link is broken or the topology could not be analyzed.");
            break;
        }
    }

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SearchInputTerminalFromOutputTerminal(
    UCHAR    terminalLink,
    UCHAR &  numOfChannels,
    USHORT & terminalType,
    UCHAR &  volumeUnitID,
    UCHAR &  muteUnitID
)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    enum
    {
        MAX_OF_UNITS = 10
    };

    PAGED_CODE();

    ULONG numOfAcOutputTerminalInfo = m_acOutputTerminalInfo.GetNumOfArray();
    UCHAR sourceID = USBAudioConfiguration::InvalidID;

    numOfChannels = 0;
    terminalType = NS_USBAudio0200::LINE_CONNECTOR;
    volumeUnitID = USBAudioConfiguration::InvalidID;
    muteUnitID = USBAudioConfiguration::InvalidID;

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - num of ac output terminal info %u", numOfAcOutputTerminalInfo);
    for (ULONG index = 0; index < numOfAcOutputTerminalInfo; index++)
    {
        NS_USBAudio0200::PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR outputTerminalDescriptor = nullptr;
        if (NT_SUCCESS(m_acOutputTerminalInfo.Get(index, outputTerminalDescriptor)))
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - terminal id %u, terminal link %u", outputTerminalDescriptor->bTerminalID, terminalLink);
            if (outputTerminalDescriptor->bTerminalID == terminalLink)
            {
                sourceID = outputTerminalDescriptor->bSourceID;
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bSourceID %u", outputTerminalDescriptor->bSourceID);
                break;
            }
        }
    }

    for (ULONG units = 0; units < MAX_OF_UNITS; units++)
    {
        ULONG numOfGenericAudioDescriptorInfo = m_genericAudioDescriptorInfo.GetNumOfArray();
        UCHAR sourceIDBackup = sourceID;

        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - souceID id %u", sourceID);
        for (ULONG index = 0; index < numOfGenericAudioDescriptorInfo; index++)
        {
            NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR genericAudioDescriptor = nullptr;
            if (NT_SUCCESS(m_genericAudioDescriptorInfo.Get(index, genericAudioDescriptor)))
            {
                switch (genericAudioDescriptor->bDescriptorSubtype)
                {
                case NS_USBAudio0200::INPUT_TERMINAL:
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - input terminal bTerminalID %u, bCSSourceID %u", ((NS_USBAudio0200::PCS_AC_INPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor)->bTerminalID, ((NS_USBAudio0200::PCS_AC_INPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor)->bCSourceID);
                    if (((NS_USBAudio0200::PCS_AC_INPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor)->bTerminalID == sourceID)
                    {
                        numOfChannels = ((NS_USBAudio0200::PCS_AC_INPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor)->bNrChannels;
                        terminalType = ((NS_USBAudio0200::PCS_AC_INPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor)->wTerminalType;
                        return STATUS_SUCCESS;
                    }
                    break;
                    break;
                case NS_USBAudio0200::FEATURE_UNIT: {
                    NS_USBAudio0200::PCS_AC_FEATURE_UNIT_DESCRIPTOR featureUnitDescriptor = (NS_USBAudio0200::PCS_AC_FEATURE_UNIT_DESCRIPTOR)genericAudioDescriptor;
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - feature unit unit id %u", featureUnitDescriptor->bUnitID);
                    if (featureUnitDescriptor->bUnitID == sourceID)
                    {
                        UCHAR size = 4; // CS_AC_FEATURE_UNIT_DESCRIPTOR::bmaControls
                        UCHAR channels = (featureUnitDescriptor->bLength - offsetof(NS_USBAudio0200::CS_AC_FEATURE_UNIT_DESCRIPTOR, ch)) / size;
                        for (UCHAR ch = 0; ch < channels; ++ch)
                        {
                            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - feature unit ch[%u] bmaControls %02x %02x %02x %02x", ch, featureUnitDescriptor->ch[ch].bmaControls[3], featureUnitDescriptor->ch[ch].bmaControls[2], featureUnitDescriptor->ch[ch].bmaControls[1], featureUnitDescriptor->ch[ch].bmaControls[0]);
                            if (featureUnitDescriptor->ch[ch].bmaControls[0] & NS_USBAudio0200::FEATURE_UNIT_BMA_MUTE_CONTROL_MASK)
                            {
                                muteUnitID = featureUnitDescriptor->bUnitID;
                            }
                            if (featureUnitDescriptor->ch[ch].bmaControls[0] & NS_USBAudio0200::FEATURE_UNIT_BMA_VOLUME_CONTROL_MASK)
                            {
                                volumeUnitID = featureUnitDescriptor->bUnitID;
                            }
                        }
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - feature unit source id %u", featureUnitDescriptor->bSourceID);
                        sourceID = featureUnitDescriptor->bSourceID;
                        break;
                    }
                }
                break;
                default:
                case NS_USBAudio0200::CLOCK_MULTIPLIER:
                case NS_USBAudio0200::CLOCK_SELECTOR:
                case NS_USBAudio0200::CLOCK_SOURCE:
                case NS_USBAudio0200::EXTENSION_UNIT:
                case NS_USBAudio0200::MIXER_UNIT:
                case NS_USBAudio0200::OUTPUT_TERMINAL:
                case NS_USBAudio0200::PROCESSING_UNIT:
                case NS_USBAudio0200::SAMPLE_RATE_CONVERTER:
                case NS_USBAudio0200::SELECTOR_UNIT:
                    break;
                }
            }
        }
        if (sourceIDBackup == sourceID)
        {
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_DESCRIPTOR, "The topology link is broken or the topology could not be analyzed.");
            break;
        }
    }

    return status;
}

// ======================================================================

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudio2StreamInterface::USBAudio2StreamInterface(
    WDFOBJECT                 parentObject,
    PUSB_INTERFACE_DESCRIPTOR descriptor
)
    : USBAudioStreamInterface(parentObject, descriptor)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudio2StreamInterface::~USBAudio2StreamInterface()
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    //
    // m_usbAudioDataFormat is deleted in the destructor of USBAudioDataFormatManager.
    //
    m_usbAudioDataFormat = nullptr;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudio2StreamInterface * USBAudio2StreamInterface::Create(
    WDFOBJECT                       parentObject,
    const PUSB_INTERFACE_DESCRIPTOR descriptor
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    USBAudio2StreamInterface * streamInterface = new (POOL_FLAG_NON_PAGED, DRIVER_TAG) USBAudio2StreamInterface(parentObject, descriptor);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");

    return streamInterface;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio2StreamInterface::IsValidAlternateSetting(
    ULONG validAlternateSettingMap,
    UCHAR alternateSetting
)
{
    PAGED_CODE();

    // UCHAR controlSize = validAlternateSettingMap & 0xff;

    validAlternateSettingMap >>= 8;

    return (validAlternateSettingMap & (1 << alternateSetting)) ? true : false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio2StreamInterface::IsInterfaceSupportingFormats()
{
    PAGED_CODE();

    return USBAudioDataFormat::IsSupportedFormat(m_formatType, m_audioDataFormat);
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2StreamInterface::CheckInterfaceConfiguration(
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG    validAlternateSettingMap = 0;
    UCHAR    activeAlternateSetting = 0;

    PAGED_CODE();

    if ((GetCurrentBmControls() & NS_USBAudio0200::AS_VAL_ALT_SETTINGS_CONTROL_MASK) == NS_USBAudio0200::AS_VAL_ALT_SETTINGS_CONTROL_READ)
    {
        status = ControlRequestGetACTValAltSettingsControl(deviceContext, GetInterfaceNumber(), validAlternateSettingMap);
        if (NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, validAlternateSettingMap 0x%x, control read only", GetInterfaceNumber(), validAlternateSettingMap);
        }
    }
    else
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, validAlternateSettingMap, control disable", GetInterfaceNumber());
    }

    if ((GetCurrentBmControls() & NS_USBAudio0200::AS_ACT_ALT_SETTING_CONTROL_MASK) == NS_USBAudio0200::AS_ACT_ALT_SETTING_CONTROL_READ)
    {
        status = ControlRequestGetACTAltSettingsControl(deviceContext, GetInterfaceNumber(), activeAlternateSetting);
        if (NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, activeAlternateSetting 0x%x, control read only", GetInterfaceNumber(), activeAlternateSetting);
        }
    }
    else
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, activeAlternateSetting, control disable", GetInterfaceNumber());
    }

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2StreamInterface::SetFormatType(
    const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(descriptor == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE((descriptor->bDescriptorType != NS_USBAudio0200::CS_INTERFACE) || (descriptor->bDescriptorSubtype != NS_USBAudio0200::FORMAT_TYPE), STATUS_INVALID_PARAMETER);

    switch (((NS_USBAudio0200::PCS_AS_TYPE_I_FORMAT_TYPE_DESCRIPTOR)descriptor)->bFormatType)
    {
    case NS_USBAudio0200::FORMAT_TYPE_I: {
        if ((m_formatITypeDescriptor != nullptr) || (m_formatIIITypeDescriptor != nullptr))
        {
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_DESCRIPTOR, "Format type I or III descriptor is already set.");
        }
        NS_USBAudio0200::PCS_AS_TYPE_I_FORMAT_TYPE_DESCRIPTOR formatITypeDescriptor = (NS_USBAudio0200::PCS_AS_TYPE_I_FORMAT_TYPE_DESCRIPTOR)descriptor;

        m_formatITypeDescriptor = formatITypeDescriptor;
        m_enableGetFormatType = false;

        //
        // If multiple formats are supported, allow obtaining the format type via a Control Request.
        //
        if (m_csAsInterfaceDescriptor != nullptr)
        {
            ULONG formats = USBAudioDataFormat::ConverBmFormats(m_csAsInterfaceDescriptor->bmFormats);
            for (ULONG mask = 1, count = 0; mask != 0; mask <<= 1)
            {
                ULONG format = formats & mask;
                if (format != 0)
                {
                    if (count == 0)
                    {
                        m_formatType = ((NS_USBAudio0200::PCS_AS_TYPE_I_FORMAT_TYPE_DESCRIPTOR)descriptor)->bFormatType;
                    }
                    count++;
                    if (count >= 2)
                    {
                        m_enableGetFormatType = true;
                        TraceEvents(TRACE_LEVEL_WARNING, TRACE_DESCRIPTOR, "Several formats are defined.");
                        break;
                    }
                }
            }
        }
        else
        {
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_DESCRIPTOR, "Class-Specific AS interface descriptor is null.");
        }
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, alternate setting %u, %u ch, %u bytes per sample, %u valid bits, audio data format 0x%x, %s", GetInterfaceNumber(), GetAlternateSetting(), GetCurrentChannels(), formatITypeDescriptor->bSubslotSize, formatITypeDescriptor->bBitResolution, m_formatITypeDescriptor != nullptr ? m_formatITypeDescriptor->bFormatType : 0, m_enableGetFormatType ? "get audio data format enable." : " ");
    }
    break;
    case NS_USBAudio0200::FORMAT_TYPE_III: {
        if ((m_formatITypeDescriptor != nullptr) || (m_formatIIITypeDescriptor != nullptr))
        {
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_DESCRIPTOR, "Format type I or III descriptor is already set.");
        }
        NS_USBAudio0200::PCS_AS_TYPE_III_FORMAT_TYPE_DESCRIPTOR formatIIITypeDescriptor = (NS_USBAudio0200::PCS_AS_TYPE_III_FORMAT_TYPE_DESCRIPTOR)descriptor;
        m_enableGetFormatType = false;

        m_formatIIITypeDescriptor = formatIIITypeDescriptor;
        //
        // If multiple formats are supported, allow obtaining the format type via a Control Request.
        //
        if (m_csAsInterfaceDescriptor != nullptr)
        {
            ULONG formats = USBAudioDataFormat::ConverBmFormats(m_csAsInterfaceDescriptor->bmFormats);
            for (ULONG mask = 1, count = 0; mask != 0; mask <<= 1)
            {
                ULONG format = formats & mask;
                if (format != 0)
                {
                    if (count == 0)
                    {
                        m_formatType = ((NS_USBAudio0200::PCS_AS_TYPE_III_FORMAT_TYPE_DESCRIPTOR)descriptor)->bFormatType;
                    }
                    count++;
                    if (count >= 2)
                    {
                        m_enableGetFormatType = true;
                        TraceEvents(TRACE_LEVEL_WARNING, TRACE_DESCRIPTOR, "Several formats are defined.");
                        break;
                    }
                }
            }
        }
        else
        {
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_DESCRIPTOR, "Class-Specific AS interface descriptor is null.");
        }
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, alternate setting %u, %u ch, %u bytes per sample, %u valid bits, audio data format 0x%x, %s", GetInterfaceNumber(), GetAlternateSetting(), GetCurrentChannels(), formatIIITypeDescriptor->bSubslotSize, formatIIITypeDescriptor->bBitResolution, m_formatIIITypeDescriptor != nullptr ? m_formatIIITypeDescriptor->bFormatType : 0, m_enableGetFormatType ? "get audio data format enable." : " ");
    }
    break;
    case NS_USBAudio0200::FORMAT_TYPE_II:
    case NS_USBAudio0200::FORMAT_TYPE_IV:
    default:
        status = STATUS_NOT_SUPPORTED;
        break;
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2StreamInterface::SetGeneral(const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(descriptor == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE((descriptor->bDescriptorType != NS_USBAudio0200::CS_INTERFACE) || (descriptor->bDescriptorSubtype != NS_USBAudio0200::AS_GENERAL), STATUS_INVALID_PARAMETER);

    if (m_csAsInterfaceDescriptor != nullptr)
    {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_DESCRIPTOR, "AS interface descriptor is already set.");
    }

    NS_USBAudio0200::PCS_AS_INTERFACE_DESCRIPTOR csAsInterfaceDescriptor = (NS_USBAudio0200::PCS_AS_INTERFACE_DESCRIPTOR)descriptor;
    if (!USBAudioDataFormat::IsSupportedFormat(csAsInterfaceDescriptor->bFormatType, *((ULONG *)(csAsInterfaceDescriptor->bmFormats))))
    {
        // skip this descriptor;
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_DESCRIPTOR, "This format is not supported.");
    }
    else
    {
        m_csAsInterfaceDescriptor = csAsInterfaceDescriptor;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2StreamInterface::SetIsochronousAudioDataEndpoint(
    const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(descriptor == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE((descriptor->bDescriptorType != NS_USBAudio0200::CS_ENDPOINT) || (descriptor->bDescriptorSubtype != NS_USBAudio0200::EP_GENERAL), STATUS_INVALID_PARAMETER);

    if (m_isochronousAudioDataEndpointDescriptor != nullptr)
    {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_DESCRIPTOR, "CS isochronous audio data endpoint descriptor is already set.");
    }

    NS_USBAudio0200::PCS_AS_ISOCHRONOUS_AUDIO_DATA_ENDPOINT_DESCRIPTOR isochronousAudioDataEndpointDescriptor = (NS_USBAudio0200::PCS_AS_ISOCHRONOUS_AUDIO_DATA_ENDPOINT_DESCRIPTOR)descriptor;

    if (isochronousAudioDataEndpointDescriptor->bLockDelayUnits == NS_USBAudio0200::LOCK_DELAY_UNIT_MILLISECONDS)
    {
        m_lockDelay = isochronousAudioDataEndpointDescriptor->wLockDelay;
    }
    m_isochronousAudioDataEndpointDescriptor = isochronousAudioDataEndpointDescriptor;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudio2StreamInterface::GetCurrentTerminalLink()
{
    PAGED_CODE();

    return (m_csAsInterfaceDescriptor != nullptr) ? m_csAsInterfaceDescriptor->bTerminalLink : USBAudioConfiguration::InvalidID;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudio2StreamInterface::GetCurrentBmControls()
{
    PAGED_CODE();

    return (m_csAsInterfaceDescriptor != nullptr) ? m_csAsInterfaceDescriptor->bmControls : 0;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudio2StreamInterface::GetCurrentChannels()
{
    PAGED_CODE();

    return (m_csAsInterfaceDescriptor != nullptr) ? m_csAsInterfaceDescriptor->bNrChannels : 0;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudio2StreamInterface::GetCurrentChannelNames()
{
    PAGED_CODE();

    return (m_csAsInterfaceDescriptor != nullptr) ? m_csAsInterfaceDescriptor->iChannelNames : USBAudioConfiguration::InvalidString;
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG
USBAudio2StreamInterface::GetMaxSupportedBytesPerSample()
{
    ULONG maxSupportedBytesPerSample = 0;

    PAGED_CODE();

    if (m_formatITypeDescriptor != nullptr)
    {
        maxSupportedBytesPerSample = m_formatITypeDescriptor->bSubslotSize;
    }
    else if (m_formatIIITypeDescriptor != nullptr)
    {
        maxSupportedBytesPerSample = m_formatIIITypeDescriptor->bSubslotSize;
    }
    return maxSupportedBytesPerSample;
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG USBAudio2StreamInterface::GetMaxSupportedValidBitsPerSample()
{
    ULONG maxSupportedValidBitsPerSample = 0;

    PAGED_CODE();

    if (m_formatITypeDescriptor != nullptr)
    {
        maxSupportedValidBitsPerSample = m_formatITypeDescriptor->bBitResolution;
    }
    else if (m_formatIIITypeDescriptor != nullptr)
    {
        maxSupportedValidBitsPerSample = m_formatIIITypeDescriptor->bBitResolution;
    }
    return maxSupportedValidBitsPerSample;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudio2StreamInterface::GetCurrentActiveAlternateSetting()
{
    PAGED_CODE();
    return m_activeAlternateSetting;
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG USBAudio2StreamInterface::GetCurrentValidAlternateSettingMap()
{
    PAGED_CODE();
    return m_validAlternateSettingMap;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudio2StreamInterface::GetValidBitsPerSample()
{
    UCHAR validBitsPerSample = 0;

    PAGED_CODE();

    if (m_formatITypeDescriptor != nullptr)
    {
        validBitsPerSample = m_formatITypeDescriptor->bBitResolution;
    }
    else if (m_formatIIITypeDescriptor != nullptr)
    {
        validBitsPerSample = m_formatIIITypeDescriptor->bBitResolution;
    }
    return validBitsPerSample;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudio2StreamInterface::GetBytesPerSample()
{
    UCHAR bytesPerSample = 0;

    PAGED_CODE();

    if (m_formatITypeDescriptor != nullptr)
    {
        bytesPerSample = m_formatITypeDescriptor->bSubslotSize;
    }
    else if (m_formatIIITypeDescriptor != nullptr)
    {
        bytesPerSample = m_formatIIITypeDescriptor->bSubslotSize;
    }
    return bytesPerSample;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio2StreamInterface::HasInputIsochronousEndpoint()
{
    PAGED_CODE();

    if (m_usbAudioEndpoints != nullptr)
    {
        for (ULONG index = 0; index < GetNumEndpoints(); index++)
        {
            if (m_usbAudioEndpoints[index] != nullptr)
            {
                UCHAR endpointAddress = 0;
                UCHAR endpointAttribute = 0;
                if (GetEndpointAddress(index, endpointAddress) && GetEndpointAttribute(index, endpointAttribute))
                {
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - endpointAddress = 0x%x, direct in %!bool!", endpointAddress, USB_ENDPOINT_DIRECTION_IN(endpointAddress));
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - endpointAttribute = 0x%x, 0x%x, 0x%x", endpointAttribute, USB_ENDPOINT_TYPE_ISOCHRONOUS_USAGE(endpointAttribute), USB_ENDPOINT_TYPE_ISOCHRONOUS_USAGE_FEEDBACK_ENDPOINT);
                    if (((endpointAttribute & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_TYPE_ISOCHRONOUS) && USB_ENDPOINT_DIRECTION_IN(endpointAddress) && (USB_ENDPOINT_TYPE_ISOCHRONOUS_USAGE(endpointAttribute) != USB_ENDPOINT_TYPE_ISOCHRONOUS_USAGE_FEEDBACK_ENDPOINT))
                    {
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - endpointAttribute = 0x%x, 0x%x", endpointAttribute, USB_ENDPOINT_TYPE_ISOCHRONOUS_USAGE(endpointAttribute));
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio2StreamInterface::HasOutputIsochronousEndpoint()
{
    PAGED_CODE();

    if (m_usbAudioEndpoints != nullptr)
    {
        for (ULONG index = 0; index < GetNumEndpoints(); index++)
        {
            if (m_usbAudioEndpoints[index] != nullptr)
            {
                UCHAR endpointAddress = 0;
                UCHAR endpointAttribute = 0;
                if (GetEndpointAddress(index, endpointAddress) && GetEndpointAttribute(index, endpointAttribute))
                {
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - endpointAddress = 0x%x, direct in %!bool!", endpointAddress, USB_ENDPOINT_DIRECTION_OUT(endpointAddress));
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - endpointAttribute = 0x%x, 0x%x", endpointAttribute, USB_ENDPOINT_TYPE_ISOCHRONOUS_USAGE(endpointAttribute));
                    if (((endpointAttribute & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_TYPE_ISOCHRONOUS) && USB_ENDPOINT_DIRECTION_OUT(endpointAddress))
                    {
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - endpointAttribute = 0x%x, 0x%x", endpointAttribute, USB_ENDPOINT_TYPE_ISOCHRONOUS_USAGE(endpointAttribute));
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio2StreamInterface::HasFeedbackEndpoint()
{
    PAGED_CODE();

    if (m_usbAudioEndpoints != nullptr)
    {
        for (ULONG index = 0; index < GetNumEndpoints(); index++)
        {
            if (m_usbAudioEndpoints[index] != nullptr)
            {
                UCHAR endpointAttribute = 0;
                if (GetEndpointAttribute(index, endpointAttribute))
                {
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - endpointAttribute = 0x%x, 0x%x, 0x%x", endpointAttribute, USB_ENDPOINT_TYPE_ISOCHRONOUS_USAGE(endpointAttribute), USB_ENDPOINT_TYPE_ISOCHRONOUS_USAGE_FEEDBACK_ENDPOINT);
                    if (((endpointAttribute & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_TYPE_ISOCHRONOUS) && (USB_ENDPOINT_TYPE_ISOCHRONOUS_USAGE(endpointAttribute) == USB_ENDPOINT_TYPE_ISOCHRONOUS_USAGE_FEEDBACK_ENDPOINT))
                    {
                        USHORT maxPacketSize = 0;
                        UCHAR  interval = 0;
                        if (GetMaxPacketSize(index, maxPacketSize))
                        {
                            if (maxPacketSize != 4)
                            {
                                TraceEvents(TRACE_LEVEL_WARNING, TRACE_DESCRIPTOR, "This driver cannot deal feedback packet length %u.", maxPacketSize);
                                return false;
                            }
                        }
                        else
                        {
                            return false;
                        }
                        if (GetInterval(index, interval))
                        {
                            if (interval > 4)
                            {
                                TraceEvents(TRACE_LEVEL_WARNING, TRACE_DESCRIPTOR, "Microsoft USB driver stack cannot deal feedback interval %u.", interval);
                                return false;
                            }
                        }
                        else
                        {
                            return false;
                        }
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudio2StreamInterface::GetFeedbackEndpointAddress()
{
    PAGED_CODE();

    if (m_usbAudioEndpoints != nullptr)
    {
        for (ULONG index = 0; index < GetNumEndpoints(); index++)
        {
            if (m_usbAudioEndpoints[index] != nullptr)
            {
                UCHAR endpointAddress = 0;
                UCHAR endpointAttribute = 0;
                if (GetEndpointAddress(index, endpointAddress) && GetEndpointAttribute(index, endpointAttribute))
                {
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - endpointAddress = 0x%x", endpointAddress);
                    if (((endpointAttribute & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_TYPE_ISOCHRONOUS) && (USB_ENDPOINT_TYPE_ISOCHRONOUS_USAGE(endpointAttribute) == USB_ENDPOINT_TYPE_ISOCHRONOUS_USAGE_FEEDBACK_ENDPOINT))
                    {
                        return endpointAddress;
                    }
                }
            }
        }
    }

    return 0;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudio2StreamInterface::GetFeedbackInterval()
{
    PAGED_CODE();

    if (m_usbAudioEndpoints != nullptr)
    {
        for (ULONG index = 0; index < GetNumEndpoints(); index++)
        {
            if (m_usbAudioEndpoints[index] != nullptr)
            {
                UCHAR endpointAttribute = 0;
                if (GetEndpointAttribute(index, endpointAttribute))
                {
                    if (((endpointAttribute & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_TYPE_ISOCHRONOUS) && (USB_ENDPOINT_TYPE_ISOCHRONOUS_USAGE(endpointAttribute) == USB_ENDPOINT_TYPE_ISOCHRONOUS_USAGE_FEEDBACK_ENDPOINT))
                    {
                        UCHAR interval = 0;
                        if (GetInterval(index, interval))
                        {
                            return interval;
                        }
                        else
                        {
                            return 0;
                        }
                    }
                }
            }
        }
    }

    return 0;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio2StreamInterface::IsValidAudioDataFormat(
    ULONG formatType,
    ULONG audioDataFormat
)
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - format type %u %u, audio data format 0x%x, 0x%x", m_formatType, formatType, m_audioDataFormat, audioDataFormat);

    return ((formatType == m_formatType) && (m_audioDataFormat & audioDataFormat));
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2StreamInterface::UpdateCurrentACTValAltSettingsControl(
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;
    UCHAR    activeAlternateSetting = 0;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(deviceContext == nullptr, STATUS_INVALID_PARAMETER);

    if ((GetCurrentBmControls() & NS_USBAudio0200::AS_ACT_ALT_SETTING_CONTROL_MASK) == NS_USBAudio0200::AS_ACT_ALT_SETTING_CONTROL_READ)
    {
        status = ControlRequestGetACTAltSettingsControl(deviceContext, GetInterfaceNumber(), activeAlternateSetting);
        if (NT_SUCCESS(status))
        {
            m_activeAlternateSetting = activeAlternateSetting;
        }
        else if (status == STATUS_UNSUCCESSFUL)
        {
            // For devices that do not support NS_USBAudio0200::AS_ACT_ALT_SETTING_CONTROL, return STATUS_SUCCESS.
            m_activeAlternateSetting = 0;
            status = STATUS_SUCCESS;
        }
    }
    else
    {
        m_activeAlternateSetting = 0;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2StreamInterface::UpdateCurrentACTAltSettingsControl(
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG    validAlternateSettingMap = 0;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(deviceContext == nullptr, STATUS_INVALID_PARAMETER);

    if ((GetCurrentBmControls() & NS_USBAudio0200::AS_VAL_ALT_SETTINGS_CONTROL_MASK) == NS_USBAudio0200::AS_VAL_ALT_SETTINGS_CONTROL_READ)
    {
        status = ControlRequestGetACTValAltSettingsControl(deviceContext, GetInterfaceNumber(), validAlternateSettingMap);
        if (NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, validAlternateSettingMap 0x%x, control read only", GetInterfaceNumber(), validAlternateSettingMap);
            m_validAlternateSettingMap = validAlternateSettingMap;
        }
        else if (status == STATUS_UNSUCCESSFUL)
        {
            // For devices that do not support NS_USBAudio0200::AS_VAL_ALT_SETTINGS_CONTROL, return STATUS_SUCCESS.
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_DESCRIPTOR, " - interface %u, validAlternateSettingMap 0x%x, control read only. %!STATUS!", GetInterfaceNumber(), validAlternateSettingMap, status);
            m_validAlternateSettingMap = 0;
            status = STATUS_SUCCESS;
        }
    }
    else
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, validAlternateSettingMap, control disable", GetInterfaceNumber());
        m_validAlternateSettingMap = 0;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2StreamInterface::UpdateCurrentAudioDataFormat(
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG    audioDataFormat = 0;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(deviceContext == nullptr, STATUS_INVALID_PARAMETER);

    if (m_enableGetFormatType)
    {
        status = ControlRequestGetAudioDataFormat(deviceContext, GetInterfaceNumber(), audioDataFormat);

        // If the device does not support NS_USBAudio0200::AS_AUDIO_DATA_FORMAT_CONTROL, the default value NS_USBAudio0200::PCM will be used.
        if (NT_SUCCESS(status))
        {
            m_currentAudioDataFormat = audioDataFormat;
        }
    }

    if (m_csAsInterfaceDescriptor != nullptr)
    {
        m_audioDataFormat = USBAudioDataFormat::ConverBmFormats(m_csAsInterfaceDescriptor->bmFormats);
    }
    else
    {
        m_audioDataFormat = 0;
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, alternate setting %u, This interface is not supported.", GetInterfaceNumber(), GetAlternateSetting());
    }
    if (audioDataFormat == 0)
    {
        m_currentAudioDataFormat = m_audioDataFormat;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2StreamInterface::QueryCurrentAttributeAll(
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    if (GetAlternateSetting() != 0)
    {
        RETURN_NTSTATUS_IF_FAILED(UpdateCurrentACTValAltSettingsControl(deviceContext));

        RETURN_NTSTATUS_IF_FAILED(UpdateCurrentACTAltSettingsControl(deviceContext));

        status = UpdateCurrentAudioDataFormat(deviceContext);
        if (status == STATUS_UNSUCCESSFUL)
        {
            // If the device does not support NS_USBAudio0200::AS_AUDIO_DATA_FORMAT_CONTROL, treat the call as a success.
            status = STATUS_SUCCESS;
        }
    }

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2StreamInterface::RegisterUSBAudioDataFormatManager(
    USBAudioDataFormatManager & usbAudioDataFormatManagerIn,
    USBAudioDataFormatManager & usbAudioDataFormatManagerOut
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");
    if ((m_csAsInterfaceDescriptor != nullptr) && (m_usbAudioEndpoints != nullptr) && (m_usbAudioDataFormat == nullptr))
    {
        ULONG formats = USBAudioDataFormat::ConverBmFormats(m_csAsInterfaceDescriptor->bmFormats);

        if ((m_formatITypeDescriptor != nullptr) || (m_formatIIITypeDescriptor != nullptr))
        {
            UCHAR formatType = 0;
            UCHAR subslotSize = 0;
            UCHAR bitResolution = 0;
            if (m_formatITypeDescriptor != nullptr)
            {
                formatType = m_formatITypeDescriptor->bFormatType;
                subslotSize = m_formatITypeDescriptor->bSubslotSize;
                bitResolution = m_formatITypeDescriptor->bBitResolution;
            }
            if (m_formatIIITypeDescriptor != nullptr)
            {
                formatType = m_formatIIITypeDescriptor->bFormatType;
                subslotSize = m_formatIIITypeDescriptor->bSubslotSize;
                bitResolution = m_formatIIITypeDescriptor->bBitResolution;
            }
            for (ULONG index = 0; (index < GetNumEndpoints() && (m_usbAudioDataFormat == nullptr)); index++)
            {
                UCHAR endpointAddress = 0;
                UCHAR endpointAttribute = 0;
                if (GetEndpointAddress(index, endpointAddress) && GetEndpointAttribute(index, endpointAttribute))
                {
                    if ((endpointAttribute & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_TYPE_ISOCHRONOUS)
                    {

                        for (ULONG mask = 1; mask != 0; mask <<= 1)
                        {
                            ULONG format = formats & mask;
                            if (format != 0)
                            {
                                USBAudioDataFormat * usbAudioDataFormat = nullptr;
                                UCHAR                formatArray[4] = {
                                    format & 0xff, (format >> 8) & 0xff, (format >> 16) & 0xff, (format >> 24) & 0xff
                                };
                                if (USB_ENDPOINT_DIRECTION_IN(endpointAddress))
                                {
                                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " register input");
                                    RETURN_NTSTATUS_IF_FAILED(usbAudioDataFormatManagerIn.SetUSBAudioDataFormat(formatType, formatArray, subslotSize, bitResolution, usbAudioDataFormat));
                                }
                                else
                                {
                                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " register output");
                                    RETURN_NTSTATUS_IF_FAILED(usbAudioDataFormatManagerOut.SetUSBAudioDataFormat(formatType, formatArray, subslotSize, bitResolution, usbAudioDataFormat));
                                }
                                m_usbAudioDataFormat = usbAudioDataFormat;
                            }
                        }
                    }
                }
            }
        }
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioInterfaceInfo::USBAudioInterfaceInfo(
    WDFOBJECT parentObject
)
    : m_parentObject(parentObject)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioInterfaceInfo::~USBAudioInterfaceInfo()
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");
    ULONG numOfAlternateInterface = m_usbAudioAlternateInterfaces.GetNumOfArray();

    for (ULONG index = 0; index < numOfAlternateInterface; index++)
    {
        USBAudioInterface * usbAudioInterface = nullptr;
        if (NT_SUCCESS(m_usbAudioAlternateInterfaces.Get(index, usbAudioInterface)))
        {
            if (usbAudioInterface != nullptr)
            {
                delete usbAudioInterface;
            }
        }
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudioInterfaceInfo::StoreInterface(USBAudioInterface * interface)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(interface == nullptr, STATUS_INVALID_PARAMETER);

    status = m_usbAudioAlternateInterfaces.Set(m_parentObject, interface->GetAlternateSetting(), interface);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudioInterfaceInfo::GetInterfaceNumber(ULONG & interfaceNumber)
{
    PAGED_CODE();

    ULONG numOfAlternateInterface = m_usbAudioAlternateInterfaces.GetNumOfArray();

    for (ULONG index = 0; index < numOfAlternateInterface; index++)
    {
        USBAudioInterface * usbAudioInterface = nullptr;
        if (NT_SUCCESS(m_usbAudioAlternateInterfaces.Get(index, usbAudioInterface)))
        {
            if (usbAudioInterface != nullptr)
            {
                interfaceNumber = usbAudioInterface->GetInterfaceNumber();
                return STATUS_SUCCESS;
            }
        }
    }

    return STATUS_NO_DATA_DETECTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioInterfaceInfo::IsStreamInterface()
{
    bool                isStreamInterface = false;
    USBAudioInterface * usbAudioInterface = nullptr;

    PAGED_CODE();

    ULONG numOfAlternateInterface = m_usbAudioAlternateInterfaces.GetNumOfArray();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "numOfAlternateInterface = %u", numOfAlternateInterface);
    if (numOfAlternateInterface == 0)
    {
        return false;
    }

    if (NT_SUCCESS(m_usbAudioAlternateInterfaces.Get(0, usbAudioInterface)))
    {
        isStreamInterface = usbAudioInterface->IsStreamInterface();
    }
    return isStreamInterface;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioInterfaceInfo::IsControlInterface()
{
    bool                isControlInterface = false;
    USBAudioInterface * usbAudioInterface = nullptr;

    PAGED_CODE();

    if (NT_SUCCESS(m_usbAudioAlternateInterfaces.Get(0, usbAudioInterface)))
    {
        isControlInterface = usbAudioInterface->IsControlInterface();
    }
    return isControlInterface;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudioInterfaceInfo::QueryCurrentAttributeAll(
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    ULONG numOfAlternateInterface = m_usbAudioAlternateInterfaces.GetNumOfArray();

    for (ULONG index = 0; index < numOfAlternateInterface; index++)
    {
        USBAudioInterface * usbAudioInterface = nullptr;
        if (NT_SUCCESS(m_usbAudioAlternateInterfaces.Get(index, usbAudioInterface)))
        {
            status = ((USBAudioControlInterface *)usbAudioInterface)->QueryCurrentAttributeAll(deviceContext);
        }
    }
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudioInterfaceInfo::QueryRangeAttributeAll(
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    if (IsControlInterface())
    {
        ULONG numOfAlternateInterface = m_usbAudioAlternateInterfaces.GetNumOfArray();

        for (ULONG index = 0; index < numOfAlternateInterface; index++)
        {
            USBAudioInterface * usbAudioInterface = nullptr;
            if (NT_SUCCESS(m_usbAudioAlternateInterfaces.Get(index, usbAudioInterface)))
            {
                status = ((USBAudioControlInterface *)usbAudioInterface)->QueryRangeAttributeAll(deviceContext);
            }
        }
    }
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudioInterfaceInfo::CheckInterfaceConfiguration(
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    if (IsStreamInterface())
    {
        USBAudioInterface * usbAudioInterface = nullptr;
        if (NT_SUCCESS(m_usbAudioAlternateInterfaces.Get(0, usbAudioInterface)))
        {
            status = ((USBAudioStreamInterface *)usbAudioInterface)->CheckInterfaceConfiguration(deviceContext);
        }
    }

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioInterfaceInfo::GetMaxPacketSize(
    IsoDirection direction,
    ULONG &      maxPacketSize
)
{
    bool   result = false;
    USHORT interfaceMaxPacketSize = 0;

    PAGED_CODE();

    if (IsStreamInterface())
    {
        ULONG numOfAlternateInterface = m_usbAudioAlternateInterfaces.GetNumOfArray();

        for (ULONG index = 0; index < numOfAlternateInterface; index++)
        {
            USBAudioInterface * usbAudioInterface = nullptr;
            if (NT_SUCCESS(m_usbAudioAlternateInterfaces.Get(index, usbAudioInterface)))
            {
                USHORT currentMaxPacketSize = 0;
                if (usbAudioInterface->GetMaxPacketSize(direction, currentMaxPacketSize))
                {
                    result = true;
                    if (currentMaxPacketSize > interfaceMaxPacketSize)
                    {
                        interfaceMaxPacketSize = currentMaxPacketSize;
                    }
                }
            }
        }
    }

    if (result)
    {
        maxPacketSize = interfaceMaxPacketSize;
    }

    return result;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
USBAudioInterfaceInfo::GetMaxSupportedValidBitsPerSample(
    bool    isInput,
    ULONG   desiredFormatType,
    ULONG   desiredFormat,
    ULONG & maxSupportedBytesPerSample,
    ULONG & maxSupportedValidBitsPerSample
)
{
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    ULONG    currentMaxSupportedBytesPerSample = 0;
    ULONG    currentMaxSupportedValidBitsPerSample = 0;

    PAGED_CODE();

    maxSupportedBytesPerSample = 0;
    maxSupportedValidBitsPerSample = 0;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    if (IsStreamInterface())
    {
        ULONG numOfAlternateInterface = m_usbAudioAlternateInterfaces.GetNumOfArray();

        for (ULONG index = 1; index < numOfAlternateInterface; index++)
        {
            USBAudioInterface * usbAudioInterface = nullptr;

            if (NT_SUCCESS(m_usbAudioAlternateInterfaces.Get(index, usbAudioInterface)))
            {
                if (((USBAudioStreamInterface *)usbAudioInterface)->IsInterfaceSupportingFormats() && usbAudioInterface->IsSupportDirection(isInput) && ((USBAudioStreamInterface *)usbAudioInterface)->IsValidAudioDataFormat(desiredFormatType, desiredFormat))
                {
                    currentMaxSupportedValidBitsPerSample = ((USBAudioStreamInterface *)usbAudioInterface)->GetMaxSupportedValidBitsPerSample();
                    currentMaxSupportedBytesPerSample = ((USBAudioStreamInterface *)usbAudioInterface)->GetMaxSupportedBytesPerSample();
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, alternate setting %u", usbAudioInterface->GetInterfaceNumber(), usbAudioInterface->GetAlternateSetting());
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - currentMaxSupportedValidBitsPerSample %u, maxSupportedValidBitsPerSample %u", currentMaxSupportedValidBitsPerSample, maxSupportedValidBitsPerSample);
                    if (currentMaxSupportedValidBitsPerSample > maxSupportedValidBitsPerSample)
                    {
                        maxSupportedValidBitsPerSample = currentMaxSupportedValidBitsPerSample;
                        maxSupportedBytesPerSample = currentMaxSupportedBytesPerSample;
                    }
                }
            }
        }
    }

    if (maxSupportedValidBitsPerSample != 0)
    {
        status = STATUS_SUCCESS;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
USBAudioInterfaceInfo::GetNearestSupportedValidBitsPerSamples(
    bool    isInput,
    ULONG   desiredFormatType,
    ULONG   desiredFormat,
    ULONG & nearestSupportedBytesPerSample,
    ULONG & nearestSupportedValidBitsPerSample
)
{
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    ULONG    currentNearestSupportedBytesPerSample = 0;
    ULONG    currentNearestSupportedValidBitsPerSample = 0;
    ULONG    validBitsPerSampleDiff = ~(ULONG)0;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    if (IsStreamInterface())
    {
        ULONG numOfAlternateInterface = m_usbAudioAlternateInterfaces.GetNumOfArray();

        for (ULONG index = 1; index < numOfAlternateInterface; index++)
        {
            USBAudioInterface * usbAudioInterface = nullptr;

            if (NT_SUCCESS(m_usbAudioAlternateInterfaces.Get(index, usbAudioInterface)))
            {
                if (((USBAudioStreamInterface *)usbAudioInterface)->IsInterfaceSupportingFormats() && usbAudioInterface->IsSupportDirection(isInput) && ((USBAudioStreamInterface *)usbAudioInterface)->IsValidAudioDataFormat(desiredFormatType, desiredFormat))
                {
                    ULONG validBitsPerSample = ((USBAudioStreamInterface *)usbAudioInterface)->GetValidBitsPerSample();
                    ULONG bytesPerSample = ((USBAudioStreamInterface *)usbAudioInterface)->GetBytesPerSample();
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, alternate setting %u", usbAudioInterface->GetInterfaceNumber(), usbAudioInterface->GetAlternateSetting());
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - validBitsPerSample %u, nearestSupportedValidBitsPerSample %u", validBitsPerSample, nearestSupportedValidBitsPerSample);

                    if (validBitsPerSample == nearestSupportedValidBitsPerSample)
                    {
                        currentNearestSupportedBytesPerSample = nearestSupportedBytesPerSample;
                        currentNearestSupportedValidBitsPerSample = nearestSupportedValidBitsPerSample;
                        break;
                    }
                    else if (validBitsPerSample > nearestSupportedValidBitsPerSample)
                    {
                        if (validBitsPerSampleDiff > (validBitsPerSample - nearestSupportedValidBitsPerSample))
                        {
                            validBitsPerSampleDiff = validBitsPerSample - nearestSupportedValidBitsPerSample;
                            currentNearestSupportedBytesPerSample = bytesPerSample;
                            currentNearestSupportedValidBitsPerSample = validBitsPerSample;
                        }
                        else if (validBitsPerSampleDiff == (validBitsPerSample - nearestSupportedValidBitsPerSample))
                        {
                            if (currentNearestSupportedValidBitsPerSample < validBitsPerSample)
                            {
                                currentNearestSupportedBytesPerSample = bytesPerSample;
                                currentNearestSupportedValidBitsPerSample = validBitsPerSample;
                            }
                        }
                    }
                    else
                    {
                        if (validBitsPerSampleDiff > (nearestSupportedValidBitsPerSample - validBitsPerSample))
                        {
                            validBitsPerSampleDiff = nearestSupportedValidBitsPerSample - validBitsPerSample;
                            currentNearestSupportedBytesPerSample = bytesPerSample;
                            currentNearestSupportedValidBitsPerSample = validBitsPerSample;
                        }
                        else if (validBitsPerSampleDiff == (nearestSupportedValidBitsPerSample - validBitsPerSample))
                        {
                            if (currentNearestSupportedValidBitsPerSample < validBitsPerSample)
                            {
                                currentNearestSupportedBytesPerSample = bytesPerSample;
                                currentNearestSupportedValidBitsPerSample = validBitsPerSample;
                            }
                        }
                    }
                }
            }
        }
    }

    if (currentNearestSupportedValidBitsPerSample != 0)
    {
        nearestSupportedValidBitsPerSample = currentNearestSupportedValidBitsPerSample;
        nearestSupportedBytesPerSample = currentNearestSupportedBytesPerSample;
        status = STATUS_SUCCESS;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioInterfaceInfo::IsSupportDirection(
    bool isInput
)
{
    PAGED_CODE();

    if (IsStreamInterface())
    {
        USBAudioInterface * usbAudioInterface = nullptr;
        ULONG               numOfAlternateInterface = m_usbAudioAlternateInterfaces.GetNumOfArray();

        if (numOfAlternateInterface >= 2)
        {
            if (NT_SUCCESS(m_usbAudioAlternateInterfaces.Get(1, usbAudioInterface)))
            {
                return usbAudioInterface->IsSupportDirection(isInput);
            }
        }
    }

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioInterfaceInfo::GetTerminalLink(
    UCHAR & terminalLink
)
{
    PAGED_CODE();

    if (IsStreamInterface())
    {
        USBAudioInterface * usbAudioInterface = nullptr;
        ULONG               numOfAlternateInterface = m_usbAudioAlternateInterfaces.GetNumOfArray();

        if (numOfAlternateInterface >= 2)
        {
            if (NT_SUCCESS(m_usbAudioAlternateInterfaces.Get(1, usbAudioInterface)))
            {
                terminalLink = ((USBAudioStreamInterface *)usbAudioInterface)->GetCurrentTerminalLink();
                return true;
            }
        }
    }

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
USBAudioInterfaceInfo::SelectAlternateInterface(
    PDEVICE_CONTEXT    deviceContext,
    bool               isInput,
    ULONG              desiredFormatType,
    ULONG              desiredFormat,
    ULONG              desiredBytesPerSample,
    ULONG              desiredValidBitsPerSample,
    CURRENT_SETTINGS & currentSettings
)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG    validAlternateSettingMap = 0;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    if (IsStreamInterface())
    {
        ULONG maxPacketSize = 0;
        ULONG numOfAlternateInterface = m_usbAudioAlternateInterfaces.GetNumOfArray();

        GetMaxPacketSize(isInput ? IsoDirection::In : IsoDirection::Out, maxPacketSize);

        for (ULONG index = 0; index < numOfAlternateInterface; index++)
        {
            USBAudioInterface * usbAudioInterface = nullptr;
            if (NT_SUCCESS(m_usbAudioAlternateInterfaces.Get(index, usbAudioInterface)))
            {
                USBAudioStreamInterface * usbAudioStreamInterface = (USBAudioStreamInterface *)usbAudioInterface;
                RETURN_NTSTATUS_IF_FAILED(usbAudioStreamInterface->QueryCurrentAttributeAll(deviceContext));

                if (index != 0)
                {
                    validAlternateSettingMap = usbAudioStreamInterface->GetCurrentValidAlternateSettingMap();
                }
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - index %u, validAlternateSettingMap 0x%x, is valid alternate setting %u", index, validAlternateSettingMap, USBAudio2StreamInterface::IsValidAlternateSetting(validAlternateSettingMap, (UCHAR)index));
                if ((validAlternateSettingMap == 0) || ((validAlternateSettingMap >> 8) == 0x01) || USBAudio2StreamInterface::IsValidAlternateSetting(validAlternateSettingMap, (UCHAR)index))
                {
                    if (!usbAudioStreamInterface->IsEndpointTypeSupported(USB_ENDPOINT_TYPE_ISOCHRONOUS))
                    {
                        // skip interfaces other than those with an isochronous endpoint.
                        continue;
                    }
                    if (!usbAudioStreamInterface->IsSupportDirection(isInput))
                    {
                        // skip interfaces that do not have a specified endpoint direction
                        continue;
                    }
                    if (usbAudioStreamInterface->IsValidAudioDataFormat(desiredFormatType, desiredFormat))
                    {
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, alternate setting %u, index %u", usbAudioStreamInterface->GetInterfaceNumber(), usbAudioStreamInterface->GetAlternateSetting(), index);

                        // If you want to allow selection of audio data format, modify this.
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bytes per sample %u , desired bytes per sample %u, valid bits per sample %u, desired valid bits per sample %u, channels %u", usbAudioStreamInterface->GetBytesPerSample(), desiredBytesPerSample, usbAudioStreamInterface->GetValidBitsPerSample(), desiredValidBitsPerSample, usbAudioStreamInterface->GetCurrentChannels());
                        if ((usbAudioStreamInterface->GetBytesPerSample() == desiredBytesPerSample) && (usbAudioStreamInterface->GetValidBitsPerSample() == desiredValidBitsPerSample) && (usbAudioStreamInterface->GetCurrentChannels() != 0))
                        {
                            currentSettings.InterfaceNumber = (UCHAR)usbAudioStreamInterface->GetInterfaceNumber();
                            currentSettings.AlternateSetting = (UCHAR)usbAudioStreamInterface->GetAlternateSetting();
                            currentSettings.EndpointAddress = usbAudioStreamInterface->GetEndpointAddress();
                            currentSettings.TerminalLink = usbAudioStreamInterface->GetCurrentTerminalLink();
                            currentSettings.Channels = usbAudioStreamInterface->GetCurrentChannels();
                            currentSettings.ChannelNames = usbAudioStreamInterface->GetCurrentChannelNames();
                            currentSettings.BytesPerSample = usbAudioStreamInterface->GetBytesPerSample();
                            currentSettings.InterfaceClass = usbAudioStreamInterface->GetInterfaceClass();
                            currentSettings.InterfaceProtocol = usbAudioStreamInterface->GetInterfaceProtocol();
                            currentSettings.ValidBitsPerSample = usbAudioStreamInterface->GetValidBitsPerSample();
                            currentSettings.MaxFramesPerPacket = maxPacketSize / (currentSettings.Channels * currentSettings.BytesPerSample);
                            currentSettings.MaxPacketSize = maxPacketSize;
                            currentSettings.LockDelay = usbAudioStreamInterface->GetLockDelay();
                            if (usbAudioStreamInterface->HasFeedbackEndpoint())
                            {
                                currentSettings.FeedbackInterfaceNumber = usbAudioStreamInterface->GetInterfaceNumber();
                                currentSettings.FeedbackAlternateSetting = usbAudioStreamInterface->GetAlternateSetting();
                                currentSettings.FeedbackEndpointAddress = usbAudioStreamInterface->GetFeedbackEndpointAddress();
                                currentSettings.FeedbackInterval = usbAudioStreamInterface->GetFeedbackInterval();
                            }
                            // currentSettings.SupportedSampleRate
                            // currentSettings.AltSupportedSampleRate
                            // currentSettings.MaxSampleRate
                            // currentSettings.MinSampleRate
                            // currentSettings.SamplePerFrame
                        }
                        currentSettings.IsDeviceAdaptive = usbAudioStreamInterface->IsEndpointTypeIsochronousSynchronizationSupported(USB_ENDPOINT_TYPE_ISOCHRONOUS_SYNCHRONIZATION_ADAPTIVE);
                        currentSettings.IsDeviceSynchronous = usbAudioStreamInterface->IsEndpointTypeIsochronousSynchronizationSupported(USB_ENDPOINT_TYPE_ISOCHRONOUS_SYNCHRONIZATION_SYNCHRONOUS);
                    }
                }
            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
USBAudioInterfaceInfo::SetCurrentSampleFrequency(
    PDEVICE_CONTEXT deviceContext,
    ULONG           desiredSampleRate
)
{
    NTSTATUS            status = STATUS_SUCCESS;
    USBAudioInterface * usbAudioInterface = nullptr;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    if (NT_SUCCESS(m_usbAudioAlternateInterfaces.Get(0, usbAudioInterface)))
    {
        status = ((USBAudioControlInterface *)usbAudioInterface)->SetCurrentSampleFrequency(deviceContext, desiredSampleRate);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
USBAudioInterfaceInfo::GetCurrentSampleFrequency(
    PDEVICE_CONTEXT deviceContext,
    ULONG &         sampleRate
)
{
    NTSTATUS            status = STATUS_SUCCESS;
    USBAudioInterface * usbAudioInterface = nullptr;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    sampleRate = 0;
    if (NT_SUCCESS(m_usbAudioAlternateInterfaces.Get(0, usbAudioInterface)))
    {
        status = ((USBAudioControlInterface *)usbAudioInterface)->GetCurrentSampleFrequency(deviceContext, sampleRate);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
bool USBAudioInterfaceInfo::CanSetSampleFrequency(
    bool isInput
)
{
    USBAudioInterface * usbAudioInterface = nullptr;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    if (NT_SUCCESS(m_usbAudioAlternateInterfaces.Get(0, usbAudioInterface)))
    {
        return ((USBAudioControlInterface *)usbAudioInterface)->CanSetSampleFrequency(isInput);
    }

    return false;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudioInterfaceInfo::SearchOutputTerminalFromInputTerminal(
    UCHAR    terminalLink,
    UCHAR &  numOfChannels,
    USHORT & terminalType,
    UCHAR &  volumeUnitID,
    UCHAR &  muteUnitID
)
{
    NTSTATUS            status = STATUS_SUCCESS;
    USBAudioInterface * usbAudioInterface = nullptr;

    PAGED_CODE();

    RETURN_NTSTATUS_IF_FAILED(m_usbAudioAlternateInterfaces.Get(0, usbAudioInterface));
    status = ((USBAudio2ControlInterface *)usbAudioInterface)->SearchOutputTerminalFromInputTerminal(terminalLink, numOfChannels, terminalType, volumeUnitID, muteUnitID);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudioInterfaceInfo::SearchInputTerminalFromOutputTerminal(
    UCHAR    terminalLink,
    UCHAR &  numOfChannels,
    USHORT & terminalType,
    UCHAR &  volumeUnitID,
    UCHAR &  muteUnitID
)
{
    NTSTATUS            status = STATUS_SUCCESS;
    USBAudioInterface * usbAudioInterface = nullptr;

    PAGED_CODE();

    RETURN_NTSTATUS_IF_FAILED(m_usbAudioAlternateInterfaces.Get(0, usbAudioInterface));
    status = ((USBAudio2ControlInterface *)usbAudioInterface)->SearchInputTerminalFromOutputTerminal(terminalLink, numOfChannels, terminalType, volumeUnitID, muteUnitID);

    return status;
}

// ======================================================================
_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioConfiguration * USBAudioConfiguration::Create(
    PDEVICE_CONTEXT        deviceContext,
    PUSB_DEVICE_DESCRIPTOR usbDeviceDescriptor
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    USBAudioConfiguration * usbAudioConfiguration = new (POOL_FLAG_NON_PAGED, DRIVER_TAG) USBAudioConfiguration(deviceContext, usbDeviceDescriptor);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");

    return usbAudioConfiguration;
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioConfiguration::USBAudioConfiguration(PDEVICE_CONTEXT deviceContext, PUSB_DEVICE_DESCRIPTOR usbDeviceDescriptor)
    : m_deviceContext(deviceContext), m_usbDeviceDescriptor(usbDeviceDescriptor)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioConfiguration::~USBAudioConfiguration()
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    if (m_usbConfigurationDescriptor != nullptr)
    {
        for (ULONG interfaceIndex = 0; interfaceIndex < m_usbConfigurationDescriptor->bNumInterfaces; interfaceIndex++)
        {
            if (m_usbAudioInterfaceInfoes[interfaceIndex] != nullptr)
            {
                delete m_usbAudioInterfaceInfoes[interfaceIndex];
                m_usbAudioInterfaceInfoes[interfaceIndex] = nullptr;
            }
        }

        if (m_usbAudioInterfaceInfoesMemory != nullptr)
        {
            WdfObjectDelete(m_usbAudioInterfaceInfoesMemory);
            m_usbAudioInterfaceInfoesMemory = nullptr;
            m_usbAudioInterfaceInfoes = nullptr;
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
USBAudioConfiguration::CreateInterface(const PUSB_INTERFACE_DESCRIPTOR descriptor, USBAudioInterface *& usbAudioInterface)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(descriptor == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE(descriptor->bLength < NS_USBAudio::SIZE_OF_USB_INTERFACE_DESCRIPTOR, STATUS_INVALID_PARAMETER);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, " - num interfaces %u, interface %u", m_usbConfigurationDescriptor->bNumInterfaces, descriptor->bInterfaceNumber);

    auto createInterfaceScope = wil::scope_exit([&]() {
        if (!NT_SUCCESS(status) && (usbAudioInterface != nullptr))
        {
            delete usbAudioInterface;
            usbAudioInterface = nullptr;
        }
    });

    if (isInterfaceProtocolUSBAudio2(descriptor->bInterfaceProtocol))
    {
        m_isUSBAudio2 = true;

        switch (descriptor->bInterfaceSubClass)
        {
        case USB_AUDIO_CONTROL_SUB_CLASS:
            usbAudioInterface = USBAudio2ControlInterface::Create(m_deviceContext->UsbDevice, descriptor);
            RETURN_NTSTATUS_IF_TRUE_ACTION(usbAudioInterface == nullptr, status = STATUS_INSUFFICIENT_RESOURCES, status);
            break;
        case USB_AUDIO_STREAMING_SUB_CLASS:
            usbAudioInterface = USBAudio2StreamInterface::Create(m_deviceContext->UsbDevice, descriptor);
            RETURN_NTSTATUS_IF_TRUE_ACTION(usbAudioInterface == nullptr, status = STATUS_INSUFFICIENT_RESOURCES, status);
            break;
        default:
            break;
        }
    }
    else
    {
#if false
        switch (descriptor->bInterfaceSubClass)
        {
        case USB_AUDIO_CONTROL_SUB_CLASS:
            usbAudioInterface = USBAudio1ControlInterface::Create(m_deviceContext->UsbDevice, descriptor);
            RETURN_NTSTATUS_IF_TRUE_ACTION(usbAudioInterface == nullptr, status = STATUS_INSUFFICIENT_RESOURCES, status);
            break;
        case USB_AUDIO_STREAMING_SUB_CLASS:
            usbAudioInterface = USBAudio1StreamInterface::Create(m_deviceContext->UsbDevice, descriptor);
            RETURN_NTSTATUS_IF_TRUE_ACTION(usbAudioInterface == nullptr, status = STATUS_INSUFFICIENT_RESOURCES, status);
            break;
        default:
            break;
        }
#else
        // only USB Audio 2.0
        status = STATUS_NOT_SUPPORTED;
#endif
    }
    if (usbAudioInterface != nullptr)
    {
        bool isStored = false;
        for (ULONG interfaceIndex = 0; interfaceIndex < m_usbConfigurationDescriptor->bNumInterfaces; interfaceIndex++)
        {
            if (m_usbAudioInterfaceInfoes[interfaceIndex] != nullptr)
            {
                ULONG interfaceNumber = 0;
                status = m_usbAudioInterfaceInfoes[interfaceIndex]->GetInterfaceNumber(interfaceNumber);
                RETURN_NTSTATUS_IF_FAILED_MSG(status, "GetInterfaceNumber failed");
                if (interfaceNumber == descriptor->bInterfaceNumber)
                {
                    status = m_usbAudioInterfaceInfoes[interfaceIndex]->StoreInterface(usbAudioInterface);
                    RETURN_NTSTATUS_IF_FAILED_MSG(status, "StoreInterface failed");
                    isStored = true;
                    break;
                }
            }
        }

        if (!isStored)
        {
            for (ULONG interfaceIndex = 0; interfaceIndex < m_usbConfigurationDescriptor->bNumInterfaces; interfaceIndex++)
            {
                if (m_usbAudioInterfaceInfoes[interfaceIndex] == nullptr)
                {
                    m_usbAudioInterfaceInfoes[interfaceIndex] = new (POOL_FLAG_NON_PAGED, DRIVER_TAG) USBAudioInterfaceInfo(m_deviceContext->UsbDevice);
                    RETURN_NTSTATUS_IF_TRUE_ACTION(m_usbAudioInterfaceInfoes[interfaceIndex] == nullptr, status = STATUS_INSUFFICIENT_RESOURCES, status);

                    status = m_usbAudioInterfaceInfoes[interfaceIndex]->StoreInterface(usbAudioInterface);
                    RETURN_NTSTATUS_IF_FAILED_MSG(status, "StoreInterface failed");
                    m_numOfUsbAudioInterfaceInfo++;
                    isStored = true;
                    break;
                }
            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
USBAudioConfiguration::ParseInterfaceDescriptor(const PUSB_INTERFACE_DESCRIPTOR descriptor, USBAudioInterface *& lastInterface, bool & hasTargetInterface)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    if (descriptor->bLength >= NS_USBAudio::SIZE_OF_USB_INTERFACE_DESCRIPTOR)
    {
        if ((descriptor->bInterfaceClass == USB_DEVICE_CLASS_AUDIO) &&
            ((descriptor->bInterfaceSubClass == USB_AUDIO_CONTROL_SUB_CLASS) || (descriptor->bInterfaceSubClass == USB_AUDIO_STREAMING_SUB_CLASS)))
        {
            hasTargetInterface = true;
        }
    }
    if (hasTargetInterface)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, "<VID %04x>", m_usbDeviceDescriptor->idVendor);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, "<PID %04x>", m_usbDeviceDescriptor->idProduct);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, "<BCD %04x>", m_usbDeviceDescriptor->bcdDevice);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, alternate %u", (ULONG)descriptor->bInterfaceNumber, (ULONG)descriptor->bAlternateSetting);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface class %u, interface sub class %u, interface protocol %u", (ULONG)descriptor->bInterfaceClass, (ULONG)descriptor->bInterfaceSubClass, (ULONG)descriptor->bInterfaceProtocol);

        lastInterface = nullptr;
        status = CreateInterface(descriptor, lastInterface);
    }
    else
    {
        lastInterface = nullptr;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
USBAudioConfiguration::ParseEndpointDescriptor(PUSB_ENDPOINT_DESCRIPTOR descriptor, USBAudioInterface *& lastInterface)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(descriptor == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE(descriptor->bLength < NS_USBAudio::SIZE_OF_USB_ENDPOINT_DESCRIPTOR, STATUS_INVALID_PARAMETER);

    if ((lastInterface != nullptr) && (descriptor->bLength >= NS_USBAudio::SIZE_OF_USB_ENDPOINT_DESCRIPTOR))
    {
        status = lastInterface->SetEndpoint(descriptor);
        if (NT_SUCCESS(status))
        {
            status = ((USBAudioStreamInterface *)lastInterface)->RegisterUSBAudioDataFormatManager(m_inputUsbAudioDataFormatManager, m_outputUsbAudioDataFormatManager);
            if (lastInterface->IsStreamInterface())
            {
                if (((USBAudioStreamInterface *)lastInterface)->HasInputIsochronousEndpoint())
                {
                    m_isInputIsochronousInterfaceExists = true;
                }
                if (((USBAudioStreamInterface *)lastInterface)->HasOutputIsochronousEndpoint())
                {
                    m_isOutputIsochronousInterfaceExists = true;
                }
            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
USBAudioConfiguration::ParseEndpointCompanionDescriptor(PUSB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR descriptor, USBAudioInterface *& lastInterface)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(descriptor == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE(descriptor->bLength < NS_USBAudio::SIZE_OF_USB_SSENDPOINT_COMPANION_DESCRIPTOR, STATUS_INVALID_PARAMETER);

    if ((lastInterface != nullptr) && (descriptor->bLength >= NS_USBAudio::SIZE_OF_USB_ENDPOINT_DESCRIPTOR))
    {
        status = lastInterface->SetEndpointCompanion(descriptor);

        if (NT_SUCCESS(status))
        {
            m_deviceContext->SuperSpeedCompatible = true;
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
USBAudioConfiguration::ParseCSInterface(const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor, USBAudioInterface *& lastInterface)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    if ((lastInterface != nullptr) && (descriptor->bLength >= sizeof(NS_USBAudio::CS_GENERIC_AUDIO_DESCRIPTOR)))
    {
        if (lastInterface->IsStreamInterface())
        {
            switch (descriptor->bDescriptorSubtype)
            {
            case NS_USBAudio0200::FORMAT_TYPE:
                status = ((USBAudioStreamInterface *)lastInterface)->SetFormatType(descriptor);
                if (NT_SUCCESS(status))
                {
                    status = ((USBAudioStreamInterface *)lastInterface)->RegisterUSBAudioDataFormatManager(m_inputUsbAudioDataFormatManager, m_outputUsbAudioDataFormatManager);
                }
                break;
            case NS_USBAudio0200::AS_GENERAL:
                status = ((USBAudioStreamInterface *)lastInterface)->SetGeneral(descriptor);
                if (NT_SUCCESS(status))
                {
                    status = ((USBAudioStreamInterface *)lastInterface)->RegisterUSBAudioDataFormatManager(m_inputUsbAudioDataFormatManager, m_outputUsbAudioDataFormatManager);
                }
                break;
            default:
                break;
            }
        }
        else if (lastInterface->IsControlInterface())
        {
            switch (descriptor->bDescriptorSubtype)
            {
            case NS_USBAudio0200::CLOCK_SOURCE:
                status = ((USBAudioControlInterface *)lastInterface)->SetClockSource(descriptor);
                break;
            case NS_USBAudio0200::INPUT_TERMINAL:
                status = ((USBAudioControlInterface *)lastInterface)->SetInputTerminal(descriptor);
                break;
            case NS_USBAudio0200::OUTPUT_TERMINAL:
                status = ((USBAudioControlInterface *)lastInterface)->SetOutputTerminal(descriptor);
                break;
            case NS_USBAudio0200::MIXER_UNIT:
                break;
            case NS_USBAudio0200::SELECTOR_UNIT:
                break;
            case NS_USBAudio0200::FEATURE_UNIT:
                status = ((USBAudioControlInterface *)lastInterface)->SetFeatureUnit(descriptor);
                break;
            case NS_USBAudio0200::PROCESSING_UNIT:
                break;
            case NS_USBAudio0200::EXTENSION_UNIT:
                break;
            case NS_USBAudio0200::CLOCK_SELECTOR:
                status = ((USBAudioControlInterface *)lastInterface)->SetClockSelector(descriptor);
                break;
            case NS_USBAudio0200::CLOCK_MULTIPLIER:
                break;
            case NS_USBAudio0200::SAMPLE_RATE_CONVERTER:
                break;
            default:
                break;
            }
            if (NT_SUCCESS(status))
            {
                status = ((USBAudioControlInterface *)lastInterface)->SetGenericAudioDescriptor(descriptor);
            }
        }
        else
        {
            // do nothing.
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
USBAudioConfiguration::ParseCSEndpoint(NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor, USBAudioInterface *& lastInterface)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    if ((lastInterface != nullptr) && (descriptor->bLength >= sizeof(NS_USBAudio::CS_GENERIC_AUDIO_DESCRIPTOR)) && lastInterface->IsStreamInterface())
    {
        status = ((USBAudioStreamInterface *)lastInterface)->SetIsochronousAudioDataEndpoint(descriptor);
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
USBAudioConfiguration::SetCurrentSampleFrequency(
    PDEVICE_CONTEXT deviceContext,
    ULONG           desiredSampleRate
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    for (ULONG interfaceIndex = 0; interfaceIndex < m_numOfUsbAudioInterfaceInfo; interfaceIndex++)
    {
        if ((m_usbAudioInterfaceInfoes[interfaceIndex] != nullptr) && m_usbAudioInterfaceInfoes[interfaceIndex]->IsControlInterface())
        {
            RETURN_NTSTATUS_IF_FAILED(m_usbAudioInterfaceInfoes[interfaceIndex]->SetCurrentSampleFrequency(deviceContext, desiredSampleRate));
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
USBAudioConfiguration::GetCurrentSampleFrequency(
    PDEVICE_CONTEXT deviceContext,
    ULONG &         sampleRate
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    sampleRate = 0;
    for (ULONG interfaceIndex = 0; interfaceIndex < m_numOfUsbAudioInterfaceInfo; interfaceIndex++)
    {
        if ((m_usbAudioInterfaceInfoes[interfaceIndex] != nullptr) && m_usbAudioInterfaceInfoes[interfaceIndex]->IsControlInterface())
        {
            RETURN_NTSTATUS_IF_FAILED(m_usbAudioInterfaceInfoes[interfaceIndex]->GetCurrentSampleFrequency(deviceContext, sampleRate));
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioConfiguration::CanSetSampleFrequency()
{
    PAGED_CODE();

    for (ULONG interfaceIndex = 0; interfaceIndex < m_numOfUsbAudioInterfaceInfo; interfaceIndex++)
    {
        if ((m_usbAudioInterfaceInfoes[interfaceIndex] != nullptr) && m_usbAudioInterfaceInfoes[interfaceIndex]->IsControlInterface())
        {
            if (hasInputAndOutputIsochronousInterfaces() || hasInputIsochronousInterface())
            {
                return m_usbAudioInterfaceInfoes[interfaceIndex]->CanSetSampleFrequency(true);
            }
            else if (hasOutputIsochronousInterface())
            {
                return m_usbAudioInterfaceInfoes[interfaceIndex]->CanSetSampleFrequency(false);
            }
            else
            {
                return false;
            }
        }
    }

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
USBAudioConfiguration::SelectAlternateInterface(
    PDEVICE_CONTEXT deviceContext,
    bool            isInput,
    ULONG           desiredFormatType,
    ULONG           desiredFormat,
    ULONG           desiredBytesPerSample,
    ULONG           desiredValidBitsPerSample
)
{
    NTSTATUS         status = STATUS_SUCCESS;
    CURRENT_SETTINGS currentSettings{};

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - %!bool! format type %u, format %u, bytes per sample %u, valid bits per sample %u", isInput, desiredFormatType, desiredFormat, desiredBytesPerSample, desiredValidBitsPerSample);

    for (ULONG interfaceIndex = 0; interfaceIndex < m_numOfUsbAudioInterfaceInfo; interfaceIndex++)
    {
        if ((m_usbAudioInterfaceInfoes[interfaceIndex] != nullptr) && m_usbAudioInterfaceInfoes[interfaceIndex]->IsStreamInterface())
        {
            status = m_usbAudioInterfaceInfoes[interfaceIndex]->SelectAlternateInterface(deviceContext, isInput, desiredFormatType, desiredFormat, desiredBytesPerSample, desiredValidBitsPerSample, currentSettings);
        }
    }

    //
    // Even if iChannelNames is set, if the string descriptor is an internal device, iChannelNames is invalid.
    //
    if (currentSettings.ChannelNames != USBAudioConfiguration::InvalidString)
    {
        WDFMEMORY channelNameMemory = nullptr;
        USHORT *  channelName = nullptr;
        if (!NT_SUCCESS(GetStringDescriptor(deviceContext->UsbDevice, 0, LANGID_EN_US, channelNameMemory, channelName)))
        {
            currentSettings.ChannelNames = USBAudioConfiguration::InvalidString;
        }
        else
        {
            WdfObjectDelete(channelNameMemory);
            channelNameMemory = nullptr;
            channelName = nullptr;
        }
    }

    // Set UAC_AUDIO_PROPERTY based on the collected current settings.
    if (isInput)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - current bytes per sample %u, %u", currentSettings.BytesPerSample, m_deviceContext->AudioProperty.InputBytesPerSample);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - current valid bits per sample %u, %u", currentSettings.ValidBitsPerSample, m_deviceContext->AudioProperty.InputValidBitsPerSample);
        deviceContext->InputIsoPacketSize = currentSettings.MaxPacketSize;
        deviceContext->InputLockDelay = currentSettings.LockDelay;
        deviceContext->AudioProperty.InputInterfaceNumber = currentSettings.InterfaceNumber;
        deviceContext->AudioProperty.InputAlternateSetting = currentSettings.AlternateSetting;
        deviceContext->AudioProperty.InputEndpointNumber = currentSettings.EndpointAddress;
        deviceContext->AudioProperty.InputBytesPerBlock = currentSettings.Channels * currentSettings.BytesPerSample;
        deviceContext->AudioProperty.InputMaxSamplesPerPacket = currentSettings.MaxFramesPerPacket;
        deviceContext->AudioProperty.InputFormatType = desiredFormatType;
        deviceContext->AudioProperty.InputFormat = desiredFormat;
        deviceContext->AudioProperty.InputBytesPerSample = currentSettings.BytesPerSample;
        deviceContext->AudioProperty.InputValidBitsPerSample = currentSettings.ValidBitsPerSample;
        deviceContext->InputUsbChannels = currentSettings.Channels;
        deviceContext->InputChannelNames = currentSettings.ChannelNames;
    }
    else
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - current bytes per sample %u, %u", currentSettings.BytesPerSample, m_deviceContext->AudioProperty.OutputBytesPerSample);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - current valid bits per sample %u, %u", currentSettings.ValidBitsPerSample, m_deviceContext->AudioProperty.OutputValidBitsPerSample);
        deviceContext->OutputIsoPacketSize = currentSettings.MaxPacketSize;
        deviceContext->OutputLockDelay = currentSettings.LockDelay;
        deviceContext->AudioProperty.OutputInterfaceNumber = currentSettings.InterfaceNumber;
        deviceContext->AudioProperty.OutputAlternateSetting = currentSettings.AlternateSetting;
        deviceContext->AudioProperty.OutputEndpointNumber = currentSettings.EndpointAddress;
        deviceContext->AudioProperty.OutputBytesPerBlock = currentSettings.Channels * currentSettings.BytesPerSample;
        deviceContext->AudioProperty.OutputMaxSamplesPerPacket = currentSettings.MaxFramesPerPacket;
        deviceContext->AudioProperty.OutputFormatType = desiredFormatType;
        deviceContext->AudioProperty.OutputFormat = desiredFormat;
        deviceContext->AudioProperty.OutputBytesPerSample = currentSettings.BytesPerSample;
        deviceContext->AudioProperty.OutputValidBitsPerSample = currentSettings.ValidBitsPerSample;
        deviceContext->IsDeviceAdaptive = currentSettings.IsDeviceAdaptive;
        deviceContext->IsDeviceSynchronous = currentSettings.IsDeviceSynchronous;
        deviceContext->OutputUsbChannels = currentSettings.Channels;
        deviceContext->OutputChannelNames = currentSettings.ChannelNames;
    }
    if (currentSettings.FeedbackInterfaceNumber != 0)
    {
        deviceContext->FeedbackProperty.FeedbackInterfaceNumber = currentSettings.FeedbackInterfaceNumber;
        deviceContext->FeedbackProperty.FeedbackAlternateSetting = currentSettings.FeedbackAlternateSetting;
        deviceContext->FeedbackProperty.FeedbackEndpointNumber = currentSettings.FeedbackEndpointAddress;
        deviceContext->FeedbackProperty.FeedbackInterval = currentSettings.FeedbackInterval;
    }

    if (m_deviceContext->DeviceClass == 0)
    {
        m_deviceContext->DeviceClass = currentSettings.InterfaceClass;
    }

    if (m_deviceContext->DeviceProtocol == 0)
    {
        m_deviceContext->DeviceProtocol = currentSettings.InterfaceProtocol;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
/*++

Routine Description:

    Parses USB CONFIGURATION DESCRIPTOR and holds the descriptors required for
    creating an ACX Device and streaming USB Audio.

Arguments:

    usbConfigurationDescriptor - USB CONFIGURATION DESCRIPTOR of the target device

Return Value:

    NTSTATUS - NT status value

--*/
USBAudioConfiguration::ParseDescriptors(PUSB_CONFIGURATION_DESCRIPTOR usbConfigurationDescriptor)
{
    NTSTATUS              status = STATUS_SUCCESS;
    ULONG                 current = 0;
    ULONG                 totalLength = usbConfigurationDescriptor->wTotalLength;
    PBYTE                 byteArray = (PBYTE)usbConfigurationDescriptor;
    bool                  hasTargetInterface = false;
    bool                  hasAnyTargetInterface = false;
    USBAudioInterface *   lastInterface = nullptr;
    WDF_OBJECT_ATTRIBUTES attributes;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    m_usbConfigurationDescriptor = usbConfigurationDescriptor;

    RETURN_NTSTATUS_IF_TRUE(m_usbConfigurationDescriptor == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE(m_usbConfigurationDescriptor->bNumInterfaces == 0, STATUS_UNSUCCESSFUL);
    RETURN_NTSTATUS_IF_TRUE(m_usbAudioInterfaceInfoes != nullptr, STATUS_UNSUCCESSFUL);

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bLength             = %u", m_usbConfigurationDescriptor->bLength);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bDescriptorType     = %u", m_usbConfigurationDescriptor->bDescriptorType);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - wTotalLength        = %u", m_usbConfigurationDescriptor->wTotalLength);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bNumInterfaces      = %u", m_usbConfigurationDescriptor->bNumInterfaces);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bConfigurationValue = %u", m_usbConfigurationDescriptor->bConfigurationValue);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - iConfiguration      = %u", m_usbConfigurationDescriptor->iConfiguration);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bmAttributes        = %u", m_usbConfigurationDescriptor->bmAttributes);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - MaxPower            = %u", m_usbConfigurationDescriptor->MaxPower);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = m_deviceContext->UsbDevice;

    RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(USBAudioInterfaceInfo *) * m_usbConfigurationDescriptor->bNumInterfaces, &m_usbAudioInterfaceInfoesMemory, (PVOID *)&m_usbAudioInterfaceInfoes));

    RtlZeroMemory(m_usbAudioInterfaceInfoes, sizeof(USBAudioInterfaceInfo *) * m_usbConfigurationDescriptor->bNumInterfaces);

    m_deviceContext->DesiredSampleFormat = UACSampleFormat::UAC_SAMPLE_FORMAT_PCM;
    m_deviceContext->AudioProperty.CurrentSampleFormat = m_deviceContext->DesiredSampleFormat;

    m_deviceContext->AudioProperty.SupportedSampleRate = 0;
    m_deviceContext->AudioProperty.VendorId = m_usbDeviceDescriptor->idVendor;
    m_deviceContext->AudioProperty.ProductId = m_usbDeviceDescriptor->idProduct;
    m_deviceContext->AudioProperty.DeviceRelease = m_usbDeviceDescriptor->bcdDevice;
    m_deviceContext->AudioProperty.PacketsPerSec = m_deviceContext->FramesPerMs * 1000;

    if (m_deviceContext->DeviceName == nullptr)
    {
        if (m_usbDeviceDescriptor->iProduct != 0)
        {
            status = GetStringDescriptor(m_deviceContext->UsbDevice, m_usbDeviceDescriptor->iProduct, LANGID_EN_US, m_deviceContext->DeviceNameMemory, m_deviceContext->DeviceName);
            if (!NT_SUCCESS(status))
            {
                status = GetDefaultProductName(m_deviceContext->UsbDevice, m_deviceContext->DeviceNameMemory, m_deviceContext->DeviceName);
            }
        }
        else
        {
            status = GetDefaultProductName(m_deviceContext->UsbDevice, m_deviceContext->DeviceNameMemory, m_deviceContext->DeviceName);
        }
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DESCRIPTOR, "Get DeviceName  failed %!STATUS!", status);
            return status;
        }
    }
    if (m_deviceContext->DeviceName != nullptr)
    {
        RtlStringCchCopyW(m_deviceContext->AudioProperty.ProductName, UAC_MAX_PRODUCT_NAME_LENGTH, m_deviceContext->DeviceName);
    }

    if (m_deviceContext->SerialNumber == nullptr)
    {
        if (m_usbDeviceDescriptor->iSerialNumber != 0)
        {
            status = GetStringDescriptor(m_deviceContext->UsbDevice, m_usbDeviceDescriptor->iSerialNumber, LANGID_EN_US, m_deviceContext->SerialNumberMemory, m_deviceContext->SerialNumber);
            if (!NT_SUCCESS(status))
            {
                m_deviceContext->SerialNumber = nullptr;
                status = STATUS_SUCCESS;
            }
        }
    }

    while ((current < totalLength) && NT_SUCCESS(status))
    {
        if ((totalLength - current) >= NS_USBAudio::SIZE_OF_USB_DESCRIPTOR_HEADER)
        {
            PUSB_COMMON_DESCRIPTOR commonDescriptor = (PUSB_COMMON_DESCRIPTOR) & (byteArray[current]);
            if ((totalLength - current) >= commonDescriptor->bLength)
            {
                switch (commonDescriptor->bDescriptorType)
                {
                case USB_INTERFACE_DESCRIPTOR_TYPE:
                    status = ParseInterfaceDescriptor((PUSB_INTERFACE_DESCRIPTOR)commonDescriptor, lastInterface, hasTargetInterface);
                    if (NT_SUCCESS(status))
                    {
                        hasAnyTargetInterface |= hasTargetInterface;
                    }
                    break;
                case USB_ENDPOINT_DESCRIPTOR_TYPE:
                    status = ParseEndpointDescriptor((PUSB_ENDPOINT_DESCRIPTOR)commonDescriptor, lastInterface);
                    break;
                case EUSB2_ISOCH_ENDPOINT_COMPANION_DESCRIPTOR_TYPE:
                    status = ParseEndpointCompanionDescriptor((PUSB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR)commonDescriptor, lastInterface);
                    break;
                case NS_USBAudio0200::CS_INTERFACE:
                    status = ParseCSInterface((NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR)commonDescriptor, lastInterface);
                    break;
                case NS_USBAudio0200::CS_ENDPOINT:
                    status = ParseCSEndpoint((NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR)commonDescriptor, lastInterface);
                    break;
                case NS_USBAudio0200::CS_STRING:
                    // do nothing.
                    break;
                default:
                    // do nothing.
                    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, " bDescriptionType %u, 0x%x, %u", current, commonDescriptor->bDescriptorType, commonDescriptor->bLength);
                    break;
                }
            }
            else
            {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DESCRIPTOR, "USB Descriptor Header is invalid");
            }
            current += commonDescriptor->bLength;
        }
    }

    //
    if (!hasAnyTargetInterface)
    {
        // No target interface found.
        status = STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if (NT_SUCCESS(status))
    {
        if (hasInputAndOutputIsochronousInterfaces())
        {
            m_deviceContext->AudioProperty.SupportedSampleFormats = GetUSBAudioDataFormatManager(true)->GetSupportedSampleFormats() & GetUSBAudioDataFormatManager(false)->GetSupportedSampleFormats();
        }
        else if (hasInputIsochronousInterface())
        {
            m_deviceContext->AudioProperty.SupportedSampleFormats = GetUSBAudioDataFormatManager(true)->GetSupportedSampleFormats();
        }
        else
        {
            m_deviceContext->AudioProperty.SupportedSampleFormats = GetUSBAudioDataFormatManager(false)->GetSupportedSampleFormats();
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudioConfiguration::QueryDeviceFeatures()
/*++

Routine Description:

    Queries all control settings for the current device.

Arguments:

Return Value:

    NTSTATUS - NT status value

--*/
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(m_usbAudioInterfaceInfoes == nullptr, STATUS_UNSUCCESSFUL);

    for (ULONG index = 0; index < m_numOfUsbAudioInterfaceInfo; index++)
    {
        if (m_usbAudioInterfaceInfoes[index] != nullptr)
        {
            if (m_usbAudioInterfaceInfoes[index]->IsControlInterface())
            {
                status = m_usbAudioInterfaceInfoes[index]->QueryRangeAttributeAll(m_deviceContext);
                if (!NT_SUCCESS(status))
                {
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " QueryRangeAttributeAll failed %!STATUS!", status);
                }
            }
        }
    }

    //
    // https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/usb-2-0-audio-drivers
    // The USB Audio 2.0 driver doesn't support clock selection. The driver
    // uses the Clock Source Entity, which is selected by default and never
    // issues a Clock Selector Control SET CUR request.
    //
    for (ULONG index = 0; index < m_numOfUsbAudioInterfaceInfo; index++)
    {
        if (m_usbAudioInterfaceInfoes[index] != nullptr)
        {
            status = m_usbAudioInterfaceInfoes[index]->QueryCurrentAttributeAll(m_deviceContext);
            if (!NT_SUCCESS(status))
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " QueryCurrentAttributeAll failed %!STATUS!", status);
            }
        }
    }

    // for test.
    // CheckInterfaceConfiguration();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudioConfiguration::CheckInterfaceConfiguration()
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG    sampleRate = 0;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(m_usbAudioInterfaceInfoes == nullptr, STATUS_UNSUCCESSFUL);

    RETURN_NTSTATUS_IF_FAILED(GetCurrentSampleFrequency(m_deviceContext, sampleRate));

    for (ULONG sampleRateListIndex = 0; sampleRateListIndex < c_SampleRateCount; ++sampleRateListIndex)
    {
        if (m_deviceContext->AudioProperty.SupportedSampleRate & (1 << sampleRateListIndex))
        {
            ULONG updatedSampleRate = 0;
            SetCurrentSampleFrequency(m_deviceContext, c_SampleRateList[sampleRateListIndex]);
            GetCurrentSampleFrequency(m_deviceContext, updatedSampleRate);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - desired sample rate %u, updated sample rate %u", c_SampleRateList[sampleRateListIndex], updatedSampleRate);

            // {
            //     LARGE_INTEGER waitTime;
            //     waitTime.QuadPart = -3 * 10 * 1000 * 1000; // 3 sec
            //     KeDelayExecutionThread(KernelMode, FALSE, &waitTime);
            // }

            for (ULONG interfaceIndex = 0; interfaceIndex < m_numOfUsbAudioInterfaceInfo; interfaceIndex++)
            {
                if ((m_usbAudioInterfaceInfoes[interfaceIndex] != nullptr) && m_usbAudioInterfaceInfoes[interfaceIndex]->IsStreamInterface())
                {
                    m_usbAudioInterfaceInfoes[interfaceIndex]->CheckInterfaceConfiguration(m_deviceContext);
                }
            }
        }
    }

    status = SetCurrentSampleFrequency(m_deviceContext, sampleRate);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudioConfiguration::ActivateAudioInterface(
    ULONG desiredSampleRate,
    ULONG desiredFormatType,
    ULONG desiredFormat,
    ULONG inputDesiredBytesPerSample,
    ULONG inputDesiredValidBitsPerSample,
    ULONG outputDesiredBytesPerSample,
    ULONG outputDesiredValidBitsPerSample,
    bool  forceSetSampleRate
)
/*++

Routine Description:

    The interface is made active according to the specified Sample Rate.

Arguments:

    desiredSampleRate -

    desiredbytesPerSample -

Return Value:

    NTSTATUS - NT status value

--*/
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG    sampleRate = 0;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - desired sample rate %u, format type %u, format %u, in bytes per sample %u, in valid bits per sample %u, out bytes per sample %u, out valid bits per sample %u", desiredSampleRate, desiredFormatType, desiredFormat, inputDesiredBytesPerSample, inputDesiredValidBitsPerSample, outputDesiredBytesPerSample, outputDesiredValidBitsPerSample);

    RETURN_NTSTATUS_IF_TRUE(m_deviceContext->AudioProperty.PacketsPerSec == 0, STATUS_UNSUCCESSFUL);

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - input    SelectedAlternateSettings %u, NumberConfiguredPipes %u", m_deviceContext->InputInterfaceAndPipe.SelectedAlternateSetting, m_deviceContext->InputInterfaceAndPipe.NumberConfiguredPipes);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - output   SelectedAlternateSettings %u, NumberConfiguredPipes %u", m_deviceContext->OutputInterfaceAndPipe.SelectedAlternateSetting, m_deviceContext->OutputInterfaceAndPipe.NumberConfiguredPipes);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - feedback SelectedAlternateSettings %u, NumberConfiguredPipes %u", m_deviceContext->FeedbackInterfaceAndPipe.SelectedAlternateSetting, m_deviceContext->FeedbackInterfaceAndPipe.NumberConfiguredPipes);

    status = STATUS_INVALID_PARAMETER;
    for (ULONG frameRateListIndex = 0, sampleRateMask = 1; frameRateListIndex < c_SampleRateCount; ++frameRateListIndex, sampleRateMask <<= 1)
    {
        if ((m_deviceContext->AudioProperty.SupportedSampleRate & sampleRateMask) && (desiredSampleRate == c_SampleRateList[frameRateListIndex]))
        {
            status = STATUS_SUCCESS;
            break;
        }
    }
    RETURN_NTSTATUS_IF_FAILED(status);

    // Set the desiredSampleRate for the device.
    RETURN_NTSTATUS_IF_FAILED(GetCurrentSampleFrequency(m_deviceContext, sampleRate));

    if (((sampleRate != desiredSampleRate) || (forceSetSampleRate)) && CanSetSampleFrequency())
    {
        // Ignore the return value since some devices may fail to set the sample rate.
        status = SetCurrentSampleFrequency(m_deviceContext, desiredSampleRate);
        if (NT_SUCCESS(status))
        {
            sampleRate = desiredSampleRate;
        }
#if false
		// verify
		ULONG updatedSampleRate = 0;
		GetCurrentSampleFrequency(m_deviceContext, updatedSampleRate);
		TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - current sample rate %u, desired sample rate %u, updated sample rate %u", sampleRate, desiredSampleRate, updatedSampleRate);
#endif
    }

    // Determines the input interface and alternate settings.
    RETURN_NTSTATUS_IF_FAILED(SelectAlternateInterface(m_deviceContext, true, desiredFormatType, desiredFormat, inputDesiredBytesPerSample, inputDesiredValidBitsPerSample));

    // Determines the output interface and alternate settings.
    RETURN_NTSTATUS_IF_FAILED(SelectAlternateInterface(m_deviceContext, false, desiredFormatType, desiredFormat, outputDesiredBytesPerSample, outputDesiredValidBitsPerSample));

    m_deviceContext->ClassicFramesPerIrp = (m_deviceContext->AudioProperty.PacketsPerSec == 1000 ? m_deviceContext->Params.ClassicFramesPerIrp : m_deviceContext->Params.ClassicFramesPerIrp2);
    if (m_deviceContext->ClassicFramesPerIrp == 0)
    {
        m_deviceContext->ClassicFramesPerIrp = 1;
    }
    m_deviceContext->AudioProperty.SampleRate = sampleRate;
    m_deviceContext->AudioProperty.SamplesPerPacket = m_deviceContext->AudioProperty.SampleRate / m_deviceContext->AudioProperty.PacketsPerSec;
    m_deviceContext->DesiredSampleFormat = USBAudioDataFormat::ConvertFormatToSampleFormat(desiredFormatType, desiredFormat);
    m_deviceContext->AudioProperty.CurrentSampleFormat = m_deviceContext->DesiredSampleFormat;
    m_deviceContext->AudioProperty.SampleType = USBAudioDataFormat::ConverSampleFormatToSampleType(m_deviceContext->AudioProperty.CurrentSampleFormat, max(m_deviceContext->AudioProperty.InputBytesPerSample, m_deviceContext->AudioProperty.OutputBytesPerSample), max(m_deviceContext->AudioProperty.InputValidBitsPerSample, m_deviceContext->AudioProperty.OutputValidBitsPerSample));

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
USBAudioConfiguration::GetChannelName(
    bool        isInput,
    ULONG       channel,
    WDFMEMORY & memory,
    PWSTR &     channelName
)
/*++

Routine Description:

    Get the channel name


Arguments:

    isInput -

    channel -

    memory -

    channelName -

Return Value:

    NTSTATUS - NT status value

--*/
{
    NTSTATUS status = STATUS_NOT_SUPPORTED;

    PAGED_CODE();

    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry, %!bool!, %u", isInput, channel);

    if (isInput)
    {
        if (m_deviceContext->InputChannelNames != USBAudioConfiguration::InvalidString)
        {
            status = GetStringDescriptor(m_deviceContext->UsbDevice, (UCHAR)(m_deviceContext->InputChannelNames + channel), LANGID_EN_US, memory, channelName);
        }
    }
    else
    {
        if (m_deviceContext->OutputChannelNames != USBAudioConfiguration::InvalidString)
        {
            status = GetStringDescriptor(m_deviceContext->UsbDevice, (UCHAR)(m_deviceContext->OutputChannelNames + channel), LANGID_EN_US, memory, channelName);
        }
    }
    // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - %!bool!, %u, %u, %u", isInput, channel, m_deviceContext->InputUsbChannels, m_deviceContext->OutputUsbChannels);

    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!,", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
USBAudioConfiguration::GetStereoChannelName(
    bool        isInput,
    ULONG       channel,
    WDFMEMORY & memory,
    PWSTR &     channelName
)
/*++

Routine Description:

    Get the stereo channel name


Arguments:

    isInput -

    channel -

    memory -

    channelName -

Return Value:

    NTSTATUS - NT status value

--*/
{
    WDFMEMORY             leftMemory = nullptr;
    WDFMEMORY             rightMemory = nullptr;
    PWSTR                 leftChannelName = nullptr;
    PWSTR                 rightChannelName = nullptr;
    WDF_OBJECT_ATTRIBUTES attributes;

    PAGED_CODE();

    memory = nullptr;
    channelName = nullptr;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    auto getStereoChannelNameScope = wil::scope_exit([&]() {
        if (leftMemory != nullptr)
        {
            WdfObjectDelete(leftMemory);
            leftMemory = nullptr;
        }

        if (rightMemory != nullptr)
        {
            WdfObjectDelete(rightMemory);
            rightMemory = nullptr;
        }
        leftChannelName = nullptr;
        rightChannelName = nullptr;
    });

    RETURN_NTSTATUS_IF_FAILED(GetChannelName(isInput, channel, leftMemory, leftChannelName));
    RETURN_NTSTATUS_IF_FAILED(GetChannelName(isInput, channel + 1, rightMemory, rightChannelName));

    size_t bufferSize = 0;
    size_t nameLength = 0;

    WdfMemoryGetBuffer(leftMemory, &bufferSize);
    RETURN_NTSTATUS_IF_FAILED(RtlStringCbLengthW(leftChannelName, bufferSize, &nameLength));
    ULONG leftLength = (ULONG)(nameLength / sizeof(WCHAR));

    WdfMemoryGetBuffer(rightMemory, &bufferSize);
    RETURN_NTSTATUS_IF_FAILED(RtlStringCbLengthW(rightChannelName, bufferSize, &nameLength));
    ULONG rightLength = (ULONG)(nameLength / sizeof(WCHAR));

    ULONG length = leftLength + rightLength + 1 /* "/" */ + 1;
    ULONG index = 0;
    ULONG indexLast;

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = m_deviceContext->UsbDevice;
    RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, length * sizeof(WCHAR), &memory, (PVOID *)&channelName));

    for (index = 0; (index < leftLength) && (index < rightLength); index++)
    {
        if (leftChannelName[index] != rightChannelName[index])
        {
            break;
        }
    }

    RtlStringCchCopyNW(channelName, length, leftChannelName, index);
    indexLast = index;

    if ((index != leftLength) || (index != rightLength))
    {
        if (leftLength > index)
        {
            RtlStringCchCatNW(channelName, length, &(leftChannelName[index]), leftLength - index);
        }
        RtlStringCchCatNW(channelName, length, L"/", 1);
        if (rightLength > indexLast)
        {
            RtlStringCchCatNW(channelName, length, &(rightChannelName[indexLast]), rightLength - indexLast);
        }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - %!bool! channel %d, %ws, %ws, %ws", isInput, channel, leftChannelName, rightChannelName, channelName);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG
USBAudioConfiguration::GetMaxPacketSize(
    IsoDirection direction
)
{
    ULONG maxPacketSize = 0;
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    for (ULONG interfaceIndex = 0; interfaceIndex < m_numOfUsbAudioInterfaceInfo; interfaceIndex++)
    {
        if ((m_usbAudioInterfaceInfoes[interfaceIndex] != nullptr) && m_usbAudioInterfaceInfoes[interfaceIndex]->IsStreamInterface())
        {
            ULONG currentMaxPacketSize = 0;
            if (m_usbAudioInterfaceInfoes[interfaceIndex]->GetMaxPacketSize(direction, currentMaxPacketSize))
            {
                if (currentMaxPacketSize > maxPacketSize)
                {
                    maxPacketSize = currentMaxPacketSize;
                }
            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");

    return maxPacketSize;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
USBAudioConfiguration::GetMaxSupportedValidBitsPerSample(
    bool    isInput,
    ULONG   desiredFormatType,
    ULONG   desiredFormat,
    ULONG & maxSupportedBytesPerSample,
    ULONG & maxSupportedValidBitsPerSample
)
{
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    ULONG    currentMaxSupportedBytesPerSample = 0;
    ULONG    currentMaxSupportedValidBitsPerSample = 0;

    PAGED_CODE();

    maxSupportedBytesPerSample = 0;
    maxSupportedValidBitsPerSample = 0;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry, %!bool!", isInput);

    for (ULONG interfaceIndex = 0; interfaceIndex < m_numOfUsbAudioInterfaceInfo; interfaceIndex++)
    {
        if ((m_usbAudioInterfaceInfoes[interfaceIndex] != nullptr) && m_usbAudioInterfaceInfoes[interfaceIndex]->IsStreamInterface())
        {
            if (NT_SUCCESS(m_usbAudioInterfaceInfoes[interfaceIndex]->GetMaxSupportedValidBitsPerSample(isInput, desiredFormatType, desiredFormat, currentMaxSupportedBytesPerSample, currentMaxSupportedValidBitsPerSample)))
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - desiredFormatType %u, desiredFormat %u, currentMaxSupportedValidBitsPerSample %u, maxSupportedValidBitsPerSample %u", desiredFormatType, desiredFormat, currentMaxSupportedValidBitsPerSample, maxSupportedValidBitsPerSample);
                if (currentMaxSupportedValidBitsPerSample > maxSupportedValidBitsPerSample)
                {
                    maxSupportedValidBitsPerSample = currentMaxSupportedValidBitsPerSample;
                    maxSupportedBytesPerSample = currentMaxSupportedBytesPerSample;
                }
            }
        }
    }

    if (maxSupportedValidBitsPerSample != 0)
    {
        status = STATUS_SUCCESS;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!, %u, %u", status, maxSupportedBytesPerSample, maxSupportedValidBitsPerSample);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
USBAudioConfiguration::GetNearestSupportedValidBitsPerSamples(
    bool    isInput,
    ULONG   desiredFormatType,
    ULONG   desiredFormat,
    ULONG & nearestSupportedBytesPerSample,
    ULONG & nearestSupportedValidBitsPerSample
)
{
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    ULONG    currentNearestSupportedBytesPerSample = 0;
    ULONG    currentNearestSupportedValidBitsPerSample = 0;
    ULONG    validBitsPerSampleDiff = ~(ULONG)0;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry, %!bool!", isInput);

    for (ULONG interfaceIndex = 0; interfaceIndex < m_numOfUsbAudioInterfaceInfo; interfaceIndex++)
    {
        if ((m_usbAudioInterfaceInfoes[interfaceIndex] != nullptr) && m_usbAudioInterfaceInfoes[interfaceIndex]->IsStreamInterface())
        {
            ULONG bytesPerSample = nearestSupportedBytesPerSample;
            ULONG validBitsPerSample = nearestSupportedValidBitsPerSample;

            if (NT_SUCCESS(m_usbAudioInterfaceInfoes[interfaceIndex]->GetNearestSupportedValidBitsPerSamples(isInput, desiredFormatType, desiredFormat, bytesPerSample, validBitsPerSample)))
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - desiredFormatType %u, desiredFormat %u, validBitsPerSample %u, nearestSupportedValidBitsPerSample %u", desiredFormatType, desiredFormat, validBitsPerSample, nearestSupportedValidBitsPerSample);

                if (validBitsPerSample == nearestSupportedValidBitsPerSample)
                {
                    currentNearestSupportedBytesPerSample = nearestSupportedBytesPerSample;
                    currentNearestSupportedValidBitsPerSample = nearestSupportedValidBitsPerSample;
                    break;
                }
                else if (validBitsPerSample > nearestSupportedValidBitsPerSample)
                {
                    if (validBitsPerSampleDiff > (validBitsPerSample - nearestSupportedValidBitsPerSample))
                    {
                        validBitsPerSampleDiff = validBitsPerSample - nearestSupportedValidBitsPerSample;
                        currentNearestSupportedBytesPerSample = bytesPerSample;
                        currentNearestSupportedValidBitsPerSample = validBitsPerSample;
                    }
                    else if (validBitsPerSampleDiff == (validBitsPerSample - nearestSupportedValidBitsPerSample))
                    {
                        if (currentNearestSupportedValidBitsPerSample < validBitsPerSample)
                        {
                            currentNearestSupportedBytesPerSample = bytesPerSample;
                            currentNearestSupportedValidBitsPerSample = validBitsPerSample;
                        }
                    }
                }
                else
                {
                    if (validBitsPerSampleDiff > (nearestSupportedValidBitsPerSample - validBitsPerSample))
                    {
                        validBitsPerSampleDiff = nearestSupportedValidBitsPerSample - validBitsPerSample;
                        currentNearestSupportedBytesPerSample = bytesPerSample;
                        currentNearestSupportedValidBitsPerSample = validBitsPerSample;
                    }
                    else if (validBitsPerSampleDiff == (nearestSupportedValidBitsPerSample - validBitsPerSample))
                    {
                        if (currentNearestSupportedValidBitsPerSample < validBitsPerSample)
                        {
                            currentNearestSupportedBytesPerSample = bytesPerSample;
                            currentNearestSupportedValidBitsPerSample = validBitsPerSample;
                        }
                    }
                }
            }
        }
    }

    if (currentNearestSupportedValidBitsPerSample != 0)
    {
        nearestSupportedValidBitsPerSample = currentNearestSupportedValidBitsPerSample;
        nearestSupportedBytesPerSample = currentNearestSupportedBytesPerSample;
        status = STATUS_SUCCESS;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!, %u, %u", status, nearestSupportedBytesPerSample, nearestSupportedValidBitsPerSample);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
USBAudioConfiguration::GetNearestSupportedSampleRate(
    _Inout_ ULONG & sampleRate
)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG    newSampleRate = 0;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    for (ULONG frameRateListIndex = 0, sampleRateMask = 1; frameRateListIndex < c_SampleRateCount; ++frameRateListIndex, sampleRateMask <<= 1)
    {
        if ((m_deviceContext->AudioProperty.SupportedSampleRate & sampleRateMask))
        {
            if ((c_SampleRateList[frameRateListIndex] >= sampleRate) || (newSampleRate == 0))
            {
                newSampleRate = c_SampleRateList[frameRateListIndex];
            }
        }
    }
    sampleRate = newSampleRate;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!, %u", status, sampleRate);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioDataFormatManager *
USBAudioConfiguration::GetUSBAudioDataFormatManager(
    bool isInput
)
{
    PAGED_CODE();

    return (isInput) ? &m_inputUsbAudioDataFormatManager : &m_outputUsbAudioDataFormatManager;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioConfiguration::isInterfaceProtocolUSBAudio2(
    UCHAR interfaceProtocol
) const
{
    bool isUSBAudio2 = false;

    PAGED_CODE();

    if (interfaceProtocol == NS_USBAudio0200::IP_VERSION_02_00)
    {
        isUSBAudio2 = true;
    }
    return isUSBAudio2;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioConfiguration::isUSBAudio2() const
{
    PAGED_CODE();

    return m_isUSBAudio2;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
bool USBAudioConfiguration::hasInputIsochronousInterface() const
{
    return m_isInputIsochronousInterfaceExists;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
bool USBAudioConfiguration::hasOutputIsochronousInterface() const
{
    return m_isOutputIsochronousInterfaceExists;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
bool USBAudioConfiguration::hasInputAndOutputIsochronousInterfaces() const
{
    return hasInputIsochronousInterface() && hasOutputIsochronousInterface();
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioConfiguration::GetDescriptor(
    WDFUSBDEVICE usbDevice,
    UCHAR        urbDescriptorType,
    UCHAR        index,
    USHORT       languageId,
    WDFMEMORY &  memory,
    PVOID &      descriptor
)
{
    NTSTATUS              status = STATUS_SUCCESS;
    ULONG                 length = 0;
    ULONG                 retry = 1;
    WDF_OBJECT_ATTRIBUTES attributes;
    PURB                  urb = nullptr;
    WDFMEMORY             urbMemory = nullptr;

    _IRQL_limited_to_(PASSIVE_LEVEL);

    PAGED_CODE();

    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry. urbDescriptorType: %d, index: %d, languageId: %d", urbDescriptorType, index, languageId);

    memory = nullptr;
    descriptor = nullptr;

    auto getDescriptorScope = wil::scope_exit([&]() {
        if (urbMemory != nullptr)
        {
            WdfObjectDelete(urbMemory);
            urbMemory = nullptr;
        }
        if (!NT_SUCCESS(status))
        {
            if (memory != nullptr)
            {
                WdfObjectDelete(memory);
                memory = nullptr;
            }
            descriptor = nullptr;
        }
    });

    switch (urbDescriptorType)
    {
    case USB_DEVICE_DESCRIPTOR_TYPE:
        length = sizeof(USB_DEVICE_DESCRIPTOR);
        break;
    case USB_CONFIGURATION_DESCRIPTOR_TYPE:
        length = sizeof(USB_CONFIGURATION_DESCRIPTOR);
        break;
    case USB_STRING_DESCRIPTOR_TYPE:
        length = sizeof(USB_STRING_DESCRIPTOR);
        break;
    default:
        status = STATUS_INVALID_PARAMETER;
        return status;
    }

    status = WdfUsbTargetDeviceCreateUrb(
        usbDevice,
        nullptr,
        &urbMemory,
        nullptr
    );
    RETURN_NTSTATUS_IF_FAILED_MSG(status, "Could not allocate URB for an open-streams request.");

    size_t bufferSize = 0;
    urb = (PURB)WdfMemoryGetBuffer(urbMemory, &bufferSize);
    if (bufferSize < sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DESCRIPTOR, "The memory size allocated by WdfUsbTargetDeviceCreateUrb is small.");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    while (retry != 0)
    {
        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

        attributes.ParentObject = usbDevice;

        status = WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, length, &memory, (PVOID *)&descriptor);
        if (!NT_SUCCESS(status))
        {
            return status;
        }
        UsbBuildGetDescriptorRequest(
            urb,                                                    // Points to the URB to be formatted
            (USHORT)sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST), // Size of the URB.
            urbDescriptorType,                                      // Type of descriptor
            index,                                                  // Index of the configuration
            languageId,                                             // Not used for configuration descriptors
            descriptor,                                             // Points to a USB_CONFIGURATION_DESCRIPTOR structure
            nullptr,                                                // Not required because we are providing a buffer not MDL
            length,                                                 // Size of the USB_CONFIGURATION_DESCRIPTOR structure.
            nullptr                                                 // Reserved.
        );
        // Send the request synchronously.
        status = WdfUsbTargetDeviceSendUrbSynchronously(
            usbDevice,
            nullptr,
            nullptr,
            urb
        );

        if (NT_SUCCESS(status))
        {
            if (urbDescriptorType == USB_CONFIGURATION_DESCRIPTOR_TYPE)
            {
                PUSB_CONFIGURATION_DESCRIPTOR configDesc = (PUSB_CONFIGURATION_DESCRIPTOR)descriptor;
                if (configDesc->wTotalLength <= length)
                {
                    // Get all the descriptors.
                    break;
                }
                else
                {
                    // Only the configuration descriptor was obtained, so try again specifying the size of the entire descriptor.
                    length = configDesc->wTotalLength;
                }
            }
            else
            {
                PUSB_COMMON_DESCRIPTOR commonDesc = (PUSB_COMMON_DESCRIPTOR)descriptor;
                if (commonDesc->bLength <= length)
                {
                    // Success.
                    break;
                }
                else
                {
                    // Successful, but the actual length is longer than the prepared buffer, so adjust the buffer length and try again.
                    length = commonDesc->bLength;
                }
            }
        }
        else
        {
            // Failed. Retry until the specified number of retries is reached.
            --retry;
        }

        WdfObjectDelete(memory);
        memory = nullptr;
        descriptor = nullptr;
    }

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioConfiguration::GetStringDescriptor(
    WDFUSBDEVICE usbDevice,
    UCHAR        index,
    USHORT       languageId,
    WDFMEMORY &  memory,
    PWSTR &      string
)
{
    NTSTATUS               status = STATUS_SUCCESS;
    WDF_OBJECT_ATTRIBUTES  attributes;
    WDFMEMORY              descriptorMemory = nullptr;
    PUSB_STRING_DESCRIPTOR descriptor = nullptr;
    PVOID                  data = nullptr;

    _IRQL_limited_to_(PASSIVE_LEVEL);

    PAGED_CODE();

    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry. index: %d, languageId: %d", index, languageId);

    auto getStringDescriptorScope = wil::scope_exit([&]() {
        if (descriptorMemory != nullptr)
        {
            WdfObjectDelete(descriptorMemory);
            descriptorMemory = nullptr;
        }
        descriptor = nullptr;

        if (!NT_SUCCESS(status))
        {
            if (memory != nullptr)
            {
                WdfObjectDelete(memory);
                memory = nullptr;
            }
            string = nullptr;
        }
    });

    status = GetDescriptor(
        usbDevice,
        USB_STRING_DESCRIPTOR_TYPE,
        0,
        0,
        descriptorMemory,
        data
    );

    if (!NT_SUCCESS(status))
    {
        return status;
    }
    descriptor = static_cast<PUSB_STRING_DESCRIPTOR>(data);

    ULONG languages = (descriptor->bLength - 2) / sizeof(WCHAR);

    ULONG i;
    for (i = 0; i < languages; ++i)
    {
        if (descriptor->bString[i] == languageId)
        {
            break;
        }
    }
    if (i == languages)
    {
        languageId = descriptor->bString[0];
    }

    WdfObjectDelete(descriptorMemory);
    descriptorMemory = nullptr;
    data = nullptr;

    status = GetDescriptor(
        usbDevice,
        USB_STRING_DESCRIPTOR_TYPE,
        index,
        languageId,
        descriptorMemory,
        data
    );

    if (!NT_SUCCESS(status))
    {
        return status;
    }
    descriptor = static_cast<PUSB_STRING_DESCRIPTOR>(data);

    if (descriptor->bLength < 4)
    {
        status = STATUS_NO_DATA_DETECTED;
        return status;
    }

    ULONG stringLength = ((descriptor->bLength - 2) / sizeof(WCHAR)) + 1;
    // The first -2 is the descriptor header, and the last +1 is the NULL terminator.

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

    attributes.ParentObject = usbDevice;

    status = WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, stringLength * sizeof(WCHAR), &memory, (PVOID *)&string);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    RtlCopyMemory(string, descriptor->bString, (stringLength - 1) * sizeof(WCHAR));
    (string)[stringLength - 1] = L'\0';

    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioConfiguration::GetDefaultProductName(
    WDFOBJECT   parentObject,
    WDFMEMORY & memory,
    PWSTR &     string
)
{
    WDF_OBJECT_ATTRIBUTES attributes;
    NTSTATUS              status = STATUS_SUCCESS;

    _IRQL_limited_to_(PASSIVE_LEVEL);

    PAGED_CODE();
    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    DWORD  length = (DWORD)wcslen(DEFAULT_PRODUCT_NAME);
    size_t sizeOfDefaultProductName = (length + 1) * sizeof(WCHAR);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

    attributes.ParentObject = parentObject;
    status = WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeOfDefaultProductName, &memory, (PVOID *)&string);
    if (!NT_SUCCESS(status))
    {
        return status;
    }
    RtlStringCbCopyW(string, sizeOfDefaultProductName, DEFAULT_PRODUCT_NAME);

    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudioConfiguration::SearchOutputTerminalFromInputTerminal(UCHAR terminalLink, UCHAR & numOfChannels, USHORT & terminalType, UCHAR & volumeUnitID, UCHAR & muteUnitID)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    PAGED_CODE();

    for (ULONG index = 0; index < m_numOfUsbAudioInterfaceInfo; index++)
    {
        if (m_usbAudioInterfaceInfoes[index] != nullptr)
        {
            if (m_usbAudioInterfaceInfoes[index]->IsControlInterface())
            {
                status = m_usbAudioInterfaceInfoes[index]->SearchOutputTerminalFromInputTerminal(terminalLink, numOfChannels, terminalType, volumeUnitID, muteUnitID);
                return status;
            }
        }
    }

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudioConfiguration::SearchInputTerminalFromOutputTerminal(UCHAR terminalLink, UCHAR & numOfChannels, USHORT & terminalType, UCHAR & volumeUnitID, UCHAR & muteUnitID)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    PAGED_CODE();

    for (ULONG index = 0; index < m_numOfUsbAudioInterfaceInfo; index++)
    {
        if (m_usbAudioInterfaceInfoes[index] != nullptr)
        {
            if (m_usbAudioInterfaceInfoes[index]->IsControlInterface())
            {
                status = m_usbAudioInterfaceInfoes[index]->SearchInputTerminalFromOutputTerminal(terminalLink, numOfChannels, terminalType, volumeUnitID, muteUnitID);
                return status;
            }
        }
    }

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioConfiguration::GetCurrentTerminalLink(
    bool    isInput,
    UCHAR & terminalLink
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    terminalLink = USBAudioConfiguration::InvalidID;

    for (ULONG interfaceIndex = 0; interfaceIndex < m_numOfUsbAudioInterfaceInfo; interfaceIndex++)
    {
        // Get the stream interface for the target direction
        if ((m_usbAudioInterfaceInfoes[interfaceIndex] != nullptr) && m_usbAudioInterfaceInfoes[interfaceIndex]->IsSupportDirection(isInput))
        {
            ULONG interfaceNumber = 0;
            status = m_usbAudioInterfaceInfoes[interfaceIndex]->GetInterfaceNumber(interfaceNumber);

            if (isInput)
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, input interface number %u", interfaceNumber, m_deviceContext->AudioProperty.InputInterfaceNumber);
            }
            else
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, output interface number %u", interfaceNumber, m_deviceContext->AudioProperty.OutputInterfaceNumber);
            }

            if (NT_SUCCESS(status) && ((isInput && (interfaceNumber == m_deviceContext->AudioProperty.InputInterfaceNumber)) || (!isInput && (interfaceNumber == m_deviceContext->AudioProperty.OutputInterfaceNumber))))
            {
                // Gets the terminal link defined in the Class-Specific AS Interface Descriptor.
                if (m_usbAudioInterfaceInfoes[interfaceIndex]->GetTerminalLink(terminalLink))
                {
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - terminal link %u", terminalLink);
                    break;
                }
            }
        }
    }

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioConfiguration::GetStreamChannelInfo(
    bool     isInput,
    UCHAR &  numOfChannels,
    USHORT & terminalType,
    UCHAR &  volumeUnitID,
    UCHAR &  muteUnitID
)
{
    NTSTATUS status = STATUS_SUCCESS;
    UCHAR    terminalLink = USBAudioConfiguration::InvalidID;

    PAGED_CODE();

    numOfChannels = 0;
    volumeUnitID = USBAudioConfiguration::InvalidID;
    muteUnitID = USBAudioConfiguration::InvalidID;

    RETURN_NTSTATUS_IF_FAILED(GetCurrentTerminalLink(isInput, terminalLink));

    if (terminalLink != USBAudioConfiguration::InvalidID)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - terminal link %u", terminalLink);
        if (isInput)
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - terminal link %u", terminalLink);
            status = SearchInputTerminalFromOutputTerminal(terminalLink, numOfChannels, terminalType, volumeUnitID, muteUnitID);
        }
        else
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - terminal link %u", terminalLink);
            status = SearchOutputTerminalFromInputTerminal(terminalLink, numOfChannels, terminalType, volumeUnitID, muteUnitID);
        }
    }

    if ((terminalLink == USBAudioConfiguration::InvalidID) || !NT_SUCCESS(status) || (numOfChannels == 0))
    {
        //
        // The topology link is broken or the topology could not be analyzed,
        // so the number of channels in the Class-Specific AS Interface
        // Descriptor of the Stream Interface is used.
        //
        if ((isInput && hasInputIsochronousInterface()) || (!isInput && hasOutputIsochronousInterface()))
        {
            if (numOfChannels == 0)
            {
                TraceEvents(TRACE_LEVEL_WARNING, TRACE_DESCRIPTOR, "The number of channels listed in the terminal is 0. terminal link %u, %!STATUS!", terminalLink, status);
            }
            else
            {
                TraceEvents(TRACE_LEVEL_WARNING, TRACE_DESCRIPTOR, "The topology link is broken or the topology could not be analyzed. terminal link %u, %!STATUS!", terminalLink, status);
            }
        }
        status = STATUS_SUCCESS;
        if (isInput)
        {
            numOfChannels = static_cast<UCHAR>(m_deviceContext->InputUsbChannels);
            if (terminalLink == USBAudioConfiguration::InvalidID)
            {
                terminalType = NS_USBAudio0200::LINE_CONNECTOR;
            }
        }
        else
        {
            numOfChannels = static_cast<UCHAR>(m_deviceContext->OutputUsbChannels);
            if (terminalLink == USBAudioConfiguration::InvalidID)
            {
                terminalType = NS_USBAudio0200::LINE_CONNECTOR;
            }
        }
        volumeUnitID = muteUnitID = USBAudioConfiguration::InvalidID;
    }

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioConfiguration::GetStreamChannelInfoAdjusted(
    bool     isInput,
    UCHAR &  numOfChannels,
    USHORT & terminalType,
    UCHAR &  volumeUnitID,
    UCHAR &  muteUnitID
)
{
    PAGED_CODE();

    RETURN_NTSTATUS_IF_FAILED(GetStreamChannelInfo(isInput, numOfChannels, terminalType, volumeUnitID, muteUnitID));

    if (numOfChannels == 0)
    {
        numOfChannels = 1;
    }

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
_Use_decl_annotations_
bool USBAudioConfiguration::IsDeviceSplittable(
    bool isInput
)
{
    PAGED_CODE();

    //
    // If USB Audio Data Format Type III is included,
    // the device will not be split.
    //

    bool isDeviceSplittable = ((GetUSBAudioDataFormatManager(isInput)->GetSupportedSampleFormats() & USBAudioDataFormat::GetSampleFormatsTypeIII()) == 0);

    return isDeviceSplittable;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioConfiguration::GetStreamDevices(
    bool    isInput,
    ULONG & numOfDevices
)
{
    UCHAR  numOfChannels = 0;
    USHORT terminalType;
    UCHAR  volumeUnitID;
    UCHAR  muteUnitID;

    PAGED_CODE();

    RETURN_NTSTATUS_IF_FAILED(GetStreamChannelInfo(isInput, numOfChannels, terminalType, volumeUnitID, muteUnitID));

    if (!IsDeviceSplittable(isInput))
    {
        numOfDevices = 1;
    }
    else
    {
        numOfDevices = (numOfChannels / 2) + (numOfChannels % 2); // stereo or stereo + mono
    }

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioConfiguration::GetStreamDevicesAdjusted(
    bool    isInput,
    ULONG & numOfDevices
)
{
    PAGED_CODE();

    RETURN_NTSTATUS_IF_FAILED(GetStreamDevices(isInput, numOfDevices));
    if (numOfDevices == 0)
    {
        numOfDevices = 1;
    }

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioConfiguration::GetStreamChannels(
    bool    isInput,
    UCHAR & numOfChannels
)
{
    USHORT terminalType;
    UCHAR  volumeUnitID;
    UCHAR  muteUnitID;

    PAGED_CODE();

    RETURN_NTSTATUS_IF_FAILED(GetStreamChannelInfo(isInput, numOfChannels, terminalType, volumeUnitID, muteUnitID));

    return STATUS_SUCCESS;
}
