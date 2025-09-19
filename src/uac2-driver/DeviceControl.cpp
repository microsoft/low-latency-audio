// Copyright (c) Microsoft Corporation.
// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================



/*++

Module Name:

    DeviceControl.cpp

Abstract:

    Implements control transfers for USB device.



Environment:

    Kernel-mode Driver Framework

--*/

#include "Driver.h"
#include "Device.h"
#include "DeviceControl.h"
#include "Public.h"
#include "Common.h"
#include "USBAudio.h"
#include "USBAudioConfiguration.h"
#include "ErrorStatistics.h"

#ifndef __INTELLISENSE__
#include "DeviceControl.tmh"
#endif

#define UsbMakeBmRequestType(Dir, Type, Recipient) (UCHAR)(((Dir & 0x1) << 7) | ((Type & 0x3) << 5) | (Recipient & 0x1f))

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS
ControlRequest(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           requestType,
    _In_ UCHAR           request,
    _In_ USHORT          value,
    _In_ USHORT          index,
    _Inout_ PVOID        dataBuffer,
    _In_ ULONG           dataBufferLength,
    _Out_opt_ PULONG     dataLength,
    _In_ ULONG           msTimeout = 1000 // 0 = Infinite wait
)
{
    NTSTATUS  status = STATUS_SUCCESS;
    PURB      urb = nullptr;
    WDFMEMORY urbMemory = nullptr;

    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(deviceContext == nullptr, STATUS_INVALID_PARAMETER);

    auto controlRequestScope = wil::scope_exit([&]() {
        if (urbMemory)
        {
            WdfObjectDelete(urbMemory);
        }
    });

    if (dataLength != nullptr)
    {
        *dataLength = 0;
    }

    ULONG direction = (requestType >> 7) & 0x1;
    ULONG type = requestType & 0x7f;

    if (((type & 0x60) == 0x20 && !deviceContext->SupportedControl.ClassRequestSupported) ||
        ((type & 0x60) == 0x40 && !deviceContext->SupportedControl.VendorRequestSupported))
    {
        return STATUS_UNSUCCESSFUL;
    }

    ULONG  requestTimeoutMs = deviceContext->SupportedControl.RequestTimeOut;
    USHORT function;
    switch (type)
    {
    case 0x00:
        function = URB_FUNCTION_CLEAR_FEATURE_TO_DEVICE;
        break;
    case 0x01:
        function = URB_FUNCTION_CLEAR_FEATURE_TO_INTERFACE;
        break;
    case 0x02:
        function = URB_FUNCTION_CLEAR_FEATURE_TO_ENDPOINT;
        break;
    case 0x20:
        function = URB_FUNCTION_CLASS_DEVICE;
        break;
    case 0x21:
        function = URB_FUNCTION_CLASS_INTERFACE;
        break;
    case 0x22:
        function = URB_FUNCTION_CLASS_ENDPOINT;
        break;
    case 0x40:
        function = URB_FUNCTION_VENDOR_DEVICE;
        break;
    case 0x41:
        function = URB_FUNCTION_VENDOR_INTERFACE;
        break;
    case 0x42:
        function = URB_FUNCTION_VENDOR_ENDPOINT;
        break;
    default:
        return STATUS_INVALID_PARAMETER;
    }

    status = WdfUsbTargetDeviceCreateUrb(
        deviceContext->UsbDevice,
        nullptr,
        &urbMemory,
        nullptr
    );
    RETURN_NTSTATUS_IF_FAILED_MSG(status, "WdfUsbTargetDeviceCreateUrb failed");

    size_t bufferSize = 0;
    urb = (PURB)WdfMemoryGetBuffer(urbMemory, &bufferSize);
    if (bufferSize < sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_CTRLREQUEST, "The memory size allocated by WdfUsbTargetDeviceCreateUrb is small.");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    bool bubbleDetected = false;

    ULONG maxRetry = deviceContext->SupportedControl.RequestRetry;
    for (ULONG retry = 0; retry < maxRetry; ++retry)
    {
        if (type < 0x10)
        {
            UsbBuildFeatureRequest(
                urb,
                function,
                value,
                index,
                nullptr
            );
        }
        else
        {
            UsbBuildVendorRequest(
                urb,
                function,
                sizeof(_URB_CONTROL_VENDOR_OR_CLASS_REQUEST),
                ((direction == 1) ? (USBD_SHORT_TRANSFER_OK | USBD_TRANSFER_DIRECTION_IN) : 0),
                0,
                request,
                value,
                index,
                dataBuffer,
                nullptr,
                dataBufferLength,
                nullptr
            );
        }

        //
        // ince some devices may return incorrect responses when sending
        // Vendor Requests in succession, an interval of 10 ms is required.
        //
        LARGE_INTEGER waitTime;
        waitTime.QuadPart = deviceContext->LastVendorRequestTime.QuadPart + (10LL * 10000LL);

        KeDelayExecutionThread(KernelMode, FALSE, &waitTime);

        KeQuerySystemTime(&deviceContext->LastVendorRequestTime);

        if (requestTimeoutMs != 0)
        {
            status = SendUrbSyncWithTimeout(deviceContext, urb, requestTimeoutMs);
        }
        else
        {
            status = SendUrbSync(deviceContext, urb);
        }

        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_CTRLREQUEST, "Vendor control request failed, type %02x, request %02x, value %04x, index %04x, Status %!STATUS! ,URB status 0x%x", requestType, request, value, index, status, urb->UrbControlVendorClassRequest.Hdr.Status);
            if (urb->UrbControlVendorClassRequest.Hdr.Status == USBD_STATUS_STALL_PID)
            {
                break;
            }
            if (status != STATUS_DEVICE_BUSY)
            {
                deviceContext->ErrorStatistics->LogErrorOccurrence(ErrorStatus::VendorControlFailed, 0);
                // InterlockedIncrement((PLONG)&deviceContext->TotalDriverError);
                // InterlockedIncrement((PLONG)&deviceContext->DriverError[0]);
                // InterlockedIncrement((PLONG)&deviceContext->DriverError[2]);
            }
            if (status == STATUS_NO_SUCH_DEVICE || status == STATUS_DEVICE_DOES_NOT_EXIST)
            {
                break;
            }
            if (urb->UrbControlVendorClassRequest.Hdr.Status == USBD_STATUS_BABBLE_DETECTED)
            {
                if (retry == maxRetry - 1)
                {
                    status = STATUS_BUFFER_TOO_SMALL;
                    break;
                }
                else
                {
                    LARGE_INTEGER sleepTime;
                    sleepTime.QuadPart = -100LL * 10000LL;
                    KeDelayExecutionThread(KernelMode, FALSE, &sleepTime);
                    bubbleDetected = true;
                    continue;
                }
            }
            else
            {
                if (retry == 0)
                {
                    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CTRLREQUEST, "retry...");
                    requestTimeoutMs = msTimeout;
                }
                else
                {
                    break;
                }
            }
        }
        if (NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CTRLREQUEST, "Vendor control request success, type %02x, request %02x, value %04x, index %04x, Status %08x ,URB status %08x", requestType, request, value, index, status, urb->UrbControlVendorClassRequest.Hdr.Status);
            if (bubbleDetected)
            {
                LARGE_INTEGER sleepTime;
                sleepTime.QuadPart = -100LL * 10000LL;
                KeDelayExecutionThread(KernelMode, FALSE, &sleepTime);
                bubbleDetected = false;
                continue;
            }
            if (dataLength != nullptr)
            {
                *dataLength = urb->UrbControlVendorClassRequest.TransferBufferLength;
            }
            if (retry != 0)
            {
                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CTRLREQUEST, "retry succeed.");
            }
            break;
        }
    }

    return status;
}

// Layout 1 Parameter Block
__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS GetCurrentSetting(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           interfaceNumber,
    _In_ UCHAR           entityID,
    _In_ ULONG           controlSelector,
    _In_ UCHAR           channelNumber,
    _Out_ UCHAR &        current
)
{
    ULONG dataLength = 0;

    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(deviceContext == nullptr, STATUS_INVALID_PARAMETER);

    current = 0;
    NTSTATUS status = ControlRequest(
        deviceContext,
        UsbMakeBmRequestType(BMREQUEST_DEVICE_TO_HOST, BMREQUEST_CLASS, BMREQUEST_TO_INTERFACE),
        NS_USBAudio0200::CUR,
        (((USHORT)controlSelector) << 8) | channelNumber,
        (((USHORT)entityID) << 8) | interfaceNumber,
        &current,
        sizeof(current),
        &dataLength
    );
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, "%!FUNC! %!STATUS!", status);

    return status;
}

// Layout 2 Parameter Block
__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS GetCurrentSetting(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           interfaceNumber,
    _In_ UCHAR           entityID,
    _In_ ULONG           controlSelector,
    _In_ UCHAR           channelNumber,
    _Out_ USHORT &       current
)
{
    ULONG dataLength = 0;

    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(deviceContext == nullptr, STATUS_INVALID_PARAMETER);

    current = 0;
    NTSTATUS status = ControlRequest(
        deviceContext,
        UsbMakeBmRequestType(BMREQUEST_DEVICE_TO_HOST, BMREQUEST_CLASS, BMREQUEST_TO_INTERFACE),
        NS_USBAudio0200::CUR,
        (((USHORT)controlSelector) << 8) | channelNumber,
        (((USHORT)entityID) << 8) | interfaceNumber,
        &current,
        sizeof(current),
        &dataLength
    );

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, "%!FUNC! %!STATUS!", status);
    return status;
}

// Layout 3 Parameter Block
__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS GetCurrentSetting(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           interfaceNumber,
    _In_ UCHAR           entityID,
    _In_ ULONG           controlSelector,
    _In_ UCHAR           channelNumber,
    _Out_ ULONG &        current
)
{
    ULONG dataLength = 0;
    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(deviceContext == nullptr, STATUS_INVALID_PARAMETER);

    current = 0;

    NTSTATUS status = ControlRequest(
        deviceContext,
        UsbMakeBmRequestType(BMREQUEST_DEVICE_TO_HOST, BMREQUEST_CLASS, BMREQUEST_TO_INTERFACE),
        NS_USBAudio0200::CUR,
        (((USHORT)controlSelector) << 8) | channelNumber,
        (((USHORT)entityID) << 8) | interfaceNumber,
        &current,
        sizeof(current),
        &dataLength
    );

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, "%!FUNC! %!STATUS!", status);
    return status;
}

// Layout 1 Parameter Block
__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS SetCurrentSetting(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           interfaceNumber,
    _In_ UCHAR           entityID,
    _In_ ULONG           controlSelector,
    _In_ UCHAR           channelNumber,
    _In_ UCHAR           current
)
{
    ULONG dataLength = 0;

    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(deviceContext == nullptr, STATUS_INVALID_PARAMETER);

    NTSTATUS status = ControlRequest(
        deviceContext,
        UsbMakeBmRequestType(BMREQUEST_HOST_TO_DEVICE, BMREQUEST_CLASS, BMREQUEST_TO_INTERFACE),
        NS_USBAudio0200::CUR,
        (((USHORT)controlSelector) << 8) | channelNumber,
        (((USHORT)entityID) << 8) | interfaceNumber,
        &current,
        sizeof(current),
        &dataLength
    );

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, "%!FUNC! %!STATUS!", status);
    return status;
}

// Layout 2 Parameter Block
__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS SetCurrentSetting(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           interfaceNumber,
    _In_ UCHAR           entityID,
    _In_ ULONG           controlSelector,
    _In_ UCHAR           channelNumber,
    _In_ USHORT          current
)
{
    ULONG dataLength = 0;

    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(deviceContext == nullptr, STATUS_INVALID_PARAMETER);

    NTSTATUS status = ControlRequest(
        deviceContext,
        UsbMakeBmRequestType(BMREQUEST_HOST_TO_DEVICE, BMREQUEST_CLASS, BMREQUEST_TO_INTERFACE),
        NS_USBAudio0200::CUR,
        (((USHORT)controlSelector) << 8) | channelNumber,
        (((USHORT)entityID) << 8) | interfaceNumber,
        &current,
        sizeof(current),
        &dataLength
    );

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, "%!FUNC! %!STATUS!", status);
    return status;
}

// Layout 3 Parameter Block
__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS SetCurrentSetting(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           interfaceNumber,
    _In_ UCHAR           entityID,
    _In_ ULONG           controlSelector,
    _In_ UCHAR           channelNumber,
    _In_ ULONG           current
)
{
    ULONG dataLength = 0;

    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(deviceContext == nullptr, STATUS_INVALID_PARAMETER);

    NTSTATUS status = ControlRequest(
        deviceContext,
        UsbMakeBmRequestType(BMREQUEST_HOST_TO_DEVICE, BMREQUEST_CLASS, BMREQUEST_TO_INTERFACE),
        NS_USBAudio0200::CUR,
        (((USHORT)controlSelector) << 8) | channelNumber,
        (((USHORT)entityID) << 8) | interfaceNumber,
        &current,
        sizeof(current),
        &dataLength
    );

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, "%!FUNC! %!STATUS!", status);
    return status;
}

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS GetRangeParameterBlockLayout2(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           interfaceNumber,
    _In_ UCHAR           entityID,
    _In_ UCHAR           controlSelector,
    _In_ UCHAR           channelNumber,
    _Inout_ USHORT &     length,
    _In_opt_ void *      dataBuffer
)
/*++

Routine Description:

    Gets the 2-byte Control RANGE Parameter Block, or the size required to get it.

Arguments:

    deviceContext -

    interfaceNumber -

    entityID -

    controlSelector -

    channelNumber -

    length - Input is the size of the dataBuffer. Output is the required size of the dataBuffer or the size written to the dataBuffer.

    dataBuffer - nullptr or a buffer of length bytes or more

Return Value:

    NTSTATUS - NT status value. If nullptr is specified for dataBuffer, the required size is set to length and STATUS_BUFFER_TOO_SMALL is returned.

--*/
{
    ULONG                                                  dataBufferLength = 0;
    void *                                                 dataBufferTemp = nullptr;
    NS_USBAudio0200::CONTROL_RANGE_PARAMETER_BLOCK_LAYOUT2 block{};

    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(deviceContext == nullptr, STATUS_INVALID_PARAMETER);

    if (dataBuffer == nullptr)
    {
        dataBufferLength = sizeof(block);
        dataBufferTemp = &block;
    }
    else
    {
        dataBufferLength = length;
        dataBufferTemp = dataBuffer;
    }

    NTSTATUS status = ControlRequest(
        deviceContext,
        UsbMakeBmRequestType(BMREQUEST_DEVICE_TO_HOST, BMREQUEST_CLASS, BMREQUEST_TO_INTERFACE),
        NS_USBAudio0200::RANGE,
        (((USHORT)controlSelector) << 8) | channelNumber,
        (((USHORT)entityID) << 8) | interfaceNumber,
        dataBufferTemp,
        dataBufferLength,
        &dataBufferLength
    );

    if (NT_SUCCESS(status))
    {
        NS_USBAudio0200::CONTROL_RANGE_PARAMETER_BLOCK_LAYOUT2 * parameterBlock = (NS_USBAudio0200::CONTROL_RANGE_PARAMETER_BLOCK_LAYOUT2 *)dataBufferTemp;
        if (parameterBlock->wNumSubRanges != 0)
        {
            length = sizeof(NS_USBAudio0200::CONTROL_RANGE_PARAMETER_BLOCK_LAYOUT2) + (sizeof(NS_USBAudio0200::CONTROL_RANGE_PARAMETER_BLOCK_LAYOUT2) - sizeof(USHORT)) * (parameterBlock->wNumSubRanges - 1);
        }
        else
        {
            length = sizeof(NS_USBAudio0200::CONTROL_RANGE_PARAMETER_BLOCK_LAYOUT2);
        }
        if (dataBuffer == nullptr)
        {
            status = STATUS_BUFFER_TOO_SMALL;
        }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, "%!FUNC! %!STATUS!", status);
    return status;
}

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS GetRangeParameterBlockLayout3(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           interfaceNumber,
    _In_ UCHAR           entityID,
    _In_ UCHAR           controlSelector,
    _In_ UCHAR           channelNumber,
    _Inout_ USHORT &     length,
    _In_opt_ void *      dataBuffer
)
/*++

Routine Description:

    Gets the 4-byte Control RANGE Parameter Block, or the size required to get it.

Arguments:

    deviceContext -

    interfaceNumber -

    entityID -

    controlSelector -

    channelNumber -

    length - Input is the size of the dataBuffer. Output is the required size of the dataBuffer or the size written to the dataBuffer.

    dataBuffer - nullptr or a buffer of length bytes or more

Return Value:

    NTSTATUS - NT status value. If nullptr is specified for dataBuffer, the required size is set to length and STATUS_BUFFER_TOO_SMALL is returned.

--*/
{
    ULONG                                                  dataBufferLength = 0;
    void *                                                 dataBufferTemp = nullptr;
    NS_USBAudio0200::CONTROL_RANGE_PARAMETER_BLOCK_LAYOUT3 block{};

    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(deviceContext == nullptr, STATUS_INVALID_PARAMETER);

    if (dataBuffer == nullptr)
    {
        dataBufferLength = sizeof(block);
        dataBufferTemp = &block;
    }
    else
    {
        dataBufferLength = length;
        dataBufferTemp = dataBuffer;
    }

    NTSTATUS status = ControlRequest(
        deviceContext,
        UsbMakeBmRequestType(BMREQUEST_DEVICE_TO_HOST, BMREQUEST_CLASS, BMREQUEST_TO_INTERFACE),
        NS_USBAudio0200::RANGE,
        (((USHORT)controlSelector) << 8) | channelNumber,
        (((USHORT)entityID) << 8) | interfaceNumber,
        dataBufferTemp,
        dataBufferLength,
        &dataBufferLength
    );

    if (NT_SUCCESS(status))
    {
        NS_USBAudio0200::CONTROL_RANGE_PARAMETER_BLOCK_LAYOUT3 * parameterBlock = (NS_USBAudio0200::CONTROL_RANGE_PARAMETER_BLOCK_LAYOUT3 *)dataBufferTemp;
        if (parameterBlock->wNumSubRanges != 0)
        {
            length = sizeof(NS_USBAudio0200::CONTROL_RANGE_PARAMETER_BLOCK_LAYOUT3) + (sizeof(NS_USBAudio0200::CONTROL_RANGE_PARAMETER_BLOCK_LAYOUT3) - sizeof(USHORT)) * (parameterBlock->wNumSubRanges - 1);
        }
        else
        {
            length = sizeof(NS_USBAudio0200::CONTROL_RANGE_PARAMETER_BLOCK_LAYOUT3);
        }
        if (dataBuffer == nullptr)
        {
            status = STATUS_BUFFER_TOO_SMALL;
        }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, "%!FUNC! %!STATUS!", status);
    return status;
}

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static _Success_(return)
NTSTATUS GetRangeWithAllocate(
    _In_ WDFOBJECT       parentObject,
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           interfaceNumber,
    _In_ UCHAR           entityID,
    _In_ UCHAR           controlSelector,
    _In_ UCHAR           channelNumber,
    _Out_ WDFMEMORY &    memory,
    _Out_ NS_USBAudio0200::PCONTROL_RANGE_PARAMETER_BLOCK_LAYOUT2 & parameterBlock
)
{
    USHORT                length = 0;
    WDF_OBJECT_ATTRIBUTES attributes;

    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(parentObject == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE(deviceContext == nullptr, STATUS_INVALID_PARAMETER);

    memory = nullptr;
    parameterBlock = nullptr;

    NTSTATUS status = GetRangeParameterBlockLayout2(deviceContext, interfaceNumber, entityID, controlSelector, channelNumber, length, nullptr);

    if (status != STATUS_BUFFER_TOO_SMALL)
    {
        return status;
    }
    if (length == 0)
    {
        return STATUS_UNSUCCESSFUL;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = parentObject;
    RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, length, &memory, (PVOID *)&parameterBlock));

    status = GetRangeParameterBlockLayout2(deviceContext, interfaceNumber, entityID, controlSelector, channelNumber, length, parameterBlock);

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, "%!FUNC! %!STATUS!", status);
    return status;
}

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static _Success_(return)
NTSTATUS GetRangeWithAllocate(
    _In_ WDFOBJECT       parentObject,
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           interfaceNumber,
    _In_ UCHAR           entityID,
    _In_ UCHAR           controlSelector,
    _In_ UCHAR           channelNumber,
    _Out_ WDFMEMORY &    memory,
    _Out_ NS_USBAudio0200::PCONTROL_RANGE_PARAMETER_BLOCK_LAYOUT3 & parameterBlock
)
{
    USHORT                length = 0;
    WDF_OBJECT_ATTRIBUTES attributes;

    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(parentObject == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE(deviceContext == nullptr, STATUS_INVALID_PARAMETER);

    memory = nullptr;
    parameterBlock = nullptr;

    NTSTATUS status = GetRangeParameterBlockLayout3(deviceContext, interfaceNumber, entityID, controlSelector, channelNumber, length, nullptr);

    if (status != STATUS_BUFFER_TOO_SMALL)
    {
        return status;
    }
    if (length == 0)
    {
        return STATUS_UNSUCCESSFUL;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = parentObject;
    RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, length, &memory, (PVOID *)&parameterBlock));

    status = GetRangeParameterBlockLayout3(deviceContext, interfaceNumber, entityID, controlSelector, channelNumber, length, parameterBlock);

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, "%!FUNC! %!STATUS!", status);
    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS ControlRequestGetSampleFrequency(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           interfaceNumber,
    UCHAR           entityID,
    ULONG &         sampleRate
)
{
    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(deviceContext == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE(!deviceContext->UsbAudioConfiguration->isUSBAudio2(), STATUS_NOT_SUPPORTED);

    NTSTATUS status = GetCurrentSetting(
        deviceContext,
        interfaceNumber,
        entityID,
        NS_USBAudio0200::CS_SAM_FREQ_CONTROL,
        0,
        sampleRate
    );

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, "%!FUNC! %!STATUS!, %d", status, sampleRate);
    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS ControlRequestSetSampleFrequency(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           interfaceNumber,
    UCHAR           entityID,
    ULONG           desiredSampleRate
)
{
    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(deviceContext == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE(!deviceContext->UsbAudioConfiguration->isUSBAudio2(), STATUS_NOT_SUPPORTED);

    NTSTATUS status = SetCurrentSetting(
        deviceContext,
        interfaceNumber,
        entityID,
        NS_USBAudio0200::CS_SAM_FREQ_CONTROL,
        0,
        desiredSampleRate
    );

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, "%!FUNC! %!STATUS!, %d", status, desiredSampleRate);
    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS ControlRequestGetSampleFrequencyRange(
    PDEVICE_CONTEXT                                           deviceContext,
    UCHAR                                                     interfaceNumber,
    UCHAR                                                     entityID,
    WDFMEMORY &                                               memory,
    NS_USBAudio0200::PCONTROL_RANGE_PARAMETER_BLOCK_LAYOUT3 & parameterBlock
)
{
    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(deviceContext == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE(!deviceContext->UsbAudioConfiguration->isUSBAudio2(), STATUS_NOT_SUPPORTED);

    NTSTATUS status = GetRangeWithAllocate(
        deviceContext->UsbDevice,
        deviceContext,
        interfaceNumber,
        entityID,
        NS_USBAudio0200::CS_SAM_FREQ_CONTROL,
        0,
        memory,
        parameterBlock
    );

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, "%!FUNC! %!STATUS!", status);
    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS ControlRequestGetClockSelector(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           interfaceNumber,
    UCHAR           entityID,
    UCHAR &         clockSelectorIndex
)
{
    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(deviceContext == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE(!deviceContext->UsbAudioConfiguration->isUSBAudio2(), STATUS_NOT_SUPPORTED);

    NTSTATUS status = GetCurrentSetting(
        deviceContext,
        interfaceNumber,
        entityID,
        NS_USBAudio0200::CX_CLOCK_SELECTOR_CONTROL,
        0,
        clockSelectorIndex
    );

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, "%!FUNC! %!STATUS!", status);
    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS ControlRequestSetClockSelector(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           interfaceNumber,
    UCHAR           entityID,
    UCHAR           clockSelectorIndex
)
{
    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(deviceContext == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE(!deviceContext->UsbAudioConfiguration->isUSBAudio2(), STATUS_NOT_SUPPORTED);

    NTSTATUS status = SetCurrentSetting(
        deviceContext,
        interfaceNumber,
        entityID,
        NS_USBAudio0200::CX_CLOCK_SELECTOR_CONTROL,
        0,
        clockSelectorIndex
    );

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, "%!FUNC! %!STATUS!", status);
    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
ControlRequestGetACTValAltSettingsControl(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           interfaceNumber,
    ULONG &         validAlternateSettingMap
)
{
    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(deviceContext == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE(!deviceContext->UsbAudioConfiguration->isUSBAudio2(), STATUS_NOT_SUPPORTED);

    NTSTATUS status = GetCurrentSetting(
        deviceContext,
        interfaceNumber,
        0,
        NS_USBAudio0200::AS_VAL_ALT_SETTINGS_CONTROL,
        0,
        validAlternateSettingMap
    );

    if (NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, " - AS_VAL_ALT_SETTINGS_CONTROL : %02x %02x", validAlternateSettingMap & 0xff, (validAlternateSettingMap >> 8) & 0xff);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, "%!FUNC! %!STATUS!", status);
    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
ControlRequestGetACTAltSettingsControl(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           interfaceNumber,
    UCHAR &         activeAlternateSetting
)
{
    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(deviceContext == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE(!deviceContext->UsbAudioConfiguration->isUSBAudio2(), STATUS_NOT_SUPPORTED);

    activeAlternateSetting = 0;

    NTSTATUS status = GetCurrentSetting(
        deviceContext,
        interfaceNumber,
        0,
        NS_USBAudio0200::AS_ACT_ALT_SETTING_CONTROL,
        0,
        activeAlternateSetting
    );

    if (NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, " - AS_ACT_ALT_SETTING_CONTROL : %02x", activeAlternateSetting);
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, "%!FUNC! %!STATUS!", status);
    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS ControlRequestGetAudioDataFormat(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           interfaceNumber,
    ULONG &         audioDataFormat
)
{
    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(!deviceContext->UsbAudioConfiguration->isUSBAudio2(), STATUS_NOT_SUPPORTED);

    NTSTATUS status = GetCurrentSetting(
        deviceContext,
        interfaceNumber,
        0,
        NS_USBAudio0200::AS_AUDIO_DATA_FORMAT_CONTROL,
        0,
        audioDataFormat
    );

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, "%!FUNC! %!STATUS!", status);
    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS ControlRequestSetAudioDataFormat(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           interfaceNumber,
    ULONG           audioDataFormat
)
{
    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(!deviceContext->UsbAudioConfiguration->isUSBAudio2(), STATUS_NOT_SUPPORTED);

    NTSTATUS status = SetCurrentSetting(
        deviceContext,
        interfaceNumber,
        0,
        NS_USBAudio0200::AS_AUDIO_DATA_FORMAT_CONTROL,
        0,
        audioDataFormat
    );

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, "%!FUNC! %!STATUS!", status);
    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS ControlRequestGetMute(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           interfaceNumber,
    UCHAR           entityID,
    UCHAR           channel,
    bool &          mute
)
{
    UCHAR current;
    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(deviceContext == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE(!deviceContext->UsbAudioConfiguration->isUSBAudio2(), STATUS_NOT_SUPPORTED);

    NTSTATUS status = GetCurrentSetting(
        deviceContext,
        interfaceNumber,
        entityID,
        NS_USBAudio0200::FU_MUTE_CONTROL,
        channel,
        current
    );

    if (NT_SUCCESS(status))
    {
        mute = current ? true : false;
    }
    else
    {
        mute = true;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, "%!FUNC! %!STATUS!", status);
    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS ControlRequestSetMute(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           interfaceNumber,
    UCHAR           entityID,
    UCHAR           channel,
    bool            mute
)
{
    UCHAR current = mute ? 1 : 0;

    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(deviceContext == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE(!deviceContext->UsbAudioConfiguration->isUSBAudio2(), STATUS_NOT_SUPPORTED);

    NTSTATUS status = SetCurrentSetting(
        deviceContext,
        interfaceNumber,
        entityID,
        NS_USBAudio0200::FU_MUTE_CONTROL,
        channel,
        current
    );

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, "%!FUNC! %!STATUS!", status);
    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS ControlRequestGetVolume(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           interfaceNumber,
    UCHAR           entityID,
    UCHAR           channel,
    USHORT &        volume
)
{
    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(deviceContext == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE(!deviceContext->UsbAudioConfiguration->isUSBAudio2(), STATUS_NOT_SUPPORTED);

    NTSTATUS status = GetCurrentSetting(
        deviceContext,
        interfaceNumber,
        entityID,
        NS_USBAudio0200::FU_VOLUME_CONTROL,
        channel,
        volume
    );

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, "%!FUNC! %!STATUS!", status);
    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS ControlRequestSetVolume(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           interfaceNumber,
    UCHAR           entityID,
    UCHAR           channel,
    USHORT          volume
)
{
    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(deviceContext == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE(!deviceContext->UsbAudioConfiguration->isUSBAudio2(), STATUS_NOT_SUPPORTED);

    NTSTATUS status = SetCurrentSetting(
        deviceContext,
        interfaceNumber,
        entityID,
        NS_USBAudio0200::FU_VOLUME_CONTROL,
        channel,
        volume
    );

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, "%!FUNC! %!STATUS!", status);
    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS ControlRequestGetVolumeRange(
    PDEVICE_CONTEXT                                           deviceContext,
    UCHAR                                                     interfaceNumber,
    UCHAR                                                     entityID,
    UCHAR                                                     channel,
    WDFMEMORY &                                               memory,
    NS_USBAudio0200::PCONTROL_RANGE_PARAMETER_BLOCK_LAYOUT2 & parameterBlock
)
{
    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(deviceContext == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE(!deviceContext->UsbAudioConfiguration->isUSBAudio2(), STATUS_NOT_SUPPORTED);

    NTSTATUS status = GetRangeWithAllocate(
        deviceContext->UsbDevice,
        deviceContext,
        interfaceNumber,
        entityID,
        NS_USBAudio0200::FU_VOLUME_CONTROL,
        channel,
        memory,
        parameterBlock
    );

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, "%!FUNC! %!STATUS!", status);
    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS ControlRequestGetAutoGainControl(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           interfaceNumber,
    UCHAR           entityID,
    UCHAR           channel,
    bool &          autoGain
)
{
    UCHAR current;
    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(deviceContext == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE(!deviceContext->UsbAudioConfiguration->isUSBAudio2(), STATUS_NOT_SUPPORTED);

    NTSTATUS status = GetCurrentSetting(
        deviceContext,
        interfaceNumber,
        entityID,
        NS_USBAudio0200::FU_AUTOMATIC_GAIN_CONTROL,
        channel,
        current
    );
    if (NT_SUCCESS(status))
    {
        autoGain = current ? true : false;
    }
    else
    {
        autoGain = false;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, "%!FUNC! %!STATUS!", status);
    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS ControlRequestSetAutoGainControl(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           interfaceNumber,
    UCHAR           entityID,
    UCHAR           channel,
    bool            autoGain
)
{
    UCHAR current = autoGain ? 1 : 0;

    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(!deviceContext->UsbAudioConfiguration->isUSBAudio2(), STATUS_NOT_SUPPORTED);

    NTSTATUS status = SetCurrentSetting(
        deviceContext,
        interfaceNumber,
        entityID,
        NS_USBAudio0200::FU_AUTOMATIC_GAIN_CONTROL,
        channel,
        current
    );

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, "%!FUNC! %!STATUS!", status);
    return status;
}

#if 0
// UAC 1.0 only
PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
ControlRequestSetSampleRate(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           endpointAddress,
    ULONG           desiredSampleRate
)
{
    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(deviceContext == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE(deviceContext->UsbAudioConfiguration->isUSBAudio2(), STATUS_NOT_SUPPORTED);

    NTSTATUS status = ControlRequest(
        deviceContext,
        UsbMakeBmRequestType(BMREQUEST_HOST_TO_DEVICE, BMREQUEST_CLASS, BMREQUEST_TO_ENDPOINT),
        NS_USBAudio::SET_CUR,
        NS_USBAudio0100::SAMPLING_FREQ_CONTROL << 8,
        endpointAddress,
        &desiredSampleRate,
        3,
        nullptr,
        100
    );
    if (NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, " - SET_CUR SAMPLING_FREQ_CONTROL : %d", desiredSampleRate);
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, "%!FUNC! %!STATUS!", status);
    return status;
}

// UAC 1.0 only
PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
ControlRequestGetSampleRate(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           endpointAddress,
    ULONG &         currentSampleRate
)
{
    PAGED_CODE();

    RETURN_NTSTATUS_IF_TRUE(deviceContext == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE(deviceContext->UsbAudioConfiguration->isUSBAudio2(), STATUS_NOT_SUPPORTED);

    NTSTATUS status = ControlRequest(
        deviceContext,
        UsbMakeBmRequestType(BMREQUEST_DEVICE_TO_HOST, BMREQUEST_CLASS, BMREQUEST_TO_ENDPOINT),
        NS_USBAudio::GET_CUR,
        NS_USBAudio0100::SAMPLING_FREQ_CONTROL << 8,
        endpointAddress,
        &currentSampleRate,
        3,
        nullptr,
        100
    );
    if (NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, " - GET_CUR SAMPLING_FREQ_CONTROL : %0d", currentSampleRate);
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CTRLREQUEST, "%!FUNC! %!STATUS!", status);
    return status;
}
#endif
