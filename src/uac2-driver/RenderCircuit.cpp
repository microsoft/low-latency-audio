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

    RenderCircuit.cpp

Abstract:

    Render Circuit. This file contains routines to create and handle
    render circuit with no offload.

Environment:

    Kernel-mode Driver Framework

--*/

#include "Private.h"
#include "Public.h"
#include <ks.h>
#include <mmsystem.h>
#include <ksmedia.h>
#include "AudioFormats.h"
#include "StreamEngine.h"
#include "CircuitHelper.h"
#include "Device.h"
#include "Common.h"
#include "UAC_User.h"
#include "USBAudioConfiguration.h"

#ifndef __INTELLISENSE__
#include "RenderCircuit.tmh"
#endif

#pragma warning(disable : 4127)

//
//  Local function prototypes
//

static ACX_PROPERTY_ITEM s_PropertyItems[] = {
    {
        &KSPROPSETID_LowLatencyAudio,                     // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::GetAudioProperty),
        ACX_PROPERTY_ITEM_FLAG_GET,                       // ULONG Flags;
        EvtUSBAudioAcxDriverGetAudioProperty,             // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                                // PVOID Reserved;
        0,                                                // ULONG ControlCb;
        sizeof(UAC_AUDIO_PROPERTY),                       // ULONG ValueCb;
    },
    {
        &KSPROPSETID_LowLatencyAudio,                     // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::GetChannelInfo),
        ACX_PROPERTY_ITEM_FLAG_GET,                       // ULONG Flags;
        EvtUSBAudioAcxDriverGetChannelInfo,               // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                                // PVOID Reserved;
        0,                                                // ULONG ControlCb;
        0,                                                // ULONG ValueCb; (variable length)
    },
    {
        &KSPROPSETID_LowLatencyAudio,                     // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::GetClockInfo),
        ACX_PROPERTY_ITEM_FLAG_GET,                       // ULONG Flags;
        EvtUSBAudioAcxDriverGetClockInfo,                 // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                                // PVOID Reserved;
        0,                                                // ULONG ControlCb;
        0,                                                // ULONG ValueCb; (variable length)
    },
    {
        &KSPROPSETID_LowLatencyAudio,                     // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::GetLatencyOffsetOfSampleRate),
        ACX_PROPERTY_ITEM_FLAG_GET,                       // ULONG Flags;
        EvtUSBAudioAcxDriverGetLatencyOffsetOfSampleRate, // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                                // PVOID Reserved;
        sizeof(UAC_SET_FLAGS_CONTEXT),                    // ULONG ControlCb;
        0,                                                // ULONG ValueCb; (variable length)
    },
    {
        &KSPROPSETID_LowLatencyAudio,                     // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::SetClockSource),
        ACX_PROPERTY_ITEM_FLAG_SET,                       // ULONG Flags;
        EvtUSBAudioAcxDriverSetClockSource,               // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                                // PVOID Reserved;
        0,                                                // ULONG ControlCb;
        sizeof(UAC_SET_CLOCK_SOURCE_CONTEXT),             // ULONG ValueCb;
    },
    {
        &KSPROPSETID_LowLatencyAudio,                     // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::SetSampleFormat),
        ACX_PROPERTY_ITEM_FLAG_SET,                       // ULONG Flags;
        EvtUSBAudioAcxDriverSetSampleFormat,              // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                                // PVOID Reserved;
        0,                                                // ULONG ControlCb;
        sizeof(ULONG),                                    // ULONG ValueCb;
    },
    {
        &KSPROPSETID_LowLatencyAudio,                     // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::ChangeSampleRate),
        ACX_PROPERTY_ITEM_FLAG_SET,                       // ULONG Flags;
        EvtUSBAudioAcxDriverChangeSampleRate,             // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                                // PVOID Reserved;
        0,                                                // ULONG ControlCb;
        sizeof(ULONG),                                    // ULONG ValueCb;
    },
    {
        &KSPROPSETID_LowLatencyAudio,                     // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::GetAsioOwnership),
        ACX_PROPERTY_ITEM_FLAG_SET,                       // ULONG Flags;
        EvtUSBAudioAcxDriverGetAsioOwnership,             // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                                // PVOID Reserved;
        0,                                                // ULONG ControlCb;
        0,                                                // ULONG ValueCb;
    },
    {
        &KSPROPSETID_LowLatencyAudio,                     // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::StartAsioStream),
        ACX_PROPERTY_ITEM_FLAG_SET,                       // ULONG Flags;
        EvtUSBAudioAcxDriverStartAsioStream,              // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                                // PVOID Reserved;
        0,                                                // ULONG ControlCb;
        0,                                                // ULONG ValueCb;
    },
    {
        &KSPROPSETID_LowLatencyAudio,                     // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::StopAsioStream),
        ACX_PROPERTY_ITEM_FLAG_SET,                       // ULONG Flags;
        EvtUSBAudioAcxDriverStopAsioStream,               // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                                // PVOID Reserved;
        0,                                                // ULONG ControlCb;
        0,                                                // ULONG ValueCb;
    },
    {
        &KSPROPSETID_LowLatencyAudio,                     // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::SetAsioBuffer),
        ACX_PROPERTY_ITEM_FLAG_SET,                       // ULONG Flags;
        EvtUSBAudioAcxDriverSetAsioBuffer,                // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                                // PVOID Reserved;
        0,                                                // ULONG ControlCb;  (variable length)
        0,                                                // ULONG ValueCb;  (variable length)
    },
    {
        &KSPROPSETID_LowLatencyAudio,                     // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::UnsetAsioBuffer),
        ACX_PROPERTY_ITEM_FLAG_SET,                       // ULONG Flags;
        EvtUSBAudioAcxDriverUnsetAsioBuffer,              // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                                // PVOID Reserved;
        0,                                                // ULONG ControlCb;
        0,                                                // ULONG ValueCb;
    },
    {
        &KSPROPSETID_LowLatencyAudio,                     // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::ReleaseAsioOwnership),
        ACX_PROPERTY_ITEM_FLAG_SET,                       // ULONG Flags;
        EvtUSBAudioAcxDriverReleaseAsioOwnership,         // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                                // PVOID Reserved;
        0,                                                // ULONG ControlCb;
        0,                                                // ULONG ValueCb;
    },
    {
        &KSPROPSETID_LowLatencyAudio,                     // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::GetBufferPeriod),
        ACX_PROPERTY_ITEM_FLAG_GET,                       // ULONG Flags;
        EvtUSBAudioAcxDriverGetBufferPeriod,              // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                                // PVOID Reserved;
        0,                                                // ULONG ControlCb;
        sizeof(ULONG),                                    // ULONG ValueCb;
    },
    {
        &KSPROPSETID_LowLatencyAudio,                     // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::SetBufferPeriod),
        ACX_PROPERTY_ITEM_FLAG_SET,                       // ULONG Flags;
        EvtUSBAudioAcxDriverSetBufferPeriod,              // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                                // PVOID Reserved;
        0,                                                // ULONG ControlCb;
        sizeof(ULONG),                                    // ULONG ValueCb;
    },
    {
        &KSPROPSETID_LowLatencyAudio,                     // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::GetInputLatency),
        ACX_PROPERTY_ITEM_FLAG_GET,                       // ULONG Flags;
        EvtUSBAudioAcxDriverGetInputLatency,              // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                                // PVOID Reserved;
        0,                                                // ULONG ControlCb;
        sizeof(LONG),                                     // ULONG ValueCb;
    },
    {
        &KSPROPSETID_LowLatencyAudio,                     // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::GetOutputLatency),
        ACX_PROPERTY_ITEM_FLAG_GET,                       // ULONG Flags;
        EvtUSBAudioAcxDriverGetOutputLatency,             // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                                // PVOID Reserved;
        0,                                                // ULONG ControlCb;
        sizeof(LONG),                                     // ULONG ValueCb;
    },
    {
        &KSPROPSETID_LowLatencyAudio,                     // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::SetAsioDevice),
        ACX_PROPERTY_ITEM_FLAG_SET,                       // ULONG Flags;
        EvtUSBAudioAcxDriverSetAsioDevice,                // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                                // PVOID Reserved;
        0,                                                // ULONG ControlCb;
        0,                                                // ULONG ValueCb;
    },
    {
        &KSPROPSETID_LowLatencyAudio,                     // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::GetAsioDevice),
        ACX_PROPERTY_ITEM_FLAG_GET,                       // ULONG Flags;
        EvtUSBAudioAcxDriverGetAsioDevice,                // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                                // PVOID Reserved;
        0,                                                // ULONG ControlCb;
        0,                                                // ULONG ValueCb;
    }
};

PAGED_CODE_SEG
NTSTATUS
CodecR_EvtAcxPinSetDataFormat(
    _In_ ACXPIN /* Pin */,
    _In_ ACXDATAFORMAT /* DataFormat */
)
/*++

Routine Description:

    This ACX pin callback sets the device/mixed format.

Return Value:

    NTSTATUS

--*/
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");

    // NOTE: update device/mixed format here.
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
VOID CodecR_EvtAcxPinDataFormatChangeNotification(
    _In_ ACXPIN Pin,
    _In_        ACXTARGETCIRCUIT /* TargetCircuit */,
    _In_ ULONG  TargetPinId
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - pin id = %d, target pin id = %u", AcxPinGetId(Pin), TargetPinId);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");
}

///////////////////////////////////////////////////////////
//
// For more information on mute element see: https://docs.microsoft.com/en-us/windows-hardware/drivers/audio/ksnodetype-mute
//
_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
NTAPI
CodecR_EvtMuteAssignState(
    _In_ ACXMUTE Mute,
    _In_ ULONG   Channel,
    _In_ ULONG   State
)
{
    NTSTATUS              status = STATUS_SUCCESS;
    PDEVICE_CONTEXT       deviceContext;
    PMUTE_ELEMENT_CONTEXT muteContext;
    bool                  muteState = (State != 0) ? true : false;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");
    muteContext = GetMuteElementContext(Mute);
    ASSERT(muteContext);

    deviceContext = GetDeviceContext(muteContext->Device);
    ASSERT(deviceContext != nullptr);

    // If the device is designed to support mute control,
    // the implementation should be added here.

    //
    // Use first channel for all channels setting.
    //
    if ((Channel != ALL_CHANNELS_ID) && (Channel < muteContext->NumberOfChannels))
    {
        if (muteContext->MuteState[Channel] != muteState)
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - set current mute %!bool!, entity ID 0x%02x, channel %d", (muteState != 0) ? true : false, muteContext->EntityID, Channel);
            status = deviceContext->UsbAudioConfiguration->SetCurrentMute(deviceContext, muteContext->EntityID, (UCHAR)Channel, muteState);
        }
        muteContext->MuteState[Channel] = muteState;
    }
    else if (Channel == ALL_CHANNELS_ID)
    {
        for (ULONG i = 0; i < muteContext->NumberOfChannels; ++i)
        {
            if (muteContext->MuteState[i] != muteState)
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - set current mute %!bool!, entity ID 0x%02x, channel %d", (muteState != 0) ? true : false, muteContext->EntityID, Channel);
                status = deviceContext->UsbAudioConfiguration->SetCurrentMute(deviceContext, muteContext->EntityID, (UCHAR)i, muteState);
            }
            muteContext->MuteState[i] = muteState;
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
NTAPI
CodecR_EvtMuteRetrieveState(
    _In_ ACXMUTE  Mute,
    _In_ ULONG    Channel,
    _Out_ ULONG * State
)
{
    PMUTE_ELEMENT_CONTEXT muteContext;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");

    muteContext = GetMuteElementContext(Mute);
    ASSERT(muteContext);

    // If the device is designed to support mute control,
    // the implementation should be added here.

    *State = 0;
    //
    // Use first channel for all channels setting.
    //
    if ((Channel != ALL_CHANNELS_ID) && (Channel < muteContext->NumberOfChannels))
    {
        *State = muteContext->MuteState[Channel];
    }
    else if (Channel == ALL_CHANNELS_ID)
    {
        *State = muteContext->MuteState[0];
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit, state = %d", *State);

    return STATUS_SUCCESS;
}

///////////////////////////////////////////////////////////
//
// For more information on volume element see: https://docs.microsoft.com/en-us/windows-hardware/drivers/audio/ksnodetype-volume
//
_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
NTAPI
CodecR_EvtRampedVolumeAssignLevel(
    _In_ ACXVOLUME Volume,
    _In_ ULONG     Channel,
    _In_ LONG      VolumeLevel,
    _In_           ACX_VOLUME_CURVE_TYPE /* CurveType */,
    _In_           ULONGLONG /* CurveDuration */
)
{
    NTSTATUS                status = STATUS_SUCCESS;
    PDEVICE_CONTEXT         deviceContext;
    PVOLUME_ELEMENT_CONTEXT volumeContext;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry, channel %u, volume level %ld", Channel, VolumeLevel);

    volumeContext = GetVolumeElementContext(Volume);
    ASSERT(volumeContext);

    deviceContext = GetDeviceContext(volumeContext->Device);
    ASSERT(deviceContext != nullptr);

    // If the device is designed to support volume control,
    // the implementation should be added here.

    if ((Channel != ALL_CHANNELS_ID) && (Channel < volumeContext->NumberOfChannels))
    {
        if (volumeContext->VolumeLevel[Channel] != VolumeLevel)
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - set current volume %ld, entity ID 0x%02x, channel %d", VolumeLevel, volumeContext->EntityID, Channel);
            status = deviceContext->UsbAudioConfiguration->SetCurrentVolume(deviceContext, volumeContext->EntityID, (UCHAR)Channel, VolumeLevel);
            if (NT_SUCCESS(status))
            {
                volumeContext->VolumeLevel[Channel] = VolumeLevel;
            }
        }
    }
    else if (Channel == ALL_CHANNELS_ID)
    {
        for (ULONG i = 0; i < volumeContext->NumberOfChannels; ++i)
        {
            if (volumeContext->VolumeLevel[i] != VolumeLevel)
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - set current volume %ld, entity ID 0x%02x, channel %d", VolumeLevel, volumeContext->EntityID, i);
                status = deviceContext->UsbAudioConfiguration->SetCurrentVolume(deviceContext, volumeContext->EntityID, (UCHAR)i, VolumeLevel);
                if (NT_SUCCESS(status))
                {
                    volumeContext->VolumeLevel[i] = VolumeLevel;
                }
            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
NTAPI
CodecR_EvtVolumeRetrieveLevel(
    _In_ ACXVOLUME Volume,
    _In_ ULONG     Channel,
    _Out_ LONG *   VolumeLevel
)
{
    PVOLUME_ELEMENT_CONTEXT volumeContext;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry, channel %u", Channel);

    volumeContext = GetVolumeElementContext(Volume);
    ASSERT(volumeContext);

    // If the device is designed to support volume control,
    // the implementation should be added here.

    *VolumeLevel = 0;

    if ((Channel != ALL_CHANNELS_ID) && (Channel < volumeContext->NumberOfChannels))
    {
        *VolumeLevel = volumeContext->VolumeLevel[Channel];
    }
    else if (Channel == ALL_CHANNELS_ID)
    {
        *VolumeLevel = volumeContext->VolumeLevel[0];
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit, channel %u, volume level %ld", Channel, *VolumeLevel);

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
NTSTATUS
CodecR_EvtAcxPinRetrieveName(
    _In_ ACXPIN           Pin,
    _Out_ PUNICODE_STRING Name
)
/*++

Routine Description:

    The ACX pin callback EvtAcxPinRetrieveName calls this function in order to retrieve the pin name.

Return Value:

    NTSTATUS

--*/
{
    NTSTATUS       status = STATUS_SUCCESS;
    WDFMEMORY      memory = nullptr;
    PWSTR          channelName = nullptr;
    UNICODE_STRING retrievedName;

    PAGED_CODE();

    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");

    CODEC_PIN_CONTEXT * pinContext = GetCodecPinContext(Pin);
    ASSERT(pinContext != nullptr);

    PDEVICE_CONTEXT deviceContext = GetDeviceContext(pinContext->Device);
    ASSERT(deviceContext != nullptr);

    if (pinContext->NumOfChannelsPerDevice == 1)
    {
        RETURN_NTSTATUS_IF_FAILED(deviceContext->UsbAudioConfiguration->GetChannelName(false, pinContext->Channel, memory, channelName));
    }
    else
    {
        RETURN_NTSTATUS_IF_FAILED(deviceContext->UsbAudioConfiguration->GetStereoChannelName(false, pinContext->Channel, memory, channelName));
    }
    RtlInitUnicodeString(&retrievedName, channelName);

    *Name = retrievedName;

    WdfObjectDelete(memory);
    memory = nullptr;
    channelName = nullptr;

    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");

    return status;
}

NONPAGED_CODE_SEG
VOID CodecR_EvtPinContextCleanup(
    _In_ WDFOBJECT /* WdfPin */
)
/*++

Routine Description:

    In this callback, it cleans up pin context.

Arguments:

    WdfDevice - WDF device object

Return Value:

    nullptr

--*/
{
}

PAGED_CODE_SEG
VOID CodecR_EvtCircuitCleanup(
    _In_ WDFOBJECT wdfObject
)
{
    PCODEC_RENDER_CIRCUIT_CONTEXT circuitContext;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");

    ACXCIRCUIT circuit = (ACXCIRCUIT)wdfObject;

    circuitContext = GetRenderCircuitContext(circuit);
    ASSERT(circuitContext != nullptr);

    if (circuitContext->VolumeElementsMemory != nullptr)
    {
        WdfObjectDelete(circuitContext->VolumeElementsMemory);
        circuitContext->VolumeElementsMemory = nullptr;
        circuitContext->VolumeElements = nullptr;
    }
    if (circuitContext->MuteElementsMemory != nullptr)
    {
        WdfObjectDelete(circuitContext->MuteElementsMemory);
        circuitContext->MuteElementsMemory = nullptr;
        circuitContext->MuteElements = nullptr;
    }
    circuitContext->NumOfDevices = 0;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");
}

PAGED_CODE_SEG
NTSTATUS
CodecR_AddStaticRender(
    _In_ WDFDEVICE              Device,
    _In_ const GUID *           ComponentGuid,
    _In_ const UNICODE_STRING * CircuitName
)
/*++

Routine Description:

    Creates the static render circuit (pictured below) and
    adds it to the device context. This is called when a
    new device is detected and the AddDevice call is made
    by the pnp manager.

    ***************************************************************************
    * Render Circuit                                                          *
    *                                                                         *
    *              +--------------------------------------------+             *
    *              |                                            |             *
    *              |    +-------------+      +-------------+    |             *
    * Host  ------>|    | Volume Node |      |  Mute Node  |    |---> Bridge  *
    * Pin          |    +-------------+      +-------------+    |      Pin    *
    *              |                                            |             *
    *              +--------------------------------------------+             *
    *                                                                         *
    ***************************************************************************

    For example, if the Circuit name is "Speaker0", the path to the device interface for this Circuit would be:
    "\\?\usb#vid_0499&pid_1509#5&3821233e&0&11#{6994ad04-93ef-11d0-a3cc-00a0c9223196}\RenderDevice0"

Return Value:

    NTSTATUS

--*/
{
    NTSTATUS               status = STATUS_SUCCESS;
    PDEVICE_CONTEXT        deviceContext;
    PRENDER_DEVICE_CONTEXT renderDevContext;
    ACXCIRCUIT             renderCircuit = nullptr;
    WDF_OBJECT_ATTRIBUTES  attributes;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");

    deviceContext = GetDeviceContext(Device);
    ASSERT(deviceContext != nullptr);

    //
    // Alloc audio context to current device.
    //
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, RENDER_DEVICE_CONTEXT);
    RETURN_NTSTATUS_IF_FAILED(WdfObjectAllocateContext(Device, &attributes, (PVOID *)&renderDevContext));
    ASSERT(renderDevContext);

    //
    // Create a render circuit associated with this child device.
    //
    RETURN_NTSTATUS_IF_FAILED(CodecR_CreateRenderCircuit(Device, ComponentGuid, CircuitName, deviceContext->AudioProperty.SupportedSampleRate /* & GetSampleRateMask(deviceContext->AudioProperty.SampleRate) */, &renderCircuit));

    deviceContext->Render = renderCircuit;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");

    return status;
}

PAGED_CODE_SEG
NTSTATUS
Render_AllocateSupportedFormats(
    _In_ WDFDEVICE                   Device,
    _In_ ACXPIN                      Pin,
    _In_ ACXCIRCUIT                  Circuit,
    _In_ const ULONG                 SupportedSampleRate,
    _In_ const ULONG                 Channels,
    _In_ USBAudioDataFormatManager * UsbAudioDataFormatManager
)
{
    NTSTATUS                            status = STATUS_SUCCESS;
    ACXDATAFORMAT                       acxDataFormat{};
    ACXDATAFORMATLIST                   formatList;
    KSDATAFORMAT_WAVEFORMATEXTENSIBLE * ksDataFormatWaveFormatExtensible = nullptr;
    WDFMEMORY                           ksDataFormatWaveFormatExtensibleMemory = nullptr;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");

    auto allocateSupportedFormatsScope = wil::scope_exit([&]() {
        if (ksDataFormatWaveFormatExtensibleMemory != nullptr)
        {
            WdfObjectDelete(ksDataFormatWaveFormatExtensibleMemory);
            ksDataFormatWaveFormatExtensibleMemory = nullptr;
        }
        ksDataFormatWaveFormatExtensible = nullptr;
    });

    ///////////////////////////////////////////////////////////
    //
    // Define supported formats for the host pin.
    //

    //
    // The raw processing mode list is associated with each single circuit
    // by ACX. The driver uses this DDI to retrieve the built-in raw
    // data-format list.
    //
    formatList = AcxPinGetRawDataFormatList(Pin);
    RETURN_NTSTATUS_IF_TRUE(formatList == nullptr, STATUS_INSUFFICIENT_RESOURCES);

    for (ULONG mask = 1, index = 0; mask != 0; mask <<= 1, index++)
    {
        if (mask & SupportedSampleRate)
        {
            ULONG sampleRate = GetSampleRateFromIndex(index);

            ///////////////////////////////////////////////////////////
            //
            // Allocate the formats this circuit supports.
            //
            for (ULONG formatIndex = 0; formatIndex < UsbAudioDataFormatManager->GetNumOfUSBAudioDataFormats(); formatIndex++)
            {
                UCHAR bytesPerSample = UsbAudioDataFormatManager->GetBytesPerSample(formatIndex);
                UCHAR validBits = UsbAudioDataFormatManager->GetValidBits(formatIndex);

                status = USBAudioDataFormat::BuildWaveFormatExtensible(
                    Device,
                    sampleRate,
                    (UCHAR)Channels,
                    bytesPerSample,
                    validBits,
                    UsbAudioDataFormatManager->GetFormatType(formatIndex),
                    UsbAudioDataFormatManager->GetFormat(formatIndex),
                    false,
                    ksDataFormatWaveFormatExtensible,
                    ksDataFormatWaveFormatExtensibleMemory
                );

                if (NT_SUCCESS(status))
                {
                    RETURN_NTSTATUS_IF_FAILED(AllocateFormat(ksDataFormatWaveFormatExtensible, Circuit, Device, &acxDataFormat));

                    // The driver uses this DDI to add data formats to the raw
                    // processing mode list associated with the current circuit.
                    RETURN_NTSTATUS_IF_FAILED(AcxDataFormatListAddDataFormat(formatList, acxDataFormat));
                }
                else if (status != STATUS_NOT_SUPPORTED)
                {
                    RETURN_NTSTATUS_IF_FAILED(status);
                }

                if (ksDataFormatWaveFormatExtensibleMemory != nullptr)
                {
                    WdfObjectDelete(ksDataFormatWaveFormatExtensibleMemory);
                    ksDataFormatWaveFormatExtensibleMemory = nullptr;
                }
                ksDataFormatWaveFormatExtensible = nullptr;
            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
CodecR_CreateRenderCircuit(
    WDFDEVICE              Device,
    const GUID *           ComponentGuid,
    const UNICODE_STRING * CircuitName,
    const ULONG            SupportedSampleRate,
    ACXCIRCUIT *           Circuit
)
/*++

Routine Description:

    This routine builds the CODEC render circuit.

Return Value:

    NT status value

--*/
{
    NTSTATUS                       status = STATUS_SUCCESS;
    PDEVICE_CONTEXT                deviceContext;
    WDF_OBJECT_ATTRIBUTES          attributes;
    ACXCIRCUIT                     circuit;
    CODEC_RENDER_CIRCUIT_CONTEXT * circuitContext;
    WDFMEMORY                      pinsMemory = nullptr;
    ACXPIN *                       pins = nullptr;
    WDFMEMORY                      elementsMemory = nullptr;
    ACXELEMENT *                   elements = nullptr;
    ACX_CONNECTION *               connections = nullptr;
    UCHAR                          numOfChannels = 0;
    USHORT                         terminalType = 0;
    UCHAR                          volumeUnitID = USBAudioConfiguration::InvalidID;
    UCHAR                          muteUnitID = USBAudioConfiguration::InvalidID;
    ULONG                          numOfDevices = 0;
    ULONG                          numOfConnections = 0;
    ULONG                          numOfRemainingChannels = 0;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");

    auto createRenderCircuitScope = wil::scope_exit([&]() {
        if (pinsMemory != nullptr)
        {
            WdfObjectDelete(pinsMemory);
            pinsMemory = nullptr;
            pins = nullptr;
        }

        if (elementsMemory != nullptr)
        {
            WdfObjectDelete(elementsMemory);
            elementsMemory = nullptr;
            elements = nullptr;
        }

        if (connections != nullptr)
        {
            delete[] connections;
            connections = nullptr;
        }
    });

    deviceContext = GetDeviceContext(Device);
    ASSERT(deviceContext != nullptr);

    RETURN_NTSTATUS_IF_FAILED(deviceContext->UsbAudioConfiguration->GetStreamChannelInfoAdjusted(false, numOfChannels, terminalType, volumeUnitID, muteUnitID));
    RETURN_NTSTATUS_IF_FAILED(deviceContext->UsbAudioConfiguration->GetStreamDevicesAdjusted(false, numOfDevices));
    numOfRemainingChannels = numOfChannels;

    if (!deviceContext->UsbAudioConfiguration->IsEnableFeatureUnit(false))
    {
        volumeUnitID = muteUnitID = USBAudioConfiguration::InvalidID;
    }

    USBAudioDataFormatManager * usbAudioDataFormatManager = deviceContext->UsbAudioConfiguration->GetUSBAudioDataFormatManager(false);
    RETURN_NTSTATUS_IF_TRUE_ACTION(usbAudioDataFormatManager == nullptr, status = STATUS_INVALID_PARAMETER, status);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = Device;
    RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(ACXPIN) * CodecRenderPinCount * numOfDevices, &pinsMemory, (PVOID *)&pins));
    RtlZeroMemory(pins, sizeof(ACXPIN) * CodecRenderPinCount * numOfDevices);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = Device;
    RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(ACXELEMENT) * RenderElementCount * numOfDevices, &elementsMemory, (PVOID *)&elements));
    RtlZeroMemory(elements, sizeof(ACXELEMENT) * RenderElementCount * numOfDevices);

    numOfConnections = (RenderElementCount + 1) * numOfDevices;
    connections = new (POOL_FLAG_NON_PAGED, DRIVER_TAG) ACX_CONNECTION[numOfConnections];
    if (connections == nullptr)
    {
        RETURN_NTSTATUS_IF_FAILED(STATUS_INSUFFICIENT_RESOURCES);
    }
    RtlZeroMemory(connections, sizeof(ACX_CONNECTION) * numOfConnections);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - num of channels = %u, num of connections = %u", numOfChannels, numOfConnections);
    //
    // Init output value.
    //
    *Circuit = nullptr;

    ///////////////////////////////////////////////////////////
    //
    // Create a circuit.
    //
    {
        PACXCIRCUIT_INIT               circuitInit = nullptr;
        ACX_CIRCUIT_PNPPOWER_CALLBACKS powerCallbacks;

        //
        // The driver uses this DDI to allocate an ACXCIRCUIT_INIT
        // structure. This opaque structure is used when creating
        // a standalone audio circuit representing an audio device.
        //
        circuitInit = AcxCircuitInitAllocate(Device);

        //
        // The driver uses this DDI to free the allocated
        // ACXCIRCUIT_INIT structure when an error is detected.
        // Normally the structures is deleted/cleared by ACX when
        // an ACX circuit is created successfully.
        //
        auto circuitInitScope = wil::scope_exit([&circuitInit]() {
            if (circuitInit)
            {
                AcxCircuitInitFree(circuitInit);
            }
        });

        //
        // The driver uses this DDI to specify the Component ID
        // of the ACX circuit. This ID is a guid that uniquely
        // identifies the circuit instance (vendor specific).
        //
        AcxCircuitInitSetComponentId(circuitInit, ComponentGuid);

        //
        // The driver uses this DDI to specify the circuit name.
        // For standalone circuits, this is the audio device name
        // which is used by clients to open handles to the audio devices.
        //
        (VOID) AcxCircuitInitAssignName(circuitInit, CircuitName);

        //
        // The driver uses this DDI to specify the circuit type. The
        // circuit type can be AcxCircuitTypeRender, AcxCircuitTypeCapture,
        // AcxCircuitTypeOther, or AcxCircuitTypeMaximum (for validation).
        //
        AcxCircuitInitSetCircuitType(circuitInit, AcxCircuitTypeRender);

        //
        // The driver uses this DDI to assign its (if any) power callbacks.
        //
        ACX_CIRCUIT_PNPPOWER_CALLBACKS_INIT(&powerCallbacks);
        powerCallbacks.EvtAcxCircuitPowerUp = CodecR_EvtCircuitPowerUp;
        powerCallbacks.EvtAcxCircuitPowerDown = CodecR_EvtCircuitPowerDown;
        AcxCircuitInitSetAcxCircuitPnpPowerCallbacks(circuitInit, &powerCallbacks);

        //
        // The driver uses this DDI to register for a stream-create callback.
        //
        RETURN_NTSTATUS_IF_FAILED(AcxCircuitInitAssignAcxCreateStreamCallback(circuitInit, CodecR_EvtCircuitCreateStream));

        //
        // Private Property Handler
        //
        RETURN_NTSTATUS_IF_FAILED(AcxCircuitInitAssignProperties(circuitInit, s_PropertyItems, ARRAYSIZE(s_PropertyItems)));

        //
        // The driver uses this DDI to create a new ACX circuit.
        //
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, CODEC_RENDER_CIRCUIT_CONTEXT);
        attributes.EvtCleanupCallback = CodecR_EvtCircuitCleanup;
        RETURN_NTSTATUS_IF_FAILED(AcxCircuitCreate(Device, &attributes, &circuitInit, &circuit));

        circuitContext = GetRenderCircuitContext(circuit);
        ASSERT(circuitContext);

        circuitContext->NumOfDevices = numOfDevices;

        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = circuit;
        RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(ACXELEMENT) * numOfDevices, &(circuitContext->VolumeElementsMemory), (PVOID *)&(circuitContext->VolumeElements)));
        RtlZeroMemory(circuitContext->VolumeElements, sizeof(ACXVOLUME) * numOfDevices);

        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = circuit;
        RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(ACXELEMENT) * numOfDevices, &(circuitContext->MuteElementsMemory), (PVOID *)&(circuitContext->MuteElements)));
        RtlZeroMemory(circuitContext->MuteElements, sizeof(ACXMUTE) * numOfDevices);

        circuitInitScope.release();
    }

    //
    // Post circuit creation initialization.
    //
    ULONG elementIndex = 0;
    for (ULONG index = 0; index < numOfDevices; index++)
    {
        ULONG numOfChannelsPerDevice;

        if ((numOfRemainingChannels > 2) && deviceContext->UsbAudioConfiguration->IsDeviceSplittable(false))
        {
            numOfChannelsPerDevice = 2;
        }
        else
        {
            numOfChannelsPerDevice = (UCHAR)numOfRemainingChannels;
        }
        numOfRemainingChannels -= numOfChannelsPerDevice;

        ///////////////////////////////////////////////////////////
        //
        // Create mute and volume elements.
        //
        {
            if (volumeUnitID != USBAudioConfiguration::InvalidID)
            { // Volume Enable
                //
                // The driver uses this DDI to assign its volume element callbacks.
                //
                ACX_VOLUME_CALLBACKS volumeCallbacks;
                ACX_VOLUME_CALLBACKS_INIT(&volumeCallbacks);
                volumeCallbacks.EvtAcxRampedVolumeAssignLevel = CodecR_EvtRampedVolumeAssignLevel;
                volumeCallbacks.EvtAcxVolumeRetrieveLevel = CodecR_EvtVolumeRetrieveLevel;

                //
                // Create Volume element
                //
                ACX_VOLUME_CONFIG volumeCfg;
                ACX_VOLUME_CONFIG_INIT(&volumeCfg);

                RETURN_NTSTATUS_IF_FAILED(deviceContext->UsbAudioConfiguration->GetVolumeConfiguration(volumeUnitID, volumeCfg.Minimum, volumeCfg.Maximum, volumeCfg.SteppingDelta));
                volumeCfg.ChannelsCount = numOfChannelsPerDevice;
                volumeCfg.Name = &KSAUDFNAME_VOLUME_CONTROL;
                volumeCfg.Callbacks = &volumeCallbacks;
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - volume minimum %ld (0x%lx), maximum %ld (0x%lx), stepping delta %ld (0x%lx)", volumeCfg.Minimum, volumeCfg.Minimum, volumeCfg.Maximum, volumeCfg.Maximum, volumeCfg.SteppingDelta, volumeCfg.SteppingDelta);

                WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, VOLUME_ELEMENT_CONTEXT);
                attributes.ParentObject = circuit;

                RETURN_NTSTATUS_IF_FAILED(AcxVolumeCreate(circuit, &attributes, &volumeCfg, (ACXVOLUME *)&elements[elementIndex]));

                //
                // Saving the volume elements in the circuit context.
                //
                circuitContext->VolumeElements[index] = (ACXVOLUME)elements[elementIndex];

                PVOLUME_ELEMENT_CONTEXT volumeContext = GetVolumeElementContext(circuitContext->VolumeElements[index]);
                ASSERT(volumeContext);

                RtlZeroMemory(volumeContext, sizeof(VOLUME_ELEMENT_CONTEXT));
                volumeContext->Device = Device;
                volumeContext->EntityID = volumeUnitID;
                volumeContext->NumberOfChannels = min(numOfChannelsPerDevice, MAX_CHANNELS);
                elementIndex++;
            }

            if (muteUnitID != USBAudioConfiguration::InvalidID)
            { // Mute Enable
                //
                // The driver uses this DDI to assign its mute element callbacks.
                //
                ACX_MUTE_CALLBACKS muteCallbacks;
                ACX_MUTE_CALLBACKS_INIT(&muteCallbacks);
                muteCallbacks.EvtAcxMuteAssignState = CodecR_EvtMuteAssignState;
                muteCallbacks.EvtAcxMuteRetrieveState = CodecR_EvtMuteRetrieveState;

                //
                // Create Mute element
                //
                ACX_MUTE_CONFIG muteCfg;
                ACX_MUTE_CONFIG_INIT(&muteCfg);
                muteCfg.ChannelsCount = numOfChannelsPerDevice;
                muteCfg.Name = &KSAUDFNAME_WAVE_MUTE;
                muteCfg.Callbacks = &muteCallbacks;

                WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, MUTE_ELEMENT_CONTEXT);
                attributes.ParentObject = circuit;

                RETURN_NTSTATUS_IF_FAILED(AcxMuteCreate(circuit, &attributes, &muteCfg, (ACXMUTE *)&elements[elementIndex]));
                //
                // Saving the mute elements in the circuit context.
                //
                circuitContext->MuteElements[index] = (ACXMUTE)elements[elementIndex];

                PMUTE_ELEMENT_CONTEXT muteContext = GetMuteElementContext(circuitContext->MuteElements[index]);
                ASSERT(muteContext);

                RtlZeroMemory(muteContext, sizeof(MUTE_ELEMENT_CONTEXT));
                muteContext->Device = Device;
                muteContext->EntityID = muteUnitID;
                muteContext->NumberOfChannels = min(numOfChannelsPerDevice, MAX_CHANNELS);
                elementIndex++;
            }
        }

        ///////////////////////////////////////////////////////////
        //
        // Create the pins for the circuit.
        //
        {
            ACX_PIN_CONFIG      pinCfg;
            CODEC_PIN_CONTEXT * pinContext;
            ACX_PIN_CALLBACKS   pinCallbacks;

            ///////////////////////////////////////////////////////////
            //
            // Create Render Pin.
            //

            ACX_PIN_CONFIG_INIT(&pinCfg);
            pinCfg.Id = index * CodecRenderPinCount + CodecRenderHostPin;
            pinCfg.Type = AcxPinTypeSink;
            pinCfg.Communication = AcxPinCommunicationSink;
            pinCfg.Category = &KSCATEGORY_AUDIO;

            WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, CODEC_PIN_CONTEXT);
            attributes.EvtCleanupCallback = CodecR_EvtPinContextCleanup;
            attributes.ParentObject = circuit;

            //
            // The driver uses this DDI to create one or more pins on the circuits.
            //
            RETURN_NTSTATUS_IF_FAILED(AcxPinCreate(circuit, &attributes, &pinCfg, &pins[index * CodecRenderPinCount + CodecRenderHostPin]));

            pinContext = GetCodecPinContext(pins[index * CodecRenderPinCount + CodecRenderHostPin]);
            ASSERT(pinContext);
            pinContext->Device = Device;
            pinContext->CodecPinType = CodecPinTypeHost;
            pinContext->DeviceIndex = index;
            pinContext->Channel = index * 2;
            pinContext->NumOfChannelsPerDevice = numOfChannelsPerDevice;

            ///////////////////////////////////////////////////////////
            //
            // Create Device Bridge Pin.
            //
            ACX_PIN_CALLBACKS_INIT(&pinCallbacks);
            if (deviceContext->OutputProperty.ChannelNames != USBAudioConfiguration::InvalidString)
            {
                pinCallbacks.EvtAcxPinRetrieveName = CodecR_EvtAcxPinRetrieveName;
            }

            ACX_PIN_CONFIG_INIT(&pinCfg);
            pinCfg.Type = AcxPinTypeSource;
            pinCfg.Communication = AcxPinCommunicationNone;

            //
            // When category is KSNODETYPE_SPEAKER, the name given by
            // EvtAcxPinRetrieveName is not used and becomes Speaker.
            //
            // To solve this problem, when category is KSNODETYPE_SPEAKER and
            // the name of EvtAcxPinRetrieveName is valid, change it to
            // KSNODETYPE_LINE_CONNECTOR.
            //
            if (IsEqualGUID(*ConvertTerminalType(terminalType), KSNODETYPE_SPEAKER) && (deviceContext->OutputProperty.ChannelNames != USBAudioConfiguration::InvalidString))
            {
                pinCfg.Category = &KSNODETYPE_LINE_CONNECTOR;
            }
            else
            {
                pinCfg.Category = ConvertTerminalType(terminalType);
            }

            pinCfg.PinCallbacks = &pinCallbacks;

            WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, CODEC_PIN_CONTEXT);
            attributes.EvtCleanupCallback = CodecR_EvtPinContextCleanup;
            attributes.ParentObject = circuit;

            //
            // The driver uses this DDI to create one or more pins on the circuits.
            //
            RETURN_NTSTATUS_IF_FAILED(AcxPinCreate(circuit, &attributes, &pinCfg, &pins[index * CodecRenderPinCount + CodecRenderBridgePin]));

            pinContext = GetCodecPinContext(pins[index * CodecRenderPinCount + CodecRenderBridgePin]);
            ASSERT(pinContext);
            pinContext->Device = Device;
            pinContext->CodecPinType = CodecPinTypeDevice;
            pinContext->DeviceIndex = index;
            pinContext->Channel = index * 2;
            pinContext->NumOfChannelsPerDevice = numOfChannelsPerDevice;
        }

        ///////////////////////////////////////////////////////////
        //
        // Add audio jack to bridge pin.
        // For more information on audio jack see: https://docs.microsoft.com/en-us/windows/win32/api/devicetopology/ns-devicetopology-ksjack_description
        //
        {
            ACX_JACK_CALLBACKS jackCallbacks;
            ACX_JACK_CONFIG    jackCfg;
            ACXJACK            jack;
            PJACK_CONTEXT      jackContext;

            ACX_JACK_CALLBACKS_INIT(&jackCallbacks);
            jackCallbacks.EvtAcxJackRetrievePresenceState = EvtJackRetrievePresence;

            ACX_JACK_CONFIG_INIT(&jackCfg);
            jackCfg.Description.ChannelMapping = (numOfChannelsPerDevice == 1 ? SPEAKER_FRONT_CENTER : SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
            jackCfg.Description.Color = RGB(0, 0, 0);
            jackCfg.Description.ConnectionType = AcxConnTypeAtapiInternal;
            jackCfg.Description.GeoLocation = AcxGeoLocFront;
            jackCfg.Description.GenLocation = AcxGenLocPrimaryBox;
            jackCfg.Description.PortConnection = AcxPortConnIntegratedDevice;
            jackCfg.Flags = AcxJackConfigJackDetection;
            jackCfg.Callbacks = &jackCallbacks;
            WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, JACK_CONTEXT);
            attributes.ParentObject = pins[index * CodecRenderPinCount + CodecRenderBridgePin];

            RETURN_NTSTATUS_IF_FAILED(AcxJackCreate(pins[index * CodecRenderPinCount + CodecRenderBridgePin], &attributes, &jackCfg, &jack));

            ASSERT(jack != nullptr);

            jackContext = GetJackContext(jack);
            ASSERT(jackContext);
            jackContext->IsConnected = true;

            PCODEC_PIN_CONTEXT pinContext = GetCodecPinContext(pins[index * CodecCapturePinCount + CodecCaptureBridgePin]);
            pinContext->jack = jack;

            RETURN_NTSTATUS_IF_FAILED(AcxPinAddJacks(pins[index * CodecRenderPinCount + CodecRenderBridgePin], &jack, 1));
        }

        if (deviceContext->UsbAudioConfiguration->HasOutputIsochronousInterface())
        {
            RETURN_NTSTATUS_IF_FAILED(Render_AllocateSupportedFormats(Device, pins[index * CodecRenderPinCount + CodecRenderHostPin], circuit, SupportedSampleRate, numOfChannelsPerDevice, usbAudioDataFormatManager));
        }
    }

    //
    // The driver uses this DDI post circuit creation to add ACXELEMENTs.
    //
    if (elementIndex != 0)
    {
        RETURN_NTSTATUS_IF_FAILED(AcxCircuitAddElements(circuit, elements, elementIndex));
    }
    ///////////////////////////////////////////////////////////
    //
    // The driver uses this DDI post circuit creation to add ACXPINs.
    //
    RETURN_NTSTATUS_IF_FAILED(AcxCircuitAddPins(circuit, pins, CodecRenderPinCount * numOfDevices));

    {
        ULONG connectionIndex = 0;
        //              Circuit layout
        //           +---------------------------+
        //           |   +--------+   +------+   |
        //  Host -0->|---| volume |---| mute |---|-1-> Bridge Pin
        //           |   +--------+   +------+   |
        //           |       0           1       |
        //           |                +------+   |
        //  Host -2->|----------------| mute |---|-3-> Bridge Pin
        //           |                +------+   |
        //           |                   2       |
        //           |   +--------+              |
        //  Host -4->|---| volume |--------------|-5-> Bridge Pin
        //           |   +--------+              |
        //           |       3                   |
        //           |                           |
        //  Host -6->|---------------------------|-7-> Bridge Pin
        //           |                           |
        //           +---------------------------+
        elementIndex = 0;

        for (UCHAR index = 0; index < numOfDevices; index++)
        {
            if (circuitContext->VolumeElements[index] != nullptr)
            {
                if (circuitContext->MuteElements[index] != nullptr)
                {
                    ACX_CONNECTION_INIT(&connections[connectionIndex], circuit, circuitContext->VolumeElements[index]);
                    connections[connectionIndex].FromPin.Id = index * CodecRenderPinCount + CodecRenderHostPin;
                    connectionIndex++;

                    ACX_CONNECTION_INIT(&connections[connectionIndex], circuitContext->VolumeElements[index], circuitContext->MuteElements[index]);
                    connectionIndex++;

                    ACX_CONNECTION_INIT(&connections[connectionIndex], circuitContext->MuteElements[index], circuit);
                    connections[connectionIndex].ToPin.Id = index * CodecRenderPinCount + CodecRenderBridgePin;
                    connectionIndex++;
                }
                else
                {
                    ACX_CONNECTION_INIT(&connections[connectionIndex], circuit, circuitContext->VolumeElements[index]);
                    connections[connectionIndex].FromPin.Id = index * CodecRenderPinCount + CodecRenderHostPin;
                    connectionIndex++;

                    ACX_CONNECTION_INIT(&connections[connectionIndex], circuitContext->VolumeElements[index], circuit);
                    connections[connectionIndex].ToPin.Id = index * CodecRenderPinCount + CodecRenderBridgePin;
                    connectionIndex++;
                }
            }
            else
            {
                if (circuitContext->MuteElements[index] != nullptr)
                {
                    ACX_CONNECTION_INIT(&connections[connectionIndex], circuit, circuitContext->MuteElements[index]);
                    connections[connectionIndex].FromPin.Id = index * CodecRenderPinCount + CodecRenderHostPin;
                    connectionIndex++;

                    ACX_CONNECTION_INIT(&connections[connectionIndex], circuitContext->MuteElements[index], circuit);
                    connections[connectionIndex].ToPin.Id = index * CodecRenderPinCount + CodecRenderBridgePin;
                    connectionIndex++;
                }
                else
                {
                    ACX_CONNECTION_INIT(&connections[connectionIndex], circuit, circuit);
                    connections[connectionIndex].FromPin.Id = index * CodecRenderPinCount + CodecRenderHostPin;
                    connections[connectionIndex].ToPin.Id = index * CodecRenderPinCount + CodecRenderBridgePin;
                    connectionIndex++;
                }
            }
        }
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - connection index = %u", connectionIndex);
        //
        // Add the connections linking circuit to elements.
        //
        RETURN_NTSTATUS_IF_FAILED(AcxCircuitAddConnections(circuit, connections, connectionIndex));
    }

    //
    // Set output value.
    //
    *Circuit = circuit;

    //
    // Done.
    //
    status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
CodecR_EvtCircuitPowerUp(
    _In_ WDFDEVICE /* Device */,
    _In_ ACXCIRCUIT  Circuit,
    _In_ WDF_POWER_DEVICE_STATE /* PreviousState */
)
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");

    NTSTATUS status = AddPropertyToCircuitInterface(Circuit, ARRAYSIZE(c_InterfaceProperties), c_InterfaceProperties);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
CodecR_EvtCircuitPowerDown(
    _In_ WDFDEVICE /* Device */,
    _In_ ACXCIRCUIT /* Circuit */,
    _In_ WDF_POWER_DEVICE_STATE /* TargetState */
)
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
NTSTATUS
CodecR_EvtCircuitCreateStream(
    _In_ WDFDEVICE       Device,
    _In_ ACXCIRCUIT      Circuit,
    _In_ ACXPIN          Pin,
    _In_ PACXSTREAM_INIT StreamInit,
    _In_ ACXDATAFORMAT   StreamFormat,
    _In_ const GUID * /* SignalProcessingMode */,
    _In_ ACXOBJECTBAG /* VarArguments */
)
/*++

Routine Description:

    This routine creates a stream for the specified circuit.

Return Value:

    NT status value

--*/
{
    NTSTATUS                       status;
    PDEVICE_CONTEXT                deviceContext;
    PRENDER_DEVICE_CONTEXT         renderDeviceContext;
    WDF_OBJECT_ATTRIBUTES          attributes;
    ACXSTREAM                      stream;
    STREAMENGINE_CONTEXT *         streamContext;
    ACX_STREAM_CALLBACKS           streamCallbacks;
    ACX_RT_STREAM_CALLBACKS        rtCallbacks;
    CRenderStreamEngine *          renderStreamEngine = nullptr;
    CODEC_PIN_TYPE                 codecPinType;
    PCODEC_PIN_CONTEXT             pinContext;
    ACX_PIN_TYPE                   pinType;
    CODEC_RENDER_CIRCUIT_CONTEXT * circuitContext;

    auto streamEngineScope = wil::scope_exit([&renderStreamEngine]() {
        delete renderStreamEngine;
    });

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - render pin id %u", AcxPinGetId(Pin));

    TraceAcxDataFormat(TRACE_LEVEL_VERBOSE, StreamFormat);

    // ASSERT(IsEqualGUID(*SignalProcessingMode, AUDIO_SIGNALPROCESSINGMODE_RAW));

    ACXDATAFORMAT selectedDataFormat = StreamFormat;
    if (GetFormatTagFromAcxDataFormat(StreamFormat) != WAVE_FORMAT_EXTENSIBLE)
    {
        status = ConvertWaveFormatExToWaveFormatExtensible(Device, Circuit, StreamFormat, selectedDataFormat);
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "ConvertWaveFormatExToWaveFormatExtensible %!STATUS!", status);
        if (!NT_SUCCESS(status))
        {
            return status;
        }
    }

    ASSERT(Circuit != nullptr);
    circuitContext = GetRenderCircuitContext(Circuit);
    ASSERT(circuitContext);

    deviceContext = GetDeviceContext(Device);
    ASSERT(deviceContext != nullptr);

    renderDeviceContext = GetRenderDeviceContext(Device);
    ASSERT(renderDeviceContext != nullptr);
    UNREFERENCED_PARAMETER(renderDeviceContext);

    pinContext = GetCodecPinContext(Pin);
    codecPinType = pinContext->CodecPinType;

    pinType = AcxPinGetType(Pin);

    if (USBAudioAcxDriverHasAsioOwnership(deviceContext))
    {
        ACXDATAFORMAT dataFormat = nullptr;
        status = USBAudioAcxDriverGetCurrentDataFormat(deviceContext, false, dataFormat);
        RETURN_NTSTATUS_IF_FAILED(status);

        ACXDATAFORMAT stereoDataFormat;
        RETURN_NTSTATUS_IF_FAILED(SplitAcxDataFormatByDeviceChannels(Device, Circuit, pinContext->NumOfChannelsPerDevice, stereoDataFormat, dataFormat));

        if (!AcxDataFormatIsEqual(stereoDataFormat, selectedDataFormat))
        {
            status = STATUS_NOT_SUPPORTED;
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit %!STATUS!", status);
            return status;
        }
    }

    //
    // Init streaming callbacks.
    //
    ACX_STREAM_CALLBACKS_INIT(&streamCallbacks);
    streamCallbacks.EvtAcxStreamPrepareHardware = EvtStreamPrepareHardware;
    streamCallbacks.EvtAcxStreamReleaseHardware = EvtStreamReleaseHardware;
    streamCallbacks.EvtAcxStreamRun = EvtStreamRun;
    streamCallbacks.EvtAcxStreamPause = EvtStreamPause;

    RETURN_NTSTATUS_IF_FAILED(AcxStreamInitAssignAcxStreamCallbacks(StreamInit, &streamCallbacks));

    //
    // Init RT streaming callbacks.
    //
    ACX_RT_STREAM_CALLBACKS_INIT(&rtCallbacks);
    rtCallbacks.EvtAcxStreamGetHwLatency = EvtStreamGetHwLatency;
    rtCallbacks.EvtAcxStreamAllocateRtPackets = EvtStreamAllocateRtPackets;
    rtCallbacks.EvtAcxStreamFreeRtPackets = EvtStreamFreeRtPackets;
    rtCallbacks.EvtAcxStreamSetRenderPacket = CodecR_EvtStreamSetRenderPacket;
    rtCallbacks.EvtAcxStreamGetCurrentPacket = EvtStreamGetCurrentPacket;
    rtCallbacks.EvtAcxStreamGetPresentationPosition = EvtStreamGetPresentationPosition;

    RETURN_NTSTATUS_IF_FAILED(AcxStreamInitAssignAcxRtStreamCallbacks(StreamInit, &rtCallbacks));

    //
    // Buffer notifications are supported.
    //
    AcxStreamInitSetAcxRtStreamSupportsNotifications(StreamInit);

    //
    // Create the stream.
    //
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, STREAMENGINE_CONTEXT);
    attributes.EvtDestroyCallback = EvtStreamDestroy;
    RETURN_NTSTATUS_IF_FAILED(AcxRtStreamCreate(Device, Circuit, &attributes, &StreamInit, &stream));

    //
    // Create the virtual streaming engine which will control
    // streaming logic for the render circuit.
    //
    renderStreamEngine = new (POOL_FLAG_NON_PAGED, DRIVER_TAG) CRenderStreamEngine(deviceContext, stream, selectedDataFormat, pinContext->DeviceIndex, pinContext->Channel, pinContext->NumOfChannelsPerDevice, FALSE /* , nullptr */);
    RETURN_NTSTATUS_IF_TRUE(renderStreamEngine == nullptr, STATUS_INSUFFICIENT_RESOURCES);

    streamContext = GetStreamEngineContext(stream);
    ASSERT(streamContext);
    streamContext->StreamEngine = (PVOID)renderStreamEngine;
    streamContext->DeviceIndex = pinContext->DeviceIndex;
    streamContext->Channel = pinContext->Channel;
    streamContext->NumOfChannelsPerDevice = pinContext->NumOfChannelsPerDevice;

    renderStreamEngine = nullptr;

    //
    // Done.
    //

    status = STATUS_SUCCESS;
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
NTSTATUS
CodecR_EvtStreamSetRenderPacket(
    _In_ ACXSTREAM Stream,
    _In_ ULONG     Packet,
    _In_ ULONG     Flags,
    _In_ ULONG     EosPacketLength
)
{
    PSTREAMENGINE_CONTEXT context;
    CRenderStreamEngine * streamEngine = nullptr;

    PAGED_CODE();
    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");

    context = GetStreamEngineContext(Stream);

    streamEngine = static_cast<CRenderStreamEngine *>(context->StreamEngine);

    NTSTATUS status = streamEngine->SetRenderPacket(Packet, Flags, EosPacketLength);

    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
CodecR_VolumeChangeLevelNotification(
    ACXCIRCUIT Circuit,
    UCHAR      EntityID
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");

    CODEC_RENDER_CIRCUIT_CONTEXT * circuitContext = GetRenderCircuitContext(Circuit);
    ASSERT(circuitContext);

    for (ULONG index = 0; index < circuitContext->NumOfDevices; index++)
    {
        if (circuitContext->VolumeElements[index] != nullptr)
        {
            PVOLUME_ELEMENT_CONTEXT volumeContext = GetVolumeElementContext(circuitContext->VolumeElements[index]);
            ASSERT(volumeContext);

            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - volume context entity ID 0x%02x, entity ID 0x%02x", volumeContext->EntityID, EntityID);
            if (volumeContext->EntityID == EntityID)
            {
                bool            notify = false;
                LONG            volume;
                PDEVICE_CONTEXT deviceContext = GetDeviceContext(volumeContext->Device);
                ASSERT(deviceContext != nullptr);

                for (ULONG i = 0; i < volumeContext->NumberOfChannels; ++i)
                {
                    status = deviceContext->UsbAudioConfiguration->GetCurrentVolume(deviceContext, volumeContext->EntityID, (UCHAR)i, volume);
                    if (NT_SUCCESS(status))
                    {
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - get current volume %ld, entity ID 0x%02x, channel %d", volume, volumeContext->EntityID, i);
                        if (volumeContext->VolumeLevel[i] != volume)
                        {
                            volumeContext->VolumeLevel[i] = volume;
                            notify = true;
                        }
                    }
                }
                if (notify)
                {
                    AcxVolumeChangeLevelNotification(circuitContext->VolumeElements[index]);
                }
                return STATUS_SUCCESS;
            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
CodecR_MuteChangeStateNotification(
    ACXCIRCUIT Circuit,
    UCHAR      EntityID
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");

    CODEC_RENDER_CIRCUIT_CONTEXT * circuitContext = GetRenderCircuitContext(Circuit);
    ASSERT(circuitContext);

    for (ULONG index = 0; index < circuitContext->NumOfDevices; index++)
    {
        if (circuitContext->MuteElements[index] != nullptr)
        {
            PMUTE_ELEMENT_CONTEXT muteContext = GetMuteElementContext(circuitContext->MuteElements[index]);
            ASSERT(muteContext);

            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - mute context entity ID 0x%02x, entity ID 0x%02x", muteContext->EntityID, EntityID);
            if (muteContext->EntityID == EntityID)
            {
                bool            notify = false;
                bool            mute;
                PDEVICE_CONTEXT deviceContext = GetDeviceContext(muteContext->Device);
                ASSERT(deviceContext != nullptr);

                for (ULONG i = 0; i < muteContext->NumberOfChannels; ++i)
                {
                    status = deviceContext->UsbAudioConfiguration->GetCurrentMute(deviceContext, muteContext->EntityID, (UCHAR)i, mute);
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - get current mute %!bool!, entity ID 0x%02x, channel %d", (mute != 0) ? true : false, muteContext->EntityID, i);
                    if (NT_SUCCESS(status))
                    {
                        if (muteContext->MuteState[i] != mute)
                        {
                            muteContext->MuteState[i] = mute;
                            notify = true;
                        }
                    }
                }
                if (notify)
                {
                    AcxMuteChangeStateNotification(circuitContext->MuteElements[index]);
                }
                return STATUS_SUCCESS;
            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
CodecR_ConnectorChangeStateNotification(
    ACXCIRCUIT Circuit,
    UCHAR      EntityID
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");

    ULONG pinCount = AcxCircuitGetPinsCount(Circuit) / CodecRenderPinCount;

    for (ULONG pinIndex = 0; pinIndex < pinCount; ++pinIndex)
    {
        ULONG  pinID = pinIndex * CodecRenderPinCount + CodecRenderBridgePin;
        ACXPIN pin = AcxCircuitGetPinById(Circuit, pinID);

        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "pinID %u, pin %p", pinID, pin);

        if (pin == nullptr)
        {
            continue;
        }

        PCODEC_PIN_CONTEXT pinContext = GetCodecPinContext(pin);

        PDEVICE_CONTEXT deviceContext = GetDeviceContext(pinContext->Device);

        NS_USBAudio::AUDIO_CHANNEL_CLUSTER_DESCRIPTOR connectorState;

        status = deviceContext->UsbAudioConfiguration->GetCurrentConnectorState(
            deviceContext,
            EntityID,
            connectorState
        );

        BOOLEAN isConnected = false;
        if (0 < connectorState.bmChannelConfig)
        {
            isConnected = true;
        }

        if (pinContext->jack == nullptr)
        {
            continue;
        }

        ACXJACK       jack = pinContext->jack;
        PJACK_CONTEXT jackContext = GetJackContext(jack);
        jackContext->IsConnected = isConnected;
        AcxJackChangeStateNotification(jack);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit %!STATUS!", status);

    return status;
}