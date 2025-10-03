// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

/*++

Module Name:

    TransferObject.cpp

Abstract:

    Implement a class to manage a single USB transfer.

Environment:

    Kernel-mode Driver Framework

--*/

#include "Driver.h"
#include "Device.h"
#include "Public.h"
#include "Common.h"
#include "USBAudio.h"
#include "TransferObject.h"
#include "StreamObject.h"

#ifndef __INTELLISENSE__
#include "TransferObject.tmh"
#endif

_Use_decl_annotations_
PAGED_CODE_SEG
TransferObject * TransferObject::Create(
    PDEVICE_CONTEXT deviceContext,
    StreamObject *  streamObject,
    LONG            index,
    IsoDirection    direction
)
{
    PAGED_CODE();

    return new (POOL_FLAG_NON_PAGED, DRIVER_TAG) TransferObject(deviceContext, streamObject, index, direction);
}

_Use_decl_annotations_
PAGED_CODE_SEG
TransferObject::TransferObject(
    PDEVICE_CONTEXT deviceContext,
    StreamObject *  streamObject,
    LONG            index,
    IsoDirection    direction
)
    : m_deviceContext(deviceContext), m_streamObject(streamObject), m_index(index), m_direction(direction)
{
    NTSTATUS              status = STATUS_SUCCESS;
    WDF_OBJECT_ATTRIBUTES attributes;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

    attributes.ParentObject = m_deviceContext->Device;
    status = WdfSpinLockCreate(&attributes, &m_spinLock);
    ASSERT(NT_SUCCESS(status));

    KeInitializeEvent(&m_requestCompletedEvent, NotificationEvent, TRUE);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
TransferObject::~TransferObject()
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");
    Free();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
TransferObject::AttachDataBuffer(
    PUCHAR dataBuffer,
    ULONG  numIsoPackets, // Number of packets in IsoPacket within the URB.
    ULONG  isoPacketSize, // Interval per offset of IsoPacket within the URB, for receive operations.
    ULONG  maxXferSize    // Size of the buffer used for transfer in the URB.
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry, m_index = %u, numIsoPackets = %u, isoPacketSize = %u, maxXferSize = %u", m_index, numIsoPackets, isoPacketSize, maxXferSize);

    IF_TRUE_ACTION_JUMP(numIsoPackets == 0, status = STATUS_INVALID_PARAMETER, AttachDataBuffer_Exit);
    IF_TRUE_ACTION_JUMP(isoPacketSize == 0, status = STATUS_INVALID_PARAMETER, AttachDataBuffer_Exit);
    IF_TRUE_ACTION_JUMP(maxXferSize == 0, status = STATUS_INVALID_PARAMETER, AttachDataBuffer_Exit);

    m_dataBuffer = dataBuffer;
    m_numIsoPackets = numIsoPackets;
    m_isoPacketSize = isoPacketSize;
    m_maxXferSize = maxXferSize;

AttachDataBuffer_Exit:

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void TransferObject::Free()
{
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry, m_index = %u", m_index);

    WdfSpinLockAcquire(m_spinLock);

    if (m_dataBufferMdl != nullptr)
    {
        IoFreeMdl(m_dataBufferMdl);
        m_dataBufferMdl = nullptr;
    }

    if (m_urbMemory != nullptr)
    {
        // The UrbMemory allocated by WdfUsbTargetDeviceCreateIsochUrb() should not be freed manually; it is managed by the WDF framework.
        // Freeing it in the driver will cause a BSOD.
        // WdfObjectDelete(m_urbMemory);
        m_urbMemory = nullptr;
        m_urb = nullptr;
    }
    m_dataBuffer = nullptr;
    WdfSpinLockRelease(m_spinLock);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
TransferObject::Reset()
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry, m_index = %u", m_index);

    m_isCompleted = false;
    m_feedbackRemainder = 0;
    m_feedbackSamples = 0;
    m_presendSamples = 0;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
NTSTATUS
TransferObject::SetUrbIsochronousParametersInput(
    ULONG                          startFrame,
    WDFUSBPIPE                     pipe,
    bool                           asap,
    EVT_WDF_OBJECT_CONTEXT_CLEANUP requestContextCleanup
)
{
    NTSTATUS              status = STATUS_SUCCESS;
    WDF_OBJECT_ATTRIBUTES attributes;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry, startFrame = %u, NumPackets = %u, PacketSize = %u, maxXferSize = %u, m_index = %u", startFrame, m_numIsoPackets, m_isoPacketSize, m_maxXferSize, m_index);

    auto setUrbIsochronousParametersInScope = wil::scope_exit([&]() {
        if (!NT_SUCCESS(status))
        {
            if (status == STATUS_INVALID_PARAMETER)
            {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, " - NumPackets = %d, PacketSize = %d, maxXferSize = %d, pipe = %p, m_dataBuffer = %p", m_numIsoPackets, m_isoPacketSize, m_maxXferSize, pipe, m_dataBuffer);
            }
            if (status == STATUS_UNSUCCESSFUL)
            {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, " - ContiguousMemory = %p, m_request = %p", m_deviceContext->ContiguousMemory, m_request);
            }
            Free();
        }

        if (!NT_SUCCESS(status))
        {
            WdfSpinLockAcquire(m_spinLock);

            // Since WdfRequestCreate is used to create the request within this function, the following call is unnecessary.
            // WdfRequestCompleteWithInformation(request, status, 0);
            if (m_request != nullptr)
            {
                WdfObjectDelete(m_request);
                m_request = nullptr;
            }
            WdfSpinLockRelease(m_spinLock);
        }
    });

    RETURN_NTSTATUS_IF_TRUE_ACTION(pipe == nullptr, status = STATUS_INVALID_PARAMETER, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION(m_deviceContext->ContiguousMemory == nullptr, status = STATUS_UNSUCCESSFUL, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION(m_dataBuffer == nullptr, status = STATUS_INVALID_PARAMETER, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION(m_request != nullptr, status = STATUS_UNSUCCESSFUL, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION(m_numIsoPackets == 0, status = STATUS_UNSUCCESSFUL, status);

    // Since the context set by WdfDeviceInitSetRequestAttributes() is not applied to the request created here, a new ISOCHRONOUS_REQUEST_CONTEXT is set.
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, ISOCHRONOUS_REQUEST_CONTEXT);
    attributes.ParentObject = m_deviceContext->Device;
    attributes.EvtCleanupCallback = requestContextCleanup;

    // If nullptr is specified for the WDFIOTARGET IoTarget in WdfRequestCreate(),
    // a BSOD with KMODE_EXCEPTION_NOT_HANDLED (1e) will occur in WdfRequestRetrieveInputWdmMdl().
    // If WDF_OBJECT_ATTRIBUTES::ParentObject is nullptr in WdfRequestCreate(),
    // a BSOD with DRIVER_IRQL_NOT_LESS_OR_EQUAL (d1) will occur in FxRequest::GetMdl within WdfRequestRetrieveInputWdmMdl().
    {
        WdfSpinLockAcquire(m_spinLock);
        status = WdfRequestCreate(&attributes, WdfUsbTargetPipeGetIoTarget(pipe), &m_request);
        WdfSpinLockRelease(m_spinLock);
    }
    RETURN_NTSTATUS_IF_FAILED_MSG(status, "WdfRequestCreate failed");

    {
        WdfSpinLockAcquire(m_spinLock);
        if (m_dataBufferMdl == nullptr)
        {
            // Using an MDL allocated with maxXferSize set to 0 causes a DRIVER_IRQL_NOT_LESS_OR_EQUAL (d1) BSOD in USBXHCI.SYS.
            m_dataBufferMdl = IoAllocateMdl(m_dataBuffer, (ULONG)m_maxXferSize, FALSE, FALSE, nullptr);
            if (m_dataBufferMdl == nullptr)
            {
                WdfSpinLockRelease(m_spinLock);
                status = STATUS_INSUFFICIENT_RESOURCES;
                RETURN_NTSTATUS_IF_FAILED(status);
            }
            MmBuildMdlForNonPagedPool(m_dataBufferMdl);
        }
        WdfSpinLockRelease(m_spinLock);
    }

    {
        WdfSpinLockAcquire(m_spinLock);
        if (m_urbMemory == nullptr)
        {
            //
            // Allocate memory for URB.
            //
            WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
            attributes.ParentObject = m_request; // Specifying m_deviceContext->UsbDevice causes a DRIVER_IRQL_NOT_LESS_OR_EQUAL (d1) BSOD in USBXHCI.SYS.
            status = WdfUsbTargetDeviceCreateIsochUrb(m_deviceContext->UsbDevice, &attributes, m_numIsoPackets, &m_urbMemory, nullptr);

            if (!NT_SUCCESS(status))
            {
                WdfSpinLockRelease(m_spinLock);
                RETURN_NTSTATUS_IF_FAILED_MSG(status, "WdfUsbTargetDeviceCreateIsochUrb failed");
            }
            m_urb = static_cast<PURB>(WdfMemoryGetBuffer(m_urbMemory, nullptr));
        }
        WdfSpinLockRelease(m_spinLock);
    }

    ULONG siz = GET_ISO_URB_SIZE(m_numIsoPackets);

    //    RtlZeroMemory(m_urb, siz);

    {
        PPIPE_CONTEXT pipeContext = GetPipeContext(pipe);
        ULONG         TotalLength = pipeContext->TransferSizePerFrame;
        ULONG         numberOfFrames;
        ULONG         numberOfPackets;

        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - classic frames per irp       = %u", m_deviceContext->ClassicFramesPerIrp);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - frames per ms                = %u", m_deviceContext->FramesPerMs);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - max burst override           = %u", m_deviceContext->SupportedControl.MaxBurstOverride);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - bInterval                    = %u", m_deviceContext->InputInterfaceAndPipe.PipeInfo.Interval);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - maximum packet size          = %u", m_deviceContext->InputInterfaceAndPipe.PipeInfo.MaximumPacketSize);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - transfer size per frame      = %u", pipeContext->TransferSizePerFrame);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - transfer size per microframe = %u", pipeContext->TransferSizePerMicroframe);

        RETURN_NTSTATUS_IF_TRUE_ACTION(pipeContext->TransferSizePerFrame == 0, status = STATUS_UNSUCCESSFUL, status);

        if (m_deviceContext->IsDeviceSuperSpeed && m_deviceContext->SuperSpeedCompatible)
        {
            RETURN_NTSTATUS_IF_TRUE_ACTION(pipeContext->TransferSizePerMicroframe == 0, status = STATUS_UNSUCCESSFUL, status);
            numberOfFrames = TotalLength / pipeContext->TransferSizePerFrame;
            numberOfPackets = TotalLength / pipeContext->TransferSizePerMicroframe;
        }
        else if (m_deviceContext->IsDeviceHighSpeed)
        {
            RETURN_NTSTATUS_IF_TRUE_ACTION(pipeContext->TransferSizePerMicroframe == 0, status = STATUS_UNSUCCESSFUL, status);
            numberOfFrames = TotalLength / pipeContext->TransferSizePerFrame;
            numberOfPackets = TotalLength / pipeContext->TransferSizePerMicroframe;
        }
        else
        {
            numberOfPackets = TotalLength / pipeContext->TransferSizePerFrame;
            numberOfFrames = numberOfPackets;
        }

        // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "old UrbIsochronousTransfer.Hdr.Length           = %u", GET_ISO_URB_SIZE(numberOfPackets));
        // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "old UrbIsochronousTransfer.TransferBufferLength = %u", TotalLength);
        // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "old UrbIsochronousTransfer.NumberOfPackets      = %u", numberOfPackets);
    }

    ULONG samplesPerIrp = (m_deviceContext->AudioProperty.SampleRate * m_deviceContext->ClassicFramesPerIrp) / 1000;
    ULONG samplesPerPacket = 0;
    ULONG extraSamples = 0;

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - samplesPerIrp    = %u", samplesPerIrp);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - samplesPerPacket = %u", samplesPerPacket);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - extraSamples     = %u", extraSamples);

    m_urb->UrbIsochronousTransfer.StartFrame = startFrame;
    m_urb->UrbIsochronousTransfer.Hdr.Length = (USHORT)siz;
    m_urb->UrbIsochronousTransfer.Hdr.Function = URB_FUNCTION_ISOCH_TRANSFER;
    m_urb->UrbIsochronousTransfer.PipeHandle = WdfUsbTargetPipeWdmGetPipeHandle(pipe);
    m_urb->UrbIsochronousTransfer.TransferBufferMDL = m_dataBufferMdl;
    m_urb->UrbIsochronousTransfer.NumberOfPackets = m_numIsoPackets;
    m_urb->UrbIsochronousTransfer.UrbLink = nullptr;

    {
        m_urb->UrbIsochronousTransfer.TransferFlags = USBD_TRANSFER_DIRECTION_IN | (asap ? USBD_START_ISO_TRANSFER_ASAP : 0);
        m_urb->UrbIsochronousTransfer.TransferBufferLength = m_numIsoPackets * m_isoPacketSize;

        for (ULONG i = 0; i < m_urb->UrbIsochronousTransfer.NumberOfPackets; ++i)
        {
            m_urb->UrbIsochronousTransfer.IsoPacket[i].Offset = i * m_isoPacketSize;
            m_urb->UrbIsochronousTransfer.IsoPacket[i].Length = m_isoPacketSize;
            m_urb->UrbIsochronousTransfer.IsoPacket[i].Status = 0;
            m_isoPacketBuffer[i] = &(m_dataBuffer[m_urb->UrbIsochronousTransfer.IsoPacket[i].Offset]);
            // Do not initialize m_isoPacketLength as it will be referenced in MixingEngineThread.
            // m_isoPacketLength[i]                              = 0;
        }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - UrbIsochronousTransfer.Hdr.Length           = %u", m_urb->UrbIsochronousTransfer.Hdr.Length);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - UrbIsochronousTransfer.TransferBufferLength = %u", m_urb->UrbIsochronousTransfer.TransferBufferLength);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - UrbIsochronousTransfer.NumberOfPackets      = %u", m_urb->UrbIsochronousTransfer.NumberOfPackets);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");

    return status;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
NTSTATUS
TransferObject::SetUrbIsochronousParametersOutput(
    ULONG                          startFrame,
    WDFUSBPIPE                     pipe,
    bool                           asap,
    EVT_WDF_OBJECT_CONTEXT_CLEANUP requestContextCleanup
)
{
    NTSTATUS              status = STATUS_SUCCESS;
    WDF_OBJECT_ATTRIBUTES attributes;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry, startFrame = %u, NumPackets = %u, PacketSize = %u, maxXferSize = %u, m_index = %u", startFrame, m_numIsoPackets, m_isoPacketSize, m_maxXferSize, m_index);

    auto setUrbIsochronousParametersOutScope = wil::scope_exit([&]() {
        if (!NT_SUCCESS(status))
        {
            if (status == STATUS_INVALID_PARAMETER)
            {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, " - NumPackets = %d, PacketSize = %d, maxXferSize = %d, pipe = %p, m_dataBuffer = %p", m_numIsoPackets, m_isoPacketSize, m_maxXferSize, pipe, m_dataBuffer);
            }
            if (status == STATUS_UNSUCCESSFUL)
            {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, " - ContiguousMemory = %p, m_request = %p", m_deviceContext->ContiguousMemory, m_request);
            }
            Free();
        }

        if (!NT_SUCCESS(status))
        {
            WdfSpinLockAcquire(m_spinLock);

            // Since the request is created using WdfRequestCreate inside this function, the following call is unnecessary.
            // WdfRequestCompleteWithInformation(request, status, 0);
            if (m_request != nullptr)
            {
                WdfObjectDelete(m_request);
                m_request = nullptr;
            }
            WdfSpinLockRelease(m_spinLock);
        }
    });

    RETURN_NTSTATUS_IF_TRUE_ACTION(pipe == nullptr, status = STATUS_INVALID_PARAMETER, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION(m_deviceContext->ContiguousMemory == nullptr, status = STATUS_UNSUCCESSFUL, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION(m_dataBuffer == nullptr, status = STATUS_INVALID_PARAMETER, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION(m_request != nullptr, status = STATUS_UNSUCCESSFUL, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION(m_numIsoPackets == 0, status = STATUS_UNSUCCESSFUL, status);

    // The context set by WdfDeviceInitSetRequestAttributes() is not applied to the request created here, so a new ISOCHRONOUS_REQUEST_CONTEXT is set.
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, ISOCHRONOUS_REQUEST_CONTEXT);
    attributes.ParentObject = m_deviceContext->Device;
    attributes.EvtCleanupCallback = requestContextCleanup;

    // Specifying nullptr for the WDFIOTARGET IoTarget in WdfRequestCreate() causes a KMODE_EXCEPTION_NOT_HANDLED (1e) BSOD in WdfRequestRetrieveInputWdmMdl().
    // If WDF_OBJECT_ATTRIBUTES::ParentObject is set to nullptr in WdfRequestCreate(), a DRIVER_IRQL_NOT_LESS_OR_EQUAL (d1) BSOD occurs in FxRequest::GetMdl within WdfRequestRetrieveInputWdmMdl().
    {
        WdfSpinLockAcquire(m_spinLock);
        status = WdfRequestCreate(&attributes, WdfUsbTargetPipeGetIoTarget(pipe), &m_request);
        WdfSpinLockRelease(m_spinLock);
    }
    RETURN_NTSTATUS_IF_FAILED_MSG(status, "WdfRequestCreate failed");

    {
        WdfSpinLockAcquire(m_spinLock);
        if (m_dataBufferMdl == nullptr)
        {
            // Using an MDL allocated with maxXferSize set to 0 causes a DRIVER_IRQL_NOT_LESS_OR_EQUAL (d1) BSOD in USBXHCI.SYS.
            m_dataBufferMdl = IoAllocateMdl(m_dataBuffer, (ULONG)m_maxXferSize, FALSE, FALSE, nullptr);
            if (m_dataBufferMdl == nullptr)
            {
                WdfSpinLockRelease(m_spinLock);
                status = STATUS_INSUFFICIENT_RESOURCES;
                RETURN_NTSTATUS_IF_FAILED(status);
            }
            MmBuildMdlForNonPagedPool(m_dataBufferMdl);
        }
        WdfSpinLockRelease(m_spinLock);
    }

    {
        WdfSpinLockAcquire(m_spinLock);
        if (m_urbMemory == nullptr)
        {
            //
            // Allocate memory for URB.
            //
            WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
            attributes.ParentObject = m_request; // Specifying m_deviceContext->UsbDevice causes a DRIVER_IRQL_NOT_LESS_OR_EQUAL (d1) BSOD in USBXHCI.SYS.
            status = WdfUsbTargetDeviceCreateIsochUrb(m_deviceContext->UsbDevice, &attributes, m_numIsoPackets, &m_urbMemory, nullptr);

            if (!NT_SUCCESS(status))
            {
                WdfSpinLockRelease(m_spinLock);
                RETURN_NTSTATUS_IF_FAILED_MSG(status, "WdfUsbTargetDeviceCreateIsochUrb failed");
            }
            m_urb = static_cast<PURB>(WdfMemoryGetBuffer(m_urbMemory, nullptr));
        }
        WdfSpinLockRelease(m_spinLock);
    }

    ULONG siz = GET_ISO_URB_SIZE(m_numIsoPackets);

    //    RtlZeroMemory(m_urb, siz);

    {
        PPIPE_CONTEXT pipeContext = GetPipeContext(pipe);
        ULONG         TotalLength = pipeContext->TransferSizePerFrame;
        ULONG         numberOfFrames;
        ULONG         numberOfPackets;

        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - classic frames per irp       = %u", m_deviceContext->ClassicFramesPerIrp);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - frames per ms                = %u", m_deviceContext->FramesPerMs);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - max burst override           = %u", m_deviceContext->SupportedControl.MaxBurstOverride);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - bInterval                    = %u", m_deviceContext->OutputInterfaceAndPipe.PipeInfo.Interval);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - maximum packet size          = %u", m_deviceContext->OutputInterfaceAndPipe.PipeInfo.MaximumPacketSize);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - transfer size per frame      = %u", pipeContext->TransferSizePerFrame);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - transfer size per microframe = %u", pipeContext->TransferSizePerMicroframe);

        RETURN_NTSTATUS_IF_TRUE_ACTION(pipeContext->TransferSizePerFrame == 0, status = STATUS_UNSUCCESSFUL, status);

        if (m_deviceContext->IsDeviceSuperSpeed && m_deviceContext->SuperSpeedCompatible)
        {
            RETURN_NTSTATUS_IF_TRUE_ACTION(pipeContext->TransferSizePerMicroframe == 0, status = STATUS_UNSUCCESSFUL, status);
            numberOfFrames = TotalLength / pipeContext->TransferSizePerFrame;
            numberOfPackets = TotalLength / pipeContext->TransferSizePerMicroframe;
        }
        else if (m_deviceContext->IsDeviceHighSpeed)
        {
            RETURN_NTSTATUS_IF_TRUE_ACTION(pipeContext->TransferSizePerMicroframe == 0, status = STATUS_UNSUCCESSFUL, status);
            numberOfFrames = TotalLength / pipeContext->TransferSizePerFrame;
            numberOfPackets = TotalLength / pipeContext->TransferSizePerMicroframe;
        }
        else
        {
            numberOfPackets = TotalLength / pipeContext->TransferSizePerFrame;
            numberOfFrames = numberOfPackets;
        }

        // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "old UrbIsochronousTransfer.Hdr.Length           = %u", GET_ISO_URB_SIZE(numberOfPackets));
        // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "old UrbIsochronousTransfer.TransferBufferLength = %u", TotalLength);
        // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "old UrbIsochronousTransfer.NumberOfPackets      = %u", numberOfPackets);
    }

    ULONG samplesPerIrp = (m_deviceContext->AudioProperty.SampleRate * m_deviceContext->ClassicFramesPerIrp) / 1000;
    ULONG samplesPerPacket = 0;
    ULONG extraSamples = 0;

    samplesPerPacket = samplesPerIrp / m_numIsoPackets;
    extraSamples = samplesPerIrp - samplesPerPacket * m_numIsoPackets;

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - samplesPerIrp    = %u", samplesPerIrp);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - samplesPerPacket = %u", samplesPerPacket);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - extraSamples     = %u", extraSamples);

    m_urb->UrbIsochronousTransfer.StartFrame = startFrame;
    m_urb->UrbIsochronousTransfer.Hdr.Length = (USHORT)siz;
    m_urb->UrbIsochronousTransfer.Hdr.Function = URB_FUNCTION_ISOCH_TRANSFER;
    m_urb->UrbIsochronousTransfer.PipeHandle = WdfUsbTargetPipeWdmGetPipeHandle(pipe);
    m_urb->UrbIsochronousTransfer.TransferBufferMDL = m_dataBufferMdl;
    m_urb->UrbIsochronousTransfer.NumberOfPackets = m_numIsoPackets;
    m_urb->UrbIsochronousTransfer.UrbLink = nullptr;

    {
        ULONG totalProcessedBytes = 0;
        m_urb->UrbIsochronousTransfer.TransferFlags = USBD_TRANSFER_DIRECTION_OUT | (asap ? USBD_START_ISO_TRANSFER_ASAP : 0);
        m_urb->UrbIsochronousTransfer.TransferBufferLength = m_streamObject->CalculateTransferSizeAndSetURB(m_index, m_urb, startFrame, m_numIsoPackets, GetLockDelayCount(), &m_asyncPacketsCount, &m_syncPacketsCount);

        DecrementLockDelayCount();

        for (ULONG i = 0; i < m_urb->UrbIsochronousTransfer.NumberOfPackets; ++i)
        {
            m_urb->UrbIsochronousTransfer.IsoPacket[i].Status = 0;
            m_isoPacketBuffer[i] = &(m_dataBuffer[m_urb->UrbIsochronousTransfer.IsoPacket[i].Offset]);
            m_isoPacketLength[i] = m_urb->UrbIsochronousTransfer.IsoPacket[i].Length;
            m_totalProcessedBytesSoFar[i] = totalProcessedBytes;
            totalProcessedBytes += m_urb->UrbIsochronousTransfer.IsoPacket[i].Length;
        }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - UrbIsochronousTransfer.Hdr.Length           = %u", m_urb->UrbIsochronousTransfer.Hdr.Length);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - UrbIsochronousTransfer.TransferBufferLength = %u", m_urb->UrbIsochronousTransfer.TransferBufferLength);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - UrbIsochronousTransfer.NumberOfPackets      = %u", m_urb->UrbIsochronousTransfer.NumberOfPackets);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");

    return status;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
NTSTATUS
TransferObject::SetUrbIsochronousParametersFeedback(
    ULONG                          startFrame,
    WDFUSBPIPE                     pipe,
    bool                           asap,
    EVT_WDF_OBJECT_CONTEXT_CLEANUP requestContextCleanup
)
{
    NTSTATUS              status = STATUS_SUCCESS;
    WDF_OBJECT_ATTRIBUTES attributes;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry, startFrame = %u, NumPackets = %u, PacketSize = %u, maxXferSize = %u, m_index = %u", startFrame, m_numIsoPackets, m_isoPacketSize, m_maxXferSize, m_index);

    auto setUrbIsochronousParametersFeedbackScope = wil::scope_exit([&]() {
        if (!NT_SUCCESS(status))
        {
            if (status == STATUS_INVALID_PARAMETER)
            {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, " - NumPackets = %d, PacketSize = %d, maxXferSize = %d, pipe = %p, m_dataBuffer = %p", m_numIsoPackets, m_isoPacketSize, m_maxXferSize, pipe, m_dataBuffer);
            }
            if (status == STATUS_UNSUCCESSFUL)
            {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, " - ContiguousMemory = %p, m_request = %p", m_deviceContext->ContiguousMemory, m_request);
            }
            Free();
        }

        if (!NT_SUCCESS(status))
        {
            WdfSpinLockAcquire(m_spinLock);

            // Since the request is created using WdfRequestCreate inside this function, the following call is unnecessary.
            // WdfRequestCompleteWithInformation(request, status, 0);
            if (m_request != nullptr)
            {
                WdfObjectDelete(m_request);
                m_request = nullptr;
            }
            WdfSpinLockRelease(m_spinLock);
        }
    });

    RETURN_NTSTATUS_IF_TRUE_ACTION(pipe == nullptr, status = STATUS_INVALID_PARAMETER, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION(m_deviceContext->ContiguousMemory == nullptr, status = STATUS_UNSUCCESSFUL, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION(m_dataBuffer == nullptr, status = STATUS_INVALID_PARAMETER, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION(m_request != nullptr, status = STATUS_UNSUCCESSFUL, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION(m_numIsoPackets == 0, status = STATUS_UNSUCCESSFUL, status);

    // The context set by WdfDeviceInitSetRequestAttributes() is not applied to the request created here, so a new ISOCHRONOUS_REQUEST_CONTEXT is set.
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, ISOCHRONOUS_REQUEST_CONTEXT);
    attributes.ParentObject = m_deviceContext->Device;
    attributes.EvtCleanupCallback = requestContextCleanup;

    // If nullptr is specified for the WDFIOTARGET IoTarget in WdfRequestCreate(), a KMODE_EXCEPTION_NOT_HANDLED (1e) BSOD occurs in WdfRequestRetrieveInputWdmMdl().
    // If WDF_OBJECT_ATTRIBUTES::ParentObject is set to nullptr in WdfRequestCreate(), a DRIVER_IRQL_NOT_LESS_OR_EQUAL (d1) BSOD occurs in FxRequest::GetMdl within WdfRequestRetrieveInputWdmMdl().
    {
        WdfSpinLockAcquire(m_spinLock);
        status = WdfRequestCreate(&attributes, WdfUsbTargetPipeGetIoTarget(pipe), &m_request);
        WdfSpinLockRelease(m_spinLock);
    }
    RETURN_NTSTATUS_IF_FAILED_MSG(status, "WdfRequestCreate failed");

    {
        WdfSpinLockAcquire(m_spinLock);
        if (m_dataBufferMdl == nullptr)
        {
            // Using an MDL allocated with maxXferSize set to 0 causes a DRIVER_IRQL_NOT_LESS_OR_EQUAL (d1) BSOD in USBXHCI.SYS.
            m_dataBufferMdl = IoAllocateMdl(m_dataBuffer, (ULONG)m_maxXferSize, FALSE, FALSE, nullptr);
            if (m_dataBufferMdl == nullptr)
            {
                WdfSpinLockRelease(m_spinLock);
                status = STATUS_INSUFFICIENT_RESOURCES;
                RETURN_NTSTATUS_IF_FAILED(status);
            }

            MmBuildMdlForNonPagedPool(m_dataBufferMdl);
        }
        WdfSpinLockRelease(m_spinLock);
    }

    {
        WdfSpinLockAcquire(m_spinLock);
        if (m_urbMemory == nullptr)
        {
            //
            // Allocate memory for URB.
            //
            WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
            attributes.ParentObject = m_request; // Specifying m_deviceContext->UsbDevice causes a DRIVER_IRQL_NOT_LESS_OR_EQUAL (d1) BSOD in USBXHCI.SYS.
            status = WdfUsbTargetDeviceCreateIsochUrb(m_deviceContext->UsbDevice, &attributes, m_numIsoPackets, &m_urbMemory, nullptr);

            if (!NT_SUCCESS(status))
            {
                WdfSpinLockRelease(m_spinLock);
                RETURN_NTSTATUS_IF_FAILED_MSG(status, "WdfUsbTargetDeviceCreateIsochUrb failed");
            }
            m_urb = static_cast<PURB>(WdfMemoryGetBuffer(m_urbMemory, nullptr));
        }
        WdfSpinLockRelease(m_spinLock);
    }

    ULONG siz = GET_ISO_URB_SIZE(m_numIsoPackets);

    //    RtlZeroMemory(m_urb, siz);

    {
        PPIPE_CONTEXT pipeContext = GetPipeContext(pipe);
        ULONG         TotalLength = pipeContext->TransferSizePerFrame;
        ULONG         numberOfFrames;
        ULONG         numberOfPackets;

        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - classic frames per irp       = %u", m_deviceContext->ClassicFramesPerIrp);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - frames per ms                = %u", m_deviceContext->FramesPerMs);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - max burst override           = %u", m_deviceContext->SupportedControl.MaxBurstOverride);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - bInterval                    = %u", m_deviceContext->FeedbackInterfaceAndPipe.PipeInfo.Interval);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - maximum packet size          = %u", m_deviceContext->FeedbackInterfaceAndPipe.PipeInfo.MaximumPacketSize);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - transfer size per frame      = %u", pipeContext->TransferSizePerFrame);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - transfer size per microframe = %u", pipeContext->TransferSizePerMicroframe);

        RETURN_NTSTATUS_IF_TRUE_ACTION(pipeContext->TransferSizePerFrame == 0, status = STATUS_UNSUCCESSFUL, status);

        if (m_deviceContext->IsDeviceSuperSpeed && m_deviceContext->SuperSpeedCompatible)
        {
            RETURN_NTSTATUS_IF_TRUE_ACTION(pipeContext->TransferSizePerMicroframe == 0, status = STATUS_UNSUCCESSFUL, status);
            numberOfFrames = TotalLength / pipeContext->TransferSizePerFrame;
            numberOfPackets = TotalLength / pipeContext->TransferSizePerMicroframe;
        }
        else if (m_deviceContext->IsDeviceHighSpeed)
        {
            RETURN_NTSTATUS_IF_TRUE_ACTION(pipeContext->TransferSizePerMicroframe == 0, status = STATUS_UNSUCCESSFUL, status);
            numberOfFrames = TotalLength / pipeContext->TransferSizePerFrame;
            numberOfPackets = TotalLength / pipeContext->TransferSizePerMicroframe;
        }
        else
        {
            numberOfPackets = TotalLength / pipeContext->TransferSizePerFrame;
            numberOfFrames = numberOfPackets;
        }

        // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "old UrbIsochronousTransfer.Hdr.Length           = %u", GET_ISO_URB_SIZE(numberOfPackets));
        // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "old UrbIsochronousTransfer.TransferBufferLength = %u", TotalLength);
        // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "old UrbIsochronousTransfer.NumberOfPackets      = %u", numberOfPackets);
    }

    ULONG samplesPerIrp = (m_deviceContext->AudioProperty.SampleRate * m_deviceContext->ClassicFramesPerIrp) / 1000;
    ULONG samplesPerPacket = 0;
    ULONG extraSamples = 0;

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - samplesPerIrp    = %u", samplesPerIrp);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - samplesPerPacket = %u", samplesPerPacket);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - extraSamples     = %u", extraSamples);

    m_urb->UrbIsochronousTransfer.StartFrame = startFrame;
    m_urb->UrbIsochronousTransfer.Hdr.Length = (USHORT)siz;
    m_urb->UrbIsochronousTransfer.Hdr.Function = URB_FUNCTION_ISOCH_TRANSFER;
    m_urb->UrbIsochronousTransfer.PipeHandle = WdfUsbTargetPipeWdmGetPipeHandle(pipe);
    m_urb->UrbIsochronousTransfer.TransferBufferMDL = m_dataBufferMdl;
    // TEMPORARY WORKAROUND: There is no description in isorwrc : m_Urb->UrbIsochronousTransfer.TransferBuffer       = nullptr;
    m_urb->UrbIsochronousTransfer.NumberOfPackets = m_numIsoPackets;
    m_urb->UrbIsochronousTransfer.UrbLink = nullptr;

    {
        m_urb->UrbIsochronousTransfer.TransferFlags = USBD_TRANSFER_DIRECTION_IN | (asap ? USBD_START_ISO_TRANSFER_ASAP : 0);
        m_urb->UrbIsochronousTransfer.TransferBufferLength = m_numIsoPackets * m_isoPacketSize;

        for (ULONG i = 0; i < m_urb->UrbIsochronousTransfer.NumberOfPackets; ++i)
        {
            m_urb->UrbIsochronousTransfer.IsoPacket[i].Offset = i * m_isoPacketSize;
            m_urb->UrbIsochronousTransfer.IsoPacket[i].Length = 0;
            m_urb->UrbIsochronousTransfer.IsoPacket[i].Status = 0;
            m_isoPacketBuffer[i] = &(m_dataBuffer[m_urb->UrbIsochronousTransfer.IsoPacket[i].Offset]);
        }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - UrbIsochronousTransfer.Hdr.Length           = %u", m_urb->UrbIsochronousTransfer.Hdr.Length);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - UrbIsochronousTransfer.TransferBufferLength = %u", m_urb->UrbIsochronousTransfer.TransferBufferLength);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - UrbIsochronousTransfer.NumberOfPackets      = %u", m_urb->UrbIsochronousTransfer.NumberOfPackets);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");

    return status;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
NTSTATUS
TransferObject::FreeRequest()
{
    NTSTATUS status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry, m_index = %u", m_index);

    WdfSpinLockAcquire(m_spinLock);
    if (m_request != nullptr)
    {
        // Don't call WdfRequestComplete() on a Request created with WdfRequestCreate(); instead, call WdfObjectDelete().
        // https://learn.microsoft.com/ja-jp/windows-hardware/drivers/ddi/wdfrequest/nf-wdfrequest-wdfrequestcreate
        // https://learn.microsoft.com/ja-jp/windows-hardware/drivers/wdf/completing-i-o-requests
        // Calling WdfRequestCompleteWithInformation() causes a BSOD.
        // WdfRequestCompleteWithInformation(Request, status = STATUS_SUCCESS, length);
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "Call WdfObjectDelete");
        WdfObjectDelete(m_request);
        m_request = nullptr;
    }

    WdfSpinLockRelease(m_spinLock);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
NTSTATUS
TransferObject::FreeUrb()
{
    NTSTATUS status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry, m_index = %u", m_index);

    WdfSpinLockAcquire(m_spinLock);
    if (m_urbMemory != nullptr)
    {
        // The UrbMemory allocated by WdfUsbTargetDeviceCreateIsochUrb() should not be freed manually; it is managed by the WDF framework.
        // If the driver frees it manually, a BSOD will occur.
        // WdfObjectDelete(m_urbMemory);
        // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "WdfObjectDelete(%p)", m_urbMemory);
        m_urbMemory = nullptr;
        m_urb = nullptr;
    }

    WdfSpinLockRelease(m_spinLock);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void TransferObject::DumpUrbPacket(
    LPCSTR label
)
{

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "%s", label);

    WdfSpinLockAcquire(m_spinLock);
    if (m_urb != nullptr)
    {
        ULONG numberOfPackets = m_urb->UrbIsochronousTransfer.NumberOfPackets;
        // ULONG numberOfPackets = 1;
        for (ULONG i = 0; i < numberOfPackets; i++)
        {
            CHAR outputString[100] = "";
            sprintf_s(outputString, sizeof(outputString), "[%d] Urb IsoPacket [%d] Offset %d", m_index, i, m_urb->UrbIsochronousTransfer.IsoPacket[i].Offset);
            DumpByteArray(outputString, (UCHAR *)&(m_dataBuffer[m_urb->UrbIsochronousTransfer.IsoPacket[i].Offset]), m_urb->UrbIsochronousTransfer.IsoPacket[i].Length);
        }
    }
    WdfSpinLockRelease(m_spinLock);
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
NTSTATUS
TransferObject::SendIsochronousRequest(
    IsoDirection                       direction,
    PFN_WDF_REQUEST_COMPLETION_ROUTINE completionRoutine
)
{
    NTSTATUS                     status = STATUS_SUCCESS;
    WDFUSBPIPE                   pipe = nullptr;
    PISOCHRONOUS_REQUEST_CONTEXT requestContext = nullptr;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry, m_index = %u", m_index);

    auto sendIsochronousRequestScope = wil::scope_exit([&]() {
    });

    RETURN_NTSTATUS_IF_TRUE_ACTION(m_deviceContext->Device == nullptr, status = STATUS_UNSUCCESSFUL, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION(m_request == nullptr, status = STATUS_UNSUCCESSFUL, status);

    WdfSpinLockAcquire(m_spinLock);

    if (direction == IsoDirection::In)
    {
        pipe = m_deviceContext->InputInterfaceAndPipe.Pipe;
    }
    else if (direction == IsoDirection::Out)
    {
        pipe = m_deviceContext->OutputInterfaceAndPipe.Pipe;
    }
    else
    {
        pipe = m_deviceContext->FeedbackInterfaceAndPipe.Pipe;
    }

    requestContext = GetIsochronousRequestContext(m_request);
    NT_ASSERT(requestContext != nullptr);

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "GetIsochronousRequestContext(request) = %p", requestContext);
    //
    // Associate the URB with the request.
    //
    status = WdfUsbTargetPipeFormatRequestForUrb(pipe, m_request, m_urbMemory, nullptr);
    if (!NT_SUCCESS(status))
    {
        WdfSpinLockRelease(m_spinLock);
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "WdfUsbTargetPipeFormatRequestForUrb failed");
        return status;
    }
    WdfRequestSetCompletionRoutine(m_request, completionRoutine, requestContext);

    requestContext->DeviceContext = m_deviceContext;
    requestContext->StreamObject = const_cast<StreamObject *>(m_streamObject);
    requestContext->TransferObject = this;
    requestContext->UrbMemory = m_urbMemory;

    KeClearEvent(&m_requestCompletedEvent);

#if defined(DBG)
    if (false)
    {
        DumpUrbPacket("SendIsochronousRequest");
    }
#endif

    m_isRequested = true;
    if (WdfRequestSend(m_request, WdfUsbTargetPipeGetIoTarget(pipe), WDF_NO_SEND_OPTIONS) == FALSE)
    {
        m_isRequested = false;
        status = WdfRequestGetStatus(m_request);
        if (!NT_SUCCESS(status))
        {
            WdfSpinLockRelease(m_spinLock);
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "WdfRequestSend failed");
            return status;
        }
    }
    WdfSpinLockRelease(m_spinLock);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");

    return status;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
NTSTATUS
TransferObject::CancelRequest()
{
    NTSTATUS status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry, m_index = %u", m_index);

    WdfSpinLockAcquire(m_spinLock);
    if (m_isRequested && (m_request != nullptr))
    {
        m_isRequested = false;
        WdfSpinLockRelease(m_spinLock);

        LARGE_INTEGER timeout{};
        // Isochronous only
        timeout.QuadPart = (UAC_MAX_CLASSIC_FRAMES_PER_IRP * UAC_DEFAULT_FIRST_PACKET_LATENCY) * -20000LL;

        WdfRequestCancelSentRequest(m_request);
        status = KeWaitForSingleObject(&m_requestCompletedEvent, Executive, KernelMode, FALSE, &timeout);
    }
    else
    {
        WdfSpinLockRelease(m_spinLock);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void TransferObject::CompleteRequest(
    ULONGLONG completedTimeUs,
    ULONGLONG qpcPosition,
    ULONGLONG periodUs,
    ULONGLONG periodQPCPosition
)
{
    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "%u, %llu, %llu, %llu, %llu", m_index, completedTimeUs, qpcPosition, periodUs, periodQPCPosition);
    WdfSpinLockAcquire(m_spinLock);

    m_isRequested = false;
    m_completedTimeUs = completedTimeUs;
    m_periodUs = periodUs;
    m_qpcPosition = qpcPosition;
    m_periodQPCPosition = periodQPCPosition;
    KeSetEvent(&m_requestCompletedEvent, 1, FALSE);

    WdfSpinLockRelease(m_spinLock);

    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
USBD_STATUS
TransferObject::GetUSBDStatus()
{
    USBD_STATUS usbdStatus = USBD_STATUS_SUCCESS;

    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    WdfSpinLockAcquire(m_spinLock);
    if (m_urb == nullptr)
    {
        // Treat it as a success
        usbdStatus = USBD_STATUS_SUCCESS;
    }
    else
    {
        usbdStatus = m_urb->UrbHeader.Status;
    }
    WdfSpinLockRelease(m_spinLock);

    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");

    return usbdStatus;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
bool TransferObject::IsRequested()
{
    bool isRequested;

    WdfSpinLockAcquire(m_spinLock);

    isRequested = m_isRequested;

    WdfSpinLockRelease(m_spinLock);

    return isRequested;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
ULONG
TransferObject::GetStartFrame()
{
    ULONG startFrame = 0;
    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    WdfSpinLockAcquire(m_spinLock);
    if (m_urb != nullptr)
    {
        startFrame = m_urb->UrbIsochronousTransfer.StartFrame;
    }
    WdfSpinLockRelease(m_spinLock);

    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");

    return startFrame;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
NTSTATUS
TransferObject::UpdateTransferredBytesInThisIrp(ULONG & transferredBytesInThisIrp, ULONG * invalidPacket)
{
    NTSTATUS status = STATUS_SUCCESS;
    transferredBytesInThisIrp = 0;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    WdfSpinLockAcquire(m_spinLock);

    if (m_urb != nullptr)
    {
        switch (m_direction)
        {
        case IsoDirection::In: {
            for (ULONG i = 0; i < m_urb->UrbIsochronousTransfer.NumberOfPackets; ++i)
            {
                USBD_STATUS usbdStatus = m_urb->UrbIsochronousTransfer.IsoPacket[i].Status;
                if (!USBD_SUCCESS(usbdStatus))
                {
                    TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "in frame %u iso packet %d : failed with status %08x, %d bytes", m_urb->UrbIsochronousTransfer.StartFrame, i, usbdStatus, m_urb->UrbIsochronousTransfer.IsoPacket[i].Length);
                    status = STATUS_UNSUCCESSFUL;
                }
                else
                {
                    ULONG length = m_urb->UrbIsochronousTransfer.IsoPacket[i].Length;
                    transferredBytesInThisIrp += length;
                    // Detect when a sample ends in the middle of a packet.
                    if (((length % (m_deviceContext->AudioProperty.InputBytesPerBlock) != 0) || (((length < m_deviceContext->AudioProperty.InputBytesPerBlock * (m_deviceContext->AudioProperty.SamplesPerPacket - 1)) ||
                                                                                                  (length > m_deviceContext->AudioProperty.InputBytesPerBlock * (m_deviceContext->AudioProperty.SamplesPerPacket + 1))))))
                    {
                        if (m_lockDelayCount == 0)
                        {
                            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "in frame %u iso packet %d : invalid length %u bytes, in %u bytes per sample, %u samples per packet", m_urb->UrbIsochronousTransfer.StartFrame, i, length, m_deviceContext->AudioProperty.InputBytesPerBlock, m_deviceContext->AudioProperty.SamplesPerPacket);
                            if (invalidPacket != nullptr)
                            {
                                ++(*invalidPacket);
                            }
                            // SPEC-COMPLIANT: No error handling needed for now - may be revisited if requirements change
                        }
                        else
                        {
                            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "in frame %u iso packet %d : invalid length %u bytes , LOCK DELAY ENABLE", m_urb->UrbIsochronousTransfer.StartFrame, i, length);
                        }
                    }
                    // detecting sampling rate
                    bool updated = m_streamObject->CalculateSampleRate(TRUE, m_deviceContext->AudioProperty.InputBytesPerBlock, m_deviceContext->AudioProperty.PacketsPerSec, length, m_deviceContext->AudioProperty.InputMeasuredSampleRate);
                    if (updated)
                    {
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - InputMeasuredSampleRate = %d", m_deviceContext->AudioProperty.InputMeasuredSampleRate);
                    }
                }
            }
        }
        break;
        case IsoDirection::Out: {
            // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - NumberOfPackets = %d", m_urb->UrbIsochronousTransfer.NumberOfPackets);
            for (ULONG i = 0; i < m_urb->UrbIsochronousTransfer.NumberOfPackets; ++i)
            {
                USBD_STATUS usbdStatus = m_urb->UrbIsochronousTransfer.IsoPacket[i].Status;

                // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%d] length = %d, status = %!STATUS!", i, m_urb->UrbIsochronousTransfer.IsoPacket[i].Length, usbdStatus);
                if (!USBD_SUCCESS(usbdStatus))
                {
                    TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "out frame %u iso packet %d : failed with status %08x, %d bytes, packet head %02x %02x %02x %02x", m_urb->UrbIsochronousTransfer.StartFrame, i, usbdStatus, m_urb->UrbIsochronousTransfer.IsoPacket[i].Length, m_dataBuffer[m_urb->UrbIsochronousTransfer.IsoPacket[i].Offset], m_dataBuffer[m_urb->UrbIsochronousTransfer.IsoPacket[i].Offset + 1], m_dataBuffer[m_urb->UrbIsochronousTransfer.IsoPacket[i].Offset + 2], m_dataBuffer[m_urb->UrbIsochronousTransfer.IsoPacket[i].Offset + 3]);
                    status = STATUS_UNSUCCESSFUL;
                    // SPEC-COMPLIANT: No error handling needed for now - may be revisited if requirements change
                }
                else
                {
                    // The following two comments are from sample code in Microsoft's documentation.
                    // Length is a return value for isochronous IN transfers.
                    // Length is ignored by the USB driver stack for isochronous OUT transfers.
                    // https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/transfer-data-to-isochronous-endpoints
                    // For this reason, it is not possible to detect when a sample ends in the middle of a packet.

                    // detecting sampling rate
                    ULONG length = m_urb->UrbIsochronousTransfer.IsoPacket[i].Length;
                    bool  updated = m_streamObject->CalculateSampleRate(FALSE, m_deviceContext->AudioProperty.OutputBytesPerBlock, m_deviceContext->AudioProperty.PacketsPerSec, length, m_deviceContext->AudioProperty.OutputMeasuredSampleRate);
                    if (updated)
                    {
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - OutputMeasuredSampleRate = %d", m_deviceContext->AudioProperty.OutputMeasuredSampleRate);
                    }
                }
            }
            // For isochronous out, IsoPacket[].Length field is not updated by the USB stack.
            transferredBytesInThisIrp = m_urb->UrbIsochronousTransfer.TransferBufferLength;
        }
        break;
        case IsoDirection::Feedback: {
            for (ULONG i = 0; i < m_urb->UrbIsochronousTransfer.NumberOfPackets; ++i)
            {
                USBD_STATUS usbdStatus = m_urb->UrbIsochronousTransfer.IsoPacket[i].Status;
                if (!USBD_SUCCESS(usbdStatus))
                {
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "feedback frame %u iso packet %d : failed with status %08x, %d bytes", m_urb->UrbIsochronousTransfer.StartFrame, i, usbdStatus, m_urb->UrbIsochronousTransfer.IsoPacket[i].Length);
                    status = STATUS_UNSUCCESSFUL;
                }
                else
                {
                    transferredBytesInThisIrp += m_urb->UrbIsochronousTransfer.IsoPacket[i].Length;
                }
            }
        }
        break;
        default:
            break;
        }
        m_transferredBytesInThisIrp = transferredBytesInThisIrp;
        m_totalBytesProcessed += transferredBytesInThisIrp;
    }

    WdfSpinLockRelease(m_spinLock);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit, m_index = %u, %s, m_transferredBytesInThisIrp = %u, m_totalBytesProcessed = %u", m_index, GetDirectionString(m_direction), m_transferredBytesInThisIrp, m_totalBytesProcessed);

    return status;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void TransferObject::RecordIsoPacketLength()
{
    ULONG totalProcessedBytes = 0;

    ASSERT(m_direction == IsoDirection::In);
    ASSERT(m_urb != nullptr);
    ASSERT(m_urb->UrbIsochronousTransfer.NumberOfPackets == m_numIsoPackets);

    for (ULONG i = 0; i < m_urb->UrbIsochronousTransfer.NumberOfPackets; ++i)
    {
        m_totalProcessedBytesSoFar[i] = totalProcessedBytes;
        m_isoPacketLength[i] = m_urb->UrbIsochronousTransfer.IsoPacket[i].Length;
        totalProcessedBytes += m_isoPacketLength[i];
    }
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
ULONG TransferObject::GetFeedbackSum(ULONG & validFeedback)
{
    ULONG feedbackSum = 0;
    validFeedback = 0;

    ASSERT(m_direction == IsoDirection::Feedback);
    ASSERT(m_urb != nullptr);
    ASSERT(m_urb->UrbIsochronousTransfer.NumberOfPackets == m_numIsoPackets);

    for (ULONG i = 0; i < m_urb->UrbIsochronousTransfer.NumberOfPackets; ++i)
    {

        PUCHAR inBuffer = (PUCHAR)(m_dataBuffer) + m_urb->UrbIsochronousTransfer.IsoPacket[i].Offset;
        ULONG  feedbackValue = 0;

        if (m_urb->UrbIsochronousTransfer.IsoPacket[i].Length == 3)
        {
            // If the length is 3 bytes, the value is treated as a 24-bit fixed-point number in 10.14 format.
            feedbackValue = ((ULONG)inBuffer[0]) | (((ULONG)inBuffer[1]) << 8) | (((ULONG)inBuffer[2]) << 16);
        }
        else
        {
            // If the length is any other value, the value is treated as a 32-bit fixed-point number in 16.16 format.
            feedbackValue = *(PULONG)inBuffer;
        }
        if (m_lockDelayCount != 0)
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "feedback frame %u, value %08x, LOCK DELAY ENABLED.", m_urb->UrbIsochronousTransfer.StartFrame, feedbackValue);
        }
        else
        {
            if ((i == 0) && (feedbackValue != 0))
            {
                m_streamObject->SetFeedbackStale(m_urb->UrbIsochronousTransfer.StartFrame, feedbackValue);
            }
            if (m_streamObject->IsFeedbackStable())
            {
                feedbackSum += feedbackValue;
                ++validFeedback;
            }
        }
    }
    return feedbackSum;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void TransferObject::UpdatePositionsIn(ULONG transferredSamplesInThisIrp)
{
    ASSERT(m_direction == IsoDirection::In);
    ASSERT(m_urb != nullptr);

    for (ULONG i = 0; i < m_urb->UrbIsochronousTransfer.NumberOfPackets; ++i)
    {
        m_streamObject->UpdatePositionsIn(m_urb->UrbIsochronousTransfer.IsoPacket[i].Length);
    }
    m_feedbackSamples = transferredSamplesInThisIrp;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void TransferObject::CompensateNonFeedbackOutput(
    ULONG transferredSamplesInThisIrp
)
{
    if (m_presendSamples != 0)
    {
        m_streamObject->AddCompensateSamples((LONG)transferredSamplesInThisIrp - (LONG)m_presendSamples);
        m_presendSamples = 0;
    }
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void TransferObject::SetPresendSamples(
    ULONG presendSamples
)
{
    m_presendSamples = presendSamples;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void TransferObject::SetFeedbackSamples(
    ULONG feedbackSamples
)
{
    m_feedbackSamples = feedbackSamples;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
ULONG TransferObject::GetFeedbackSamples()
{
    return m_feedbackSamples;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
IsoDirection
TransferObject::GetDirection()
{
    return m_direction;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
LONG TransferObject::GetIndex()
{
    return m_index;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
ULONG
TransferObject::GetNumPackets(
)
{
    WdfSpinLockAcquire(m_spinLock);
    ULONG NumPackets = m_numIsoPackets;
    WdfSpinLockRelease(m_spinLock);

    return NumPackets;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
PUCHAR
TransferObject::GetDataBuffer()
{
    WdfSpinLockAcquire(m_spinLock);
    PUCHAR DataBuffer = m_dataBuffer;
    WdfSpinLockRelease(m_spinLock);
    return DataBuffer;
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG
TransferObject::GetTransferredBytesInThisIrp()
{
    PAGED_CODE();
    return m_transferredBytesInThisIrp;
}

_Use_decl_annotations_
PAGED_CODE_SEG
PUCHAR
TransferObject::GetIsoPacketBuffer(
    ULONG IsoPacket
)
{
    PUCHAR buffer = nullptr;

    PAGED_CODE();

    if (m_urb != nullptr)
    {
        buffer = &(m_dataBuffer[m_urb->UrbIsochronousTransfer.IsoPacket[IsoPacket].Offset]);
    }

    return buffer;
}

_Use_decl_annotations_
PAGED_CODE_SEG
PUCHAR
TransferObject::GetRecordedIsoPacketBuffer(
    ULONG isoPacket
)
{
    PAGED_CODE();

    return m_isoPacketBuffer[isoPacket];
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG
TransferObject::GetIsoPacketOffset(
    ULONG isoPacket
)
{
    PAGED_CODE();

    ASSERT(m_urb != nullptr);

    return m_urb->UrbIsochronousTransfer.IsoPacket[isoPacket].Offset;
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG
TransferObject::GetIsoPacketLength(
    ULONG isoPacket
)
{
    PAGED_CODE();

    ASSERT(m_urb != nullptr);

    return m_urb->UrbIsochronousTransfer.IsoPacket[isoPacket].Length;
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG
TransferObject::GetRecordedIsoPacketLength(
    ULONG isoPacket
)
{
    PAGED_CODE();

    return m_isoPacketLength[isoPacket];
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG
TransferObject::GetTotalProcessedBytesSoFar(
    ULONG isoPacket
)
{
    PAGED_CODE();

    return m_totalProcessedBytesSoFar[isoPacket];
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
ULONG
TransferObject::GetNumberOfPacketsInThisIrp()
{
    ASSERT(m_urb != nullptr);

    return m_urb->UrbIsochronousTransfer.NumberOfPackets;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
ULONG TransferObject::GetStartFrameInThisIrp()
{
    ASSERT(m_urb != nullptr);

    return m_urb->UrbIsochronousTransfer.StartFrame;
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONGLONG
TransferObject::GetQPCPosition()
{
    PAGED_CODE();
    return m_qpcPosition;
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONGLONG
TransferObject::GetPeriodQPCPosition()
{
    PAGED_CODE();
    return m_periodQPCPosition;
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONGLONG
TransferObject::CalculateEstimatedQPCPosition(
    ULONG bytesCopiedUpToBoundary
)
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " %llu + (%llu * %u) / %u = %llu", m_qpcPosition, m_periodQPCPosition, bytesCopiedUpToBoundary, m_transferredBytesInThisIrp, m_qpcPosition + (m_periodQPCPosition * bytesCopiedUpToBoundary) / m_transferredBytesInThisIrp);

    return m_qpcPosition + (m_periodQPCPosition * bytesCopiedUpToBoundary) / m_transferredBytesInThisIrp;
}

_Use_decl_annotations_
PAGED_CODE_SEG
void TransferObject::SetLockDelayCount(
    _In_ ULONG lockDelayCount
)
{
    PAGED_CODE();

    m_lockDelayCount = lockDelayCount;

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - lock delay count = %u", m_lockDelayCount);
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
ULONG TransferObject::GetLockDelayCount()
{
    return m_lockDelayCount;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
bool TransferObject::DecrementLockDelayCount()
{
    bool isLockDelay = false;

    if (m_lockDelayCount != 0)
    {
        --m_lockDelayCount;
        isLockDelay = true;
    }
    return isLockDelay;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void TransferObject::DebugReport()
{
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");
    // WdfSpinLockAcquire(m_spinLock);

    if (m_urb != nullptr)
    {
        USBD_STATUS usbdStatus = m_urb->UrbHeader.Status;
        if (!USBD_SUCCESS(usbdStatus))
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] UrbIsochronousTransfer.Hdr.Length           = %u", m_index, m_urb->UrbIsochronousTransfer.Hdr.Length);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] UrbIsochronousTransfer.Hdr.Function         = %u", m_index, m_urb->UrbIsochronousTransfer.Hdr.Function);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] UrbIsochronousTransfer.Hdr.Status           = 0x%x (%!STATUS!)", m_index, m_urb->UrbIsochronousTransfer.Hdr.Status, m_urb->UrbIsochronousTransfer.Hdr.Status);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] UrbIsochronousTransfer.Hdr.UsbdDeviceHandle = %p", m_index, m_urb->UrbIsochronousTransfer.Hdr.UsbdDeviceHandle);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] UrbIsochronousTransfer.Hdr.UsbdFlags        = 0x%x", m_index, m_urb->UrbIsochronousTransfer.Hdr.UsbdFlags);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] UrbIsochronousTransfer.PipeHandle           = %p", m_index, m_urb->UrbIsochronousTransfer.PipeHandle);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] UrbIsochronousTransfer.TransferFlags        = 0x%x", m_index, m_urb->UrbIsochronousTransfer.TransferFlags);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] UrbIsochronousTransfer.TransferBufferLength = %u", m_index, m_urb->UrbIsochronousTransfer.TransferBufferLength);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] UrbIsochronousTransfer.TransferBuffer       = %p", m_index, m_urb->UrbIsochronousTransfer.TransferBuffer);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] UrbIsochronousTransfer.TransferBufferMDL    = %p", m_index, m_urb->UrbIsochronousTransfer.TransferBufferMDL);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] UrbIsochronousTransfer.UrbLink              = %p", m_index, m_urb->UrbIsochronousTransfer.UrbLink);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] UrbIsochronousTransfer.StartFrame           = %u", m_index, m_urb->UrbIsochronousTransfer.StartFrame);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] UrbIsochronousTransfer.NumberOfPackets      = %u", m_index, m_urb->UrbIsochronousTransfer.NumberOfPackets);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] UrbIsochronousTransfer.ErrorCount           = %u", m_index, m_urb->UrbIsochronousTransfer.ErrorCount);

            for (ULONG i = 0; i < m_urb->UrbIsochronousTransfer.NumberOfPackets; ++i)
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] UrbIsochronousTransfer.IsoPacket[%d].Offset = %u", m_index, i, m_urb->UrbIsochronousTransfer.IsoPacket[i].Offset);
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] UrbIsochronousTransfer.IsoPacket[%d].Length = %u", m_index, i, m_urb->UrbIsochronousTransfer.IsoPacket[i].Length);
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] UrbIsochronousTransfer.IsoPacket[%d].Status = 0x%x (%!STATUS!)", m_index, i, m_urb->UrbIsochronousTransfer.IsoPacket[i].Status, m_urb->UrbIsochronousTransfer.IsoPacket[i].Status);
            }
        }
    }
    // WdfSpinLockRelease(m_spinLock);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}
