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

    CircuitCommon.cpp

Abstract:

    Circuit Common.
    This file implements common routines for the Render Circuit and the Capture Circuit.

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
#include "AudioIsochronousEngine.h"

#ifndef __INTELLISENSE__
#include "CircuitCommon.tmh"
#endif

#pragma warning(disable : 4127)

//
//  Local function prototypes
//

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
Codec_EvtAcxPinSetDataFormat(
    ACXPIN /* Pin */,
    ACXDATAFORMAT /* DataFormat */
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

///////////////////////////////////////////////////////////
//
// For more information on mute element see: https://docs.microsoft.com/en-us/windows-hardware/drivers/audio/ksnodetype-mute
//
_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
NTAPI
Codec_EvtMuteAssignState(
    ACXMUTE Mute,
    ULONG   Channel,
    ULONG   State
)
{
    NTSTATUS              status = STATUS_SUCCESS;
    PDEVICE_CONTEXT       deviceContext;
    PMUTE_ELEMENT_CONTEXT muteContext;
    bool                  muteState = (State != 0) ? true : false;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Entry");
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
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - set current mute %!bool!, entity ID 0x%02x, channel %d", (muteState != 0) ? true : false, muteContext->EntityID, Channel);
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
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - set current mute %!bool!, entity ID 0x%02x, channel %d", (muteState != 0) ? true : false, muteContext->EntityID, Channel);
                status = deviceContext->UsbAudioConfiguration->SetCurrentMute(deviceContext, muteContext->EntityID, (UCHAR)i, muteState);
            }
            muteContext->MuteState[i] = muteState;
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
NTAPI
Codec_EvtMuteRetrieveState(
    ACXMUTE Mute,
    ULONG   Channel,
    ULONG * State
)
{
    PMUTE_ELEMENT_CONTEXT muteContext;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Entry");

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

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Exit, state = %d", *State);

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
Codec_EvtRampedVolumeAssignLevel(
    ACXVOLUME Volume,
    ULONG     Channel,
    LONG      VolumeLevel,
    ACX_VOLUME_CURVE_TYPE /* CurveType */,
    ULONGLONG /* CurveDuration */
)
{
    NTSTATUS                status = STATUS_SUCCESS;
    PDEVICE_CONTEXT         deviceContext;
    PVOLUME_ELEMENT_CONTEXT volumeContext;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Entry, channel %u, volume level %ld", Channel, VolumeLevel);

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
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - set current volume %ld, entity ID 0x%02x, channel %d", VolumeLevel, volumeContext->EntityID, Channel);
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
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - set current volume %ld, entity ID 0x%02x, channel %d", VolumeLevel, volumeContext->EntityID, i);
                status = deviceContext->UsbAudioConfiguration->SetCurrentVolume(deviceContext, volumeContext->EntityID, (UCHAR)i, VolumeLevel);
                if (NT_SUCCESS(status))
                {
                    volumeContext->VolumeLevel[i] = VolumeLevel;
                }
            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
NTAPI
Codec_EvtVolumeRetrieveLevel(
    ACXVOLUME Volume,
    ULONG     Channel,
    LONG *    VolumeLevel
)
{
    PVOLUME_ELEMENT_CONTEXT volumeContext;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Entry, channel %u", Channel);

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

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
Codec_CreateVolumeElement(
    WDFDEVICE    Device,
    ACXCIRCUIT   Circuit,
    UCHAR        VolumeUnitID,
    const GUID * Name,
    ULONG        ChannelsCount,
    ACXELEMENT & VolumeElement
)
{
    PAGED_CODE();

    WDF_OBJECT_ATTRIBUTES attributes{};

    ASSERT(Device != nullptr);
    PDEVICE_CONTEXT deviceContext = GetDeviceContext(Device);
    ASSERT(deviceContext != nullptr);
    ASSERT(Circuit != nullptr);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Entry");

    //
    // The driver uses this DDI to assign its volume element callbacks.
    //
    ACX_VOLUME_CALLBACKS volumeCallbacks;
    ACX_VOLUME_CALLBACKS_INIT(&volumeCallbacks);
    volumeCallbacks.EvtAcxRampedVolumeAssignLevel = Codec_EvtRampedVolumeAssignLevel;
    volumeCallbacks.EvtAcxVolumeRetrieveLevel = Codec_EvtVolumeRetrieveLevel;

    //
    // Create Volume element
    //
    ACX_VOLUME_CONFIG volumeCfg{};
    ACX_VOLUME_CONFIG_INIT(&volumeCfg);

    RETURN_NTSTATUS_IF_FAILED(deviceContext->UsbAudioConfiguration->GetVolumeConfiguration(VolumeUnitID, volumeCfg.Minimum, volumeCfg.Maximum, volumeCfg.SteppingDelta));
    volumeCfg.ChannelsCount = ChannelsCount;
    volumeCfg.Name = Name;
    volumeCfg.Callbacks = &volumeCallbacks;
    TraceEvents(TRACE_LEVEL_ERROR, TRACE_ENTITY, " - volume minimum %ld (0x%lx), maximum %ld (0x%lx), stepping delta %ld (0x%lx)", volumeCfg.Minimum, volumeCfg.Minimum, volumeCfg.Maximum, volumeCfg.Maximum, volumeCfg.SteppingDelta, volumeCfg.SteppingDelta);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, VOLUME_ELEMENT_CONTEXT);
    attributes.ParentObject = Circuit;

    RETURN_NTSTATUS_IF_FAILED(AcxVolumeCreate(Circuit, &attributes, &volumeCfg, (ACXVOLUME *)&VolumeElement));

    PVOLUME_ELEMENT_CONTEXT volumeContext = GetVolumeElementContext(VolumeElement);
    ASSERT(volumeContext);

    RtlZeroMemory(volumeContext, sizeof(VOLUME_ELEMENT_CONTEXT));
    volumeContext->Device = Device;
    volumeContext->EntityID = VolumeUnitID;
    volumeContext->NumberOfChannels = min(ChannelsCount, MAX_CHANNELS);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
Codec_CreateMuteElement(
    WDFDEVICE    Device,
    ACXCIRCUIT   Circuit,
    UCHAR        MuteUnitID,
    const GUID * Name,
    ULONG        ChannelsCount,
    ACXELEMENT & MuteElement
)
{
    PAGED_CODE();

    WDF_OBJECT_ATTRIBUTES attributes{};

    ASSERT(Device != nullptr);
    ASSERT(Circuit != nullptr);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Entry");

    //
    // The driver uses this DDI to assign its mute element callbacks.
    //
    ACX_MUTE_CALLBACKS muteCallbacks;
    ACX_MUTE_CALLBACKS_INIT(&muteCallbacks);
    muteCallbacks.EvtAcxMuteAssignState = Codec_EvtMuteAssignState;
    muteCallbacks.EvtAcxMuteRetrieveState = Codec_EvtMuteRetrieveState;

    //
    // Create Mute element
    //
    ACX_MUTE_CONFIG muteCfg;
    ACX_MUTE_CONFIG_INIT(&muteCfg);
    muteCfg.ChannelsCount = ChannelsCount;
    muteCfg.Name = Name;
    muteCfg.Callbacks = &muteCallbacks;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, MUTE_ELEMENT_CONTEXT);
    attributes.ParentObject = Circuit;

    RETURN_NTSTATUS_IF_FAILED(AcxMuteCreate(Circuit, &attributes, &muteCfg, (ACXMUTE *)&MuteElement));

    PMUTE_ELEMENT_CONTEXT muteContext = GetMuteElementContext(MuteElement);
    ASSERT(muteContext);

    RtlZeroMemory(muteContext, sizeof(MUTE_ELEMENT_CONTEXT));
    muteContext->Device = Device;
    muteContext->EntityID = MuteUnitID;
    muteContext->NumberOfChannels = min(ChannelsCount, MAX_CHANNELS);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
Codec_AllocateSupportedFormats(
    WDFDEVICE                   Device,
    ACXPIN                      Pin,
    ACXCIRCUIT                  Circuit,
    const ULONG                 SupportedSampleRate,
    const ULONG                 Channels,
    USBAudioDataFormatManager * UsbAudioDataFormatManager
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
