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
PAGED_CODE_SEG
_Use_decl_annotations_
const char * GetAudioNodeKindString(
    _In_ AudioNodeKind audioNodeKind
)
{
    PAGED_CODE();

    switch (audioNodeKind)
    {
    case AudioNodeKind::RenderHostPin:
        return "RenderHostPin";
        break;
    case AudioNodeKind::RenderBridgePin:
        return "RenderBridgePin";
        break;
    case AudioNodeKind::CaptureHostPin:
        return "CaptureHostPin";
        break;
    case AudioNodeKind::CaptureBridgePin:
        return "CaptureBridgePin";
        break;
    case AudioNodeKind::MuxElement:
        return "MuxElement";
        break;
    case AudioNodeKind::SuperMixElement:
        return "SuperMixElement";
        break;
    case AudioNodeKind::VolumeElement:
        return "VolumeElement";
        break;
    case AudioNodeKind::MuteElement:
        return "MuteElement";
        break;
    case AudioNodeKind::AgcElement:
        return "AgcElement";
        break;
    case AudioNodeKind::SrcElement:
        return "SrcElement";
        break;
    case AudioNodeKind::EffectElement:
        return "EffectElement";
        break;
    case AudioNodeKind::ProcessingElement:
        return "ProcessingElement";
        break;
    default:
        break;
    }
    return "Invalid";
}

PAGED_CODE_SEG
_Use_decl_annotations_
const char * GetTraversalDirectionString(
    _In_ TraversalDirection traversalDirection
)
{
    PAGED_CODE();

    if (traversalDirection == TraversalDirection::Forward)
    {
        return "Forward";
    }
    else
    {
        return "Reverse";
    }
}

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
NONPAGED_CODE_SEG
NTSTATUS
VariableArray<T, I>::Get(ULONG index, T & data) const
{
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
NONPAGED_CODE_SEG
ULONG VariableArray<T, I>::GetNumOfArray() const
{
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
NONPAGED_CODE_SEG
T * VariableArray<T, I>::begin() noexcept
{
    return &(m_array[0]);
}

template <class T, ULONG I>
_Use_decl_annotations_
NONPAGED_CODE_SEG
T * VariableArray<T, I>::end() noexcept
{
    return &(m_array[m_numOfArray]);
}

template <class T, ULONG I>
_Use_decl_annotations_
NONPAGED_CODE_SEG
const T * VariableArray<T, I>::begin() const noexcept
{
    return &(m_array[0]);
}

template <class T, ULONG I>
_Use_decl_annotations_
NONPAGED_CODE_SEG
const T * VariableArray<T, I>::end() const noexcept
{
    return &(m_array[m_numOfArray]);
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
    RETURN_NTSTATUS_IF_TRUE(m_numOfEndpoint >= GetNumEndpoints(), STATUS_DEVICE_DATA_ERROR);

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
NONPAGED_CODE_SEG
UCHAR USBAudioInterface::GetInterfaceNumber() const
{
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
bool USBAudioInterface::GetInterval(
    ULONG   index,
    UCHAR & bInterval
) const
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
    RETURN_NTSTATUS_IF_TRUE(m_numOfEndpointCompanion >= GetNumEndpoints(), STATUS_DEVICE_DATA_ERROR);

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
NONPAGED_CODE_SEG
bool USBAudioControlInterface::IsControlInterface()
{
    return true;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudioControlInterface::SetGenericAudioDescriptor(
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

_Use_decl_annotations_
PAGED_CODE_SEG
void USBAudioControlInterface::SetEntityBit(
    ULONGLONG bitmap[4],
    UCHAR     entityId
)
{
    PAGED_CODE();

    bitmap[entityId / 64] |= (0x1LL << (entityId % 64));
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioControlInterface::TestAndClearEntityBit(
    ULONGLONG bitmap[4],
    UCHAR     entityId
)
{
    PAGED_CODE();

    bool result = (bitmap[entityId / 64] & (0x1LL << (entityId % 64))) ? true : false;

    bitmap[entityId / 64] &= ~(0x1LL << (entityId % 64));

    return result;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioControlInterface::TestEntityBit(
    ULONGLONG bitmap[4],
    UCHAR     entityId
)
{
    PAGED_CODE();

    return (bitmap[entityId / 64] & (0x1LL << (entityId % 64))) ? true : false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioControlInterface::TestEntityAllBit(
    ULONGLONG bitmap[4]
)
{
    PAGED_CODE();

    return (bitmap[0] | bitmap[1] | bitmap[2] | bitmap[3]) ? true : false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
void USBAudioControlInterface::Dump()
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - control interface %u, alternate setting %u, %u endpoints", GetInterfaceNumber(), GetAlternateSetting(), GetNumEndpoints());
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
NONPAGED_CODE_SEG
bool USBAudioStreamInterface::IsControlInterface()
{
    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG USBAudioStreamInterface::GetNumOfClockSources()
{
    PAGED_CODE();

    return 0;
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG USBAudioStreamInterface::GetClockEntityCountForTerminal()
{
    PAGED_CODE();

    return 0;
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG USBAudioStreamInterface::GetLockDelay()
{
    PAGED_CODE();

    return m_lockDelay;
}

_Use_decl_annotations_
PAGED_CODE_SEG
void USBAudioStreamInterface::Dump()
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - stram interface %u, alternate setting %u, %u endpoints, has input %!bool!, has output %!bool!, has feedback %!bool!", GetInterfaceNumber(), GetAlternateSetting(), GetNumEndpoints(), HasInputIsochronousEndpoint(), HasOutputIsochronousEndpoint(), HasFeedbackEndpoint());
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " -   %u ch, %u bytes per sample, %u valid bits, %u max suported bytes per sample, %u max supported valid bits, 0x%02x feedback endpoint address, 0x%02x feedback interval", GetCurrentChannels(), GetBytesPerSample(), GetValidBitsPerSample(), GetMaxSupportedBytesPerSample(), GetMaxSupportedValidBitsPerSample(), GetFeedbackEndpointAddress(), GetFeedbackInterval());
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " -   current terminal link %u, bm controls %u, active alternate setting 0x%02x, valid alternate settingmap 0x%08x", GetCurrentTerminalLink(), GetCurrentBmControls(), GetCurrentActiveAlternateSetting(), GetCurrentValidAlternateSettingMap());
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
NTSTATUS USBAudio1ControlInterface::SetProcessingUnit(const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR /* descriptor */)
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
ULONG USBAudio1ControlInterface::GetNumOfClockSources()
{
    PAGED_CODE();

    return 0;
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG USBAudio1ControlInterface::GetClockEntityCountForTerminal()
{
    PAGED_CODE();

    return 0;
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
NTSTATUS USBAudio1ControlInterface::ReconnectClockAll(
    PDEVICE_CONTEXT /* deviceContext */
)
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
NTSTATUS USBAudio1ControlInterface::SetDefaultAttributeAll(
    PDEVICE_CONTEXT /* deviceContext */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::GetCurrentClockSourceID(
    _In_ PDEVICE_CONTEXT /* deviceContext */,
    _Inout_ UCHAR & /* clockID */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::SetCurrentClockSourceInternal(
    _In_ PDEVICE_CONTEXT /* deviceContext */,
    _In_ UCHAR /* clockSourceID */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::GetRangeSampleFrequency(
    PDEVICE_CONTEXT /* deviceContext */,
    UCHAR /* clockSourceID */,
    ULONG & /* supportedSampleRate */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudio1ControlInterface::GetInformationForHostPin(
    PDEVICE_CONTEXT /* deviceContext */,
    UCHAR /* unitID */,
    UCHAR & /* numOfChannels */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudio1ControlInterface::GetInformationForBridgePin(
    PDEVICE_CONTEXT /* deviceContext */,
    UCHAR /* unitID */,
    UCHAR & /* numOfChannels */,
    USHORT & /* terminalType */,
    UCHAR & /* channelNames */,
    NS_USBAudio::AUDIO_CHANNEL_CLUSTER_DESCRIPTOR & /* connectorState */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudio1ControlInterface::GetInformationForVolumeElement(
    PDEVICE_CONTEXT /* deviceContext */,
    UCHAR /* unitID */,
    UCHAR & /* numOfChannels */,
    LONG & /* minimum */,
    LONG & /* maximum */,
    ULONG & /* steppingDelta */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudio1ControlInterface::GetInformationForMuteElement(
    _In_ PDEVICE_CONTEXT /* deviceContext */,
    _In_ UCHAR /* unitID */,
    _Out_ UCHAR & /* numOfChannels */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudio1ControlInterface::GetInformationForSuperMixElement(
    _In_ PDEVICE_CONTEXT /* deviceContext */,
    _In_ UCHAR /* unitID */,
    _Out_ UCHAR & /* numOfInputChannels */,
    _Out_ UCHAR & /* numOfOutputChannels */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudio1ControlInterface::GetInformationForMuxElement(
    _In_ PDEVICE_CONTEXT /* deviceContext */,
    _In_ UCHAR /* unitID */,
    _Out_ UCHAR & /* numOfChannels */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudio1ControlInterface::GetInformationForAgcElement(
    _In_ PDEVICE_CONTEXT /* deviceContext */,
    _In_ UCHAR /* unitID */,
    _Out_ UCHAR & /* numOfChannels */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::SearchOutputTerminalFromInputTerminal(
    PDEVICE_CONTEXT /* deviceContext */,
    UCHAR /* terminalLink */,
    UCHAR & /* numOfChannels */,
    USHORT & /* terminalType */,
    UCHAR & /* terminalID */,
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
    PDEVICE_CONTEXT /* deviceContext */,
    UCHAR /* terminalLink */,
    UCHAR & /* numOfChannels */,
    USHORT & /* terminalType */,
    UCHAR & /* terminalID */,
    UCHAR & /* volumeUnitID */,
    UCHAR & /* muteUnitID */
)
{

    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudio1ControlInterface::WalkNextUnitTowardForward(
    ULONGLONG /* idMap */[4],
    ULONGLONG /* unvisitedUnitMap */[4],
    AudioNodeKind & /* audioNodeKind */,
    UCHAR & /* unitID */,
    ULONG & /* controlBitmap */,
    UCHAR & /* nextUnitID */,
    TraversalDirection & /* traversalDirection */,
    bool & /* hasMoreData */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudio1ControlInterface::WalkNextUnitTowardReverse(
    ULONGLONG /* idMap */[4],
    ULONGLONG /* unvisitedUnitMap */[4],
    AudioNodeKind & /* audioNodeKind */,
    UCHAR & /* unitID */,
    ULONG & /* controlBitmap */,
    UCHAR & /* nextUnitID */,
    TraversalDirection & /* traversalDirection */,
    bool & /* hasMoreData */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudio1ControlInterface::WalkNextUnvisitedUnit(
    ULONGLONG /* idMap */[4],
    ULONGLONG /* unvisitedUnitMap */[4],
    AudioNodeKind & /* audioNodeKind */,
    UCHAR & /* unitID */,
    ULONG & /* controlBitmap */,
    UCHAR & /* nextUnitID */,
    TraversalDirection & /* traversalDirection */,
    bool & /* hasMoreData */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::UpdateCurrentValue(
    const UCHAR /* entityID */,
    const UCHAR /* controlSelector */,
    const UCHAR /* controlNumber */
)
{
    return STATUS_NOT_SUPPORTED;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudio1ControlInterface::GetVolumeConfiguration(
    PDEVICE_CONTEXT /* deviceContext */,
    UCHAR /* entityID */,
    LONG & /* minimum */,
    LONG & /* maximum */,
    ULONG & /* steppingDelta */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio1ControlInterface::IsVolumeEntityUpdated()
{
    PAGED_CODE();

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio1ControlInterface::IsMuteEntityUpdated()
{
    PAGED_CODE();

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio1ControlInterface::IsInputConnectorEntityUpdated()
{
    PAGED_CODE();

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio1ControlInterface::IsOutputConnectorEntityUpdated()
{
    PAGED_CODE();

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio1ControlInterface::GetUpdatedVolumeEntity(
    UCHAR & /* entityID */
)
{
    PAGED_CODE();

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio1ControlInterface::GetUpdatedMuteEntity(
    UCHAR & /* entityID */
)
{
    PAGED_CODE();

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio1ControlInterface::GetUpdatedInputConnectorEntity(
    UCHAR & /* entityID */
)
{
    PAGED_CODE();

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio1ControlInterface::GetUpdatedOutputConnectorEntity(
    UCHAR & /* entityID */
)
{
    PAGED_CODE();

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::ValidateVolumeControl(
    _In_ UCHAR /* entityID */,
    _In_ UCHAR /* channel */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::SetCurrentVolume(
    PDEVICE_CONTEXT /* deviceContext */,
    UCHAR /* entityID */,
    UCHAR /* channel */,
    LONG /*volume */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::GetCurrentVolume(
    PDEVICE_CONTEXT /* deviceContext */,
    UCHAR /* entityID */,
    UCHAR /* channel */,
    LONG & /* volume */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::ValidateMuteControl(
    _In_ UCHAR /* entityID */,
    _In_ UCHAR /* channel */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::SetCurrentMute(
    PDEVICE_CONTEXT /* deviceContext */,
    UCHAR /* entityID */,
    UCHAR /* channel */,
    bool /* mute */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::GetCurrentMute(
    PDEVICE_CONTEXT /* deviceContext */,
    UCHAR /* entityID */,
    UCHAR /* channel */,
    bool & /* mute */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::GetCurrentConnectorState(
    PDEVICE_CONTEXT /*deviceContext*/,
    UCHAR /*entityID*/,
    NS_USBAudio::AUDIO_CHANNEL_CLUSTER_DESCRIPTOR & /*connectorState*/
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::SetCurrentSampleFrequency(
    PDEVICE_CONTEXT /* deviceContext */,
    UCHAR /* clockSourceID */,
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
    UCHAR /* clockSourceID */,
    ULONG & /* sampleRate */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio1ControlInterface::CanSetSampleFrequency(
    UCHAR /* clockSourceID */
)
{
    PAGED_CODE();

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
USBAudio1ControlInterface::GetSelectorConfiguration(
    PDEVICE_CONTEXT /* deviceContext */,
    UCHAR /* entityID */,
    UCHAR & /* pins */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::SetCurrentSelector(
    PDEVICE_CONTEXT /* deviceContext */,
    UCHAR /* entityID */,
    UCHAR /* selectorIndex */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::GetCurrentSelector(
    PDEVICE_CONTEXT /* deviceContext */,
    UCHAR /* entityID */,
    UCHAR & /* selectorIndex */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio1ControlInterface::GetClockSourceIDFromTerminal(
    UCHAR /* terminalLink */,
    UCHAR & /* clockSourceID */
)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio1ControlInterface::HasInterruptDataMessageEndpoint()
{
    PAGED_CODE();

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
void USBAudio1ControlInterface::GetInterruptDataMessageEndpoint(
    bool &  isValid,
    UCHAR & interfaceNumber,
    UCHAR & endpoint,
    UCHAR & interval
)
{
    PAGED_CODE();

    isValid = false;
    interfaceNumber = 0;
    endpoint = 0;
    interval = 0;
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
UCHAR USBAudio1StreamInterface::GetIntervalForDirection(
    _In_ bool /* isInput */
)
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
    RETURN_NTSTATUS_IF_TRUE(descriptor->bLength != NS_USBAudio0200::SIZE_OF_CS_AC_CLOCK_SOURCE_DESCRIPTOR, STATUS_DEVICE_DATA_ERROR);

    clockSourceDescriptor = (NS_USBAudio0200::PCS_AC_CLOCK_SOURCE_DESCRIPTOR)descriptor;

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bClockID       = 0x%02x", clockSourceDescriptor->bClockID);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bmAttributes   = 0x%02x", clockSourceDescriptor->bmAttributes);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bmControls     = 0x%02x", clockSourceDescriptor->bmControls);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bAssocTerminal = 0x%02x", clockSourceDescriptor->bAssocTerminal);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - iClockSource   = 0x%02x", clockSourceDescriptor->iClockSource);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - AC Clock Source : no. %u, clock ID 0x%02x", m_acClockSourceInfo.GetNumOfArray(), clockSourceDescriptor->bClockID);
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
    RETURN_NTSTATUS_IF_TRUE(descriptor->bLength != NS_USBAudio0200::SIZE_OF_CS_AC_INPUT_TERMINAL_DESCRIPTOR, STATUS_DEVICE_DATA_ERROR);

    inputTerminalDescriptor = (NS_USBAudio0200::PCS_AC_INPUT_TERMINAL_DESCRIPTOR)descriptor;

    if (descriptor->bLength >= sizeof(NS_USBAudio0200::CS_AC_INPUT_TERMINAL_DESCRIPTOR))
    {
        status = m_acInputTerminalInfo.Append(m_parentObject, inputTerminalDescriptor);

        if (NT_SUCCESS(status))
        {
            RecordClockEntity(inputTerminalDescriptor->bCSourceID);
        }
        ULONG outTerminalId = inputTerminalDescriptor->bTerminalID;
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - AC Input Terminal : terminal ID 0x%02x, channels %u", outTerminalId, inputTerminalDescriptor->bNrChannels);
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
    RETURN_NTSTATUS_IF_TRUE(descriptor->bLength != NS_USBAudio0200::SIZE_OF_CS_AC_OUTPUT_TERMINAL_DESCRIPTOR, STATUS_DEVICE_DATA_ERROR);

    outputTerminalDescriptor = (NS_USBAudio0200::PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR)descriptor;

    if (descriptor->bLength >= sizeof(NS_USBAudio0200::CS_AC_OUTPUT_TERMINAL_DESCRIPTOR))
    {
        status = m_acOutputTerminalInfo.Append(m_parentObject, outputTerminalDescriptor);

        if (NT_SUCCESS(status))
        {
            RecordClockEntity(outputTerminalDescriptor->bCSourceID);
        }

        ULONG inSourceUnitId = outputTerminalDescriptor->bSourceID;
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - AC Output Terminal : source ID 0x%02x", inSourceUnitId);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SetMixerUnit(const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor)
{
    NTSTATUS                                             status = STATUS_SUCCESS;
    NS_USBAudio0200::PCS_AC_MIXER_UNIT_DESCRIPTOR_COMMON mixerUnitDescriptor = nullptr;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(descriptor == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE((descriptor->bDescriptorType != NS_USBAudio0200::CS_INTERFACE) || (descriptor->bDescriptorSubtype != NS_USBAudio0200::MIXER_UNIT), STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE(descriptor->bLength < NS_USBAudio0200::SIZE_OF_MINIMUM_CS_AC_MIXER_UNIT_DESCRIPTOR, STATUS_DEVICE_DATA_ERROR);

    mixerUnitDescriptor = (NS_USBAudio0200::PCS_AC_MIXER_UNIT_DESCRIPTOR_COMMON)descriptor;

    if ((descriptor->bLength >= sizeof(NS_USBAudio0200::CS_AC_MIXER_UNIT_DESCRIPTOR_COMMON)) && (descriptor->bDescriptorSubtype == NS_USBAudio0200::MIXER_UNIT))
    {
        ULONG descriptorSize = sizeof(NS_USBAudio0200::CS_AC_MIXER_UNIT_DESCRIPTOR_COMMON) + mixerUnitDescriptor->bNrInPins + 1 /* bNrChannels */ + 4 /* bmChannelConfig[4] */ + 1 /* iChannelNames */ + 1 /* bmControls */ + 1 /* iMixer */;
        ULONG mixerControlsSize = 1; // minimum
        descriptorSize += mixerControlsSize;

                                     // Do not evaluate the size based on the number of input/output channels here.
        for (ULONG pin = 0; pin < mixerUnitDescriptor->bNrInPins; pin++)
        {
            UCHAR baSourceID = *(((UCHAR *)mixerUnitDescriptor) + sizeof(NS_USBAudio0200::CS_AC_MIXER_UNIT_DESCRIPTOR_COMMON) + pin);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - AC Mixer Unit : source ID [%u] 0x%02x", pin, baSourceID);
        }

        if (mixerUnitDescriptor->bLength >= descriptorSize)
        {
            status = m_acMixerUnitInfo.Append(m_parentObject, mixerUnitDescriptor);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - AC  Mixer :  ID 0x%02x", mixerUnitDescriptor->bUnitID);
        }
        else
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bLength = %d, descriptor size = %d", mixerUnitDescriptor->bLength, descriptorSize);
            status = STATUS_DEVICE_DATA_ERROR;
        }
    }
    else
    {
        status = STATUS_DEVICE_DATA_ERROR;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SetSelectorUnit(const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor)
{
    NTSTATUS                                         status = STATUS_SUCCESS;
    NS_USBAudio0200::PCS_AC_SELECTOR_UNIT_DESCRIPTOR selectorUnitDescriptor = nullptr;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(descriptor == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE((descriptor->bDescriptorType != NS_USBAudio0200::CS_INTERFACE) || (descriptor->bDescriptorSubtype != NS_USBAudio0200::SELECTOR_UNIT), STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE(descriptor->bLength < NS_USBAudio0200::SIZE_OF_MINIMUM_CS_AC_SELECTOR_UNIT_DESCRIPTOR, STATUS_DEVICE_DATA_ERROR);

    selectorUnitDescriptor = (NS_USBAudio0200::PCS_AC_SELECTOR_UNIT_DESCRIPTOR)descriptor;

    if ((descriptor->bLength >= sizeof(NS_USBAudio0200::CS_AC_SELECTOR_UNIT_DESCRIPTOR)) && (descriptor->bDescriptorSubtype == NS_USBAudio0200::SELECTOR_UNIT))
    {
        if (selectorUnitDescriptor->bLength >= (sizeof(NS_USBAudio0200::CS_AC_SELECTOR_UNIT_DESCRIPTOR) + sizeof(NS_USBAudio0200::CS_AC_SELECTOR_UNIT_DESCRIPTOR::baSourceID[0]) * (selectorUnitDescriptor->bNrInPins - 1)))
        {
            status = m_acSelectorUnitInfo.Append(m_parentObject, selectorUnitDescriptor);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - AC  Selector :  ID 0x%02x", selectorUnitDescriptor->bUnitID);
        }
        else
        {
            status = STATUS_DEVICE_DATA_ERROR;
        }
    }

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
    RETURN_NTSTATUS_IF_TRUE(descriptor->bLength < NS_USBAudio0200::SIZE_OF_MINIMUM_CS_AC_FEATURE_UNIT_DESCRIPTOR, STATUS_DEVICE_DATA_ERROR);

    featureUnitDescriptor = (NS_USBAudio0200::PCS_AC_FEATURE_UNIT_DESCRIPTOR)descriptor;

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - AC Feature Unit : unit ID 0x%02x, source ID 0x%02x", featureUnitDescriptor->bUnitID, featureUnitDescriptor->bSourceID);

    status = m_acFeatureUnitInfo.Append(m_parentObject, featureUnitDescriptor);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SetProcessingUnit(const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor)
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
    NTSTATUS                                          status = STATUS_SUCCESS;
    NS_USBAudio0200::PCS_AC_CLOCK_SELECTOR_DESCRIPTOR clockSelectorDescriptor = nullptr;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(descriptor == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE((descriptor->bDescriptorType != NS_USBAudio0200::CS_INTERFACE) || (descriptor->bDescriptorSubtype != NS_USBAudio0200::CLOCK_SELECTOR), STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE(descriptor->bLength < NS_USBAudio0200::SIZE_OF_MINIMUM_CS_AC_CLOCK_SELECTOR_DESCRIPTOR, STATUS_DEVICE_DATA_ERROR);

    clockSelectorDescriptor = (NS_USBAudio0200::PCS_AC_CLOCK_SELECTOR_DESCRIPTOR)descriptor;

    if ((descriptor->bLength >= sizeof(NS_USBAudio0200::CS_AC_CLOCK_SELECTOR_DESCRIPTOR)) && (descriptor->bDescriptorSubtype == NS_USBAudio0200::CLOCK_SELECTOR))
    {
        if (clockSelectorDescriptor->bLength >= (sizeof(NS_USBAudio0200::CS_AC_CLOCK_SELECTOR_DESCRIPTOR) + sizeof(NS_USBAudio0200::CS_AC_CLOCK_SELECTOR_DESCRIPTOR::baCSourceID[0]) * (clockSelectorDescriptor->bNrInPins - 1)))
        {
            status = m_acClockSelectorInfo.Append(m_parentObject, clockSelectorDescriptor);
            // deviceContext->ClockSelectorId = clockSelectorDescriptor->bClockID;
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - AC Clock Selector : clock ID 0x%02x", clockSelectorDescriptor->bClockID);
        }
        else
        {
            status = STATUS_DEVICE_DATA_ERROR;
        }
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
ULONG USBAudio2ControlInterface::GetNumOfClockSources()
{
    PAGED_CODE();

    return m_acClockSourceInfo.GetNumOfArray();
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG USBAudio2ControlInterface::GetClockEntityCountForTerminal()
{
    PAGED_CODE();

    return m_clockEntityCount;
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
    PAGED_CODE();

    controls = 0;
    for (auto & clockSourceDescriptor : m_acClockSourceInfo)
    {
        if (clockSourceDescriptor->bClockID == clockSourceID)
        {
            controls = clockSourceDescriptor->bmControls;
            return STATUS_SUCCESS;
        }
    }

    return STATUS_INVALID_PARAMETER;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SetCurrentSampleFrequency(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           clockSourceID,
    ULONG           desiredSampleRate
)
{
    NTSTATUS status = STATUS_INVALID_PARAMETER;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry, %u %u", clockSourceID, desiredSampleRate);

    RETURN_NTSTATUS_IF_TRUE(clockSourceID == USBAudioConfiguration::InvalidID, STATUS_INVALID_PARAMETER);

    if (CanSetSampleFrequency(clockSourceID))
    {
        status = ControlRequestSetSampleFrequency(deviceContext, GetInterfaceNumber(), clockSourceID, desiredSampleRate);
        if (NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, clock id 0x%02x, sample frequency %u", GetInterfaceNumber(), clockSourceID, desiredSampleRate);
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::GetCurrentSampleFrequency(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           clockSourceID,
    ULONG &         sampleRate
)
{
    NTSTATUS status = STATUS_SUCCESS;
    sampleRate = 0;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(clockSourceID == USBAudioConfiguration::InvalidID, STATUS_INVALID_PARAMETER);

    RETURN_NTSTATUS_IF_TRUE(clockSourceID == USBAudioConfiguration::InvalidID, STATUS_INVALID_PARAMETER);

    if (CanSetSampleFrequency(clockSourceID))
    {
        status = ControlRequestGetSampleFrequency(deviceContext, GetInterfaceNumber(), clockSourceID, sampleRate);
        if (NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, clock id 0x%02x, sample frequency %u", GetInterfaceNumber(), clockSourceID, sampleRate);
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!, %u", status, sampleRate);
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio2ControlInterface::CanSetSampleFrequency(
    UCHAR clockSourceID
)
{
    NTSTATUS status = STATUS_SUCCESS;
    bool     canSetSampleFrequency = false;
    UCHAR    sampleFrequencyControls = 0;

    PAGED_CODE();

    status = QuerySampleFrequencyControls(clockSourceID, sampleFrequencyControls);

    if (NT_SUCCESS(status))
    {
        canSetSampleFrequency = ((sampleFrequencyControls & NS_USBAudio0200::CLOCK_FREQUENCY_CONTROL_MASK) == NS_USBAudio0200::CLOCK_FREQUENCY_CONTROL_READ_WRITE);
    }
    return canSetSampleFrequency;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudio2ControlInterface::GetSelectorConfiguration(
    PDEVICE_CONTEXT /* deviceContext */,
    UCHAR   entityID,
    UCHAR & pins
)
{
    NTSTATUS status = STATUS_INVALID_PARAMETER;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    //
    // Retrieve the range of the first valid channel.
    //
    for (auto & selectorUnitDescriptor : m_acSelectorUnitInfo)
    {
        if (selectorUnitDescriptor->bUnitID == entityID)
        {
            pins = selectorUnitDescriptor->bNrInPins;
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - selector unit pins %u", pins);
            status = STATUS_SUCCESS;
            break;
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SetCurrentSelector(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           entityID,
    UCHAR           selectorIndex
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    status = ControlRequestSetSelector(deviceContext, GetInterfaceNumber(), entityID, selectorIndex);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit, %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::GetCurrentSelector(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           entityID,
    UCHAR &         selectorIndex
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    status = ControlRequestGetSelector(deviceContext, GetInterfaceNumber(), entityID, selectorIndex);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit, %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::GetClockSourceIDFromTerminal(
    UCHAR   terminalLink,
    UCHAR & clockSourceID
)
{
    PAGED_CODE();

    RETURN_NTSTATUS_IF_FAILED(GetClockSourceIDFromTerminal(true, terminalLink, clockSourceID));

    if (clockSourceID == USBAudioConfiguration::InvalidID)
    {
        RETURN_NTSTATUS_IF_FAILED(GetClockSourceIDFromTerminal(false, terminalLink, clockSourceID));
    }

    return STATUS_SUCCESS;
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
        RETURN_NTSTATUS_IF_FAILED(GetCurrentSampleFrequency(deviceContext, clockSourceID, sampleRate));

        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, clock id 0x%02x, sample frequency control is read only. sample frequency %u", GetInterfaceNumber(), clockSourceID, sampleRate);
    }

    status = ControlRequestGetSampleFrequencyRange(deviceContext, GetInterfaceNumber(), clockSourceID, memory, parameterBlock);
    if (NT_SUCCESS(status))
    {
        ASSERT(memory != nullptr);
        ASSERT(parameterBlock != nullptr);
        for (ULONG rangeIndex = 0; rangeIndex < parameterBlock->wNumSubRanges; rangeIndex++)
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, clock id 0x%02x, sample frequency range [%u] min %u, max %u,  res %u", GetInterfaceNumber(), clockSourceID, rangeIndex, parameterBlock->subrange[rangeIndex].dMIN, parameterBlock->subrange[rangeIndex].dMAX, parameterBlock->subrange[rangeIndex].dRES);
            for (ULONG sampleRateListIndex = 0; sampleRateListIndex < c_SampleRateCount; ++sampleRateListIndex)
            {
                if ((c_SampleRateList[sampleRateListIndex] >= parameterBlock->subrange[rangeIndex].dMIN) && (c_SampleRateList[sampleRateListIndex] <= parameterBlock->subrange[rangeIndex].dMAX) && ((sampleRate == 0) || (sampleRate == c_SampleRateList[sampleRateListIndex])))
                {
                    // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " <PID %04x>", deviceContext->AudioProperty.ProductId);
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
bool USBAudio2ControlInterface::HasInterruptDataMessageEndpoint()
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
                    if (((endpointAttribute & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_TYPE_INTERRUPT) &&
                        USB_ENDPOINT_DIRECTION_IN(endpointAddress))
                    {
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
void USBAudio2ControlInterface::GetInterruptDataMessageEndpoint(
    bool &  isValid,
    UCHAR & interfaceNumber,
    UCHAR & endpoint,
    UCHAR & interval
)
{
    PAGED_CODE();

    isValid = false;
    interfaceNumber = 0;
    endpoint = 0;
    interval = 0;

    for (ULONG index = 0; index < GetNumEndpoints(); index++)
    {
        UCHAR endpointAddress;
        UCHAR attributes;
        if (GetEndpointAddress(index, endpointAddress))
        {
            if (USB_ENDPOINT_DIRECTION_IN(endpointAddress) && GetAttributes(index, attributes))
            {
                if ((attributes & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_TYPE_INTERRUPT)
                {
                    isValid = true;
                    interfaceNumber = GetInterfaceNumber();
                    endpoint = endpointAddress;
                    GetInterval(index, interval);
                    return;
                }
            }
        }
    }
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

    for (auto & featureUnitDescriptor : m_acFeatureUnitInfo)
    {
        //  FU_VOLUME_CONTROL current
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

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::GetRangeSampleFrequency(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           clockSourceID,
    ULONG &         supportedSampleRate
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_FAILED(GetCurrentSupportedSampleFrequency(deviceContext, clockSourceID, supportedSampleRate));

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

    for (auto & featureUnitDescriptor : m_acFeatureUnitInfo)
    {
        // FU_VOLUME_CONTROL ranges
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
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, ch %u, unit id %02x, volume range [%u] min %u, max %u,  res %u", GetInterfaceNumber(), ch, featureUnitDescriptor->bUnitID, rangeIndex, parameterBlock->subrange[rangeIndex].wMIN, parameterBlock->subrange[rangeIndex].wMAX, parameterBlock->subrange[rangeIndex].wRES);
                    }
                    WdfObjectDelete(memory);
                }
            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SetDefaultFeatureUnit(
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    for (auto & featureUnitDescriptor : m_acFeatureUnitInfo)
    {
        // FU_VOLUME_CONTROL ranges
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
                    // const SHORT minusInfinity = static_cast<SHORT>(0x8000);
                    SHORT targetVolume = 0; // 0dB
                    ASSERT(memory != nullptr);
                    ASSERT(parameterBlock != nullptr);
                    ULONG rangeMax = 1;     // parameterBlock->wNumSubRanges;
                    for (ULONG rangeIndex = 0; rangeIndex < rangeMax; rangeIndex++)
                    {
                        // SHORT volume = minusInfinity;
                        SHORT min = static_cast<SHORT>(parameterBlock->subrange[rangeIndex].wMIN);
                        SHORT max = static_cast<SHORT>(parameterBlock->subrange[rangeIndex].wMAX);
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, ch %u, unit id %02x, volume range [%u] min %d, max %d,  res %u", GetInterfaceNumber(), ch, featureUnitDescriptor->bUnitID, rangeIndex, min, max, parameterBlock->subrange[rangeIndex].wRES);
                        targetVolume = max;
                    }
                    WdfObjectDelete(memory);
                    RETURN_NTSTATUS_IF_FAILED(ControlRequestSetVolume(deviceContext, GetInterfaceNumber(), featureUnitDescriptor->bUnitID, ch, static_cast<USHORT>(targetVolume)));
                }
            }

            if (ConvertBmaControls(featureUnitDescriptor->ch[ch].bmaControls) & NS_USBAudio0200::FEATURE_UNIT_BMA_MUTE_CONTROL_MASK)
            {
                RETURN_NTSTATUS_IF_FAILED(ControlRequestSetMute(deviceContext, GetInterfaceNumber(), featureUnitDescriptor->bUnitID, ch, false));
            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SetDefaultSelectorUnit(
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;
    UCHAR    sourceID;
    UCHAR    defaultIndex = 1;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    sourceID = USBAudioConfiguration::InvalidID;

    for (auto & selectorUnitDescriptor : m_acSelectorUnitInfo)
    {
        UCHAR selectorIndex = 0; // 1 origin
        RETURN_NTSTATUS_IF_FAILED(ControlRequestGetSelector(deviceContext, GetInterfaceNumber(), selectorUnitDescriptor->bUnitID, selectorIndex));
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bNrInPins %u, selectorIndex %u", selectorUnitDescriptor->bNrInPins, selectorIndex);
        ASSERT(selectorIndex > 0);
        if ((selectorIndex > 0) && (selectorIndex <= selectorUnitDescriptor->bNrInPins))
        {
            sourceID = selectorUnitDescriptor->baSourceID[selectorIndex - 1];
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - sourceID 0x%02x", sourceID);
        }
        if (selectorIndex != defaultIndex)
        {
            RETURN_NTSTATUS_IF_FAILED(ControlRequestSetSelector(deviceContext, GetInterfaceNumber(), selectorUnitDescriptor->bUnitID, defaultIndex));
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::GetClockSourceIDFromTerminal(
    bool    isInput,
    UCHAR   terminalLink,
    UCHAR & clockSourceID
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    clockSourceID = USBAudioConfiguration::InvalidID;

    if (terminalLink != USBAudioConfiguration::InvalidID)
    {
        if (isInput)
        {
            for (auto & outputTerminalDescriptor : m_acOutputTerminalInfo)
            {
                if (outputTerminalDescriptor->bTerminalID == terminalLink)
                {
                    clockSourceID = outputTerminalDescriptor->bCSourceID;
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - output terminal id 0x%02x, terminal type %u, bCSourceID 0x%02x", outputTerminalDescriptor->bTerminalID, outputTerminalDescriptor->wTerminalType, clockSourceID);
                    break;
                }
            }
        }
        else
        {
            for (auto & inputTerminalDescriptor : m_acInputTerminalInfo)
            {
                if (inputTerminalDescriptor->bTerminalID == terminalLink)
                {
                    clockSourceID = inputTerminalDescriptor->bCSourceID;
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - input terminal id 0x%02x, terminal type %u, bCSourceID 0x%02x", inputTerminalDescriptor->bTerminalID, inputTerminalDescriptor->wTerminalType, clockSourceID);
                    break;
                }
            }
        }
    }

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

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry clockSourceID 0x%02x", clockSourceID);

    for (auto & clockSelectorDescriptor : m_acClockSelectorInfo)
    {
        if (clockSelectorDescriptor->bClockID == clockSourceID)
        {
            //
            // If a clock selector exists, get the clock source selected by the
            // current clock selector.
            //
            UCHAR clockSelectorIndex = 0; // 1 origin
            RETURN_NTSTATUS_IF_FAILED(ControlRequestGetClockSelector(deviceContext, GetInterfaceNumber(), clockSelectorDescriptor->bClockID, clockSelectorIndex));
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bNrInPins %u, clockSelectorIndex %u", clockSelectorDescriptor->bNrInPins, clockSelectorIndex);
            ASSERT(clockSelectorIndex > 0);
            if ((clockSelectorIndex > 0) && (clockSelectorIndex <= clockSelectorDescriptor->bNrInPins))
            {
                clockSourceID = clockSelectorDescriptor->baCSourceID[clockSelectorIndex - 1];
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - clockSourceID 0x%02x", clockSourceID);
            }
            else
            {
                status = STATUS_UNSUCCESSFUL;
                RETURN_NTSTATUS_IF_FAILED(status);
            }
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
            // m_inputCurrentSampleRate = 0;
            // m_outputCurrentSampleRate = 0;
            return STATUS_SUCCESS;
        }
        RETURN_NTSTATUS_IF_FAILED(m_acClockSourceInfo.Get(0, clockSourceDescriptor));
        clockSourceID = clockSourceDescriptor->bClockID;
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - clockSourceID 0x%02x", clockSourceID);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SetCurrentClockSourceInternal(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           clockSourceID
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    for (auto & clockSelectorDescriptor : m_acClockSelectorInfo)
    {
        if (clockSelectorDescriptor->bClockID == clockSourceID)
        {
            if (clockSelectorDescriptor->bNrInPins > 1)
            {
                UCHAR currentClockSelectorIndex = 0; // 1 origin
                UCHAR targetClockSelectorIndex = 0;
                UCHAR targetClockID = USBAudioConfiguration::InvalidID;

                // Get only if multiple pins are found.
                RETURN_NTSTATUS_IF_FAILED(ControlRequestGetClockSelector(deviceContext, GetInterfaceNumber(), clockSelectorDescriptor->bClockID, currentClockSelectorIndex));
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bNrInPins %u, clockSelectorIndex %u", clockSelectorDescriptor->bNrInPins, currentClockSelectorIndex);

                for (auto & clockSourceDescriptor : m_acClockSourceInfo)
                // Finding an internal, programmable clock source
                {
                    if ((clockSourceDescriptor->bmAttributes & NS_USBAudio0200::CLOCK_TYPE_MASK) == NS_USBAudio0200::CLOCK_TYPE_INTERNAL_PROGRAMMABLE_CLOCK)
                    {
                        targetClockID = clockSourceDescriptor->bClockID;
                        break;
                    }
                }

                // Find the next preferred internal, variable clock source.
                if (targetClockID == USBAudioConfiguration::InvalidID)
                {
                    for (auto & clockSourceDescriptor : m_acClockSourceInfo)
                    {
                        if ((clockSourceDescriptor->bmAttributes & NS_USBAudio0200::CLOCK_TYPE_MASK) == NS_USBAudio0200::CLOCK_TYPE_INTERNAL_VARIABLE_CLOCK)
                        {
                            targetClockID = clockSourceDescriptor->bClockID;
                            break;
                        }
                    }
                }

                // Find the next preferred internal, fixed clock source.
                if (targetClockID == USBAudioConfiguration::InvalidID)
                {
                    for (auto & clockSourceDescriptor : m_acClockSourceInfo)
                    {
                        if ((clockSourceDescriptor->bmAttributes & NS_USBAudio0200::CLOCK_TYPE_MASK) == NS_USBAudio0200::CLOCK_TYPE_INTERNAL_FIXED_CLOCK)
                        {
                            targetClockID = clockSourceDescriptor->bClockID;
                            break;
                        }
                    }
                }

                if (targetClockID == USBAudioConfiguration::InvalidID)
                {
                    targetClockID = clockSelectorDescriptor->baCSourceID[0];
                }

                for (UCHAR clockSelectorIndex = 0; clockSelectorIndex < clockSelectorDescriptor->bNrInPins; clockSelectorIndex++)
                {
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - clockSourceID %u, target clockSourceID 0x%02x", clockSelectorDescriptor->baCSourceID[clockSelectorIndex], targetClockID);

                    if (targetClockID == clockSelectorDescriptor->baCSourceID[clockSelectorIndex])
                    {
                        targetClockSelectorIndex = clockSelectorIndex + 1; // convert to 1 origin
                        break;
                    }
                }

                if (targetClockSelectorIndex != currentClockSelectorIndex)
                {
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, clock id 0x%02x, clockSelectorIndex %u", GetInterfaceNumber(), clockSelectorDescriptor->bClockID, targetClockSelectorIndex);
                    RETURN_NTSTATUS_IF_FAILED(ControlRequestSetClockSelector(deviceContext, GetInterfaceNumber(), clockSelectorDescriptor->bClockID, targetClockSelectorIndex));
                }
            }
        }
    }
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::ReconnectClockAll(
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    for (auto & clockSelectorDescriptor : m_acClockSelectorInfo)
    {
        UCHAR clockSelectorIndex = 0; // 1 origin
        RETURN_NTSTATUS_IF_FAILED(ControlRequestGetClockSelector(deviceContext, GetInterfaceNumber(), clockSelectorDescriptor->bClockID, clockSelectorIndex));
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bNrInPins %u, clockSelectorIndex %u", clockSelectorDescriptor->bNrInPins, clockSelectorIndex);
        if ((clockSelectorDescriptor->bNrInPins > 0) && (clockSelectorIndex == 0))
        {
            clockSelectorIndex = 1;
            RETURN_NTSTATUS_IF_FAILED(ControlRequestSetClockSelector(deviceContext, GetInterfaceNumber(), clockSelectorDescriptor->bClockID, clockSelectorIndex));
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::GetCurrentSelectorSourceID(
    PDEVICE_CONTEXT deviceContext,
    UCHAR &         sourceID
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    sourceID = USBAudioConfiguration::InvalidID;

    for (auto & selectorUnitDescriptor : m_acSelectorUnitInfo)
    {
        UCHAR selectorIndex = 0; // 1 origin
        RETURN_NTSTATUS_IF_FAILED(ControlRequestGetSelector(deviceContext, GetInterfaceNumber(), selectorUnitDescriptor->bUnitID, selectorIndex));
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bNrInPins %u, selectorIndex %u", selectorUnitDescriptor->bNrInPins, selectorIndex);
        ASSERT(selectorIndex > 0);
        if ((selectorIndex > 0) && (selectorIndex <= selectorUnitDescriptor->bNrInPins))
        {
            sourceID = selectorUnitDescriptor->baSourceID[selectorIndex - 1];
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - sourceID 0x%02x", sourceID);
        }
        else
        {
            status = STATUS_UNSUCCESSFUL;
            RETURN_NTSTATUS_IF_FAILED(status);
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

    // 202303
    // RETURN_NTSTATUS_IF_FAILED(QueryCurrentSampleFrequency(deviceContext));

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

    // mixer unit current
    // TBD

    // FU_VOLUME_CONTROL ranges
    RETURN_NTSTATUS_IF_FAILED(GetRangeFeatureUnit(deviceContext));

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SetDefaultAttributeAll(
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    // RETURN_NTSTATUS_IF_FAILED(SetCurrentClockSourceInternal(deviceContext));

    RETURN_NTSTATUS_IF_FAILED(SetDefaultSelectorUnit(deviceContext));

    RETURN_NTSTATUS_IF_FAILED(SetDefaultFeatureUnit(deviceContext));

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SearchOutputTerminal(
    UCHAR &  sourceID,
    UCHAR &  numOfChannels,
    USHORT & terminalType,
    UCHAR &  terminalID,
    UCHAR &  volumeUnitID,
    UCHAR &  muteUnitID,
    SCHAR    recursionCount
)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC!  recursionCount = %d", recursionCount);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - sourceID id %02x", sourceID);

    recursionCount--;

    for (auto & genericAudioDescriptor : m_genericAudioDescriptorInfo)
    {
        switch (genericAudioDescriptor->bDescriptorSubtype)
        {
        case NS_USBAudio0200::OUTPUT_TERMINAL:
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - output terminal bTerminalID 0x%02x, bSourceID 0x%02x, bCSSourceID 0x%02x", ((NS_USBAudio0200::PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor)->bTerminalID, ((NS_USBAudio0200::PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor)->bSourceID, ((NS_USBAudio0200::PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor)->bCSourceID);
            if (((NS_USBAudio0200::PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor)->bSourceID == sourceID)
            {
                terminalType = ((NS_USBAudio0200::PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor)->wTerminalType;
                terminalID = ((NS_USBAudio0200::PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor)->bTerminalID;
                return STATUS_SUCCESS;
            }
            break;

        case NS_USBAudio0200::FEATURE_UNIT: {
            NS_USBAudio0200::PCS_AC_FEATURE_UNIT_DESCRIPTOR featureUnitDescriptor = (NS_USBAudio0200::PCS_AC_FEATURE_UNIT_DESCRIPTOR)genericAudioDescriptor;
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - feature unit bSourceID 0x%02x", featureUnitDescriptor->bSourceID);
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
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - mixer unit bUnitID 0x%02x", mixerUnitDescriptor->bUnitID);
                if (mixerUnitDescriptor->bNrInPins != 0)
                {
                    for (UCHAR pin = 0; pin < mixerUnitDescriptor->bNrInPins; ++pin)
                    {
                        UCHAR baSourceID = *(((UCHAR *)mixerUnitDescriptor) + sizeof(NS_USBAudio0200::CS_AC_MIXER_UNIT_DESCRIPTOR_COMMON) + pin);
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - mixer unit pin[%u] baSourceID 0x%02x", pin, baSourceID);
                        if (baSourceID == sourceID)
                        {
                            UCHAR sourceIDBackup = sourceID;
                            sourceID = mixerUnitDescriptor->bUnitID;
                            status = SearchOutputTerminal(sourceID, numOfChannels, terminalType, terminalID, volumeUnitID, muteUnitID, recursionCount);
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

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
void USBAudio2ControlInterface::RecordClockEntity(
    UCHAR bCSourceID
)
{
    PAGED_CODE();

    if ((m_clockEntityBitmap[bCSourceID / 32] & (1 << (bCSourceID % 32))) == 0)
    {
        m_clockEntityCount++;
        m_clockEntityBitmap[bCSourceID / 32] |= (1 << (bCSourceID % 32));
    }
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void USBAudio2ControlInterface::InterlockedSetEntityBit(
    ULONG bitmap[8],
    UCHAR entityId
)
{
    InterlockedOr((volatile LONG *)&(bitmap[entityId / 32]), (1 << (entityId % 32)));
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
bool USBAudio2ControlInterface::InterlockedTestAndClearEntityBit(
    ULONG bitmap[8],
    UCHAR entityId
)
{
    return (InterlockedAnd((volatile LONG *)&(bitmap[entityId / 32]), ~(1 << (entityId % 32))) & (1 << (entityId % 32))) ? true : false;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
bool USBAudio2ControlInterface::InterlockedTestEntityBit(
    ULONG bitmap[8],
    UCHAR entityId
)
{
    return (InterlockedCompareExchange((volatile LONG *)&(bitmap[entityId / 32]), 0, 0) & (1 << (entityId % 32))) ? true : false;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudio2ControlInterface::GetInformationForHostPin(
    PDEVICE_CONTEXT /* deviceContext */,
    UCHAR   unitID,
    UCHAR & numOfChannels
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    numOfChannels = (UCHAR)GetUnitOutputChannelCount(unitID);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!, %u channels", STATUS_SUCCESS, numOfChannels);

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudio2ControlInterface::GetInformationForBridgePin(
    PDEVICE_CONTEXT                                 deviceContext,
    UCHAR                                           unitID,
    UCHAR &                                         numOfChannels,
    USHORT &                                        terminalType,
    UCHAR &                                         channelNames,
    NS_USBAudio::AUDIO_CHANNEL_CLUSTER_DESCRIPTOR & connectorState
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    channelNames = USBAudioConfiguration::InvalidString;
    for (auto & outputTerminalDescriptor : m_acOutputTerminalInfo)
    {
        if (outputTerminalDescriptor->bTerminalID == unitID)
        {
            terminalType = outputTerminalDescriptor->wTerminalType;

            if ((outputTerminalDescriptor->bmControls[0] | (((USHORT)outputTerminalDescriptor->bmControls[1]) << 8)) & NS_USBAudio0200::AC_OUTPUT_TERMINAL_CONTROL_COPY_PROTECT_CONTROL_MASK)
            {
                RETURN_NTSTATUS_IF_FAILED(GetCurrentConnectorState(deviceContext, unitID, connectorState));
                numOfChannels = connectorState.bNrChannels;
                channelNames = connectorState.iChannelNames;
            }
            else
            {
                channelNames = outputTerminalDescriptor->iTerminal;
                numOfChannels = (UCHAR)GetUnitOutputChannelCount(outputTerminalDescriptor->bTerminalID);
                connectorState.bNrChannels = numOfChannels;
                connectorState.iChannelNames = channelNames;
                connectorState.bmChannelConfig = (numOfChannels == 1 ? NS_USBAudio0200::FRONT_CENTER : NS_USBAudio0200::FRONT_LEFT | NS_USBAudio0200::FRONT_RIGHT);
            }
            ValidateChannelNamesStringDescriptor(deviceContext, channelNames);
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", STATUS_SUCCESS);

            return STATUS_SUCCESS;
        }
    }

    for (auto & inputTerminalDescriptor : m_acInputTerminalInfo)
    {
        if (inputTerminalDescriptor->bTerminalID == unitID)
        {
            terminalType = inputTerminalDescriptor->wTerminalType;

            if ((inputTerminalDescriptor->bmControls[0] | (((USHORT)inputTerminalDescriptor->bmControls[1]) << 8)) & NS_USBAudio0200::AC_INPUT_TERMINAL_CONTROL_COPY_PROTECT_CONTROL_MASK)
            {
                RETURN_NTSTATUS_IF_FAILED(GetCurrentConnectorState(deviceContext, unitID, connectorState));
                numOfChannels = connectorState.bNrChannels;
                channelNames = connectorState.iChannelNames;
            }
            else
            {
                numOfChannels = inputTerminalDescriptor->bNrChannels;
                channelNames = inputTerminalDescriptor->iTerminal;
                connectorState.bNrChannels = numOfChannels;
                connectorState.iChannelNames = channelNames;
                connectorState.bmChannelConfig = ((ULONG)inputTerminalDescriptor->bmChannelConfig[0] | ((ULONG)inputTerminalDescriptor->bmChannelConfig[0] << 8) | ((ULONG)inputTerminalDescriptor->bmChannelConfig[0] << 16) | ((ULONG)inputTerminalDescriptor->bmChannelConfig[0] << 24));
            }
            ValidateChannelNamesStringDescriptor(deviceContext, channelNames);

            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", STATUS_SUCCESS);
            return STATUS_SUCCESS;
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", STATUS_INVALID_PARAMETER);

    return STATUS_INVALID_PARAMETER;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudio2ControlInterface::GetInformationForVolumeElement(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           unitID,
    UCHAR &         numOfChannels,
    LONG &          minimum,
    LONG &          maximum,
    ULONG &         steppingDelta
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    numOfChannels = (UCHAR)GetUnitOutputChannelCount(unitID);

    status = GetVolumeConfiguration(deviceContext, unitID, minimum, maximum, steppingDelta);

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - unit id 0x%02x, volume minimum %ld (0x%lx), maximum %ld (0x%lx), stepping delta %ld (0x%lx), %u channels", unitID, minimum, minimum, maximum, maximum, steppingDelta, steppingDelta, numOfChannels);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudio2ControlInterface::GetInformationForMuteElement(
    _In_          PDEVICE_CONTEXT /* deviceContext */,
    _In_ UCHAR    unitID,
    _Out_ UCHAR & numOfChannels
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    numOfChannels = (UCHAR)GetUnitOutputChannelCount(unitID);

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - unit id 0x%02x, %u channels", unitID, numOfChannels);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudio2ControlInterface::GetInformationForSuperMixElement(
    _In_          PDEVICE_CONTEXT /* deviceContext */,
    _In_ UCHAR    unitID,
    _Out_ UCHAR & numOfInputChannels,
    _Out_ UCHAR & numOfOutputChannels
)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    numOfInputChannels = numOfOutputChannels = 0;

    for (auto & mixerUnitDescriptor : m_acMixerUnitInfo)
    {
        if (mixerUnitDescriptor->bUnitID == unitID)
        {
            for (UCHAR pin = 0; pin < mixerUnitDescriptor->bNrInPins; ++pin)
            {
                UCHAR baSourceID = *(((UCHAR *)mixerUnitDescriptor) + sizeof(NS_USBAudio0200::CS_AC_MIXER_UNIT_DESCRIPTOR_COMMON) + pin);

                numOfInputChannels += (UCHAR)GetUnitOutputChannelCount(baSourceID);
            }
            numOfOutputChannels = ((UCHAR *)mixerUnitDescriptor)[sizeof(NS_USBAudio0200::PCS_AC_MIXER_UNIT_DESCRIPTOR_COMMON) + mixerUnitDescriptor->bNrInPins];
            status = STATUS_SUCCESS;
            break;
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!, %u input channels, %u output channels", status, numOfInputChannels, numOfOutputChannels);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudio2ControlInterface::GetInformationForMuxElement(
    _In_          PDEVICE_CONTEXT /* deviceContext */,
    _In_ UCHAR    unitID,
    _Out_ UCHAR & numOfChannels
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    numOfChannels = (UCHAR)GetUnitOutputChannelCount(unitID);

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - unit id 0x%02x, %u channels", unitID, numOfChannels);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudio2ControlInterface::GetInformationForAgcElement(
    _In_          PDEVICE_CONTEXT /* deviceContext */,
    _In_ UCHAR    unitID,
    _Out_ UCHAR & numOfChannels
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    numOfChannels = (UCHAR)GetUnitOutputChannelCount(unitID);

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - unit id 0x%02x, %u channels", unitID, numOfChannels);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SearchOutputTerminalFromInputTerminal(
    PDEVICE_CONTEXT /* deviceContext */,
    UCHAR    terminalLink,
    UCHAR &  numOfChannels,
    USHORT & terminalType,
    UCHAR &  terminalID,
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

    UCHAR sourceID = USBAudioConfiguration::InvalidID;

    numOfChannels = 0;
    terminalType = NS_USBAudio0200::LINE_CONNECTOR;
    terminalID = USBAudioConfiguration::InvalidID;
    volumeUnitID = USBAudioConfiguration::InvalidID;
    muteUnitID = USBAudioConfiguration::InvalidID;

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - num of ac input terminal info %u", m_acInputTerminalInfo.GetNumOfArray());
    for (auto & inputTerminalDescriptor : m_acInputTerminalInfo)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - terminal id 0x%02x, terminal link 0x%02x", inputTerminalDescriptor->bTerminalID, terminalLink);
        if (inputTerminalDescriptor->bTerminalID == terminalLink)
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - terminal id 0x%02x, channels %u", inputTerminalDescriptor->bTerminalID, inputTerminalDescriptor->bNrChannels);
            sourceID = inputTerminalDescriptor->bTerminalID;
            numOfChannels = inputTerminalDescriptor->bNrChannels;
            break;
        }
    }

    for (ULONG units = 0; units < MAX_OF_UNITS; units++)
    {
        enum
        {
            MAX_CHAINED_MIXER_UNITS = 1
        };
        UCHAR sourceIDBackup = sourceID;

        status = SearchOutputTerminal(sourceID, numOfChannels, terminalType, terminalID, volumeUnitID, muteUnitID, MAX_CHAINED_MIXER_UNITS);
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
ULONG USBAudio2ControlInterface::GetUnitOutputChannelCount(
    UCHAR unitID
)
{
    PAGED_CODE();

    for (auto & descriptor : m_genericAudioDescriptorInfo)
    {
        switch (descriptor->bDescriptorSubtype)
        {
        case NS_USBAudio0200::INPUT_TERMINAL:
            if (((NS_USBAudio0200::PCS_AC_INPUT_TERMINAL_DESCRIPTOR)descriptor)->bTerminalID == unitID)
            {
                return ((NS_USBAudio0200::PCS_AC_INPUT_TERMINAL_DESCRIPTOR)descriptor)->bNrChannels;
            }
            break;
        case NS_USBAudio0200::OUTPUT_TERMINAL:
            if (((NS_USBAudio0200::PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR)descriptor)->bTerminalID == unitID)
            {
                return GetUnitOutputChannelCount(((NS_USBAudio0200::PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR)descriptor)->bSourceID); // TBD Reentrant
            }
            break;
        case NS_USBAudio0200::MIXER_UNIT:
            if (((NS_USBAudio0200::PCS_AC_MIXER_UNIT_DESCRIPTOR_COMMON)descriptor)->bUnitID == unitID)
            {
                NS_USBAudio0200::PCS_AC_MIXER_UNIT_DESCRIPTOR_COMMON mixerUnitDescriptor = (NS_USBAudio0200::PCS_AC_MIXER_UNIT_DESCRIPTOR_COMMON)descriptor;
                return ((UCHAR *)mixerUnitDescriptor)[sizeof(NS_USBAudio0200::PCS_AC_MIXER_UNIT_DESCRIPTOR_COMMON) + mixerUnitDescriptor->bNrInPins];
            }
            break;
        case NS_USBAudio0200::SELECTOR_UNIT:
            if (((NS_USBAudio0200::PCS_AC_SELECTOR_UNIT_DESCRIPTOR)descriptor)->bUnitID == unitID)
            {
                return GetUnitOutputChannelCount(((NS_USBAudio0200::PCS_AC_SELECTOR_UNIT_DESCRIPTOR)descriptor)->baSourceID[0]); // TBD Reentrant
            }
            break;
        case NS_USBAudio0200::FEATURE_UNIT:
            if (((NS_USBAudio0200::PCS_AC_FEATURE_UNIT_DESCRIPTOR)descriptor)->bUnitID == unitID)
            {
                NS_USBAudio0200::PCS_AC_FEATURE_UNIT_DESCRIPTOR featureUnitDescriptor = (NS_USBAudio0200::PCS_AC_FEATURE_UNIT_DESCRIPTOR)descriptor;
                UCHAR                                           numOfChannels = (featureUnitDescriptor->bLength - offsetof(NS_USBAudio0200::CS_AC_FEATURE_UNIT_DESCRIPTOR, ch)) / (sizeof(NS_USBAudio0200::CS_AC_FEATURE_UNIT_DESCRIPTOR::ch[0]));
                numOfChannels = (numOfChannels == 0) ? 0 : (numOfChannels - 1);
                return numOfChannels;
            }
            break;
        case NS_USBAudio0200::PROCESSING_UNIT:
            // TBD
            break;
        case NS_USBAudio0200::EXTENSION_UNIT:
            if (((NS_USBAudio0200::PCS_AC_EXTENSION_UNIT_DESCRIPTOR_COMMON)descriptor)->bUnitID == unitID)
            {
                NS_USBAudio0200::PCS_AC_EXTENSION_UNIT_DESCRIPTOR_COMMON extensionUnitDescriptor = (NS_USBAudio0200::PCS_AC_EXTENSION_UNIT_DESCRIPTOR_COMMON)descriptor;
                return ((UCHAR *)extensionUnitDescriptor)[sizeof(NS_USBAudio0200::PCS_AC_EXTENSION_UNIT_DESCRIPTOR_COMMON) + extensionUnitDescriptor->bNrInPins];
            }
            break;
        default:
        case NS_USBAudio0200::CLOCK_SOURCE:
        case NS_USBAudio0200::CLOCK_SELECTOR:
        case NS_USBAudio0200::CLOCK_MULTIPLIER:
        case NS_USBAudio0200::SAMPLE_RATE_CONVERTER:
            break;
        }
    }
    return 0;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SearchInputTerminalFromOutputTerminal(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           terminalLink,
    UCHAR &         numOfChannels,
    USHORT &        terminalType,
    UCHAR &         terminalID,
    UCHAR &         volumeUnitID,
    UCHAR &         muteUnitID
)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    enum
    {
        MAX_OF_UNITS = 10
    };

    PAGED_CODE();

    UCHAR sourceID = USBAudioConfiguration::InvalidID;

    numOfChannels = 0;
    terminalType = NS_USBAudio0200::LINE_CONNECTOR;
    terminalID = USBAudioConfiguration::InvalidID;
    volumeUnitID = USBAudioConfiguration::InvalidID;
    muteUnitID = USBAudioConfiguration::InvalidID;

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - num of ac output terminal info %u", m_acOutputTerminalInfo.GetNumOfArray());
    for (auto & outputTerminalDescriptor : m_acOutputTerminalInfo)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - terminal id 0x%02x, terminal link 0x%02x", outputTerminalDescriptor->bTerminalID, terminalLink);
        if (outputTerminalDescriptor->bTerminalID == terminalLink)
        {
            sourceID = outputTerminalDescriptor->bSourceID;
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bSourceID 0x%02x", outputTerminalDescriptor->bSourceID);
            break;
        }
    }

    for (ULONG units = 0; units < MAX_OF_UNITS; units++)
    {
        UCHAR sourceIDBackup = sourceID;

        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - sourceID id %02x", sourceID);
        for (auto & genericAudioDescriptor : m_genericAudioDescriptorInfo)
        {
            switch (genericAudioDescriptor->bDescriptorSubtype)
            {
            case NS_USBAudio0200::INPUT_TERMINAL:
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - input terminal bTerminalID 0x%02x, bCSSourceID 0x%02x", ((NS_USBAudio0200::PCS_AC_INPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor)->bTerminalID, ((NS_USBAudio0200::PCS_AC_INPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor)->bCSourceID);
                if (((NS_USBAudio0200::PCS_AC_INPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor)->bTerminalID == sourceID)
                {
                    numOfChannels = ((NS_USBAudio0200::PCS_AC_INPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor)->bNrChannels;
                    terminalType = ((NS_USBAudio0200::PCS_AC_INPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor)->wTerminalType;
                    terminalID = ((NS_USBAudio0200::PCS_AC_INPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor)->bTerminalID;
                    return STATUS_SUCCESS;
                }
                break;
            case NS_USBAudio0200::FEATURE_UNIT: {
                NS_USBAudio0200::PCS_AC_FEATURE_UNIT_DESCRIPTOR featureUnitDescriptor = (NS_USBAudio0200::PCS_AC_FEATURE_UNIT_DESCRIPTOR)genericAudioDescriptor;
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - feature unit unit id %02x", featureUnitDescriptor->bUnitID);
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
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - feature unit source id %02x", featureUnitDescriptor->bSourceID);
                    sourceID = featureUnitDescriptor->bSourceID;
                }
            }
            break;
            case NS_USBAudio0200::SELECTOR_UNIT: {
                NS_USBAudio0200::PCS_AC_SELECTOR_UNIT_DESCRIPTOR selectorUnitDescriptor = (NS_USBAudio0200::PCS_AC_SELECTOR_UNIT_DESCRIPTOR)genericAudioDescriptor;
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - selector unit unit id %02x", selectorUnitDescriptor->bUnitID);
                if (selectorUnitDescriptor->bUnitID == sourceID)
                {
                    UCHAR selectorIndex = 0; // 1 origin
                    RETURN_NTSTATUS_IF_FAILED(ControlRequestGetSelector(deviceContext, GetInterfaceNumber(), selectorUnitDescriptor->bUnitID, selectorIndex));
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bNrInPins %u, selectorIndex %u", selectorUnitDescriptor->bNrInPins, selectorIndex);
                    ASSERT(selectorIndex > 0);
                    if ((selectorIndex > 0) && (selectorIndex <= selectorUnitDescriptor->bNrInPins))
                    {
                        sourceID = selectorUnitDescriptor->baSourceID[selectorIndex - 1];
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - selector unit sourceID 0x%02x", sourceID);
                    }
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
                break;
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

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudio2ControlInterface::TraverseTowardForward(
    ULONGLONG            idMap[4],
    ULONGLONG            unvisitedUnitMap[4],
    UCHAR &              unitID,
    ULONG &              controlBitmap,
    UCHAR &              nextUnitID,
    TraversalDirection & traversalDirection,
    bool &               hasMoreData
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, "%!FUNC! 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%02x, 0x%08x, 0x%02x, %s, hasMoreData = %!bool!", idMap[0], idMap[1], idMap[2], idMap[3], unvisitedUnitMap[0], unvisitedUnitMap[1], unvisitedUnitMap[2], unvisitedUnitMap[3], unitID, controlBitmap, nextUnitID, GetTraversalDirectionString(traversalDirection), hasMoreData);

    for (auto & genericAudioDescriptor : m_genericAudioDescriptorInfo)
    {
        switch (genericAudioDescriptor->bDescriptorSubtype)
        {
        case NS_USBAudio0200::OUTPUT_TERMINAL: {
            NS_USBAudio0200::PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR outputTerminalDescriptor = (NS_USBAudio0200::PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor;

            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - output terminal bTerminalID 0x%02x, bCSSourceID 0x%02x, bSourceID 0x%02x", outputTerminalDescriptor->bTerminalID, outputTerminalDescriptor->bCSourceID, outputTerminalDescriptor->bSourceID);
            if (!TestEntityBit(idMap, outputTerminalDescriptor->bTerminalID) && (outputTerminalDescriptor->bSourceID == unitID))
            {
                nextUnitID = outputTerminalDescriptor->bTerminalID;
                return STATUS_SUCCESS;
            }
        }
        break;
        case NS_USBAudio0200::FEATURE_UNIT: {
            NS_USBAudio0200::PCS_AC_FEATURE_UNIT_DESCRIPTOR featureUnitDescriptor = (NS_USBAudio0200::PCS_AC_FEATURE_UNIT_DESCRIPTOR)genericAudioDescriptor;
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - feature unit unit id 0x%02x, source id 0x%02x", featureUnitDescriptor->bUnitID, featureUnitDescriptor->bSourceID);
            if (!TestEntityBit(idMap, featureUnitDescriptor->bUnitID) && (featureUnitDescriptor->bSourceID == unitID))
            {
                controlBitmap = 0;
                nextUnitID = featureUnitDescriptor->bUnitID;
                hasMoreData = true;
                return STATUS_SUCCESS;
            }
        }
        break;
        case NS_USBAudio0200::MIXER_UNIT: {
            NS_USBAudio0200::PCS_AC_MIXER_UNIT_DESCRIPTOR_COMMON mixerUnitDescriptor = (NS_USBAudio0200::PCS_AC_MIXER_UNIT_DESCRIPTOR_COMMON)genericAudioDescriptor;
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - mixer unit bUnitID 0x%02x", mixerUnitDescriptor->bUnitID);

            // Do not perform the check within this if statement, since the comparison with unitID is done per pin.
            if (!TestEntityBit(idMap, mixerUnitDescriptor->bUnitID))
            {
                if (mixerUnitDescriptor->bNrInPins != 0)
                {
                    for (UCHAR pin = 0; pin < mixerUnitDescriptor->bNrInPins; ++pin)
                    {
                        UCHAR baSourceID = *(((UCHAR *)mixerUnitDescriptor) + sizeof(NS_USBAudio0200::CS_AC_MIXER_UNIT_DESCRIPTOR_COMMON) + pin);
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - mixer unit pin[%u] baSourceID 0x%02x", pin, baSourceID);
                        if (baSourceID == unitID)
                        {
                            controlBitmap = 0;
                            nextUnitID = mixerUnitDescriptor->bUnitID;
                            hasMoreData = true;
                            return STATUS_SUCCESS;
                        }
                    }
                }
            }
        }

        default:
        case NS_USBAudio0200::SELECTOR_UNIT:
        case NS_USBAudio0200::INPUT_TERMINAL:
        case NS_USBAudio0200::CLOCK_MULTIPLIER:
        case NS_USBAudio0200::CLOCK_SELECTOR:
        case NS_USBAudio0200::CLOCK_SOURCE:
        case NS_USBAudio0200::EXTENSION_UNIT:
        case NS_USBAudio0200::PROCESSING_UNIT:
        case NS_USBAudio0200::SAMPLE_RATE_CONVERTER:
            break;
        }
    }

    hasMoreData = false;

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudio2ControlInterface::TraverseTowardReverse(
    ULONGLONG            idMap[4],
    ULONGLONG            unvisitedUnitMap[4],
    UCHAR &              unitID,
    ULONG &              controlBitmap,
    UCHAR &              nextUnitID,
    TraversalDirection & traversalDirection,
    bool &               hasMoreData
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, "%!FUNC! 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%02x, 0x%08x, 0x%02x, %s, hasMoreData = %!bool!", idMap[0], idMap[1], idMap[2], idMap[3], unvisitedUnitMap[0], unvisitedUnitMap[1], unvisitedUnitMap[2], unvisitedUnitMap[3], unitID, controlBitmap, nextUnitID, GetTraversalDirectionString(traversalDirection), hasMoreData);

    for (auto & genericAudioDescriptor : m_genericAudioDescriptorInfo)
    {
        switch (genericAudioDescriptor->bDescriptorSubtype)
        {
        case NS_USBAudio0200::OUTPUT_TERMINAL: {
            NS_USBAudio0200::PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR outputTerminalDescriptor = (NS_USBAudio0200::PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor;
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - output terminal bTerminalID 0x%02x, bCSSourceID 0x%02x, bSourceID 0x%02x", outputTerminalDescriptor->bTerminalID, outputTerminalDescriptor->bCSourceID, outputTerminalDescriptor->bSourceID);
            if (!TestEntityBit(idMap, outputTerminalDescriptor->bSourceID) && (outputTerminalDescriptor->bTerminalID == unitID))
            {
                nextUnitID = outputTerminalDescriptor->bSourceID;
                return STATUS_SUCCESS;
            }
        }
        break;
        case NS_USBAudio0200::FEATURE_UNIT: {
            NS_USBAudio0200::PCS_AC_FEATURE_UNIT_DESCRIPTOR featureUnitDescriptor = (NS_USBAudio0200::PCS_AC_FEATURE_UNIT_DESCRIPTOR)genericAudioDescriptor;
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - feature unit unit id 0x%02x, source id 0x%02x", featureUnitDescriptor->bUnitID, featureUnitDescriptor->bSourceID);
            if (!TestEntityBit(idMap, featureUnitDescriptor->bSourceID) && (featureUnitDescriptor->bUnitID == unitID))
            {
                controlBitmap = 0;
                nextUnitID = featureUnitDescriptor->bSourceID;
                hasMoreData = true;
                return STATUS_SUCCESS;
            }
        }
        break;
        case NS_USBAudio0200::MIXER_UNIT: {
            NS_USBAudio0200::PCS_AC_MIXER_UNIT_DESCRIPTOR_COMMON mixerUnitDescriptor = (NS_USBAudio0200::PCS_AC_MIXER_UNIT_DESCRIPTOR_COMMON)genericAudioDescriptor;
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - mixer unit bUnitID 0x%02x", mixerUnitDescriptor->bUnitID);
            if (mixerUnitDescriptor->bUnitID == unitID)
            {
                if (mixerUnitDescriptor->bNrInPins != 0)
                {
                    for (UCHAR pin = 0; pin < mixerUnitDescriptor->bNrInPins; ++pin)
                    {
                        UCHAR baSourceID = *(((UCHAR *)mixerUnitDescriptor) + sizeof(NS_USBAudio0200::CS_AC_MIXER_UNIT_DESCRIPTOR_COMMON) + pin);
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - mixer unit pin[%u] baSourceID 0x%02x", pin, baSourceID);
                        if (!TestEntityBit(idMap, baSourceID))
                        {
                            controlBitmap = 0;
                            nextUnitID = baSourceID;
                            hasMoreData = true;
                            return STATUS_SUCCESS;
                        }
                    }
                    hasMoreData = false;
                }
            }
        }
        break;
        case NS_USBAudio0200::SELECTOR_UNIT: {
            NS_USBAudio0200::PCS_AC_SELECTOR_UNIT_DESCRIPTOR selectorUnitDescriptor = (NS_USBAudio0200::PCS_AC_SELECTOR_UNIT_DESCRIPTOR)genericAudioDescriptor;
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - selector unit bUnitID 0x%02x", selectorUnitDescriptor->bUnitID);
            if (selectorUnitDescriptor->bUnitID == unitID)
            {
                if (selectorUnitDescriptor->bNrInPins != 0)
                {
                    for (UCHAR index = 0; index < selectorUnitDescriptor->bNrInPins; index++)
                    {
                        UCHAR sourceID = selectorUnitDescriptor->baSourceID[index];
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - selector unit [%u] sourceID 0x%02x", index, sourceID);
                        if (!TestEntityBit(idMap, sourceID))
                        {
                            controlBitmap = 0;
                            nextUnitID = sourceID;
                            hasMoreData = true;
                            return STATUS_SUCCESS;
                        }
                    }
                    hasMoreData = false;
                }
            }
        }
        break;
        default:
        case NS_USBAudio0200::INPUT_TERMINAL:
        case NS_USBAudio0200::CLOCK_MULTIPLIER:
        case NS_USBAudio0200::CLOCK_SELECTOR:
        case NS_USBAudio0200::CLOCK_SOURCE:
        case NS_USBAudio0200::EXTENSION_UNIT:
        case NS_USBAudio0200::PROCESSING_UNIT:
        case NS_USBAudio0200::SAMPLE_RATE_CONVERTER:
            break;
        }
    }
    hasMoreData = false;

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudio2ControlInterface::WalkNextUnitTowardForward(
    ULONGLONG            idMap[4],
    ULONGLONG            unvisitedUnitMap[4],
    AudioNodeKind &      audioNodeKind,
    UCHAR &              unitID,
    ULONG &              controlBitmap,
    UCHAR &              nextUnitID,
    TraversalDirection & traversalDirection,
    bool &               hasMoreData
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    ASSERT(unitID == USBAudioConfiguration::InvalidID);

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, "%!FUNC! 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, %s, 0x%02x, 0x%08x, 0x%02x, %s, hasMoreData = %!bool!", idMap[0], idMap[1], idMap[2], idMap[3], unvisitedUnitMap[0], unvisitedUnitMap[1], unvisitedUnitMap[2], unvisitedUnitMap[3], GetAudioNodeKindString(audioNodeKind), unitID, controlBitmap, nextUnitID, GetTraversalDirectionString(traversalDirection), hasMoreData);

    for (auto & genericAudioDescriptor : m_genericAudioDescriptorInfo)
    {
        switch (genericAudioDescriptor->bDescriptorSubtype)
        {
        case NS_USBAudio0200::INPUT_TERMINAL: {
            NS_USBAudio0200::PCS_AC_INPUT_TERMINAL_DESCRIPTOR inputTerminalDescriptor = (NS_USBAudio0200::PCS_AC_INPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor;

            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - input terminal bTerminalID 0x%02x, bCSSourceID 0x%02x", inputTerminalDescriptor->bTerminalID, inputTerminalDescriptor->bCSourceID);
            if (!TestEntityBit(idMap, inputTerminalDescriptor->bTerminalID) && (inputTerminalDescriptor->bTerminalID == unitID))
            {
                SetEntityBit(idMap, inputTerminalDescriptor->bTerminalID);
                audioNodeKind = AudioNodeKind::RenderHostPin;

                return TraverseTowardForward(idMap, unvisitedUnitMap, unitID, controlBitmap, nextUnitID, traversalDirection, hasMoreData);
            }
        }
        break;
        case NS_USBAudio0200::FEATURE_UNIT: {
            // Update the unitID only after detecting and processing the mute, volume, and AGC nodes within the Feature Unit.
            // The initial processing of a Feature Unit is identified by the controlBitmap being zero.
            // On the first pass, set the corresponding bit in controlBitmap to indicate that the processing has been performed.

            NS_USBAudio0200::PCS_AC_FEATURE_UNIT_DESCRIPTOR featureUnitDescriptor = (NS_USBAudio0200::PCS_AC_FEATURE_UNIT_DESCRIPTOR)genericAudioDescriptor;
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - feature unit unit id 0x%02x, source id 0x%02x", featureUnitDescriptor->bUnitID, featureUnitDescriptor->bSourceID);
            if (!TestEntityBit(idMap, featureUnitDescriptor->bUnitID) && (featureUnitDescriptor->bUnitID == unitID))
            {
                if (controlBitmap == 0)
                {
                    UCHAR size = 4; // CS_AC_FEATURE_UNIT_DESCRIPTOR::bmaControls
                    UCHAR channels = (featureUnitDescriptor->bLength - offsetof(NS_USBAudio0200::CS_AC_FEATURE_UNIT_DESCRIPTOR, ch)) / size;
                    for (UCHAR ch = 0; ch < channels; ++ch)
                    {
                        controlBitmap |= ConvertBmaControls(featureUnitDescriptor->ch[ch].bmaControls);
                    }
                    controlBitmap &= (NS_USBAudio0200::FEATURE_UNIT_BMA_MUTE_CONTROL_MASK | NS_USBAudio0200::FEATURE_UNIT_BMA_VOLUME_CONTROL_MASK | NS_USBAudio0200::FEATURE_UNIT_BMA_AUTOMATIC_GAIN_CONTROL_MASK);
                }
                if (controlBitmap & NS_USBAudio0200::FEATURE_UNIT_BMA_AUTOMATIC_GAIN_CONTROL_MASK)
                {
                    controlBitmap &= ~NS_USBAudio0200::FEATURE_UNIT_BMA_AUTOMATIC_GAIN_CONTROL_MASK;
                    audioNodeKind = AudioNodeKind::AgcElement;
                    if (controlBitmap == 0)
                    {
                        SetEntityBit(idMap, featureUnitDescriptor->bUnitID);
                        return TraverseTowardForward(idMap, unvisitedUnitMap, unitID, controlBitmap, nextUnitID, traversalDirection, hasMoreData);
                    }
                    nextUnitID = featureUnitDescriptor->bUnitID;
                    return STATUS_SUCCESS;
                }
                else if (controlBitmap & NS_USBAudio0200::FEATURE_UNIT_BMA_MUTE_CONTROL_MASK)
                {
                    controlBitmap &= ~NS_USBAudio0200::FEATURE_UNIT_BMA_MUTE_CONTROL_MASK;
                    audioNodeKind = AudioNodeKind::MuteElement;
                    if (controlBitmap == 0)
                    {
                        SetEntityBit(idMap, featureUnitDescriptor->bUnitID);
                        return TraverseTowardForward(idMap, unvisitedUnitMap, unitID, controlBitmap, nextUnitID, traversalDirection, hasMoreData);
                    }
                    nextUnitID = featureUnitDescriptor->bUnitID;
                    return STATUS_SUCCESS;
                }
                else if (controlBitmap & NS_USBAudio0200::FEATURE_UNIT_BMA_VOLUME_CONTROL_MASK)
                {
                    controlBitmap &= ~NS_USBAudio0200::FEATURE_UNIT_BMA_VOLUME_CONTROL_MASK;
                    audioNodeKind = AudioNodeKind::VolumeElement;
                    if (controlBitmap == 0)
                    {
                        SetEntityBit(idMap, featureUnitDescriptor->bUnitID);
                        return TraverseTowardForward(idMap, unvisitedUnitMap, unitID, controlBitmap, nextUnitID, traversalDirection, hasMoreData);
                    }
                    nextUnitID = featureUnitDescriptor->bUnitID;
                    return STATUS_SUCCESS;
                }
                else
                {
                    controlBitmap = 0;
                    audioNodeKind = AudioNodeKind::Invalid;
                    SetEntityBit(idMap, featureUnitDescriptor->bUnitID);
                    return TraverseTowardForward(idMap, unvisitedUnitMap, unitID, controlBitmap, nextUnitID, traversalDirection, hasMoreData);
                }
            }
        }
        break;
        case NS_USBAudio0200::MIXER_UNIT: {
            NS_USBAudio0200::PCS_AC_MIXER_UNIT_DESCRIPTOR_COMMON mixerUnitDescriptor = (NS_USBAudio0200::PCS_AC_MIXER_UNIT_DESCRIPTOR_COMMON)genericAudioDescriptor;
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - mixer unit bUnitID 0x%02x", mixerUnitDescriptor->bUnitID);
            if (!TestEntityBit(idMap, mixerUnitDescriptor->bUnitID) && (mixerUnitDescriptor->bUnitID == unitID))
            {
                if (mixerUnitDescriptor->bNrInPins != 0)
                {
                    bool setEntityBit = true;
                    for (UCHAR pin = 0; pin < mixerUnitDescriptor->bNrInPins; ++pin)
                    {
                        UCHAR baSourceID = *(((UCHAR *)mixerUnitDescriptor) + sizeof(NS_USBAudio0200::CS_AC_MIXER_UNIT_DESCRIPTOR_COMMON) + pin);
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - mixer unit pin[%u] baSourceID 0x%02x", pin, baSourceID);
                        if (TestEntityBit(idMap, baSourceID))
                        {
                            TestAndClearEntityBit(unvisitedUnitMap, baSourceID);
                        }
                        else
                        {
                            setEntityBit = false;
                            SetEntityBit(unvisitedUnitMap, baSourceID);
                        }
                    }
                    if (setEntityBit)
                    {
                        SetEntityBit(idMap, mixerUnitDescriptor->bUnitID);
                    }
                    audioNodeKind = AudioNodeKind::SuperMixElement;
                    return TraverseTowardForward(idMap, unvisitedUnitMap, unitID, controlBitmap, nextUnitID, traversalDirection, hasMoreData);
                }
                else
                {
                    audioNodeKind = AudioNodeKind::Invalid;
                    SetEntityBit(idMap, mixerUnitDescriptor->bUnitID);
                    return TraverseTowardForward(idMap, unvisitedUnitMap, unitID, controlBitmap, nextUnitID, traversalDirection, hasMoreData);
                }
            }
        }
        break;
        case NS_USBAudio0200::OUTPUT_TERMINAL: {
            NS_USBAudio0200::PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR outputTerminalDescriptor = (NS_USBAudio0200::PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor;

            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - output terminal bTerminalID 0x%02x, bCSSourceID 0x%02x, bSourceID 0x%02x", outputTerminalDescriptor->bTerminalID, outputTerminalDescriptor->bCSourceID, outputTerminalDescriptor->bSourceID);
            if (!TestEntityBit(idMap, outputTerminalDescriptor->bTerminalID) && (outputTerminalDescriptor->bTerminalID == unitID))
            {
                SetEntityBit(idMap, outputTerminalDescriptor->bTerminalID);
                audioNodeKind = AudioNodeKind::RenderBridgePin;

                return TraverseTowardForward(idMap, unvisitedUnitMap, unitID, controlBitmap, nextUnitID, traversalDirection, hasMoreData);
            }
        }
        break;
        default:
        case NS_USBAudio0200::SELECTOR_UNIT:
        case NS_USBAudio0200::CLOCK_MULTIPLIER:
        case NS_USBAudio0200::CLOCK_SELECTOR:
        case NS_USBAudio0200::CLOCK_SOURCE:
        case NS_USBAudio0200::EXTENSION_UNIT:
        case NS_USBAudio0200::PROCESSING_UNIT:
        case NS_USBAudio0200::SAMPLE_RATE_CONVERTER:
            break;
        }
    }
    audioNodeKind = AudioNodeKind::Invalid;
    SetEntityBit(idMap, unitID);
    status = TraverseTowardForward(idMap, unvisitedUnitMap, unitID, controlBitmap, nextUnitID, traversalDirection, hasMoreData);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudio2ControlInterface::WalkNextUnitTowardReverse(
    ULONGLONG            idMap[4],
    ULONGLONG            unvisitedUnitMap[4],
    AudioNodeKind &      audioNodeKind,
    UCHAR &              unitID,
    ULONG &              controlBitmap,
    UCHAR &              nextUnitID,
    TraversalDirection & traversalDirection,
    bool &               hasMoreData
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, "%!FUNC! 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, %s, 0x%02x, 0x%08x, 0x%02x, %s, hasMoreData = %!bool!", idMap[0], idMap[1], idMap[2], idMap[3], unvisitedUnitMap[0], unvisitedUnitMap[1], unvisitedUnitMap[2], unvisitedUnitMap[3], GetAudioNodeKindString(audioNodeKind), unitID, controlBitmap, nextUnitID, GetTraversalDirectionString(traversalDirection), hasMoreData);

    ASSERT(unitID == USBAudioConfiguration::InvalidID);

    for (auto & genericAudioDescriptor : m_genericAudioDescriptorInfo)
    {
        switch (genericAudioDescriptor->bDescriptorSubtype)
        {
        case NS_USBAudio0200::OUTPUT_TERMINAL: {
            NS_USBAudio0200::PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR outputTerminalDescriptor = (NS_USBAudio0200::PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor;

            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - output terminal bTerminalID 0x%02x, bCSSourceID 0x%02x, bSourceID 0x%02x", outputTerminalDescriptor->bTerminalID, outputTerminalDescriptor->bCSourceID, outputTerminalDescriptor->bSourceID);
            if (!TestEntityBit(idMap, outputTerminalDescriptor->bTerminalID) && (outputTerminalDescriptor->bTerminalID == unitID))
            {
                SetEntityBit(idMap, outputTerminalDescriptor->bTerminalID);
                audioNodeKind = AudioNodeKind::CaptureHostPin;
                return TraverseTowardReverse(idMap, unvisitedUnitMap, unitID, controlBitmap, nextUnitID, traversalDirection, hasMoreData);
            }
        }
        break;
        case NS_USBAudio0200::INPUT_TERMINAL: {
            NS_USBAudio0200::PCS_AC_INPUT_TERMINAL_DESCRIPTOR inputTerminalDescriptor = (NS_USBAudio0200::PCS_AC_INPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor;

            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - input terminal bTerminalID 0x%02x, bCSSourceID 0x%02x", inputTerminalDescriptor->bTerminalID, inputTerminalDescriptor->bCSourceID);
            if (!TestEntityBit(idMap, inputTerminalDescriptor->bTerminalID) && (inputTerminalDescriptor->bTerminalID == unitID))
            {
                SetEntityBit(idMap, inputTerminalDescriptor->bTerminalID);
                audioNodeKind = AudioNodeKind::CaptureBridgePin;
                return TraverseTowardReverse(idMap, unvisitedUnitMap, unitID, controlBitmap, nextUnitID, traversalDirection, hasMoreData);
            }
        }
        break;
        case NS_USBAudio0200::FEATURE_UNIT: {
            // Update the unitID only after detecting and processing the mute, volume, and AGC nodes within the Feature Unit.
            // The initial processing of a Feature Unit is identified by the controlBitmap being zero.
            // On the first pass, set the corresponding bit in controlBitmap to indicate that the processing has been performed.

            NS_USBAudio0200::PCS_AC_FEATURE_UNIT_DESCRIPTOR featureUnitDescriptor = (NS_USBAudio0200::PCS_AC_FEATURE_UNIT_DESCRIPTOR)genericAudioDescriptor;
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - feature unit unit id 0x%02x, source id 0x%02x", featureUnitDescriptor->bUnitID, featureUnitDescriptor->bSourceID);
            if (!TestEntityBit(idMap, featureUnitDescriptor->bUnitID) && (featureUnitDescriptor->bUnitID == unitID))
            {
                if (controlBitmap == 0)
                {
                    UCHAR size = 4; // CS_AC_FEATURE_UNIT_DESCRIPTOR::bmaControls
                    UCHAR channels = (featureUnitDescriptor->bLength - offsetof(NS_USBAudio0200::CS_AC_FEATURE_UNIT_DESCRIPTOR, ch)) / size;
                    for (UCHAR ch = 0; ch < channels; ++ch)
                    {
                        controlBitmap |= ConvertBmaControls(featureUnitDescriptor->ch[ch].bmaControls);
                    }
                    controlBitmap &= (NS_USBAudio0200::FEATURE_UNIT_BMA_MUTE_CONTROL_MASK | NS_USBAudio0200::FEATURE_UNIT_BMA_VOLUME_CONTROL_MASK | NS_USBAudio0200::FEATURE_UNIT_BMA_AUTOMATIC_GAIN_CONTROL_MASK);
                }
                if (controlBitmap & NS_USBAudio0200::FEATURE_UNIT_BMA_VOLUME_CONTROL_MASK)
                {
                    controlBitmap &= ~NS_USBAudio0200::FEATURE_UNIT_BMA_VOLUME_CONTROL_MASK;
                    audioNodeKind = AudioNodeKind::VolumeElement;
                    if (controlBitmap == 0)
                    {
                        SetEntityBit(idMap, featureUnitDescriptor->bUnitID);
                        return TraverseTowardReverse(idMap, unvisitedUnitMap, unitID, controlBitmap, nextUnitID, traversalDirection, hasMoreData);
                    }
                    nextUnitID = featureUnitDescriptor->bUnitID;
                    return STATUS_SUCCESS;
                }
                else if (controlBitmap & NS_USBAudio0200::FEATURE_UNIT_BMA_MUTE_CONTROL_MASK)
                {
                    controlBitmap &= ~NS_USBAudio0200::FEATURE_UNIT_BMA_MUTE_CONTROL_MASK;
                    audioNodeKind = AudioNodeKind::MuteElement;
                    if (controlBitmap == 0)
                    {
                        SetEntityBit(idMap, featureUnitDescriptor->bUnitID);
                        return TraverseTowardReverse(idMap, unvisitedUnitMap, unitID, controlBitmap, nextUnitID, traversalDirection, hasMoreData);
                    }
                    nextUnitID = featureUnitDescriptor->bUnitID;
                    return STATUS_SUCCESS;
                }
                else if (controlBitmap & NS_USBAudio0200::FEATURE_UNIT_BMA_AUTOMATIC_GAIN_CONTROL_MASK)
                {
                    controlBitmap &= ~NS_USBAudio0200::FEATURE_UNIT_BMA_AUTOMATIC_GAIN_CONTROL_MASK;
                    audioNodeKind = AudioNodeKind::AgcElement;
                    if (controlBitmap == 0)
                    {
                        SetEntityBit(idMap, featureUnitDescriptor->bUnitID);
                        return TraverseTowardReverse(idMap, unvisitedUnitMap, unitID, controlBitmap, nextUnitID, traversalDirection, hasMoreData);
                    }
                    nextUnitID = featureUnitDescriptor->bUnitID;
                    return STATUS_SUCCESS;
                }
                else
                {
                    controlBitmap = 0;
                    audioNodeKind = AudioNodeKind::Invalid;
                    SetEntityBit(idMap, featureUnitDescriptor->bUnitID);
                    return TraverseTowardReverse(idMap, unvisitedUnitMap, unitID, controlBitmap, nextUnitID, traversalDirection, hasMoreData);
                }
            }
        }
        break;
        case NS_USBAudio0200::MIXER_UNIT: {
            NS_USBAudio0200::PCS_AC_MIXER_UNIT_DESCRIPTOR_COMMON mixerUnitDescriptor = (NS_USBAudio0200::PCS_AC_MIXER_UNIT_DESCRIPTOR_COMMON)genericAudioDescriptor;
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - mixer unit bUnitID 0x%02x", mixerUnitDescriptor->bUnitID);
            if (!TestEntityBit(idMap, mixerUnitDescriptor->bUnitID) && (mixerUnitDescriptor->bUnitID == unitID))
            {
                if (mixerUnitDescriptor->bNrInPins != 0)
                {
                    bool setEntityBit = true;
                    for (UCHAR pin = 0; pin < mixerUnitDescriptor->bNrInPins; pin++)
                    {
                        UCHAR baSourceID = *(((UCHAR *)mixerUnitDescriptor) + sizeof(NS_USBAudio0200::CS_AC_MIXER_UNIT_DESCRIPTOR_COMMON) + pin);
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - mixer unit pin[%u] baSourceID 0x%02x", pin, baSourceID);
                        if (TestEntityBit(idMap, baSourceID))
                        {
                            TestAndClearEntityBit(unvisitedUnitMap, baSourceID);
                        }
                        else
                        {
                            setEntityBit = false;
                            SetEntityBit(unvisitedUnitMap, baSourceID);
                        }
                    }
                    status = TraverseTowardReverse(idMap, unvisitedUnitMap, unitID, controlBitmap, nextUnitID, traversalDirection, hasMoreData);
                    TestAndClearEntityBit(unvisitedUnitMap, nextUnitID);
                    if (setEntityBit)
                    {
                        SetEntityBit(idMap, mixerUnitDescriptor->bUnitID);
                    }
                    audioNodeKind = AudioNodeKind::SuperMixElement;
                    return status;
                }
                else
                {
                    audioNodeKind = AudioNodeKind::Invalid;
                    SetEntityBit(idMap, mixerUnitDescriptor->bUnitID);
                    return TraverseTowardReverse(idMap, unvisitedUnitMap, unitID, controlBitmap, nextUnitID, traversalDirection, hasMoreData);
                }
            }
        }
        break;

        case NS_USBAudio0200::SELECTOR_UNIT: {
            NS_USBAudio0200::PCS_AC_SELECTOR_UNIT_DESCRIPTOR selectorUnitDescriptor = (NS_USBAudio0200::PCS_AC_SELECTOR_UNIT_DESCRIPTOR)genericAudioDescriptor;
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - selector unit unit id %02x", selectorUnitDescriptor->bUnitID);
            if (!TestEntityBit(idMap, selectorUnitDescriptor->bUnitID) && (selectorUnitDescriptor->bUnitID == unitID))
            {
                if (selectorUnitDescriptor->bNrInPins != 0)
                {
                    bool setEntityBit = true;
                    for (UCHAR index = 0; index < selectorUnitDescriptor->bNrInPins; index++)
                    {
                        UCHAR sourceID = selectorUnitDescriptor->baSourceID[index];
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - selector unit [%u] sourceID 0x%02x", index, sourceID);
                        if (TestEntityBit(idMap, sourceID))
                        {
                            TestAndClearEntityBit(unvisitedUnitMap, sourceID);
                        }
                        else
                        {
                            setEntityBit = false;
                            SetEntityBit(unvisitedUnitMap, sourceID);
                        }
                    }
                    status = TraverseTowardReverse(idMap, unvisitedUnitMap, unitID, controlBitmap, nextUnitID, traversalDirection, hasMoreData);
                    TestAndClearEntityBit(unvisitedUnitMap, nextUnitID);
                    if (setEntityBit)
                    {
                        SetEntityBit(idMap, selectorUnitDescriptor->bUnitID);
                    }
                    audioNodeKind = AudioNodeKind::MuxElement;
                    return status;
                }
            }
        }
        break;
        default:
        case NS_USBAudio0200::CLOCK_MULTIPLIER:
        case NS_USBAudio0200::CLOCK_SELECTOR:
        case NS_USBAudio0200::CLOCK_SOURCE:
        case NS_USBAudio0200::EXTENSION_UNIT:
        case NS_USBAudio0200::PROCESSING_UNIT:
        case NS_USBAudio0200::SAMPLE_RATE_CONVERTER:
            break;
        }
    }

    audioNodeKind = AudioNodeKind::Invalid;
    SetEntityBit(idMap, unitID);
    status = TraverseTowardReverse(idMap, unvisitedUnitMap, unitID, controlBitmap, nextUnitID, traversalDirection, hasMoreData);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudio2ControlInterface::WalkNextUnvisitedUnit(
    ULONGLONG            idMap[4],
    ULONGLONG            unvisitedUnitMap[4],
    AudioNodeKind &      audioNodeKind,
    UCHAR &              unitID,
    ULONG &              controlBitmap,
    UCHAR &              nextUnitID,
    TraversalDirection & traversalDirection,
    bool &               hasMoreData
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    auto checkEntityBit = [&idMap, unitID](UCHAR * sourceIDs, UCHAR numOfArray) noexcept -> bool {
        bool setEntityBit = true;
        if ((sourceIDs == nullptr) || (numOfArray == 0))
        {
            return true;
        }
        for (UCHAR index = 0; index < numOfArray; index++)
        {
            UCHAR sourceID = sourceIDs[index];
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - [%u] 0x%02x", index, sourceID);
            if (!TestEntityBit(idMap, sourceID) && (sourceID != unitID))
            {
                setEntityBit = false;
            }
        }
        return setEntityBit;
    };

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, "%!FUNC! 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, %s, 0x%02x, 0x%08x, 0x%02x, %s, hasMoreData = %!bool!", idMap[0], idMap[1], idMap[2], idMap[3], unvisitedUnitMap[0], unvisitedUnitMap[1], unvisitedUnitMap[2], unvisitedUnitMap[3], GetAudioNodeKindString(audioNodeKind), unitID, controlBitmap, nextUnitID, GetTraversalDirectionString(traversalDirection), hasMoreData);

    unitID = nextUnitID = USBAudioConfiguration::InvalidID;
    for (ULONG i = 1; i < 0x100; i++)
    {
        if (TestEntityBit(unvisitedUnitMap, (UCHAR)i))
        {
            unitID = nextUnitID = (UCHAR)i;
        }
    }
    if (unitID == USBAudioConfiguration::InvalidID)
    {
        unvisitedUnitMap[0] = unvisitedUnitMap[1] = unvisitedUnitMap[2] = unvisitedUnitMap[3] = 0;
        hasMoreData = false;
        return status;
    }

    for (auto & genericAudioDescriptor : m_genericAudioDescriptorInfo)
    {
        switch (genericAudioDescriptor->bDescriptorSubtype)
        {
        case NS_USBAudio0200::MIXER_UNIT: {
            NS_USBAudio0200::PCS_AC_MIXER_UNIT_DESCRIPTOR_COMMON mixerUnitDescriptor = (NS_USBAudio0200::PCS_AC_MIXER_UNIT_DESCRIPTOR_COMMON)genericAudioDescriptor;
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - mixer unit bUnitID 0x%02x", mixerUnitDescriptor->bUnitID);
            if (mixerUnitDescriptor->bNrInPins != 0)
            {
                for (UCHAR pin = 0; pin < mixerUnitDescriptor->bNrInPins; pin++)
                {
                    UCHAR baSourceID = *(((UCHAR *)mixerUnitDescriptor) + sizeof(NS_USBAudio0200::CS_AC_MIXER_UNIT_DESCRIPTOR_COMMON) + pin);
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - mixer unit pin[%u] baSourceID 0x%02x", pin, baSourceID);
                    if (baSourceID == unitID)
                    {
                        nextUnitID = baSourceID;
                        unitID = mixerUnitDescriptor->bUnitID;
                        audioNodeKind = AudioNodeKind::SuperMixElement;
                        traversalDirection = TraversalDirection::Reverse;
                        hasMoreData = true;
                        if (checkEntityBit(((UCHAR *)mixerUnitDescriptor) + sizeof(NS_USBAudio0200::CS_AC_MIXER_UNIT_DESCRIPTOR_COMMON), mixerUnitDescriptor->bNrInPins))
                        {
                            SetEntityBit(idMap, mixerUnitDescriptor->bUnitID);
                        }
                        TestAndClearEntityBit(unvisitedUnitMap, nextUnitID);
                        return status;
                    }
                }
            }
        }
        break;

        case NS_USBAudio0200::SELECTOR_UNIT: {
            NS_USBAudio0200::PCS_AC_SELECTOR_UNIT_DESCRIPTOR selectorUnitDescriptor = (NS_USBAudio0200::PCS_AC_SELECTOR_UNIT_DESCRIPTOR)genericAudioDescriptor;
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - selector unit unit id %02x", selectorUnitDescriptor->bUnitID);
            if (selectorUnitDescriptor->bNrInPins != 0)
            {
                for (UCHAR index = 0; index < selectorUnitDescriptor->bNrInPins; index++)
                {
                    UCHAR sourceID = selectorUnitDescriptor->baSourceID[index];
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - selector unit [%u] sourceID 0x%02x", index, sourceID);
                    if (sourceID == unitID)
                    {
                        nextUnitID = sourceID;
                        unitID = selectorUnitDescriptor->bUnitID;
                        audioNodeKind = AudioNodeKind::MuxElement;
                        traversalDirection = TraversalDirection::Reverse;
                        hasMoreData = true;
                        if (checkEntityBit(selectorUnitDescriptor->baSourceID, selectorUnitDescriptor->bNrInPins))
                        {
                            SetEntityBit(idMap, selectorUnitDescriptor->bUnitID);
                        }
                        TestAndClearEntityBit(unvisitedUnitMap, nextUnitID);
                        return status;
                    }
                }
            }
        }
        break;
        default:
            break;
        }
    }
    audioNodeKind = AudioNodeKind::Invalid;
    hasMoreData = false;
    return status;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::UpdateCurrentValue(
    const UCHAR entityID,
    const UCHAR controlSelector,
    const UCHAR /* controlNumber */
)
{
    NTSTATUS status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "%!FUNC! Entry");

    for (auto & genericAudioDescriptor : m_genericAudioDescriptorInfo)
    {
        switch (genericAudioDescriptor->bDescriptorSubtype)
        {
        case NS_USBAudio0200::OUTPUT_TERMINAL:
            if (((NS_USBAudio0200::PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor)->bTerminalID == entityID)
            {
                switch (controlSelector)
                {
                case NS_USBAudio0200::TE_CONNECTOR_CONTROL:
                    InterlockedSetEntityBit(m_outputConnectorUpdatedEntityBitmap, entityID);
                    break;
                default:
                    break;
                }
                return STATUS_SUCCESS;
            }
            break;
        case NS_USBAudio0200::INPUT_TERMINAL:
            if (((NS_USBAudio0200::PCS_AC_INPUT_TERMINAL_DESCRIPTOR)genericAudioDescriptor)->bTerminalID == entityID)
            {
                switch (controlSelector)
                {
                case NS_USBAudio0200::TE_CONNECTOR_CONTROL:
                    InterlockedSetEntityBit(m_inputConnectorUpdatedEntityBitmap, entityID);
                    break;
                default:
                    break;
                }
                return STATUS_SUCCESS;
            }
            break;
        case NS_USBAudio0200::FEATURE_UNIT:
            if (((NS_USBAudio0200::PCS_AC_FEATURE_UNIT_DESCRIPTOR)genericAudioDescriptor)->bUnitID == entityID)
            {
                switch (controlSelector)
                {
                case NS_USBAudio0200::FU_MUTE_CONTROL:
                    InterlockedSetEntityBit(m_muteUpdatedEntityBitmap, entityID);
                    break;
                case NS_USBAudio0200::FU_VOLUME_CONTROL:
                    InterlockedSetEntityBit(m_volumeUpdatedEntityBitmap, entityID);
                    break;
                default:
                    break;
                }
                return STATUS_SUCCESS;
            }
            break;
            break;
        default:
        case NS_USBAudio0200::MIXER_UNIT:
        case NS_USBAudio0200::CLOCK_MULTIPLIER:
        case NS_USBAudio0200::CLOCK_SELECTOR:
        case NS_USBAudio0200::CLOCK_SOURCE:
        case NS_USBAudio0200::EXTENSION_UNIT:
        case NS_USBAudio0200::PROCESSING_UNIT:
        case NS_USBAudio0200::SAMPLE_RATE_CONVERTER:
        case NS_USBAudio0200::SELECTOR_UNIT:
            break;
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudio2ControlInterface::GetVolumeConfiguration(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           entityID,
    LONG &          minimum,
    LONG &          maximum,
    ULONG &         steppingDelta
)
{
    NTSTATUS                                                status = STATUS_INVALID_PARAMETER;
    WDFMEMORY                                               memory = nullptr;
    NS_USBAudio0200::PCONTROL_RANGE_PARAMETER_BLOCK_LAYOUT2 parameterBlock = nullptr;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    //
    // Retrieve the range of the first valid channel.
    //
    for (auto & featureUnitDescriptor : m_acFeatureUnitInfo)
    {
        if (featureUnitDescriptor->bUnitID == entityID)
        {
            UCHAR numOfChannels = (featureUnitDescriptor->bLength - offsetof(NS_USBAudio0200::CS_AC_FEATURE_UNIT_DESCRIPTOR, ch)) / (sizeof(NS_USBAudio0200::CS_AC_FEATURE_UNIT_DESCRIPTOR::ch[0]));
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - feature unit numOfChannels %u", numOfChannels);
            for (UCHAR ch = 0; ch < numOfChannels; ch++)
            {
                if (ConvertBmaControls(featureUnitDescriptor->ch[ch].bmaControls) & NS_USBAudio0200::FEATURE_UNIT_BMA_VOLUME_CONTROL_MASK)
                {
                    status = ControlRequestGetVolumeRange(deviceContext, GetInterfaceNumber(), entityID, ch, memory, parameterBlock);
                    if (NT_SUCCESS(status))
                    {
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - volume range minimum %d (0x%x), maximum %d (0x%x), res %d (0x%x)", parameterBlock->subrange[0].wMIN, parameterBlock->subrange[0].wMIN, parameterBlock->subrange[0].wMAX, parameterBlock->subrange[0].wMAX, parameterBlock->subrange[0].wRES, parameterBlock->subrange[0].wRES);
                        if (parameterBlock->subrange[0].wMIN == 0x8000)
                        {
                            minimum = LONG_MIN;
                        }
                        else
                        {
                            minimum = (parameterBlock->subrange[0].wMIN * 0x10000) >> 8;
                        }
                        if (parameterBlock->subrange[0].wMAX == 0x8000)
                        {
                            maximum = LONG_MIN;
                        }
                        else
                        {
                            maximum = (parameterBlock->subrange[0].wMAX * 0x10000) >> 8;
                        }
                        steppingDelta = (ULONG)parameterBlock->subrange[0].wRES * 0x100; // 1/256 dB -> 1/65536
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
bool USBAudio2ControlInterface::InterlockedIsEntityUpdated(
    ULONG bitmap[8]
)
{
    PAGED_CODE();

    return (InterlockedCompareExchange((volatile LONG *)&(bitmap[0]), 0, 0) |
            InterlockedCompareExchange((volatile LONG *)&(bitmap[1]), 0, 0) |
            InterlockedCompareExchange((volatile LONG *)&(bitmap[2]), 0, 0) |
            InterlockedCompareExchange((volatile LONG *)&(bitmap[3]), 0, 0) |
            InterlockedCompareExchange((volatile LONG *)&(bitmap[4]), 0, 0) |
            InterlockedCompareExchange((volatile LONG *)&(bitmap[5]), 0, 0) |
            InterlockedCompareExchange((volatile LONG *)&(bitmap[6]), 0, 0) |
            InterlockedCompareExchange((volatile LONG *)&(bitmap[7]), 0, 0))
               ? true
               : false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio2ControlInterface::IsVolumeEntityUpdated()
{
    PAGED_CODE();

    return InterlockedIsEntityUpdated(m_volumeUpdatedEntityBitmap);
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio2ControlInterface::IsMuteEntityUpdated()
{
    PAGED_CODE();

    return InterlockedIsEntityUpdated(m_muteUpdatedEntityBitmap);
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio2ControlInterface::IsInputConnectorEntityUpdated()
{
    PAGED_CODE();

    return InterlockedIsEntityUpdated(m_inputConnectorUpdatedEntityBitmap);
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio2ControlInterface::IsOutputConnectorEntityUpdated()
{
    PAGED_CODE();

    return InterlockedIsEntityUpdated(m_outputConnectorUpdatedEntityBitmap);
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio2ControlInterface::InterlockedGetUpdatedEntity(
    ULONG   bitmap[8],
    UCHAR & entityID
)
{
    PAGED_CODE();

    entityID = 0;
    for (ULONG arrayIndex = 0; arrayIndex < (sizeof(bitmap) / sizeof(bitmap[0])); arrayIndex++)
    {
        if (InterlockedCompareExchange((volatile LONG *)&(bitmap[arrayIndex]), 0, 0))
        {
            for (ULONG index = 0; index < 32; index++)
            {
                entityID = (UCHAR)(index + arrayIndex * 32);
                if (InterlockedTestAndClearEntityBit(bitmap, entityID))
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
NTSTATUS USBAudio2ControlInterface::ValidateChannelNamesStringDescriptor(
    PDEVICE_CONTEXT deviceContext,
    UCHAR &         index
)
{
    PAGED_CODE();

    if (index != USBAudioConfiguration::InvalidString)
    {
        WDFMEMORY stringMemory = nullptr;
        USHORT *  string = nullptr;
        if (!NT_SUCCESS(USBAudioConfiguration::GetStringDescriptor(deviceContext->UsbDevice, index, LANGID_EN_US, stringMemory, string)))
        {
            index = USBAudioConfiguration::InvalidString;
        }
        else
        {
            WdfObjectDelete(stringMemory);
            stringMemory = nullptr;
            string = nullptr;
        }
    }
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::ValidateFeatureUnitControl(
    _In_ UCHAR entityID,
    _In_ UCHAR channel,
    _In_ ULONG controlMap
)
{
    NTSTATUS status = STATUS_INVALID_PARAMETER;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    for (auto & featureUnitDescriptor : m_acFeatureUnitInfo)
    {
        if (featureUnitDescriptor->bUnitID == entityID)
        {
            UCHAR numOfChannels = (featureUnitDescriptor->bLength - offsetof(NS_USBAudio0200::CS_AC_FEATURE_UNIT_DESCRIPTOR, ch)) / (sizeof(NS_USBAudio0200::CS_AC_FEATURE_UNIT_DESCRIPTOR::ch[0]));
            if (channel < numOfChannels)
            {
                if (ConvertBmaControls(featureUnitDescriptor->ch[channel].bmaControls) & controlMap)
                {
                    status = STATUS_SUCCESS;
                }
                else
                {
                    status = STATUS_NOT_SUPPORTED;
                }
            }
            else
            {
                status = STATUS_INVALID_PARAMETER;
            }
            break;
        }
    }

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio2ControlInterface::GetUpdatedVolumeEntity(
    UCHAR & entityID
)
{
    PAGED_CODE();

    return InterlockedGetUpdatedEntity(m_volumeUpdatedEntityBitmap, entityID);
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio2ControlInterface::GetUpdatedMuteEntity(
    UCHAR & entityID
)
{
    PAGED_CODE();

    return InterlockedGetUpdatedEntity(m_muteUpdatedEntityBitmap, entityID);
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio2ControlInterface::GetUpdatedInputConnectorEntity(
    UCHAR & entityID
)
{
    PAGED_CODE();

    return InterlockedGetUpdatedEntity(m_inputConnectorUpdatedEntityBitmap, entityID);
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudio2ControlInterface::GetUpdatedOutputConnectorEntity(
    UCHAR & entityID
)
{
    PAGED_CODE();

    return InterlockedGetUpdatedEntity(m_outputConnectorUpdatedEntityBitmap, entityID);
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::ValidateVolumeControl(
    _In_ UCHAR entityID,
    _In_ UCHAR channel
)
{
    PAGED_CODE();

    return ValidateFeatureUnitControl(entityID, channel, NS_USBAudio0200::FEATURE_UNIT_BMA_VOLUME_CONTROL_MASK);
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SetCurrentVolume(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           entityID,
    UCHAR           channel,
    LONG            volume
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    NTSTATUS status = ValidateVolumeControl(entityID, channel);

    if (NT_SUCCESS(status))
    {
        volume >>= 8;
        status = ControlRequestSetVolume(deviceContext, GetInterfaceNumber(), entityID, channel, (SHORT)volume);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit, %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::GetCurrentVolume(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           entityID,
    UCHAR           channel,
    LONG &          volume
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    NTSTATUS status = ValidateVolumeControl(entityID, channel);

    if (NT_SUCCESS(status))
    {
        USHORT currentVolume = 0;
        status = ControlRequestGetVolume(deviceContext, GetInterfaceNumber(), entityID, channel, currentVolume);

        if (NT_SUCCESS(status))
        {
            if ((ULONG)currentVolume == 0x8000)
            {
                volume = LONG_MIN;
            }
            else
            {
                volume = ((LONG)((SHORT)currentVolume) * 0x10000) >> 8;
            }
        }
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit, %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::ValidateMuteControl(
    _In_ UCHAR entityID,
    _In_ UCHAR channel
)
{
    PAGED_CODE();

    return ValidateFeatureUnitControl(entityID, channel, NS_USBAudio0200::FEATURE_UNIT_BMA_MUTE_CONTROL_MASK);
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::SetCurrentMute(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           entityID,
    UCHAR           channel,
    bool            mute
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    NTSTATUS status = ValidateMuteControl(entityID, channel);

    if (NT_SUCCESS(status))
    {
        status = ControlRequestSetMute(deviceContext, GetInterfaceNumber(), entityID, channel, mute);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit, %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::GetCurrentMute(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           entityID,
    UCHAR           channel,
    bool &          mute
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    NTSTATUS status = ValidateMuteControl(entityID, channel);

    if (NT_SUCCESS(status))
    {
        status = ControlRequestGetMute(deviceContext, GetInterfaceNumber(), entityID, channel, mute);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit, %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudio2ControlInterface::GetCurrentConnectorState(
    PDEVICE_CONTEXT                                 deviceContext,
    UCHAR                                           entityID,
    NS_USBAudio::AUDIO_CHANNEL_CLUSTER_DESCRIPTOR & connectorState
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    NTSTATUS status = ControlRequestGetCurrentConnectorState(deviceContext, GetInterfaceNumber(), entityID, connectorState);

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - entity id = 0x%02x, channels %u, bmChannelConfig 0x%08x", entityID, connectorState.bNrChannels, connectorState.bmChannelConfig);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit , %!STATUS!", status);

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

        if (descriptor->bLength != NS_USBAudio0200::SIZE_OF_CS_AS_TYPE_I_FORMAT_TYPE_DESCRIPTOR)
        {
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_DESCRIPTOR, "The length of a format type I descriptor should be %d, but this device was %d.", NS_USBAudio0200::SIZE_OF_CS_AS_TYPE_I_FORMAT_TYPE_DESCRIPTOR, descriptor->bLength);
        }

        //
        // The length of a format type I descriptor is 6, but some devices report a larger length when read, so those devices are supported."
        //
        RETURN_NTSTATUS_IF_TRUE(descriptor->bLength < NS_USBAudio0200::SIZE_OF_CS_AS_TYPE_I_FORMAT_TYPE_DESCRIPTOR, STATUS_DEVICE_DATA_ERROR);

        m_formatITypeDescriptor = formatITypeDescriptor;
        m_enableGetFormatType = false;

        //
        // If multiple formats are supported, allow obtaining the format type via a Control Request.
        //
        if (m_csAsInterfaceDescriptor != nullptr)
        {
            ULONG formats = USBAudioDataFormat::ConvertBmFormats(m_csAsInterfaceDescriptor->bmFormats);
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
        RETURN_NTSTATUS_IF_TRUE(descriptor->bLength != NS_USBAudio0200::SIZE_OF_CS_AS_TYPE_III_FORMAT_TYPE_DESCRIPTOR, STATUS_DEVICE_DATA_ERROR);
        m_enableGetFormatType = false;

        m_formatIIITypeDescriptor = formatIIITypeDescriptor;
        //
        // If multiple formats are supported, allow obtaining the format type via a Control Request.
        //
        if (m_csAsInterfaceDescriptor != nullptr)
        {
            ULONG formats = USBAudioDataFormat::ConvertBmFormats(m_csAsInterfaceDescriptor->bmFormats);
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
    RETURN_NTSTATUS_IF_TRUE(descriptor->bLength != NS_USBAudio0200::SIZE_OF_CS_AS_INTERFACE_DESCRIPTOR, STATUS_DEVICE_DATA_ERROR);

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
    RETURN_NTSTATUS_IF_TRUE(descriptor->bLength != NS_USBAudio0200::SIZE_OF_CS_AS_ISOCHRONOUS_AUDIO_DATA_ENDPOINT_DESCRIPTOR, STATUS_DEVICE_DATA_ERROR);

    if (m_isochronousAudioDataEndpointDescriptor != nullptr)
    {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_DESCRIPTOR, "CS isochronous audio data endpoint descriptor is already set.");
    }

    NS_USBAudio0200::PCS_AS_ISOCHRONOUS_AUDIO_DATA_ENDPOINT_DESCRIPTOR isochronousAudioDataEndpointDescriptor = (NS_USBAudio0200::PCS_AS_ISOCHRONOUS_AUDIO_DATA_ENDPOINT_DESCRIPTOR)descriptor;

    if (isochronousAudioDataEndpointDescriptor->bLockDelayUnits == NS_USBAudio0200::LOCK_DELAY_UNIT_MILLISECONDS)
    {
        m_lockDelay = isochronousAudioDataEndpointDescriptor->wLockDelay;
    }

    if (isochronousAudioDataEndpointDescriptor->bLockDelayUnits > NS_USBAudio0200::LOCK_DELAY_UNIT_DECODED_PCM_SAMPLES)
    {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_DESCRIPTOR, "bLockDelayUnits is %u (Reserved per USB Audio Class specification). Ignoring bLockDelayUnits and wLockDelay.", isochronousAudioDataEndpointDescriptor->bLockDelayUnits);
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
UCHAR USBAudio2StreamInterface::GetIntervalForDirection(
    bool isInput
)
{
    UCHAR bInterval = 0;

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
                    if (isInput)
                    {
                        if (((endpointAttribute & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_TYPE_ISOCHRONOUS) && USB_ENDPOINT_DIRECTION_IN(endpointAddress) && (USB_ENDPOINT_TYPE_ISOCHRONOUS_USAGE(endpointAttribute) != USB_ENDPOINT_TYPE_ISOCHRONOUS_USAGE_FEEDBACK_ENDPOINT))
                        {
                            if (GetInterval(index, bInterval))
                            {
                                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - isInput = %!bool!, bInterval = 0x%x", isInput, bInterval);
                                return bInterval;
                            }
                        }
                    }
                    else
                    {
                        if (((endpointAttribute & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_TYPE_ISOCHRONOUS) && USB_ENDPOINT_DIRECTION_OUT(endpointAddress))
                        {
                            if (GetInterval(index, bInterval))
                            {
                                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - isInput = %!bool!, bInterval = 0x%x", isInput, bInterval);
                                return bInterval;
                            }
                        }
                    }
                }
            }
        }
    }

    return bInterval;
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
        m_audioDataFormat = USBAudioDataFormat::ConvertBmFormats(m_csAsInterfaceDescriptor->bmFormats);
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
        ULONG formats = USBAudioDataFormat::ConvertBmFormats(m_csAsInterfaceDescriptor->bmFormats);

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

    for (auto & usbAudioInterface : m_usbAudioAlternateInterfaces)
    {
        if (usbAudioInterface != nullptr)
        {
            delete usbAudioInterface;
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
    RETURN_NTSTATUS_IF_TRUE(!interface->IsStreamInterface(), STATUS_INVALID_PARAMETER);

    status = m_usbAudioAlternateInterfaces.Set(m_parentObject, interface->GetAlternateSetting(), (USBAudioStreamInterface *)interface);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
NTSTATUS USBAudioInterfaceInfo::GetInterfaceNumber(ULONG & interfaceNumber)
{
    for (auto & usbAudioInterface : m_usbAudioAlternateInterfaces)
    {
        if (usbAudioInterface != nullptr)
        {
            interfaceNumber = usbAudioInterface->GetInterfaceNumber();
            return STATUS_SUCCESS;
        }
    }

    return STATUS_NO_DATA_DETECTED;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioInterfaceInfo::IsStreamInterface()
{
    bool                      isStreamInterface = false;
    USBAudioStreamInterface * usbAudioInterface = nullptr;

    PAGED_CODE();

    if (NT_SUCCESS(m_usbAudioAlternateInterfaces.Get(0, usbAudioInterface)))
    {
        isStreamInterface = usbAudioInterface->IsStreamInterface();
    }
    return isStreamInterface;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
bool USBAudioInterfaceInfo::IsControlInterface()
{
    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudioInterfaceInfo::QueryCurrentAttributeAll(
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    for (auto & usbAudioInterface : m_usbAudioAlternateInterfaces)
    {
        if (usbAudioInterface != nullptr)
        {
            RETURN_NTSTATUS_IF_FAILED(usbAudioInterface->QueryCurrentAttributeAll(deviceContext));
        }
    }
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudioInterfaceInfo::SetDefaultAttributeAll(
    PDEVICE_CONTEXT /* deviceContext */
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

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

    USBAudioStreamInterface * usbAudioInterface = nullptr;
    if (NT_SUCCESS(m_usbAudioAlternateInterfaces.Get(0, usbAudioInterface)))
    {
        status = ((USBAudioStreamInterface *)usbAudioInterface)->CheckInterfaceConfiguration(deviceContext);
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

    for (auto & usbAudioInterface : m_usbAudioAlternateInterfaces)
    {
        if (usbAudioInterface != nullptr)
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

    for (auto & usbAudioInterface : m_usbAudioAlternateInterfaces)
    {
        if ((usbAudioInterface != nullptr) && ((USBAudioStreamInterface *)usbAudioInterface)->IsInterfaceSupportingFormats() && usbAudioInterface->IsSupportDirection(isInput) && ((USBAudioStreamInterface *)usbAudioInterface)->IsValidAudioDataFormat(desiredFormatType, desiredFormat))
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

    for (auto & usbAudioInterface : m_usbAudioAlternateInterfaces)
    {
        if ((usbAudioInterface != nullptr) && ((USBAudioStreamInterface *)usbAudioInterface)->IsInterfaceSupportingFormats() && usbAudioInterface->IsSupportDirection(isInput) && ((USBAudioStreamInterface *)usbAudioInterface)->IsValidAudioDataFormat(desiredFormatType, desiredFormat))
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

    USBAudioStreamInterface * usbAudioInterface = nullptr;
    ULONG                     numOfAlternateInterface = m_usbAudioAlternateInterfaces.GetNumOfArray();

    if (numOfAlternateInterface >= 2)
    {
        if (NT_SUCCESS(m_usbAudioAlternateInterfaces.Get(1, usbAudioInterface)))
        {
            return usbAudioInterface->IsSupportDirection(isInput);
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

    USBAudioStreamInterface * usbAudioInterface = nullptr;
    ULONG                     numOfAlternateInterface = m_usbAudioAlternateInterfaces.GetNumOfArray();

    if (numOfAlternateInterface >= 2)
    {
        if (NT_SUCCESS(m_usbAudioAlternateInterfaces.Get(1, usbAudioInterface)))
        {
            terminalLink = ((USBAudioStreamInterface *)usbAudioInterface)->GetCurrentTerminalLink();
            return true;
        }
    }

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudioInterfaceInfo::RegisterUSBAudioDataFormatManager(
    USBAudioDataFormatManager & usbAudioDataFormatManagerIn,
    USBAudioDataFormatManager & usbAudioDataFormatManagerOut
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    for (auto usbAudioInterface : m_usbAudioAlternateInterfaces)
    {
        usbAudioInterface->RegisterUSBAudioDataFormatManager(usbAudioDataFormatManagerIn, usbAudioDataFormatManagerOut);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioInterfaceInfo::HasInputIsochronousEndpoint()
{
    PAGED_CODE();

    for (auto usbAudioInterface : m_usbAudioAlternateInterfaces)
    {
        if (usbAudioInterface->HasInputIsochronousEndpoint())
        {
            return true;
        }
    }

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioInterfaceInfo::HasOutputIsochronousEndpoint()
{
    PAGED_CODE();

    for (auto usbAudioInterface : m_usbAudioAlternateInterfaces)
    {
        if (usbAudioInterface->HasOutputIsochronousEndpoint())
        {
            return true;
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

    ULONG maxPacketSize = 0;
    ULONG numOfAlternateInterface = m_usbAudioAlternateInterfaces.GetNumOfArray();

    GetMaxPacketSize(isInput ? IsoDirection::In : IsoDirection::Out, maxPacketSize);

    //
    // A range-based for loop is not used because the index value is required.
    //
    for (ULONG index = 0; index < numOfAlternateInterface; index++)
    {
        USBAudioStreamInterface * usbAudioInterface = nullptr;
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
                        if (currentSettings.Channels < usbAudioStreamInterface->GetCurrentChannels())
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
                            currentSettings.Interval = usbAudioStreamInterface->GetIntervalForDirection(isInput);
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
                    }
                    currentSettings.IsDeviceAdaptive = usbAudioStreamInterface->IsEndpointTypeIsochronousSynchronizationSupported(USB_ENDPOINT_TYPE_ISOCHRONOUS_SYNCHRONIZATION_ADAPTIVE);
                    currentSettings.IsDeviceSynchronous = usbAudioStreamInterface->IsEndpointTypeIsochronousSynchronizationSupported(USB_ENDPOINT_TYPE_ISOCHRONOUS_SYNCHRONIZATION_SYNCHRONOUS);
                }
            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
void USBAudioInterfaceInfo::Dump()
{
    PAGED_CODE();

    for (auto usbAudioInterface : m_usbAudioAlternateInterfaces)
    {
        usbAudioInterface->Dump();
    }
}

// ======================================================================
_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioStreamInterfaceGroup * USBAudioStreamInterfaceGroup::Create(
    PDEVICE_CONTEXT            deviceContext,
    ULONG                      groupIndex,
    USBAudioControlInterface * usbAudioControlInterface,
    bool                       isDeviceSplittable
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry, %u, %!bool!", groupIndex, isDeviceSplittable);

    USBAudioStreamInterfaceGroup * usbAudioStreamInterfaceGroup = new (POOL_FLAG_NON_PAGED, DRIVER_TAG) USBAudioStreamInterfaceGroup(deviceContext, groupIndex, usbAudioControlInterface, isDeviceSplittable);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");

    return usbAudioStreamInterfaceGroup;
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioStreamInterfaceGroup::USBAudioStreamInterfaceGroup(
    PDEVICE_CONTEXT            deviceContext,
    ULONG                      groupIndex,
    USBAudioControlInterface * usbAudioControlInterface,
    bool                       isDeviceSplittable
)
    : m_deviceContext(deviceContext), m_usbAudioControlInterface(usbAudioControlInterface), m_isDeviceSplittable(isDeviceSplittable)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    m_targetClockSourceID = m_clockSourceID = USBAudioConfiguration::InvalidID;
    m_groupIndex = groupIndex;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioStreamInterfaceGroup::~USBAudioStreamInterfaceGroup()
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudioStreamInterfaceGroup::Append(
    USBAudioInterfaceInfo * usbAudioInterfaceInfo
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_FAILED(m_usbAudioStreamInterfaceInfoes.Append(m_deviceContext->UsbDevice, usbAudioInterfaceInfo));

    RETURN_NTSTATUS_IF_FAILED(usbAudioInterfaceInfo->RegisterUSBAudioDataFormatManager(m_inputUsbAudioDataFormatManager, m_outputUsbAudioDataFormatManager));

    if (usbAudioInterfaceInfo->HasInputIsochronousEndpoint())
    {
        m_isInputIsochronousInterfaceExists = true;
    }

    if (usbAudioInterfaceInfo->HasOutputIsochronousEndpoint())
    {
        m_isOutputIsochronousInterfaceExists = true;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
USBAudioStreamInterfaceGroup::SetCurrentSampleFrequency(
    ULONG desiredSampleRate
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    ASSERT(m_usbAudioControlInterface != nullptr);
    if (m_usbAudioControlInterface != nullptr)
    {
        RETURN_NTSTATUS_IF_FAILED(m_usbAudioControlInterface->SetCurrentSampleFrequency(m_deviceContext, m_targetClockSourceID, desiredSampleRate));
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
USBAudioStreamInterfaceGroup::GetCurrentSampleFrequency(
    ULONG & sampleRate
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    sampleRate = 0;
    ASSERT(m_usbAudioControlInterface != nullptr);
    if (m_usbAudioControlInterface != nullptr)
    {
        RETURN_NTSTATUS_IF_FAILED(m_usbAudioControlInterface->GetCurrentSampleFrequency(m_deviceContext, m_targetClockSourceID, sampleRate));
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioStreamInterfaceGroup::CanSetSampleFrequency()
{
    PAGED_CODE();

    ASSERT(m_usbAudioControlInterface != nullptr);
    if (m_usbAudioControlInterface != nullptr)
    {
        return m_usbAudioControlInterface->CanSetSampleFrequency(m_targetClockSourceID);
    }

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudioStreamInterfaceGroup::SelectAlternateInterface(
    bool                        isInput,
    AUDIO_STREAM_PROPERTY_SET & audioStreamPropertySet,
    ULONG                       desiredFormatType,
    ULONG                       desiredFormat,
    ULONG                       desiredBytesPerSample,
    ULONG                       desiredValidBitsPerSample
)
{
    NTSTATUS         status = STATUS_SUCCESS;
    CURRENT_SETTINGS currentSettings{};

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - %!bool! format type %u, format %u, bytes per sample %u, valid bits per sample %u", isInput, desiredFormatType, desiredFormat, desiredBytesPerSample, desiredValidBitsPerSample);

    // TBD
    // When multiple interfaces share the same direction, only the first encountered interface is processed.
    //
    for (auto usbAudioInterfaceInfo : m_usbAudioStreamInterfaceInfoes)
    {
        if (usbAudioInterfaceInfo != nullptr)
        {
            status = usbAudioInterfaceInfo->SelectAlternateInterface(m_deviceContext, isInput, desiredFormatType, desiredFormat, desiredBytesPerSample, desiredValidBitsPerSample, currentSettings);
        }
    }

    //
    // Even if iChannelNames is set, if the string descriptor is an internal device, iChannelNames is invalid.
    //
    if (currentSettings.ChannelNames != USBAudioConfiguration::InvalidString)
    {
        WDFMEMORY channelNameMemory = nullptr;
        USHORT *  channelName = nullptr;
        if (!NT_SUCCESS(USBAudioConfiguration::GetStringDescriptor(m_deviceContext->UsbDevice, 0, LANGID_EN_US, channelNameMemory, channelName)))
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
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - current bytes per sample %u, %u", currentSettings.BytesPerSample, audioStreamPropertySet.InputProperty.BytesPerSample);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - current valid bits per sample %u, %u", currentSettings.ValidBitsPerSample, audioStreamPropertySet.InputProperty.ValidBitsPerSample);
        audioStreamPropertySet.InputProperty.IsoPacketSize = currentSettings.MaxPacketSize;
        audioStreamPropertySet.InputProperty.LockDelay = currentSettings.LockDelay;
        audioStreamPropertySet.InputProperty.InterfaceNumber = currentSettings.InterfaceNumber;
        audioStreamPropertySet.InputProperty.AlternateSetting = currentSettings.AlternateSetting;
        audioStreamPropertySet.InputProperty.EndpointNumber = currentSettings.EndpointAddress;
        audioStreamPropertySet.InputProperty.BytesPerBlock = currentSettings.Channels * currentSettings.BytesPerSample;
        audioStreamPropertySet.InputProperty.MaxSamplesPerPacket = currentSettings.MaxFramesPerPacket;
        audioStreamPropertySet.InputProperty.FormatType = desiredFormatType;
        audioStreamPropertySet.InputProperty.Format = desiredFormat;
        audioStreamPropertySet.InputProperty.BytesPerSample = currentSettings.BytesPerSample;
        audioStreamPropertySet.InputProperty.ValidBitsPerSample = currentSettings.ValidBitsPerSample;
        audioStreamPropertySet.InputProperty.PacketsPerSec = m_deviceContext->FramesPerMs * 1000;
        if (currentSettings.Interval > 0)
        {
            audioStreamPropertySet.InputProperty.PacketsPerSec >>= (currentSettings.Interval - 1);
        }
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - InputPacketsPerSec %u, %u", audioStreamPropertySet.InputProperty.PacketsPerSec, currentSettings.Interval);
        audioStreamPropertySet.InputProperty.UsbChannels = currentSettings.Channels;
        audioStreamPropertySet.InputProperty.ChannelNames = currentSettings.ChannelNames;
    }
    else
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - current bytes per sample %u, %u", currentSettings.BytesPerSample, audioStreamPropertySet.OutputProperty.BytesPerSample);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - current valid bits per sample %u, %u", currentSettings.ValidBitsPerSample, audioStreamPropertySet.OutputProperty.ValidBitsPerSample);
        audioStreamPropertySet.OutputProperty.IsoPacketSize = currentSettings.MaxPacketSize;
        audioStreamPropertySet.OutputProperty.LockDelay = currentSettings.LockDelay;
        audioStreamPropertySet.OutputProperty.InterfaceNumber = currentSettings.InterfaceNumber;
        audioStreamPropertySet.OutputProperty.AlternateSetting = currentSettings.AlternateSetting;
        audioStreamPropertySet.OutputProperty.EndpointNumber = currentSettings.EndpointAddress;
        audioStreamPropertySet.OutputProperty.BytesPerBlock = currentSettings.Channels * currentSettings.BytesPerSample;
        audioStreamPropertySet.OutputProperty.MaxSamplesPerPacket = currentSettings.MaxFramesPerPacket;
        audioStreamPropertySet.OutputProperty.FormatType = desiredFormatType;
        audioStreamPropertySet.OutputProperty.Format = desiredFormat;
        audioStreamPropertySet.OutputProperty.BytesPerSample = currentSettings.BytesPerSample;
        audioStreamPropertySet.OutputProperty.ValidBitsPerSample = currentSettings.ValidBitsPerSample;
        audioStreamPropertySet.OutputProperty.PacketsPerSec = m_deviceContext->FramesPerMs * 1000;
        if (currentSettings.Interval > 0)
        {
            audioStreamPropertySet.OutputProperty.PacketsPerSec >>= (currentSettings.Interval - 1);
        }
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - OutputPacketsPerSec %u, %u", audioStreamPropertySet.OutputProperty.PacketsPerSec, currentSettings.Interval);
        audioStreamPropertySet.IsDeviceAdaptive = currentSettings.IsDeviceAdaptive;
        audioStreamPropertySet.IsDeviceSynchronous = currentSettings.IsDeviceSynchronous;
        audioStreamPropertySet.OutputProperty.UsbChannels = currentSettings.Channels;
        audioStreamPropertySet.OutputProperty.ChannelNames = currentSettings.ChannelNames;
    }
    if (currentSettings.FeedbackInterfaceNumber != 0)
    {
        audioStreamPropertySet.FeedbackProperty.FeedbackInterfaceNumber = currentSettings.FeedbackInterfaceNumber;
        audioStreamPropertySet.FeedbackProperty.FeedbackAlternateSetting = currentSettings.FeedbackAlternateSetting;
        audioStreamPropertySet.FeedbackProperty.FeedbackEndpointNumber = currentSettings.FeedbackEndpointAddress;
        audioStreamPropertySet.FeedbackProperty.FeedbackInterval = currentSettings.FeedbackInterval;
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
NTSTATUS USBAudioStreamInterfaceGroup::ActivateAudioInterface(
    AUDIO_STREAM_PROPERTY_SET & audioStreamPropertySet,
    ULONG                       desiredSampleRate,
    ULONG                       desiredFormatType,
    ULONG                       desiredFormat,
    ULONG                       inputDesiredBytesPerSample,
    ULONG                       inputDesiredValidBitsPerSample,
    ULONG                       outputDesiredBytesPerSample,
    ULONG                       outputDesiredValidBitsPerSample,
    bool                        forceSetSampleRate
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

    status = STATUS_INVALID_PARAMETER;
    for (ULONG frameRateListIndex = 0, sampleRateMask = 1; frameRateListIndex < c_SampleRateCount; ++frameRateListIndex, sampleRateMask <<= 1)
    {
        if ((audioStreamPropertySet.AudioProperty.SupportedSampleRate & sampleRateMask) && (desiredSampleRate == c_SampleRateList[frameRateListIndex]))
        {
            status = STATUS_SUCCESS;
            break;
        }
    }
    RETURN_NTSTATUS_IF_FAILED(status);

    // Set the desiredSampleRate for the device.
    RETURN_NTSTATUS_IF_FAILED(GetCurrentSampleFrequency(sampleRate));

    if (((sampleRate != desiredSampleRate) || (forceSetSampleRate)) && CanSetSampleFrequency())
    {
        // Ignore the return value since some devices may fail to set the sample rate.
        status = SetCurrentSampleFrequency(desiredSampleRate);
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
    RETURN_NTSTATUS_IF_FAILED(SelectAlternateInterface(true, audioStreamPropertySet, desiredFormatType, desiredFormat, inputDesiredBytesPerSample, inputDesiredValidBitsPerSample));

    // Determines the output interface and alternate settings.
    RETURN_NTSTATUS_IF_FAILED(SelectAlternateInterface(false, audioStreamPropertySet, desiredFormatType, desiredFormat, outputDesiredBytesPerSample, outputDesiredValidBitsPerSample));

    audioStreamPropertySet.AudioProperty.SampleRate = sampleRate;

    audioStreamPropertySet.InputProperty.SamplesPerPacket = 1;
    audioStreamPropertySet.OutputProperty.SamplesPerPacket = 1;
    if (audioStreamPropertySet.InputProperty.PacketsPerSec != 0)
    {
        audioStreamPropertySet.InputProperty.SamplesPerPacket = audioStreamPropertySet.AudioProperty.SampleRate / audioStreamPropertySet.InputProperty.PacketsPerSec;
    }
    if (audioStreamPropertySet.OutputProperty.PacketsPerSec != 0)
    {
        audioStreamPropertySet.OutputProperty.SamplesPerPacket = audioStreamPropertySet.AudioProperty.SampleRate / audioStreamPropertySet.OutputProperty.PacketsPerSec;
    }
    audioStreamPropertySet.DesiredSampleFormat = USBAudioDataFormat::ConvertFormatToSampleFormat(desiredFormatType, desiredFormat);
    audioStreamPropertySet.AudioProperty.CurrentSampleFormat = audioStreamPropertySet.DesiredSampleFormat;
    audioStreamPropertySet.AudioProperty.SampleType = USBAudioDataFormat::ConverSampleFormatToSampleType(audioStreamPropertySet.AudioProperty.CurrentSampleFormat, max(audioStreamPropertySet.InputProperty.BytesPerSample, audioStreamPropertySet.OutputProperty.BytesPerSample), max(audioStreamPropertySet.InputProperty.ValidBitsPerSample, audioStreamPropertySet.OutputProperty.ValidBitsPerSample));

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudioStreamInterfaceGroup::QueryRangeAttributeAll(
    AUDIO_STREAM_PROPERTY_SET & audioStreamPropertySet
)
{
    PAGED_CODE();

    m_targetClockSourceID = m_clockSourceID;

    RETURN_NTSTATUS_IF_FAILED(m_usbAudioControlInterface->SetCurrentClockSourceInternal(m_deviceContext, m_targetClockSourceID));

    RETURN_NTSTATUS_IF_FAILED(m_usbAudioControlInterface->GetCurrentClockSourceID(m_deviceContext, m_targetClockSourceID));

    //  CS_SAM_FREQ_CONTROL ranges
    RETURN_NTSTATUS_IF_FAILED(m_usbAudioControlInterface->GetRangeSampleFrequency(m_deviceContext, m_targetClockSourceID, audioStreamPropertySet.AudioProperty.SupportedSampleRate));

    audioStreamPropertySet.AudioProperty.SupportedSampleFormats = GetSupportedSampleFormats();

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
PAGED_CODE_SEG
void USBAudioStreamInterfaceGroup::SetClockSourceID(
    UCHAR clockSourceID
)
{
    PAGED_CODE();

    m_clockSourceID = clockSourceID;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR USBAudioStreamInterfaceGroup::GetClockSourceID()
{
    PAGED_CODE();

    return m_clockSourceID;
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioDataFormatManager *
USBAudioStreamInterfaceGroup::GetUSBAudioDataFormatManager(
    bool isInput
)
{
    PAGED_CODE();

    return (isInput) ? &m_inputUsbAudioDataFormatManager : &m_outputUsbAudioDataFormatManager;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
bool USBAudioStreamInterfaceGroup::HasInputIsochronousInterface() const
{
    return m_isInputIsochronousInterfaceExists;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
bool USBAudioStreamInterfaceGroup::HasOutputIsochronousInterface() const
{
    return m_isOutputIsochronousInterfaceExists;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
bool USBAudioStreamInterfaceGroup::HasInputAndOutputIsochronousInterfaces() const
{
    return HasInputIsochronousInterface() && HasOutputIsochronousInterface();
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioStreamInterfaceGroup::GetCurrentTerminalLink(
    bool                              isInput,
    const AUDIO_STREAM_PROPERTY_SET & audioStreamPropertySet,
    UCHAR &                           terminalLink
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    terminalLink = USBAudioConfiguration::InvalidID;

    for (auto usbAudioInterfaceInfo : m_usbAudioStreamInterfaceInfoes)
    {
        if ((usbAudioInterfaceInfo != nullptr) && usbAudioInterfaceInfo->IsSupportDirection(isInput))
        {
            ULONG interfaceNumber = 0;
            status = usbAudioInterfaceInfo->GetInterfaceNumber(interfaceNumber);

            if (isInput)
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, input interface number %u", interfaceNumber, audioStreamPropertySet.InputProperty.InterfaceNumber);
            }
            else
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - interface %u, output interface number %u", interfaceNumber, audioStreamPropertySet.OutputProperty.InterfaceNumber);
            }

            if (NT_SUCCESS(status) && ((isInput && (interfaceNumber == audioStreamPropertySet.InputProperty.InterfaceNumber)) || (!isInput && (interfaceNumber == audioStreamPropertySet.OutputProperty.InterfaceNumber))))
            {
                // Gets the terminal link defined in the Class-Specific AS Interface Descriptor.
                if (usbAudioInterfaceInfo->GetTerminalLink(terminalLink))
                {
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - terminal link 0x%02x", terminalLink);
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
USBAudioStreamInterfaceGroup::GetStreamChannelInfo(
    bool                              isInput,
    const AUDIO_STREAM_PROPERTY_SET & audioStreamPropertySet,
    UCHAR &                           numOfChannels,
    USHORT &                          terminalType,
    UCHAR &                           terminalID,
    UCHAR &                           volumeUnitID,
    UCHAR &                           muteUnitID
)
{
    NTSTATUS status = STATUS_SUCCESS;
    UCHAR    terminalLink = USBAudioConfiguration::InvalidID;

    PAGED_CODE();

    numOfChannels = 0;
    terminalID = USBAudioConfiguration::InvalidID;
    volumeUnitID = USBAudioConfiguration::InvalidID;
    muteUnitID = USBAudioConfiguration::InvalidID;

    RETURN_NTSTATUS_IF_FAILED(GetCurrentTerminalLink(isInput, audioStreamPropertySet, terminalLink));

    if (terminalLink != USBAudioConfiguration::InvalidID)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - terminal link 0x%02x", terminalLink);
        if (isInput)
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - terminal link 0x%02x", terminalLink);
            status = m_usbAudioControlInterface->SearchInputTerminalFromOutputTerminal(m_deviceContext, terminalLink, numOfChannels, terminalType, terminalID, volumeUnitID, muteUnitID);
        }
        else
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - terminal link 0x%02x", terminalLink);
            status = m_usbAudioControlInterface->SearchOutputTerminalFromInputTerminal(m_deviceContext, terminalLink, numOfChannels, terminalType, terminalID, volumeUnitID, muteUnitID);
        }
    }

    if ((terminalLink == USBAudioConfiguration::InvalidID) || !NT_SUCCESS(status) || (numOfChannels == 0))
    {
        //
        // The topology link is broken or the topology could not be analyzed,
        // so the number of channels in the Class-Specific AS Interface
        // Descriptor of the Stream Interface is used.
        //
        if ((isInput && HasInputIsochronousInterface()) || (!isInput && HasOutputIsochronousInterface()))
        {
            if (numOfChannels == 0)
            {
                TraceEvents(TRACE_LEVEL_WARNING, TRACE_DESCRIPTOR, "The number of channels listed in the terminal is 0. terminal link 0x%02x, %!STATUS!", terminalLink, status);
            }
            else
            {
                TraceEvents(TRACE_LEVEL_WARNING, TRACE_DESCRIPTOR, "The topology link is broken or the topology could not be analyzed. terminal link 0x%02x, %!STATUS!", terminalLink, status);
            }
        }
        status = STATUS_SUCCESS;
        if (isInput)
        {
            numOfChannels = static_cast<UCHAR>(audioStreamPropertySet.InputProperty.UsbChannels);
            if (terminalLink == USBAudioConfiguration::InvalidID)
            {
                terminalType = NS_USBAudio0200::LINE_CONNECTOR;
            }
        }
        else
        {
            numOfChannels = static_cast<UCHAR>(audioStreamPropertySet.OutputProperty.UsbChannels);
            if (terminalLink == USBAudioConfiguration::InvalidID)
            {
                terminalType = NS_USBAudio0200::LINE_CONNECTOR;
            }
        }
        volumeUnitID = muteUnitID = USBAudioConfiguration::InvalidID;
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - %!bool! %u channels, terminal type 0x%x, volumeUnitID 0x%02x, muteUnitID 0x%02x", isInput, numOfChannels, terminalType, volumeUnitID, muteUnitID);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioStreamInterfaceGroup::GetStreamChannelInfoAdjusted(
    bool                              isInput,
    const AUDIO_STREAM_PROPERTY_SET & audioStreamPropertySet,
    UCHAR &                           numOfChannels,
    USHORT &                          terminalType,
    UCHAR &                           terminalID,
    UCHAR &                           volumeUnitID,
    UCHAR &                           muteUnitID
)
{
    PAGED_CODE();

    RETURN_NTSTATUS_IF_FAILED(GetStreamChannelInfo(isInput, audioStreamPropertySet, numOfChannels, terminalType, terminalID, volumeUnitID, muteUnitID));

    if (numOfChannels == 0)
    {
        numOfChannels = 1;
    }

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioStreamInterfaceGroup::GetStreamDevices(
    bool                              isInput,
    const AUDIO_STREAM_PROPERTY_SET & audioStreamPropertySet,
    ULONG &                           numOfDevices
)
{
    UCHAR  numOfChannels = 0;
    USHORT terminalType;
    UCHAR  terminalID;
    UCHAR  volumeUnitID;
    UCHAR  muteUnitID;

    PAGED_CODE();

    RETURN_NTSTATUS_IF_FAILED(GetStreamChannelInfo(isInput, audioStreamPropertySet, numOfChannels, terminalType, terminalID, volumeUnitID, muteUnitID));

    if (m_isDeviceSplittable)
    {
        numOfDevices = (numOfChannels / 2) + (numOfChannels % 2); // stereo or stereo + mono
    }
    else
    {
        numOfDevices = 1;
    }

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioStreamInterfaceGroup::GetStreamDevicesAdjusted(
    bool                              isInput,
    const AUDIO_STREAM_PROPERTY_SET & audioStreamPropertySet,
    ULONG &                           numOfDevices
)
{
    PAGED_CODE();

    RETURN_NTSTATUS_IF_FAILED(GetStreamDevices(isInput, audioStreamPropertySet, numOfDevices));
    if (numOfDevices == 0)
    {
        numOfDevices = 1;
    }

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioStreamInterfaceGroup::GetStreamChannels(
    bool                              isInput,
    const AUDIO_STREAM_PROPERTY_SET & audioStreamPropertySet,
    UCHAR &                           numOfChannels
)
{
    USHORT terminalType;
    UCHAR  terminalID;
    UCHAR  volumeUnitID;
    UCHAR  muteUnitID;

    PAGED_CODE();

    RETURN_NTSTATUS_IF_FAILED(GetStreamChannelInfo(isInput, audioStreamPropertySet, numOfChannels, terminalType, terminalID, volumeUnitID, muteUnitID));

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioStreamInterfaceGroup::GetInformationForHostPin(
    UCHAR   unitID,
    UCHAR & numOfChannels
)
{
    PAGED_CODE();

    return m_usbAudioControlInterface->GetInformationForHostPin(m_deviceContext, unitID, numOfChannels);
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioStreamInterfaceGroup::GetInformationForBridgePin(
    UCHAR                                           unitID,
    UCHAR &                                         numOfChannels,
    USHORT &                                        terminalType,
    UCHAR &                                         channelNames,
    NS_USBAudio::AUDIO_CHANNEL_CLUSTER_DESCRIPTOR & connectorState
)
{
    PAGED_CODE();

    return m_usbAudioControlInterface->GetInformationForBridgePin(m_deviceContext, unitID, numOfChannels, terminalType, channelNames, connectorState);
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudioStreamInterfaceGroup::GetInformationForVolumeElement(
    UCHAR   unitID,
    UCHAR & numOfChannels,
    LONG &  minimum,
    LONG &  maximum,
    ULONG & steppingDelta
)
{
    PAGED_CODE();

    return m_usbAudioControlInterface->GetInformationForVolumeElement(m_deviceContext, unitID, numOfChannels, minimum, maximum, steppingDelta);
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudioStreamInterfaceGroup::GetInformationForMuteElement(
    UCHAR   unitID,
    UCHAR & numOfChannels
)
{
    PAGED_CODE();

    return m_usbAudioControlInterface->GetInformationForMuteElement(m_deviceContext, unitID, numOfChannels);
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudioStreamInterfaceGroup::GetInformationForSuperMixElement(
    UCHAR   unitID,
    UCHAR & numOfInputChannels,
    UCHAR & numOfOutputChannels
)
{
    PAGED_CODE();

    return m_usbAudioControlInterface->GetInformationForSuperMixElement(m_deviceContext, unitID, numOfInputChannels, numOfOutputChannels);
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudioStreamInterfaceGroup::GetInformationForMuxElement(
    UCHAR   unitID,
    UCHAR & numOfChannels
)
{
    PAGED_CODE();

    return m_usbAudioControlInterface->GetInformationForMuteElement(m_deviceContext, unitID, numOfChannels);
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudioStreamInterfaceGroup::GetInformationForAgcElement(
    UCHAR   unitID,
    UCHAR & numOfChannels
)
{
    PAGED_CODE();

    return m_usbAudioControlInterface->GetInformationForMuteElement(m_deviceContext, unitID, numOfChannels);
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG
USBAudioStreamInterfaceGroup::GetMaxPacketSize(
    IsoDirection direction
)
{
    ULONG maxPacketSize = 0;
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    for (auto usbAudioInterfaceInfo : m_usbAudioStreamInterfaceInfoes)
    {
        if (usbAudioInterfaceInfo != nullptr)
        {
            ULONG currentMaxPacketSize = 0;
            if (usbAudioInterfaceInfo->GetMaxPacketSize(direction, currentMaxPacketSize))
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
USBAudioStreamInterfaceGroup::GetMaxSupportedValidBitsPerSample(
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

    for (auto usbAudioInterfaceInfo : m_usbAudioStreamInterfaceInfoes)
    {
        if (usbAudioInterfaceInfo != nullptr)
        {
            if (NT_SUCCESS(usbAudioInterfaceInfo->GetMaxSupportedValidBitsPerSample(isInput, desiredFormatType, desiredFormat, currentMaxSupportedBytesPerSample, currentMaxSupportedValidBitsPerSample)))
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
USBAudioStreamInterfaceGroup::GetNearestSupportedValidBitsPerSamples(
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

    for (auto usbAudioInterfaceInfo : m_usbAudioStreamInterfaceInfoes)
    {
        if (usbAudioInterfaceInfo != nullptr)
        {
            ULONG bytesPerSample = nearestSupportedBytesPerSample;
            ULONG validBitsPerSample = nearestSupportedValidBitsPerSample;

            if (NT_SUCCESS(usbAudioInterfaceInfo->GetNearestSupportedValidBitsPerSamples(isInput, desiredFormatType, desiredFormat, bytesPerSample, validBitsPerSample)))
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
USBAudioStreamInterfaceGroup::GetNearestSupportedSampleRate(
    const AUDIO_STREAM_PROPERTY_SET & audioStreamPropertySet,
    ULONG &                           sampleRate
)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG    newSampleRate = 0;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    for (ULONG frameRateListIndex = 0, sampleRateMask = 1; frameRateListIndex < c_SampleRateCount; ++frameRateListIndex, sampleRateMask <<= 1)
    {
        if ((audioStreamPropertySet.AudioProperty.SupportedSampleRate & sampleRateMask))
        {
            if ((c_SampleRateList[frameRateListIndex] >= sampleRate) && (newSampleRate == 0))
            {
                newSampleRate = c_SampleRateList[frameRateListIndex];
            }
        }
    }
    sampleRate = newSampleRate;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!, %u", status, sampleRate);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
ULONG
USBAudioStreamInterfaceGroup::GetSupportedSampleFormats()
{
    ULONG supportedSampleFormats = 0;
    PAGED_CODE();

    if (HasInputAndOutputIsochronousInterfaces())
    {
        supportedSampleFormats = GetUSBAudioDataFormatManager(true)->GetSupportedSampleFormats() & GetUSBAudioDataFormatManager(false)->GetSupportedSampleFormats();
    }
    else if (HasInputIsochronousInterface())
    {
        supportedSampleFormats = GetUSBAudioDataFormatManager(true)->GetSupportedSampleFormats();
    }
    else
    {
        supportedSampleFormats = GetUSBAudioDataFormatManager(false)->GetSupportedSampleFormats();
    }

    return supportedSampleFormats;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioStreamInterfaceGroup::WalkNextUnitTowardForward(
    const AUDIO_STREAM_PROPERTY_SET & audioStreamPropertySet,
    ULONGLONG                         idMap[4],
    ULONGLONG                         unvisitedUnitMap[4],
    AudioNodeKind &                   audioNodeKind,
    UCHAR &                           unitID,
    ULONG &                           controlBitmap,
    UCHAR &                           nextUnitID,
    TraversalDirection &              traversalDirection,
    bool &                            hasMoreData
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, "%!FUNC! 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, %s, 0x%02x, 0x%08x, 0x%02x, %s, hasMoreData = %!bool!", idMap[0], idMap[1], idMap[2], idMap[3], unvisitedUnitMap[0], unvisitedUnitMap[1], unvisitedUnitMap[2], unvisitedUnitMap[3], GetAudioNodeKindString(audioNodeKind), unitID, controlBitmap, nextUnitID, GetTraversalDirectionString(traversalDirection), hasMoreData);

    if (unitID == USBAudioConfiguration::InvalidID) // first
    {

        if (USBAudioControlInterface::TestEntityAllBit(unvisitedUnitMap))
        {
            return m_usbAudioControlInterface->WalkNextUnvisitedUnit(idMap, unvisitedUnitMap, audioNodeKind, unitID, controlBitmap, nextUnitID, traversalDirection, hasMoreData);
        }
        else
        {
            UCHAR terminalLink = USBAudioConfiguration::InvalidID;
            RETURN_NTSTATUS_IF_FAILED(GetCurrentTerminalLink(false, audioStreamPropertySet, terminalLink));
            unitID = terminalLink;
        }
    }
    if (traversalDirection == TraversalDirection::Forward)
    {
        status = m_usbAudioControlInterface->WalkNextUnitTowardForward(idMap, unvisitedUnitMap, audioNodeKind, unitID, controlBitmap, nextUnitID, traversalDirection, hasMoreData);
    }
    else
    {
        status = m_usbAudioControlInterface->WalkNextUnitTowardReverse(idMap, unvisitedUnitMap, audioNodeKind, unitID, controlBitmap, nextUnitID, traversalDirection, hasMoreData);
    }

    if (!hasMoreData)
    {
        if (USBAudioControlInterface::TestEntityAllBit(unvisitedUnitMap))
        {
            hasMoreData = true;
            nextUnitID = USBAudioConfiguration::InvalidID;
        }
    }

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioStreamInterfaceGroup::WalkNextUnitTowardReverse(
    const AUDIO_STREAM_PROPERTY_SET & audioStreamPropertySet,
    ULONGLONG                         idMap[4],
    ULONGLONG                         unvisitedUnitMap[4],
    AudioNodeKind &                   audioNodeKind,
    UCHAR &                           unitID,
    ULONG &                           controlBitmap,
    UCHAR &                           nextUnitID,
    TraversalDirection &              traversalDirection,
    bool &                            hasMoreData
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, "%!FUNC! 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, %s, 0x%02x, 0x%08x, 0x%02x, %s, hasMoreData = %!bool!", idMap[0], idMap[1], idMap[2], idMap[3], unvisitedUnitMap[0], unvisitedUnitMap[1], unvisitedUnitMap[2], unvisitedUnitMap[3], GetAudioNodeKindString(audioNodeKind), unitID, controlBitmap, nextUnitID, GetTraversalDirectionString(traversalDirection), hasMoreData);

    if (unitID == USBAudioConfiguration::InvalidID) // first
    {
        if (USBAudioControlInterface::TestEntityAllBit(unvisitedUnitMap))
        {
            return m_usbAudioControlInterface->WalkNextUnvisitedUnit(idMap, unvisitedUnitMap, audioNodeKind, unitID, controlBitmap, nextUnitID, traversalDirection, hasMoreData);
        }
        else
        {
            UCHAR terminalLink = USBAudioConfiguration::InvalidID;
            RETURN_NTSTATUS_IF_FAILED(GetCurrentTerminalLink(true, audioStreamPropertySet, terminalLink));
            unitID = terminalLink;
        }
    }
    if (traversalDirection == TraversalDirection::Forward)
    {
        status = m_usbAudioControlInterface->WalkNextUnitTowardForward(idMap, unvisitedUnitMap, audioNodeKind, unitID, controlBitmap, nextUnitID, traversalDirection, hasMoreData);
    }
    else
    {
        status = m_usbAudioControlInterface->WalkNextUnitTowardReverse(idMap, unvisitedUnitMap, audioNodeKind, unitID, controlBitmap, nextUnitID, traversalDirection, hasMoreData);
    }
    if (!hasMoreData)
    {
        if (USBAudioControlInterface::TestEntityAllBit(unvisitedUnitMap))
        {
            hasMoreData = true;
            nextUnitID = USBAudioConfiguration::InvalidID;
        }
    }

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
ULONG
USBAudioStreamInterfaceGroup::GetGroupIndex()
{
    PAGED_CODE();

    return m_groupIndex;
}

_Use_decl_annotations_
PAGED_CODE_SEG
void USBAudioStreamInterfaceGroup::Dump()
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - USBAudioStreamInterfaceGroup %u, clock id 0x%02x, target clock id 0x%02x", m_groupIndex, GetClockSourceID(), m_targetClockSourceID);
    for (auto usbAudioInterfaceInfo : m_usbAudioStreamInterfaceInfoes)
    {
        if (usbAudioInterfaceInfo != nullptr)
        {
            usbAudioInterfaceInfo->Dump();
        }
    }
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioInterfaceInfo ** USBAudioStreamInterfaceGroup::begin() noexcept
{
    PAGED_CODE();

    return m_usbAudioStreamInterfaceInfoes.begin();
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioInterfaceInfo ** USBAudioStreamInterfaceGroup::end() noexcept
{
    PAGED_CODE();

    return m_usbAudioStreamInterfaceInfoes.end();
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioInterfaceInfo * const * USBAudioStreamInterfaceGroup::begin() const noexcept
{
    PAGED_CODE();

    return m_usbAudioStreamInterfaceInfoes.begin();
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioInterfaceInfo * const * USBAudioStreamInterfaceGroup::end() const noexcept
{
    PAGED_CODE();

    return m_usbAudioStreamInterfaceInfoes.end();
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
            if (m_usbAudioStreamInterfaceInfoes[interfaceIndex] != nullptr)
            {
                delete m_usbAudioStreamInterfaceInfoes[interfaceIndex];
                m_usbAudioStreamInterfaceInfoes[interfaceIndex] = nullptr;
            }
        }

        if (m_usbAudioStreamInterfaceInfoesMemory != nullptr)
        {
            WdfObjectDelete(m_usbAudioStreamInterfaceInfoesMemory);
            m_usbAudioStreamInterfaceInfoesMemory = nullptr;
            m_usbAudioStreamInterfaceInfoes = nullptr;
        }

        if (m_usbAudioControlInterface != nullptr)
        {
            delete m_usbAudioControlInterface;
            m_usbAudioControlInterface = nullptr;
        }

        for (auto usbAudioStreamInterfaceGroup : m_usbAudioStreamInterfaceGroups)
        {
            if (usbAudioStreamInterfaceGroup != nullptr)
            {
                delete usbAudioStreamInterfaceGroup;
            }
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

    if (IsInterfaceProtocolUSBAudio2(descriptor->bInterfaceProtocol))
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
        //
        // If the Audio Interface Protocol is not USB Audio 2.0 (IP_VERSION_02_00), ignore this interface instead of treating it as an error.
        // This allows the driver to operate correctly even when unexpected interfaces such as USB Audio 1.0 are mixed in the descriptor.
        //
        usbAudioInterface = nullptr;
        status = STATUS_SUCCESS;
#endif
    }
    if (usbAudioInterface != nullptr)
    {
        if (usbAudioInterface->IsStreamInterface())
        {
            bool isStored = false;
            for (ULONG interfaceIndex = 0; interfaceIndex < m_usbConfigurationDescriptor->bNumInterfaces; interfaceIndex++)
            {
                if (m_usbAudioStreamInterfaceInfoes[interfaceIndex] != nullptr)
                {
                    ULONG interfaceNumber = 0;
                    status = m_usbAudioStreamInterfaceInfoes[interfaceIndex]->GetInterfaceNumber(interfaceNumber);
                    RETURN_NTSTATUS_IF_FAILED_MSG(status, "GetInterfaceNumber failed");
                    if (interfaceNumber == descriptor->bInterfaceNumber)
                    {
                        status = m_usbAudioStreamInterfaceInfoes[interfaceIndex]->StoreInterface(usbAudioInterface);
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
                    if (m_usbAudioStreamInterfaceInfoes[interfaceIndex] == nullptr)
                    {
                        m_usbAudioStreamInterfaceInfoes[interfaceIndex] = new (POOL_FLAG_NON_PAGED, DRIVER_TAG) USBAudioInterfaceInfo(m_deviceContext->UsbDevice);
                        RETURN_NTSTATUS_IF_TRUE_ACTION(m_usbAudioStreamInterfaceInfoes[interfaceIndex] == nullptr, status = STATUS_INSUFFICIENT_RESOURCES, status);

                        status = m_usbAudioStreamInterfaceInfoes[interfaceIndex]->StoreInterface(usbAudioInterface);
                        RETURN_NTSTATUS_IF_FAILED_MSG(status, "StoreInterface failed");
                        m_numOfUsbAudioStreamInterfaceInfo++;
                        isStored = true;
                        break;
                    }
                }
            }
        }
        else
        {
            if (m_usbAudioControlInterface == nullptr)
            {
                m_usbAudioControlInterface = (USBAudioControlInterface *)usbAudioInterface;
            }
            else
            {
                //
                // If multiple Control Interfaces exist within a single IAD, use only the first Control Interface and ignore the remaining ones.
                //
                delete usbAudioInterface;
                usbAudioInterface = nullptr;
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

    RETURN_NTSTATUS_IF_TRUE(descriptor->bLength != NS_USBAudio::SIZE_OF_USB_INTERFACE_DESCRIPTOR, STATUS_DEVICE_DATA_ERROR);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    if (descriptor->bLength >= NS_USBAudio::SIZE_OF_USB_INTERFACE_DESCRIPTOR)
    {
        if ((descriptor->bInterfaceClass == USB_DEVICE_CLASS_AUDIO) &&
            ((descriptor->bInterfaceSubClass == USB_AUDIO_CONTROL_SUB_CLASS) || (descriptor->bInterfaceSubClass == USB_AUDIO_STREAMING_SUB_CLASS)))
        {
            hasTargetInterface = true;
        }
        else
        {
            hasTargetInterface = false;
        }
    }
    else
    {
        status = STATUS_DEVICE_DATA_ERROR;
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

        //
        // If an interface fails to be created, treat it as absent until the next interface is encountered.
        //
        if (lastInterface == nullptr)
        {
            hasTargetInterface = false;
        }
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
    // If the interface is not supported, skip this processing but complete normally.
    RETURN_NTSTATUS_IF_TRUE(lastInterface == nullptr, STATUS_SUCCESS);
    RETURN_NTSTATUS_IF_TRUE(descriptor->bLength != NS_USBAudio::SIZE_OF_USB_ENDPOINT_DESCRIPTOR, STATUS_DEVICE_DATA_ERROR);

    if ((lastInterface != nullptr) && (descriptor->bLength >= NS_USBAudio::SIZE_OF_USB_ENDPOINT_DESCRIPTOR))
    {
        status = lastInterface->SetEndpoint(descriptor);
        if (NT_SUCCESS(status))
        {
            if (lastInterface->IsControlInterface())
            {
                if (((USBAudioControlInterface *)lastInterface)->HasInterruptDataMessageEndpoint())
                {
                    m_isInterruptDataMessageInterfaceExists = true;
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
    // If the interface is not supported, skip this processing but complete normally.
    RETURN_NTSTATUS_IF_TRUE(lastInterface == nullptr, STATUS_SUCCESS);
    RETURN_NTSTATUS_IF_TRUE(descriptor->bLength != NS_USBAudio::SIZE_OF_USB_SSENDPOINT_COMPANION_DESCRIPTOR, STATUS_DEVICE_DATA_ERROR);

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

    RETURN_NTSTATUS_IF_TRUE(descriptor == nullptr, STATUS_INVALID_PARAMETER);
    // If the interface is not supported, skip this processing but complete normally.
    RETURN_NTSTATUS_IF_TRUE(lastInterface == nullptr, STATUS_SUCCESS);

    if ((lastInterface != nullptr) && (descriptor->bLength >= sizeof(NS_USBAudio::CS_GENERIC_AUDIO_DESCRIPTOR)))
    {
        if (lastInterface->IsStreamInterface())
        {
            switch (descriptor->bDescriptorSubtype)
            {
            case NS_USBAudio0200::FORMAT_TYPE:
                status = ((USBAudioStreamInterface *)lastInterface)->SetFormatType(descriptor);
                break;
            case NS_USBAudio0200::AS_GENERAL:
                status = ((USBAudioStreamInterface *)lastInterface)->SetGeneral(descriptor);
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
                status = ((USBAudioControlInterface *)lastInterface)->SetMixerUnit(descriptor);
                break;
            case NS_USBAudio0200::SELECTOR_UNIT:
                status = ((USBAudioControlInterface *)lastInterface)->SetSelectorUnit(descriptor);
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

    RETURN_NTSTATUS_IF_TRUE(descriptor == nullptr, STATUS_INVALID_PARAMETER);
    // If the interface is not supported, skip this processing but complete normally.
    RETURN_NTSTATUS_IF_TRUE(lastInterface == nullptr, STATUS_SUCCESS);

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
/*++

Routine Description:

    Parses USB CONFIGURATION DESCRIPTOR and holds the descriptors required for
    creating an ACX Device and streaming USB Audio.

Arguments:

    usbConfigurationDescriptor - USB CONFIGURATION DESCRIPTOR of the target device

Return Value:

    NTSTATUS - NT status value

    STATUS_DEVICE_DATA_ERROR Returned when the length of an individual descriptor
    within is invalid, causing an out-of-bounds reference beyond the
    USB_CONFIGURATION_DESCRIPTOR area, or when the length is 0.

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
    RETURN_NTSTATUS_IF_TRUE(m_usbAudioStreamInterfaceInfoes != nullptr, STATUS_UNSUCCESSFUL);

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bLength             = %u", m_usbConfigurationDescriptor->bLength);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bDescriptorType     = %u", m_usbConfigurationDescriptor->bDescriptorType);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - wTotalLength        = %u", m_usbConfigurationDescriptor->wTotalLength);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bNumInterfaces      = %u", m_usbConfigurationDescriptor->bNumInterfaces);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bConfigurationValue = %u", m_usbConfigurationDescriptor->bConfigurationValue);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - iConfiguration      = %u", m_usbConfigurationDescriptor->iConfiguration);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - bmAttributes        = %u", m_usbConfigurationDescriptor->bmAttributes);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - MaxPower            = %u", m_usbConfigurationDescriptor->MaxPower);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = m_deviceContext->Device;

    RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(USBAudioInterfaceInfo *) * m_usbConfigurationDescriptor->bNumInterfaces, &m_usbAudioStreamInterfaceInfoesMemory, (PVOID *)&m_usbAudioStreamInterfaceInfoes));

    RtlZeroMemory(m_usbAudioStreamInterfaceInfoes, sizeof(USBAudioInterfaceInfo *) * m_usbConfigurationDescriptor->bNumInterfaces);

    m_deviceContext->VendorId = m_usbDeviceDescriptor->idVendor;
    m_deviceContext->ProductId = m_usbDeviceDescriptor->idProduct;
    m_deviceContext->DeviceRelease = m_usbDeviceDescriptor->bcdDevice;

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
        RtlStringCchCopyW(m_deviceContext->ProductName, UAC_MAX_PRODUCT_NAME_LENGTH, m_deviceContext->DeviceName);
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
            if (commonDescriptor->bLength == 0)
            {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DESCRIPTOR, "USB Descriptor Header Length is invalid");
                status = STATUS_DEVICE_DATA_ERROR;
            }
            if (((totalLength - current) >= commonDescriptor->bLength) && NT_SUCCESS(status))
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
                status = STATUS_DEVICE_DATA_ERROR;
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DESCRIPTOR, "USB Descriptor Header Length is invalid");
            }
            current += commonDescriptor->bLength;
        }
        else
        {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DESCRIPTOR, "USB Descriptor Header Length is invalid");
            status = STATUS_DEVICE_DATA_ERROR;
        }
    }

    //
    if (!hasAnyTargetInterface && NT_SUCCESS(status))
    {
        // No target interface found.
        status = STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if (NT_SUCCESS(status))
    {
        ASSERT(m_usbAudioControlInterface != nullptr);
        if (m_usbAudioControlInterface != nullptr)
        {
            m_clockEntityCountForTerminal = m_usbAudioControlInterface->GetClockEntityCountForTerminal();

            if (IsEnableInterruptMessage())
            {
                m_usbAudioControlInterface->GetInterruptDataMessageEndpoint(m_deviceContext->InterruptMessageProperty.IsValid, m_deviceContext->InterruptMessageProperty.InterfaceNumber, m_deviceContext->InterruptMessageProperty.EndpointNumber, m_deviceContext->InterruptMessageProperty.Interval);
            }

            status = BuildUsbAudioStreamInterfaceGroups();
        }
    }

    if (NT_SUCCESS(status))
    {
        m_clockEntityCountForInterface = m_usbAudioStreamInterfaceGroups.GetNumOfArray();

        for (auto usbAudioStreamInterfaceGroup : m_usbAudioStreamInterfaceGroups)
        {
            if (usbAudioStreamInterfaceGroup != nullptr)
            {
                usbAudioStreamInterfaceGroup->Dump();
            }
        }
    }

    if (m_clockEntityCountForInterface > 1)
    {
        //
        // Multiple clock sources may be associated with the interface.
        //
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_DESCRIPTOR, "Found %d clock sources associated with the interface.", m_clockEntityCountForInterface);
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

    RETURN_NTSTATUS_IF_TRUE(m_usbAudioStreamInterfaceInfoes == nullptr, STATUS_UNSUCCESSFUL);

    ASSERT(m_usbAudioControlInterface != nullptr);
    if (m_usbAudioControlInterface != nullptr)
    {
        RETURN_NTSTATUS_IF_FAILED(m_usbAudioControlInterface->ReconnectClockAll(m_deviceContext));

        RETURN_NTSTATUS_IF_FAILED(m_usbAudioControlInterface->QueryRangeAttributeAll(m_deviceContext));
    }

    //
    // https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/usb-2-0-audio-drivers
    // The USB Audio 2.0 driver doesn't support clock selection. The driver
    // uses the Clock Source Entity, which is selected by default and never
    // issues a Clock Selector Control SET CUR request.
    //
    RETURN_NTSTATUS_IF_FAILED(m_usbAudioControlInterface->QueryCurrentAttributeAll(m_deviceContext));
    for (ULONG index = 0; index < m_numOfUsbAudioStreamInterfaceInfo; index++)
    {
        if (m_usbAudioStreamInterfaceInfoes[index] != nullptr)
        {
            RETURN_NTSTATUS_IF_FAILED(m_usbAudioStreamInterfaceInfoes[index]->QueryCurrentAttributeAll(m_deviceContext));
        }
    }

    RETURN_NTSTATUS_IF_FAILED(m_usbAudioControlInterface->SetDefaultAttributeAll(m_deviceContext));
    for (ULONG index = 0; index < m_numOfUsbAudioStreamInterfaceInfo; index++)
    {
        if (m_usbAudioStreamInterfaceInfoes[index] != nullptr)
        {
            RETURN_NTSTATUS_IF_FAILED(m_usbAudioStreamInterfaceInfoes[index]->SetDefaultAttributeAll(m_deviceContext));
        }
    }

    // for test.
    // CheckInterfaceConfiguration();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
USBAudioConfiguration::GetChannelName(
    UCHAR       channelNames,
    ULONG       channel,
    WDFMEMORY & memory,
    PWSTR &     channelName
)
/*++

Routine Description:

    Get the channel name


Arguments:

    channel -

    memory -

    channelName -

Return Value:

    NTSTATUS - NT status value

--*/
{
    NTSTATUS status = STATUS_NOT_SUPPORTED;

    PAGED_CODE();

    if (channelNames != USBAudioConfiguration::InvalidString)
    {
        status = GetStringDescriptor(m_deviceContext->UsbDevice, (UCHAR)(channelNames + channel), LANGID_EN_US, memory, channelName);
    }

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
USBAudioConfiguration::GetStereoChannelName(
    UCHAR       channelNames,
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

    RETURN_NTSTATUS_IF_FAILED(GetChannelName(channelNames, channel, leftMemory, leftChannelName));
    RETURN_NTSTATUS_IF_FAILED(GetChannelName(channelNames, channel + 1, rightMemory, rightChannelName));

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
    attributes.ParentObject = m_deviceContext->Device;
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

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - channel names %u, channel %d, %ws, %ws, %ws", channelNames, channel, leftChannelName, rightChannelName, channelName);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioConfiguration::IsInterfaceProtocolUSBAudio2(
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
bool USBAudioConfiguration::IsUSBAudio2() const
{
    PAGED_CODE();

    return m_isUSBAudio2;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
bool USBAudioConfiguration::HasInterruptDataMessageInterfaces() const
{
    return m_isInterruptDataMessageInterfaceExists;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
ULONG USBAudioConfiguration::GetClockEntityCountForTerminal() const
{
    return m_clockEntityCountForTerminal;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
ULONG USBAudioConfiguration::GetClockEntityCountForInterface() const
{
    return m_clockEntityCountForInterface;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
NTSTATUS USBAudioConfiguration::UpdateCurrentValue(
    const UCHAR interfaceNumber,
    const UCHAR entityID,
    const UCHAR controlSelector,
    const UCHAR controlNumber,
    bool &      needNotify
)
{
    NTSTATUS status = STATUS_SUCCESS;

    needNotify = false;

    ASSERT(m_usbAudioControlInterface != nullptr);
    if (m_usbAudioControlInterface != nullptr)
    {
        ULONG queriedInterfaceNumber = m_usbAudioControlInterface->GetInterfaceNumber();
        if (queriedInterfaceNumber == interfaceNumber)
        {
            needNotify = true;
            m_usbAudioControlInterface->UpdateCurrentValue(entityID, controlSelector, controlNumber);
            return STATUS_SUCCESS;
        }
    }

    return status;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
NTSTATUS USBAudioConfiguration::OnInterruptDataMessageReceived(
    const UCHAR  info,
    const UCHAR  attribute,
    const USHORT value,
    const USHORT index,
    bool &       needNotify
)
{
    NTSTATUS status = STATUS_SUCCESS;
    UCHAR    controlSelector = value >> NS_USBAudio0200::INTERRUPT_VALUE_CONTROL_SELECTOR_SHIFT;
    UCHAR    controlNumber = value & NS_USBAudio0200::INTERRUPT_VALUE_CONTROL_NUMBER_MASK;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "%!FUNC! Entry");

    needNotify = false;

    if ((info & NS_USBAudio0200::INTERRUPT_INFO_ORIGIN_MASK) == NS_USBAudio0200::INTERRUPT_INFO_ORIGIN_INTERFACE)
    {
        UCHAR interfaceNumber = index & NS_USBAudio0200::INTERRUPT_INDEX_INTERFACENUMBER_MASK;
        UCHAR entityID = index >> NS_USBAudio0200::INTERRUPT_INDEX_ENTITYID_SHIFT;

        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_INTERRUPTTRANSFER, "interface origin processing. interfece %u, entity 0x%02x, attibute %u, control selector %u, control number %u", interfaceNumber, entityID, attribute, controlSelector, controlNumber);
        switch (attribute)
        {
        case NS_USBAudio0200::CUR:
            status = UpdateCurrentValue(interfaceNumber, entityID, controlSelector, controlNumber, needNotify);
            break;
        default:
        case NS_USBAudio0200::RANGE:
        case NS_USBAudio0200::MEM:
            // do noting.
            break;
        }
    }
    else if ((info & NS_USBAudio0200::INTERRUPT_INFO_ORIGIN_MASK) == NS_USBAudio0200::INTERRUPT_INFO_ORIGIN_ENDPOINT)
    {
        UCHAR endpoint = index & NS_USBAudio0200::INTERRUPT_INDEX_ENDPOINT_MASK;

        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_INTERRUPTTRANSFER, "endpoint origin processing. endpoint %u, attribute %u, control selector %u, control number %u", endpoint, attribute, controlSelector, controlNumber);
        switch (attribute)
        {
        case NS_USBAudio0200::CUR:
            break;
        default:
        case NS_USBAudio0200::RANGE:
        case NS_USBAudio0200::MEM:
            // do noting.
            break;
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioConfiguration::IsVolumeEntityUpdated()
{
    PAGED_CODE();

    ASSERT(m_usbAudioControlInterface != nullptr);
    if (m_usbAudioControlInterface != nullptr)
    {
        return m_usbAudioControlInterface->IsVolumeEntityUpdated();
    }

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioConfiguration::IsMuteEntityUpdated()
{
    PAGED_CODE();

    ASSERT(m_usbAudioControlInterface != nullptr);
    if (m_usbAudioControlInterface != nullptr)
    {
        return m_usbAudioControlInterface->IsMuteEntityUpdated();
    }

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioConfiguration::IsInputConnectorEntityUpdated()
{
    PAGED_CODE();

    ASSERT(m_usbAudioControlInterface != nullptr);
    if (m_usbAudioControlInterface != nullptr)
    {
        return m_usbAudioControlInterface->IsInputConnectorEntityUpdated();
    }

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioConfiguration::IsOutputConnectorEntityUpdated()
{
    PAGED_CODE();

    ASSERT(m_usbAudioControlInterface != nullptr);
    if (m_usbAudioControlInterface != nullptr)
    {
        return m_usbAudioControlInterface->IsOutputConnectorEntityUpdated();
    }

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioConfiguration::GetUpdatedVolumeEntity(
    UCHAR & entityID
)
{
    PAGED_CODE();

    ASSERT(m_usbAudioControlInterface != nullptr);
    if (m_usbAudioControlInterface != nullptr)
    {
        return m_usbAudioControlInterface->GetUpdatedVolumeEntity(entityID);
    }

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioConfiguration::GetUpdatedMuteEntity(
    UCHAR & entityID
)
{
    PAGED_CODE();

    ASSERT(m_usbAudioControlInterface != nullptr);
    if (m_usbAudioControlInterface != nullptr)
    {
        return m_usbAudioControlInterface->GetUpdatedMuteEntity(entityID);
    }

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioConfiguration::GetUpdatedInputConnectorEntity(
    UCHAR & entityID
)
{
    PAGED_CODE();

    ASSERT(m_usbAudioControlInterface != nullptr);
    if (m_usbAudioControlInterface != nullptr)
    {
        return m_usbAudioControlInterface->GetUpdatedInputConnectorEntity(entityID);
    }

    return false;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioConfiguration::GetUpdatedOutputConnectorEntity(
    UCHAR & entityID
)
{
    PAGED_CODE();

    ASSERT(m_usbAudioControlInterface != nullptr);
    if (m_usbAudioControlInterface != nullptr)
    {
        return m_usbAudioControlInterface->GetUpdatedOutputConnectorEntity(entityID);
    }

    return false;
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
NTSTATUS
USBAudioConfiguration::GetVolumeConfiguration(
    UCHAR   entityID,
    LONG &  minimum,
    LONG &  maximum,
    ULONG & steppingDelta
)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    PAGED_CODE();

    ASSERT(m_usbAudioControlInterface != nullptr);
    if (m_usbAudioControlInterface != nullptr)
    {
        status = m_usbAudioControlInterface->GetVolumeConfiguration(m_deviceContext, entityID, minimum, maximum, steppingDelta);
    }

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudioConfiguration::ValidateVolumeControl(
    UCHAR entityID,
    UCHAR channel
)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    ASSERT(m_usbAudioControlInterface != nullptr);
    if (m_usbAudioControlInterface != nullptr)
    {
        status = m_usbAudioControlInterface->ValidateVolumeControl(entityID, channel);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit, %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudioConfiguration::SetCurrentVolume(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           entityID,
    UCHAR           channel,
    LONG            volume
)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    ASSERT(m_usbAudioControlInterface != nullptr);
    if (m_usbAudioControlInterface != nullptr)
    {
        status = m_usbAudioControlInterface->SetCurrentVolume(deviceContext, entityID, channel, volume);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit, %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudioConfiguration::GetCurrentVolume(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           entityID,
    UCHAR           channel,
    LONG &          volume
)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    volume = 0;

    ASSERT(m_usbAudioControlInterface != nullptr);
    if (m_usbAudioControlInterface != nullptr)
    {
        status = m_usbAudioControlInterface->GetCurrentVolume(deviceContext, entityID, channel, volume);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit, %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudioConfiguration::ValidateMuteControl(
    UCHAR entityID,
    UCHAR channel
)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    ASSERT(m_usbAudioControlInterface != nullptr);
    if (m_usbAudioControlInterface != nullptr)
    {
        status = m_usbAudioControlInterface->ValidateMuteControl(entityID, channel);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit, %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudioConfiguration::SetCurrentMute(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           entityID,
    UCHAR           channel,
    bool            mute
)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    ASSERT(m_usbAudioControlInterface != nullptr);
    if (m_usbAudioControlInterface != nullptr)
    {
        status = m_usbAudioControlInterface->SetCurrentMute(deviceContext, entityID, channel, mute);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit, %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudioConfiguration::GetCurrentMute(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           entityID,
    UCHAR           channel,
    bool &          mute
)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    mute = true;

    ASSERT(m_usbAudioControlInterface != nullptr);
    if (m_usbAudioControlInterface != nullptr)
    {
        status = m_usbAudioControlInterface->GetCurrentMute(deviceContext, entityID, channel, mute);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit, %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
USBAudioConfiguration::GetCurrentConnectorState(
    PDEVICE_CONTEXT                                 deviceContext,
    UCHAR                                           entityID,
    NS_USBAudio::AUDIO_CHANNEL_CLUSTER_DESCRIPTOR & connectorState
)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    RtlZeroMemory(&connectorState, sizeof(NS_USBAudio::AUDIO_CHANNEL_CLUSTER_DESCRIPTOR));

    ASSERT(m_usbAudioControlInterface != nullptr);
    if (m_usbAudioControlInterface != nullptr)
    {
        status = m_usbAudioControlInterface->GetCurrentConnectorState(deviceContext, entityID, connectorState);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit, %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioConfiguration::GetSelectorConfiguration(
    UCHAR   entityID,
    UCHAR & pins
)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    PAGED_CODE();

    ASSERT(m_usbAudioControlInterface != nullptr);
    if (m_usbAudioControlInterface != nullptr)
    {
        status = m_usbAudioControlInterface->GetSelectorConfiguration(m_deviceContext, entityID, pins);
    }

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudioConfiguration::SetCurrentSelector(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           entityID,
    UCHAR           selectorIndex
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    ASSERT(m_usbAudioControlInterface != nullptr);
    if (m_usbAudioControlInterface != nullptr)
    {
        RETURN_NTSTATUS_IF_FAILED(m_usbAudioControlInterface->SetCurrentSelector(deviceContext, entityID, selectorIndex));
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudioConfiguration::GetCurrentSelector(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           entityID,
    UCHAR &         selectorIndex
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    selectorIndex = 0;

    ASSERT(m_usbAudioControlInterface != nullptr);
    if (m_usbAudioControlInterface != nullptr)
    {
        RETURN_NTSTATUS_IF_FAILED(m_usbAudioControlInterface->GetCurrentSelector(deviceContext, entityID, selectorIndex));
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
_Use_decl_annotations_
bool USBAudioConfiguration::IsEnableASIO()
{
    PAGED_CODE();

    bool enableASIO = true;
    // const ULONG sampleFormatsTypeIII = USBAudioDataFormat::GetSampleFormatsTypeIII();

    //
    // If the device supports USB Audio Data Format Type III, ASIO is treated as unsupported.
    //
    // TBD 202603
    if (/* ((m_deviceContext->AudioProperty.SupportedSampleFormats & sampleFormatsTypeIII) != 0) || */ IsMultipleClockSources())
    {
        enableASIO = false;
    }

    return enableASIO;
}

PAGED_CODE_SEG
_Use_decl_annotations_
bool USBAudioConfiguration::IsMultipleClockSources()
{
    PAGED_CODE();

    bool isMultipleClockSources = (GetClockEntityCountForTerminal() > 1) || (GetClockEntityCountForInterface() > 1);

    return isMultipleClockSources;
}

PAGED_CODE_SEG
_Use_decl_annotations_
bool USBAudioConfiguration::IsDeviceSplittable()
{
    PAGED_CODE();

    bool isDeviceSplittable = IsEnableASIO();

    return isDeviceSplittable;
}

PAGED_CODE_SEG
_Use_decl_annotations_
bool USBAudioConfiguration::IsEnableFeatureUnit(
    bool /* isInput */
)
{
    PAGED_CODE();

    return !IsDeviceSplittable();
}

PAGED_CODE_SEG
_Use_decl_annotations_
bool USBAudioConfiguration::IsEnableConnectorControl(
    bool /* isInput */
)
{
    PAGED_CODE();

    return !IsDeviceSplittable();
}

PAGED_CODE_SEG
_Use_decl_annotations_
bool USBAudioConfiguration::IsEnableInterruptMessage()
{
    PAGED_CODE();

    return IsEnableFeatureUnit(true) || IsEnableFeatureUnit(false) || IsEnableConnectorControl(true) || IsEnableConnectorControl(false);
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG USBAudioConfiguration::GetNumOfStreamInterfaceGroup() const
{
    PAGED_CODE();

    return m_usbAudioStreamInterfaceGroups.GetNumOfArray();
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioStreamInterfaceGroup ** USBAudioConfiguration::begin() noexcept
{
    PAGED_CODE();

    return m_usbAudioStreamInterfaceGroups.begin();
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioStreamInterfaceGroup ** USBAudioConfiguration::end() noexcept
{
    PAGED_CODE();

    return m_usbAudioStreamInterfaceGroups.end();
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioStreamInterfaceGroup * const * USBAudioConfiguration::begin() const noexcept
{
    PAGED_CODE();

    return m_usbAudioStreamInterfaceGroups.begin();
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioStreamInterfaceGroup * const * USBAudioConfiguration::end() const noexcept
{
    PAGED_CODE();

    return m_usbAudioStreamInterfaceGroups.end();
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS USBAudioConfiguration::BuildUsbAudioStreamInterfaceGroups()
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG    groupIndex = 0;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    for (ULONG interfaceIndex = 0; interfaceIndex < m_numOfUsbAudioStreamInterfaceInfo; interfaceIndex++)
    {
        if (m_usbAudioStreamInterfaceInfoes[interfaceIndex] != nullptr)
        {
            UCHAR terminalLink = USBAudioConfiguration::InvalidID;
            if (m_usbAudioStreamInterfaceInfoes[interfaceIndex]->GetTerminalLink(terminalLink))
            {
                UCHAR clockSourceID = USBAudioConfiguration::InvalidID;
                status = m_usbAudioControlInterface->GetClockSourceIDFromTerminal(terminalLink, clockSourceID);
                if (NT_SUCCESS(status))
                {
                    bool appended = false;
                    for (auto usbAudioStreamInterfaceGroup : m_usbAudioStreamInterfaceGroups)
                    {
                        if (usbAudioStreamInterfaceGroup->GetClockSourceID() == clockSourceID)
                        {
                            RETURN_NTSTATUS_IF_FAILED(usbAudioStreamInterfaceGroup->Append(m_usbAudioStreamInterfaceInfoes[interfaceIndex]));
                            appended = true;
                        }
                    }
                    if (!appended)
                    {
                        auto usbAudioStreamInterfaceGroup = USBAudioStreamInterfaceGroup::Create(m_deviceContext, groupIndex++, m_usbAudioControlInterface, IsDeviceSplittable());
                        RETURN_NTSTATUS_IF_TRUE_ACTION(usbAudioStreamInterfaceGroup == nullptr, status = STATUS_INSUFFICIENT_RESOURCES, status);
                        RETURN_NTSTATUS_IF_FAILED(m_usbAudioStreamInterfaceGroups.Append(m_deviceContext->UsbDevice, usbAudioStreamInterfaceGroup));
                        RETURN_NTSTATUS_IF_FAILED(usbAudioStreamInterfaceGroup->Append(m_usbAudioStreamInterfaceInfoes[interfaceIndex]));
                        usbAudioStreamInterfaceGroup->SetClockSourceID(clockSourceID);
                    }
                }
            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!", status);

    return status;
}
