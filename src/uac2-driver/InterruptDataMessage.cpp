// Copyright (c) Microsoft Corporation.
// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================
// ASIO is a trademark and software of Steinberg Media Technologies GmbH

/*++

Module Name:

    InterruptDataMessage.cpp

Abstract:

   Implements the process to respond to the interrupt data message.

Environment:

    Kernel-mode Driver Framework

--*/

#include "Driver.h"
#include "Device.h"
#include "InterruptDataMessage.h"
#include "DeviceControl.h"
#include "Public.h"
#include "Common.h"
#include "USBAudio.h"
#include "USBAudioConfiguration.h"
#include "ErrorStatistics.h"
#include "WorkerThread.h"
#include "AudioIsochronousEngine.h"

#ifndef __INTELLISENSE__
#include "InterruptDataMessage.tmh"
#endif

//
//  Global variables
//

//
// Static variables

//
//  Local function prototypes
//
static __drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
void InterruptMessageWorkerThreadFunction(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ WorkerThread *  thisThread
);

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudioAcxDriverStartInterruptDataReception(
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "%!FUNC! Entry");

    if (deviceContext->InterruptMessageProperty.IsValid && deviceContext->InterruptInterfaceAndPipe.Pipe != nullptr)
    {
        WDF_USB_CONTINUOUS_READER_CONFIG continuousReaderConfig;

        ASSERT(deviceContext->InterruptMessageWorkerThread == nullptr);
        deviceContext->InterruptMessageWorkerThread = WorkerThread::CreateWorkerThread(deviceContext);

        RETURN_NTSTATUS_IF_FAILED(deviceContext->InterruptMessageWorkerThread->CreateThread(InterruptMessageWorkerThreadFunction, LOW_REALTIME_PRIORITY));

        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "%!FUNC! MaxPacketSize = %u", deviceContext->InterruptInterfaceAndPipe.PipeInfo.MaximumPacketSize);

        WDF_USB_CONTINUOUS_READER_CONFIG_INIT(&continuousReaderConfig, USBAudioAcxDriverEvtInterruptDataMessageCompletionRoutine, deviceContext, max(sizeof(NS_USBAudio0200::INTERRUPT_DATA_MESSAGE_FORMAT), deviceContext->InterruptInterfaceAndPipe.PipeInfo.MaximumPacketSize));

        RETURN_NTSTATUS_IF_FAILED(WdfUsbTargetPipeConfigContinuousReader(deviceContext->InterruptInterfaceAndPipe.Pipe, &continuousReaderConfig));

        RETURN_NTSTATUS_IF_FAILED(WdfIoTargetStart(WdfUsbTargetPipeGetIoTarget(deviceContext->InterruptInterfaceAndPipe.Pipe)));
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudioAcxDriverStopInterruptDataReception(
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "%!FUNC! Entry");

    if (deviceContext->InterruptMessageProperty.IsValid && deviceContext->InterruptInterfaceAndPipe.Pipe != nullptr)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "call WdfIoTargetStop");
        WdfIoTargetStop(WdfUsbTargetPipeGetIoTarget(deviceContext->InterruptInterfaceAndPipe.Pipe), WdfIoTargetCancelSentIo);
    }

    if (deviceContext->InterruptMessageWorkerThread != nullptr)
    {
        deviceContext->InterruptMessageWorkerThread->Terminate();
        delete deviceContext->InterruptMessageWorkerThread;
        deviceContext->InterruptMessageWorkerThread = nullptr;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

NONPAGED_CODE_SEG
_Use_decl_annotations_
VOID USBAudioAcxDriverEvtInterruptDataMessageCompletionRoutine(
    WDFUSBPIPE /* pipe */,
    WDFMEMORY  buffer,
    size_t     numBytesTransferred,
    WDFCONTEXT context
)
{
    NTSTATUS status = STATUS_SUCCESS;
    // WDFDEVICE       device;
    PDEVICE_CONTEXT deviceContext = (PDEVICE_CONTEXT)context;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "%!FUNC! Entry, %llu bytes transferred.", numBytesTransferred);

    if (sizeof(NS_USBAudio0200::INTERRUPT_DATA_MESSAGE_FORMAT) <= numBytesTransferred)
    {
        NS_USBAudio0200::PINTERRUPT_DATA_MESSAGE_FORMAT message = (NS_USBAudio0200::PINTERRUPT_DATA_MESSAGE_FORMAT)WdfMemoryGetBuffer(buffer, nullptr);

        if ((message->bInfo & NS_USBAudio0200::INTERRUPT_INFO_KIND_MASK) == NS_USBAudio0200::INTERRUPT_INFO_KIND_CLASS)
        {
            bool needNotify = false;
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_INTERRUPTTRANSFER, "message acquired class notification %llu bytes : bInfo %02x, bAttribute %02x, wValue %04x, wIndex %04x", numBytesTransferred, message->bInfo, message->bAttribute, message->wValue, message->wIndex);

            status = deviceContext->UsbAudioConfiguration->OnInterruptDataMessageReceived(message->bInfo, message->bAttribute, message->wValue, message->wIndex, needNotify);
            if (NT_SUCCESS(status) && needNotify)
            {
                if (deviceContext->InterruptMessageWorkerThread != nullptr)
                {
                    deviceContext->InterruptMessageWorkerThread->WakeUp();
                }
            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "%!FUNC! Exit %!STATUS!", status);
}

NONPAGED_CODE_SEG
_Use_decl_annotations_
BOOLEAN
USBAudioAcxDriverEvtInterruptDataMessageCompletionRoutineFailed(
    _In_             WDFUSBPIPE /* Pipe */,
    _In_ NTSTATUS    Status,
    _In_ USBD_STATUS UsbdStatus
)
{
    // WDFDEVICE device = WdfIoTargetGetDevice(WdfUsbTargetPipeGetIoTarget(Pipe));
    // PDEVICE_CONTEXT pDeviceContext = GetDeviceContext(device);

    // UNREFERENCED_PARAMETER(UsbdStatus);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "%!FUNC! Entry, status %08x, usbd status %08x!", Status, UsbdStatus);

    //
    // Clear the current switch state.
    //
    // pDeviceContext->CurrentSwitchState = 0;

    //
    // Service the pending interrupt switch change request
    //
    // OsrUsbIoctlGetInterruptMessage(device, Status);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "%!FUNC! Exit!");

    return TRUE;
}

_Use_decl_annotations_
PAGED_CODE_SEG
void InterruptMessageWorkerThreadFunction(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ WorkerThread * /* thisThread */
)
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "%!FUNC! Entry");

    for (;;)
    {
        NTSTATUS wakeupReason = STATUS_SUCCESS;
        UCHAR    entityID;

        wakeupReason = deviceContext->InterruptMessageWorkerThread->Wait();

        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "InterruptMessageWorkerThreadFunction() WakeUp reason = %d", static_cast<int>(wakeupReason));

        // If the wakeup result is an error, exit.
        if (!NT_SUCCESS(wakeupReason) || (wakeupReason == STATUS_WAIT_0))
        {
            break;
        }

        while (deviceContext->UsbAudioConfiguration->IsVolumeEntityUpdated())
        {
            if (deviceContext->UsbAudioConfiguration->GetUpdatedVolumeEntity(entityID))
            {
                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "UpdateVolumeEntity(0x%02x)", entityID);

                if (deviceContext->AudioIsochronousEngines != nullptr)
                {
                    for (ULONG index = 0; index < deviceContext->NumberOfAudioIsochronousEngines; index++)
                    {
                        if (deviceContext->AudioIsochronousEngines[index] != nullptr)
                        {
                            deviceContext->AudioIsochronousEngines[index]->VolumeChangeLevelNotification(entityID);
                        }
                    }
                }
            }
        }

        while (deviceContext->UsbAudioConfiguration->IsMuteEntityUpdated())
        {
            if (deviceContext->UsbAudioConfiguration->GetUpdatedMuteEntity(entityID))
            {
                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "UpdateMuteEntity(0x%02x)", entityID);

                if (deviceContext->AudioIsochronousEngines != nullptr)
                {
                    for (ULONG index = 0; index < deviceContext->NumberOfAudioIsochronousEngines; index++)
                    {
                        if (deviceContext->AudioIsochronousEngines[index] != nullptr)
                        {
                            deviceContext->AudioIsochronousEngines[index]->MuteChangeStateNotification(entityID);
                        }
                    }
                }
            }
        }

        while (deviceContext->UsbAudioConfiguration->IsInputConnectorEntityUpdated())
        {
            if (deviceContext->UsbAudioConfiguration->GetUpdatedInputConnectorEntity(entityID))
            {
                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "UpdateInputConnectorEntity(0x%02x)", entityID);

                if (deviceContext->AudioIsochronousEngines != nullptr)
                {
                    for (ULONG index = 0; index < deviceContext->NumberOfAudioIsochronousEngines; index++)
                    {
                        if (deviceContext->AudioIsochronousEngines[index] != nullptr)
                        {
                            deviceContext->AudioIsochronousEngines[index]->ConnectorChangeStateNotification(entityID);
                        }
                    }
                }
            }
        }

        while (deviceContext->UsbAudioConfiguration->IsOutputConnectorEntityUpdated())
        {
            if (deviceContext->UsbAudioConfiguration->GetUpdatedOutputConnectorEntity(entityID))
            {
                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "UpdateOutputConnectorEntity(0x%02x)", entityID);

                if (deviceContext->AudioIsochronousEngines != nullptr)
                {
                    for (ULONG index = 0; index < deviceContext->NumberOfAudioIsochronousEngines; index++)
                    {
                        if (deviceContext->AudioIsochronousEngines[index] != nullptr)
                        {
                            deviceContext->AudioIsochronousEngines[index]->ConnectorChangeStateNotification(entityID);
                        }
                    }
                }
            }
        }
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "%!FUNC! Exit");
}
