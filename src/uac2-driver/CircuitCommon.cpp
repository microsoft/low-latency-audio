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
PAGED_CODE_SEG
_Success_(NT_SUCCESS(return))
static NTSTATUS AddAudioJackToBridgePin(
    _In_ ACXPIN Pin,
    _In_ ULONG  SpeakerPositions
)
{
    ACX_JACK_CALLBACKS    jackCallbacks{};
    ACX_JACK_CONFIG       jackCfg{};
    ACXJACK               jack{};
    PJACK_CONTEXT         jackContext = nullptr;
    WDF_OBJECT_ATTRIBUTES attributes{};

    PAGED_CODE();

    //
    // Add audio jack to bridge pin.
    // For more information on audio jack see: https://docs.microsoft.com/en-us/windows/win32/api/devicetopology/ns-devicetopology-ksjack_description
    //
    ACX_JACK_CALLBACKS_INIT(&jackCallbacks);
    jackCallbacks.EvtAcxJackRetrievePresenceState = EvtJackRetrievePresence;

    ACX_JACK_CONFIG_INIT(&jackCfg);
    jackCfg.Description.ChannelMapping = SpeakerPositions;
    jackCfg.Description.Color = RGB(0, 0, 0);
    jackCfg.Description.ConnectionType = AcxConnTypeAtapiInternal;
    jackCfg.Description.GeoLocation = AcxGeoLocFront;
    jackCfg.Description.GenLocation = AcxGenLocPrimaryBox;
    jackCfg.Description.PortConnection = AcxPortConnIntegratedDevice;
    jackCfg.Flags = AcxJackConfigJackDetection;
    jackCfg.Callbacks = &jackCallbacks;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, JACK_CONTEXT);
    attributes.ParentObject = Pin;

    RETURN_NTSTATUS_IF_FAILED(AcxJackCreate(Pin, &attributes, &jackCfg, &jack));

    ASSERT(jack != nullptr);
    jackContext = GetJackContext(jack);
    ASSERT(jackContext);
    jackContext->IsConnected = TRUE;

    PCODEC_PIN_CONTEXT pinContext = GetCodecPinContext(Pin);
    pinContext->jack = jack;

    RETURN_NTSTATUS_IF_FAILED(AcxPinAddJacks(Pin, &jack, 1));
    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
NTSTATUS
Codec_EvtAcxPinRetrieveName(
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
    ASSERT(pinContext->AudioIsochronousEngine != nullptr);

    if (pinContext->NumOfChannelsPerDevice == 1)
    {
        RETURN_NTSTATUS_IF_FAILED(pinContext->AudioIsochronousEngine->GetChannelName(pinContext->IsInput, pinContext->Channel, memory, channelName));
    }
    else
    {
        RETURN_NTSTATUS_IF_FAILED(pinContext->AudioIsochronousEngine->GetStereoChannelName(pinContext->IsInput, pinContext->Channel, memory, channelName));
    }
    RtlInitUnicodeString(&retrievedName, channelName);

    *Name = retrievedName;

    WdfObjectDelete(memory);
    memory = nullptr;
    channelName = nullptr;

    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");

    return status;
}

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
    _In_ AudioIsochronousEngine * AudioIsochronousEngine,
    _In_ WDFDEVICE                Device,
    _In_ ACXCIRCUIT               Circuit,
    _Inout_ ACXELEMENT &          Element,
    _In_ UCHAR                    UnitID
)
{
    UCHAR                 numOfChannels = 0;
    ACXELEMENT            volumeElement = nullptr;
    WDF_OBJECT_ATTRIBUTES attributes{};

    PAGED_CODE();
    ASSERT(Device != nullptr);
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

    RETURN_NTSTATUS_IF_FAILED(AudioIsochronousEngine->GetInformationForVolumeElement(UnitID, numOfChannels, volumeCfg.Minimum, volumeCfg.Maximum, volumeCfg.SteppingDelta));

    volumeCfg.ChannelsCount = numOfChannels;
    volumeCfg.Name = &KSAUDFNAME_VOLUME_CONTROL;
    volumeCfg.Callbacks = &volumeCallbacks;
    TraceEvents(TRACE_LEVEL_ERROR, TRACE_ENTITY, " - volume minimum %ld (0x%lx), maximum %ld (0x%lx), stepping delta %ld (0x%lx)", volumeCfg.Minimum, volumeCfg.Minimum, volumeCfg.Maximum, volumeCfg.Maximum, volumeCfg.SteppingDelta, volumeCfg.SteppingDelta);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, VOLUME_ELEMENT_CONTEXT);
    attributes.ParentObject = Circuit;

    RETURN_NTSTATUS_IF_FAILED(AcxVolumeCreate(Circuit, &attributes, &volumeCfg, (ACXVOLUME *)&volumeElement));

    PVOLUME_ELEMENT_CONTEXT volumeContext = GetVolumeElementContext(volumeElement);
    ASSERT(volumeContext);

    RtlZeroMemory(volumeContext, sizeof(VOLUME_ELEMENT_CONTEXT));
    volumeContext->Device = Device;
    volumeContext->EntityID = UnitID;
    volumeContext->NumberOfChannels = min(numOfChannels, MAX_CHANNELS);

    Element = volumeElement;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
Codec_CreateMuteElement(
    _In_ AudioIsochronousEngine * AudioIsochronousEngine,
    _In_ WDFDEVICE                Device,
    _In_ ACXCIRCUIT               Circuit,
    _Inout_ ACXELEMENT &          Element,
    _In_ UCHAR                    UnitID
)
{
    UCHAR                 numOfChannels = 0;
    ACXELEMENT            muteElement = nullptr;
    WDF_OBJECT_ATTRIBUTES attributes{};

    PAGED_CODE();

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

    RETURN_NTSTATUS_IF_FAILED(AudioIsochronousEngine->GetInformationForMuteElement(UnitID, numOfChannels));

    muteCfg.ChannelsCount = numOfChannels;
    muteCfg.Name = &KSAUDFNAME_WAVE_MUTE;
    muteCfg.Callbacks = &muteCallbacks;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, MUTE_ELEMENT_CONTEXT);
    attributes.ParentObject = Circuit;

    RETURN_NTSTATUS_IF_FAILED(AcxMuteCreate(Circuit, &attributes, &muteCfg, (ACXMUTE *)&muteElement));

    PMUTE_ELEMENT_CONTEXT muteContext = GetMuteElementContext(muteElement);
    ASSERT(muteContext);

    RtlZeroMemory(muteContext, sizeof(MUTE_ELEMENT_CONTEXT));
    muteContext->Device = Device;
    muteContext->EntityID = UnitID;
    muteContext->NumberOfChannels = min(numOfChannels, MAX_CHANNELS);

    Element = muteElement;

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

_Use_decl_annotations_
NONPAGED_CODE_SEG
VOID Codec_EvtPinContextCleanup(
    WDFOBJECT /* WdfPin */
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

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS Codec_CreateRenderPin(
    AudioIsochronousEngine * AudioIsochronousEngine,
    WDFDEVICE                Device,
    ACXCIRCUIT               Circuit,
    ACXPIN &                 Pin,
    ULONG                    Id,
    ULONG                    DeviceIndex,
    ULONG                    Channel,
    ULONG                    ChannelsCount
)
{
    ACX_PIN_CONFIG        pinCfg{};
    CODEC_PIN_CONTEXT *   pinContext = nullptr;
    NTSTATUS              status = STATUS_SUCCESS;
    WDF_OBJECT_ATTRIBUTES attributes{};

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry, id %u, device index %u, channel %u, channels count %u", Id, DeviceIndex, Channel, ChannelsCount);

    ///////////////////////////////////////////////////////////
    //
    // Create Render Pin.
    //

    ACX_PIN_CONFIG_INIT(&pinCfg);
    pinCfg.Id = Id;
    pinCfg.Type = AcxPinTypeSink;
    pinCfg.Communication = AcxPinCommunicationSink;
    pinCfg.Category = &KSCATEGORY_AUDIO;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, CODEC_PIN_CONTEXT);
    attributes.EvtCleanupCallback = Codec_EvtPinContextCleanup;
    attributes.ParentObject = Circuit;

    //
    // The driver uses this DDI to create one or more pins on the circuits.
    //
    RETURN_NTSTATUS_IF_FAILED(AcxPinCreate(Circuit, &attributes, &pinCfg, &Pin));

    pinContext = GetCodecPinContext(Pin);
    ASSERT(pinContext);
    pinContext->Device = Device;
    pinContext->CodecPinType = CodecPinTypeHost;
    pinContext->DeviceIndex = DeviceIndex;
    pinContext->Channel = Channel;
    pinContext->NumOfChannelsPerDevice = ChannelsCount;
    pinContext->AudioIsochronousEngine = AudioIsochronousEngine;
    pinContext->TerminalID = USBAudioConfiguration::InvalidID;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS Codec_CreateBridgePin(
    AudioIsochronousEngine * AudioIsochronousEngine,
    WDFDEVICE                Device,
    ACXCIRCUIT               Circuit,
    ACXPIN &                 Pin,
    ULONG                    Id,
    ULONG                    DeviceIndex,
    ULONG                    Channel,
    ULONG                    ChannelsCount,
    USHORT                   TerminalType,
    UCHAR                    TerminalID
)
{
    ACX_PIN_CONFIG        pinCfg{};
    CODEC_PIN_CONTEXT *   pinContext = nullptr;
    ACX_PIN_CALLBACKS     pinCallbacks{};
    NTSTATUS              status = STATUS_SUCCESS;
    WDF_OBJECT_ATTRIBUTES attributes{};

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry, id %u, device index %u, channel %u, channels count %u", Id, DeviceIndex, Channel, ChannelsCount);

    ///////////////////////////////////////////////////////////
    //
    // Create Device Bridge Pin.
    //
    ACX_PIN_CALLBACKS_INIT(&pinCallbacks);
    if (AudioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.ChannelNames != USBAudioConfiguration::InvalidString)
    {
        pinCallbacks.EvtAcxPinRetrieveName = CodecR_EvtAcxPinRetrieveName;
    }

    ACX_PIN_CONFIG_INIT(&pinCfg);
    pinCfg.Id = Id;
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
    if (IsEqualGUID(*ConvertTerminalType(TerminalType), KSNODETYPE_SPEAKER) && (AudioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.ChannelNames != USBAudioConfiguration::InvalidString))
    {
        pinCfg.Category = &KSNODETYPE_LINE_CONNECTOR;
    }
    else
    {
        pinCfg.Category = ConvertTerminalType(TerminalType);
    }

    pinCfg.PinCallbacks = &pinCallbacks;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, CODEC_PIN_CONTEXT);
    attributes.EvtCleanupCallback = Codec_EvtPinContextCleanup;
    attributes.ParentObject = Circuit;

    //
    // The driver uses this DDI to create one or more pins on the circuits.
    //
    RETURN_NTSTATUS_IF_FAILED(AcxPinCreate(Circuit, &attributes, &pinCfg, &Pin));

    pinContext = GetCodecPinContext(Pin);
    ASSERT(pinContext);
    pinContext->Device = Device;
    pinContext->CodecPinType = CodecPinTypeDevice;
    pinContext->DeviceIndex = DeviceIndex;
    pinContext->Channel = Channel;
    pinContext->NumOfChannelsPerDevice = ChannelsCount;
    pinContext->AudioIsochronousEngine = AudioIsochronousEngine;
    pinContext->TerminalID = TerminalID;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS Codec_CreateCaptureStreamingPin(
    AudioIsochronousEngine * AudioIsochronousEngine,
    WDFDEVICE                Device,
    ACXCIRCUIT               Circuit,
    ACXPIN &                 Pin,
    ULONG                    Id,
    ULONG                    DeviceIndex,
    ULONG                    Channel,
    ULONG                    ChannelsCount
)
{
    ACX_PIN_CONFIG        pinCfg{};
    CODEC_PIN_CONTEXT *   pinContext = nullptr;
    NTSTATUS              status = STATUS_SUCCESS;
    WDF_OBJECT_ATTRIBUTES attributes{};

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry, id %u, device index %u, channel %u, channels count %u", Id, DeviceIndex, Channel, ChannelsCount);

    ///////////////////////////////////////////////////////////
    //
    // Create capture streaming pin.
    //
    ACX_PIN_CONFIG_INIT(&pinCfg);
    pinCfg.Id = Id;
    pinCfg.Type = AcxPinTypeSource;
    pinCfg.Communication = AcxPinCommunicationSink;
    pinCfg.Category = &KSCATEGORY_AUDIO;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, CODEC_PIN_CONTEXT);
    attributes.EvtCleanupCallback = Codec_EvtPinContextCleanup;
    attributes.ParentObject = Circuit;

    //
    // The driver uses this DDI to create one or more pins on the circuits.
    //
    RETURN_NTSTATUS_IF_FAILED(AcxPinCreate(Circuit, &attributes, &pinCfg, &Pin));
    ASSERT(Pin != nullptr);
    pinContext = GetCodecPinContext(Pin);
    ASSERT(pinContext);
    pinContext->Device = Device;
    pinContext->CodecPinType = CodecPinTypeHost;
    pinContext->DeviceIndex = DeviceIndex;
    pinContext->Channel = Channel;
    pinContext->NumOfChannelsPerDevice = ChannelsCount;
    pinContext->AudioIsochronousEngine = AudioIsochronousEngine;
    pinContext->TerminalID = USBAudioConfiguration::InvalidID;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS Codec_CreateCaptureEndpointPin(
    AudioIsochronousEngine * AudioIsochronousEngine,
    WDFDEVICE                Device,
    ACXCIRCUIT               Circuit,
    ACXPIN &                 Pin,
    ULONG                    Id,
    ULONG                    DeviceIndex,
    ULONG                    Channel,
    ULONG                    ChannelsCount,
    USHORT                   TerminalType,
    UCHAR                    TerminalID
)
{
    ACX_PIN_CONFIG        pinCfg{};
    CODEC_PIN_CONTEXT *   pinContext = nullptr;
    ACX_PIN_CALLBACKS     pinCallbacks{};
    NTSTATUS              status = STATUS_SUCCESS;
    WDF_OBJECT_ATTRIBUTES attributes{};

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry, id %u, device index %u, channel %u, channels count %u", Id, DeviceIndex, Channel, ChannelsCount);

    ///////////////////////////////////////////////////////////
    //
    // Create capture endpoint pin.
    //
    ACX_PIN_CALLBACKS_INIT(&pinCallbacks);
    if (AudioIsochronousEngine->GetAudioStreamPropertySet().InputProperty.ChannelNames != USBAudioConfiguration::InvalidString)
    {
        pinCallbacks.EvtAcxPinRetrieveName = CodecC_EvtAcxPinRetrieveName;
    }

    ACX_PIN_CONFIG_INIT(&pinCfg);
    pinCfg.Type = AcxPinTypeSink;
    pinCfg.Id = Id;
    pinCfg.Communication = AcxPinCommunicationNone;
    pinCfg.Category = ConvertTerminalType(TerminalType);
    pinCfg.PinCallbacks = &pinCallbacks;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, CODEC_PIN_CONTEXT);
    attributes.EvtCleanupCallback = Codec_EvtPinContextCleanup;
    attributes.ParentObject = Circuit;

    //
    // The driver uses this DDI to create one or more pins on the circuits.
    //
    RETURN_NTSTATUS_IF_FAILED(AcxPinCreate(Circuit, &attributes, &pinCfg, &Pin));
    ASSERT(Pin != nullptr);
    pinContext = GetCodecPinContext(Pin);
    ASSERT(pinContext);
    pinContext->Device = Device;
    pinContext->CodecPinType = CodecPinTypeDevice;
    pinContext->DeviceIndex = DeviceIndex;
    pinContext->Channel = Channel;
    pinContext->NumOfChannelsPerDevice = ChannelsCount;
    pinContext->AudioIsochronousEngine = AudioIsochronousEngine;
    pinContext->TerminalID = TerminalID;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS Codec_CreateRenderHostPin(
    AudioIsochronousEngine * AudioIsochronousEngine,
    WDFDEVICE                Device,
    ACXCIRCUIT               Circuit,
    ULONG                    PinID,
    ACXELEMENT &             Element,
    UCHAR                    UnitID
)
{
    NTSTATUS              status = STATUS_SUCCESS;
    ACXPIN                pin = nullptr;
    ACX_PIN_CONFIG        pinCfg{};
    CODEC_PIN_CONTEXT *   pinContext = nullptr;
    ELEMENT_CONTEXT *     elementContext = nullptr;
    WDF_OBJECT_ATTRIBUTES attributes{};

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry, unit id 0x%02x", UnitID);

    ///////////////////////////////////////////////////////////
    //
    // Create Render Pin.
    //

    ACX_PIN_CONFIG_INIT(&pinCfg);
    pinCfg.Id = PinID;
    pinCfg.Type = AcxPinTypeSink;
    pinCfg.Communication = AcxPinCommunicationSink;
    pinCfg.Category = &KSCATEGORY_AUDIO;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, CODEC_PIN_CONTEXT);
    attributes.EvtCleanupCallback = Codec_EvtPinContextCleanup;
    attributes.ParentObject = Circuit;

    //
    // The driver uses this DDI to create one or more pins on the circuits.
    //
    RETURN_NTSTATUS_IF_FAILED(AcxPinCreate(Circuit, &attributes, &pinCfg, &pin));
    ASSERT(pin != nullptr);
    Element = (ACXELEMENT)pin;

    pinContext = GetCodecPinContext(pin);
    ASSERT(pinContext);
    pinContext->IsInput = false;
    pinContext->Device = Device;
    pinContext->CodecPinType = CodecPinTypeHost;
    pinContext->DeviceIndex = 0;            // DeviceIndex;
    pinContext->Channel = 0;                // Channel;
    pinContext->NumOfChannelsPerDevice = 0; // ChannelsCount;
    pinContext->AudioIsochronousEngine = AudioIsochronousEngine;
    pinContext->TerminalID = USBAudioConfiguration::InvalidID;

    PVOID context = nullptr;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, ELEMENT_CONTEXT);
    RETURN_NTSTATUS_IF_FAILED(WdfObjectAllocateContext(Element, &attributes, &context));
    ASSERT(context);
    elementContext = (ELEMENT_CONTEXT *)context;
    elementContext->AudioNodeKind = toULONG(AudioNodeKind::RenderHostPin);
    elementContext->UnitID = UnitID;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS Codec_CreateRenderBridgePin(
    AudioIsochronousEngine * AudioIsochronousEngine,
    WDFDEVICE                Device,
    ACXCIRCUIT               Circuit,
    ULONG                    PinID,
    ACXELEMENT &             Element,
    UCHAR                    UnitID
)
{
    NTSTATUS                                      status = STATUS_SUCCESS;
    ACXPIN                                        pin = nullptr;
    ACX_PIN_CONFIG                                pinCfg{};
    ACX_PIN_CALLBACKS                             pinCallbacks{};
    CODEC_PIN_CONTEXT *                           pinContext = nullptr;
    ELEMENT_CONTEXT *                             elementContext = nullptr;
    WDF_OBJECT_ATTRIBUTES                         attributes{};
    USHORT                                        terminalType = 0;
    UCHAR                                         numOfChannels = 0;
    UCHAR                                         channelNames = 0;
    NS_USBAudio::AUDIO_CHANNEL_CLUSTER_DESCRIPTOR connectorState{};

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry, unit id 0x%02x", UnitID);

    RETURN_NTSTATUS_IF_FAILED(AudioIsochronousEngine->GetInformationForBridgePin(UnitID, numOfChannels, terminalType, channelNames, connectorState));

    ///////////////////////////////////////////////////////////
    //
    // Create Device Bridge Pin.
    //
    ACX_PIN_CALLBACKS_INIT(&pinCallbacks);
    if (channelNames != USBAudioConfiguration::InvalidString)
    {
        pinCallbacks.EvtAcxPinRetrieveName = Codec_EvtAcxPinRetrieveName;
    }

    ACX_PIN_CONFIG_INIT(&pinCfg);
    pinCfg.Id = PinID;
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
    if (IsEqualGUID(*ConvertTerminalType(terminalType), KSNODETYPE_SPEAKER) && (channelNames != USBAudioConfiguration::InvalidString))
    {
        pinCfg.Category = &KSNODETYPE_LINE_CONNECTOR;
    }
    else
    {
        pinCfg.Category = ConvertTerminalType(terminalType);
    }
    pinCfg.PinCallbacks = &pinCallbacks;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, CODEC_PIN_CONTEXT);
    attributes.EvtCleanupCallback = Codec_EvtPinContextCleanup;
    attributes.ParentObject = Circuit;

    //
    // The driver uses this DDI to create one or more pins on the circuits.
    //
    RETURN_NTSTATUS_IF_FAILED(AcxPinCreate(Circuit, &attributes, &pinCfg, &pin));
    ASSERT(pinContext);
    Element = (ACXELEMENT)pin;

    pinContext = GetCodecPinContext(pin);
    pinContext->IsInput = true;
    pinContext->Device = Device;
    pinContext->CodecPinType = CodecPinTypeDevice;
    pinContext->DeviceIndex = 0;            // DeviceIndex;
    pinContext->Channel = 0;                // Channel;
    pinContext->NumOfChannelsPerDevice = 0; // ChannelsCount;
    pinContext->AudioIsochronousEngine = AudioIsochronousEngine;
    pinContext->TerminalID = 0;             //  TerminalID;

    PVOID context = nullptr;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, ELEMENT_CONTEXT);
    RETURN_NTSTATUS_IF_FAILED(WdfObjectAllocateContext(Element, &attributes, &context));
    ASSERT(context);
    elementContext = (ELEMENT_CONTEXT *)context;
    elementContext->AudioNodeKind = toULONG(AudioNodeKind::RenderBridgePin);
    elementContext->UnitID = UnitID;

    RETURN_NTSTATUS_IF_FAILED(AddAudioJackToBridgePin(pin, ConverSpeakerPositions(connectorState.bmChannelConfig)));

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS Codec_CreateCaptureHostPin(
    AudioIsochronousEngine * AudioIsochronousEngine,
    WDFDEVICE                Device,
    ACXCIRCUIT               Circuit,
    ULONG                    PinID,
    ACXELEMENT &             Element,
    UCHAR                    UnitID
)
{
    NTSTATUS              status = STATUS_SUCCESS;
    ACXPIN                pin = nullptr;
    ACX_PIN_CONFIG        pinCfg{};
    CODEC_PIN_CONTEXT *   pinContext = nullptr;
    ELEMENT_CONTEXT *     elementContext = nullptr;
    WDF_OBJECT_ATTRIBUTES attributes{};

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry, unit id 0x%02x", UnitID);

    ///////////////////////////////////////////////////////////
    //
    // Create capture streaming pin.
    //
    ACX_PIN_CONFIG_INIT(&pinCfg);
    pinCfg.Id = PinID;
    pinCfg.Type = AcxPinTypeSource;
    pinCfg.Communication = AcxPinCommunicationSink;
    pinCfg.Category = &KSCATEGORY_AUDIO;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, CODEC_PIN_CONTEXT);
    attributes.EvtCleanupCallback = Codec_EvtPinContextCleanup;
    attributes.ParentObject = Circuit;

    //
    // The driver uses this DDI to create one or more pins on the circuits.
    //
    RETURN_NTSTATUS_IF_FAILED(AcxPinCreate(Circuit, &attributes, &pinCfg, &pin));
    ASSERT(pin != nullptr);
    Element = (ACXELEMENT)pin;

    pinContext = GetCodecPinContext(pin);
    ASSERT(pinContext);
    pinContext->Device = Device;
    pinContext->CodecPinType = CodecPinTypeHost;
    pinContext->DeviceIndex = 0;            // DeviceIndex;
    pinContext->Channel = 0;                // Channel;
    pinContext->NumOfChannelsPerDevice = 0; // ChannelsCount;
    pinContext->AudioIsochronousEngine = AudioIsochronousEngine;
    pinContext->TerminalID = USBAudioConfiguration::InvalidID;

    PVOID context = nullptr;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, ELEMENT_CONTEXT);
    RETURN_NTSTATUS_IF_FAILED(WdfObjectAllocateContext(Element, &attributes, &context));
    ASSERT(context);
    elementContext = (ELEMENT_CONTEXT *)context;
    elementContext->AudioNodeKind = toULONG(AudioNodeKind::CaptureHostPin);
    elementContext->UnitID = UnitID;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS Codec_CreateCaptureBridgePin(
    AudioIsochronousEngine * AudioIsochronousEngine,
    WDFDEVICE                Device,
    ACXCIRCUIT               Circuit,
    ULONG                    PinID,
    ACXELEMENT &             Element,
    UCHAR                    UnitID
)
{
    NTSTATUS                                      status = STATUS_SUCCESS;
    ACXPIN                                        pin = nullptr;
    ACX_PIN_CONFIG                                pinCfg{};
    ACX_PIN_CALLBACKS                             pinCallbacks{};
    CODEC_PIN_CONTEXT *                           pinContext = nullptr;
    ELEMENT_CONTEXT *                             elementContext = nullptr;
    WDF_OBJECT_ATTRIBUTES                         attributes{};
    USHORT                                        terminalType = 0;
    UCHAR                                         numOfChannels = 0;
    UCHAR                                         channelNames = 0;
    NS_USBAudio::AUDIO_CHANNEL_CLUSTER_DESCRIPTOR connectorState{};

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry, unit id 0x%02x", UnitID);

    RETURN_NTSTATUS_IF_FAILED(AudioIsochronousEngine->GetInformationForBridgePin(UnitID, numOfChannels, terminalType, channelNames, connectorState));

    ///////////////////////////////////////////////////////////
    //
    // Create capture endpoint pin.
    //
    ACX_PIN_CALLBACKS_INIT(&pinCallbacks);
    if (channelNames != USBAudioConfiguration::InvalidString)
    {
        pinCallbacks.EvtAcxPinRetrieveName = Codec_EvtAcxPinRetrieveName;
    }

    ACX_PIN_CONFIG_INIT(&pinCfg);
    pinCfg.Type = AcxPinTypeSink;
    pinCfg.Id = PinID;
    pinCfg.Communication = AcxPinCommunicationNone;
    pinCfg.Category = ConvertTerminalType(terminalType);
    pinCfg.PinCallbacks = &pinCallbacks;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, CODEC_PIN_CONTEXT);
    attributes.EvtCleanupCallback = Codec_EvtPinContextCleanup;
    attributes.ParentObject = Circuit;

    //
    // The driver uses this DDI to create one or more pins on the circuits.
    //
    RETURN_NTSTATUS_IF_FAILED(AcxPinCreate(Circuit, &attributes, &pinCfg, &pin));
    ASSERT(pin != nullptr);
    Element = (ACXELEMENT)pin;

    pinContext = GetCodecPinContext(pin);
    ASSERT(pinContext);
    pinContext->Device = Device;
    pinContext->CodecPinType = CodecPinTypeDevice;
    pinContext->DeviceIndex = 0; // DeviceIndex;
    pinContext->Channel = 0;     // Channel;
    pinContext->NumOfChannelsPerDevice = numOfChannels;
    pinContext->AudioIsochronousEngine = AudioIsochronousEngine;
    pinContext->TerminalID = UnitID;

    PVOID context = nullptr;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, ELEMENT_CONTEXT);
    RETURN_NTSTATUS_IF_FAILED(WdfObjectAllocateContext(Element, &attributes, &context));
    ASSERT(context);
    elementContext = (ELEMENT_CONTEXT *)context;
    elementContext->AudioNodeKind = toULONG(AudioNodeKind::CaptureBridgePin);
    elementContext->UnitID = UnitID;

    RETURN_NTSTATUS_IF_FAILED(AddAudioJackToBridgePin(pin, ConverSpeakerPositions(connectorState.bmChannelConfig)));

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
Codec_AllocateElements(
    WDFDEVICE                Device,
    ACXCIRCUIT               Circuit,
    bool                     IsInput,
    AudioIsochronousEngine * AudioIsochronousEngine
)
{
    NTSTATUS              status = STATUS_SUCCESS;
    WDF_OBJECT_ATTRIBUTES attributes{};
    WDFMEMORY             elementsMemory = nullptr;
    ACXELEMENT *          elements = nullptr;
    const ULONG           sizeOfElements = 0x100 * 3; // unitID max + 1 * (volume + mute + agc)
    ULONG                 elementIndex = 0;
    ULONG                 numOfElement = 0;
    bool                  hasMoreData = true;
    TraversalDirection    traversalDirection = TraversalDirection::Forward;
    AudioNodeKind         audioNodeKind = AudioNodeKind::Invalid;
    UCHAR                 unitID = USBAudioConfiguration::InvalidID;
    UCHAR                 nextUnitID = USBAudioConfiguration::InvalidID;
    ULONG                 controlBitmap = 0;
    ULONGLONG             unvisitedUnitMap[4] = {};
    ULONGLONG             idMap[4] = {};
    ULONG                 counter = 0;
    ACXELEMENT            prevElement{};
    ULONG                 pinID = 0;

    PAGED_CODE();

    auto allocateElementsScope = wil::scope_exit([&]() {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, " IsInput = %!bool!, numOfElement = %u", IsInput, numOfElement);
        if (elementsMemory != nullptr)
        {
            WdfObjectDelete(elementsMemory);
            elementsMemory = nullptr;
            elements = nullptr;
        }
    });

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");

    if (IsInput)
    {
        traversalDirection = TraversalDirection::Reverse;
    }
    else
    {
        traversalDirection = TraversalDirection::Forward;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = Device;
    RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(ACXELEMENT) * sizeOfElements, &elementsMemory, nullptr));
    elements = (ACXELEMENT *)WdfMemoryGetBuffer(elementsMemory, nullptr);
    RtlZeroMemory(elements, sizeof(ACXELEMENT) * sizeOfElements);

    if ((IsInput && AudioIsochronousEngine->HasInputIsochronousInterface()) || (!IsInput && AudioIsochronousEngine->HasOutputIsochronousInterface()))
    {
        while (hasMoreData)
        {
            RETURN_NTSTATUS_IF_FAILED(AudioIsochronousEngine->WalkNextUnit(IsInput, idMap, unvisitedUnitMap, audioNodeKind, unitID, controlBitmap, nextUnitID, traversalDirection, hasMoreData));
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - IsInput = %!bool!, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, %s, 0x%02x, 0x%08x, 0x%02x, %s, hasMoreData = %!bool!", IsInput, idMap[0], idMap[1], idMap[2], idMap[3], unvisitedUnitMap[0], unvisitedUnitMap[1], unvisitedUnitMap[2], unvisitedUnitMap[3], GetAudioNodeKindString(audioNodeKind), unitID, controlBitmap, nextUnitID, GetTraversalDirectionString(traversalDirection), hasMoreData);

            prevElement = elements[elementIndex];

            switch (audioNodeKind)
            {
            case AudioNodeKind::RenderHostPin:
                RETURN_NTSTATUS_IF_FAILED(Codec_CreateRenderHostPin(AudioIsochronousEngine, Device, Circuit, pinID, elements[elementIndex], unitID));
                pinID++;
                elementIndex++;
                break;
            case AudioNodeKind::RenderBridgePin:
                RETURN_NTSTATUS_IF_FAILED(Codec_CreateRenderBridgePin(AudioIsochronousEngine, Device, Circuit, pinID, elements[elementIndex], unitID));
                pinID++;
                elementIndex++;
                break;
            case AudioNodeKind::CaptureHostPin:
                RETURN_NTSTATUS_IF_FAILED(Codec_CreateCaptureHostPin(AudioIsochronousEngine, Device, Circuit, pinID, elements[elementIndex], unitID));
                pinID++;
                elementIndex++;
                break;
            case AudioNodeKind::CaptureBridgePin:
                RETURN_NTSTATUS_IF_FAILED(Codec_CreateCaptureBridgePin(AudioIsochronousEngine, Device, Circuit, pinID, elements[elementIndex], unitID));
                pinID++;
                elementIndex++;
                break;
            case AudioNodeKind::VolumeElement: // Feature Unit (FU_VOLUME_CONTROL) : KSNODETYPE_VOLUME
                RETURN_NTSTATUS_IF_FAILED(Codec_CreateVolumeElement(AudioIsochronousEngine, Device, Circuit, elements[elementIndex], unitID));
                elementIndex++;
                break;
            case AudioNodeKind::MuteElement: // Feature Unit (FU_MUTE_CONTROL) : KSNODETYPE_MUTE
                RETURN_NTSTATUS_IF_FAILED(Codec_CreateMuteElement(AudioIsochronousEngine, Device, Circuit, elements[elementIndex], unitID));
                elementIndex++;
                break;
            case AudioNodeKind::AgcElement: // Feature Unit (FU_AUTOMATIC_GAIN_CONTROL) : KSNODETYPE_AGC
                // RETURN_NTSTATUS_IF_FAILED(AudioIsochronousEngine->GetInformationForAutomaticGainElement(unitID));
                // createAgcElement(unitID);
                break;
            case AudioNodeKind::SuperMixElement: // Mixer Unit : KSNODETYPE_SUPERMIX
                // RETURN_NTSTATUS_IF_FAILED(AudioIsochronousEngine->GetInformationForSuperMix(unitID));
                // createSuperMix(unitID);
                break;
            case AudioNodeKind::MuxElement: // Selector Unit : KSNODETYPE_MUX
                // RETURN_NTSTATUS_IF_FAILED(AudioIsochronousEngine->GetInformationForSuperMux(unitID));
                // createMuxElement(unitID);
                break;
            case AudioNodeKind::SrcElement: // Sampling Rate Converter Unit : KSNODETYPE_SRC
                // RETURN_NTSTATUS_IF_FAILED(AudioIsochronousEngine->GetInformationForSRC(unitID));
                // createSRCElement(unitID);
                break;
            case AudioNodeKind::EffectElement: // Effect Unit : KSNODETYPE_3D_EFFECTS
                // RETURN_NTSTATUS_IF_FAILED(AudioIsochronousEngine->GetInformationForEffect(unitID));
                // createEffectElement(unitID);
                break;
            case AudioNodeKind::ProcessingElement: // Processing Unit : KSNODETYPE_MICROPHONE_ARRAY_PROCESSOR ?
                // RETURN_NTSTATUS_IF_FAILED(AudioIsochronousEngine->GetInformationForProcessing(unitID));
                // createProcessingElement(unitID);
                break;
            default:
                break;
            }
            unitID = nextUnitID;
#if 0
            if (traversalDirection == TraversalDirection::Forward)
            {
                connect(prevElement, element);
            }
            else
            {
                connect(element, prevElement);
            }
#endif
            counter++;
            if (counter > 0xff)
            {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_INTERRUPTTRANSFER, "%!FUNC! counter overflow.");
                break;
            }
        }
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_INTERRUPTTRANSFER, "%!FUNC! do nothing");
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit %!STATUS!", status);

    return status;
}
