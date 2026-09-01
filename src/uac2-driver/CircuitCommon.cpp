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

enum class PropertyItemIndex
{
    MuxProperty = 0,
    AgcProperty = 1,
    MixLevelCapsProperty = 2
};

constexpr ULONG toULONG(PropertyItemIndex index)
{
    return static_cast<ULONG>(index);
}

static ACX_PROPERTY_ITEM s_PropertyItems[] = {
    {
        &KSPROPSETID_Audio,                                                                            // const GUID * Set;
        KSPROPERTY_AUDIO_MUX_SOURCE,
        ACX_PROPERTY_ITEM_FLAG_GET | ACX_PROPERTY_ITEM_FLAG_SET | ACX_PROPERTY_ITEM_FLAG_BASICSUPPORT, // ULONG Flags;
        Codec_EvtUSBAudioAcxDriverMuxProcessRequest,                                                   // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                                                                             // PVOID Reserved;
        0,                                                                                             // ULONG ControlCb;
        sizeof(ULONG),                                                                                 // ULONG ValueCb;
        VT_UI4                                                                                         // ULONG ValueType;
    },

    {
        &KSPROPSETID_Audio,                                                                            // const GUID * Set;
        KSPROPERTY_AUDIO_AGC,
        ACX_PROPERTY_ITEM_FLAG_GET | ACX_PROPERTY_ITEM_FLAG_SET | ACX_PROPERTY_ITEM_FLAG_BASICSUPPORT, // ULONG Flags;
        Codec_EvtUSBAudioAcxDriverAgcProcessRequest,                                                   // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                                                                             // PVOID Reserved;
        sizeof(LONG) + sizeof(ULONG),                                                                  // ULONG ControlCb; KSNODEPROPERTY_AUDIO_CHANNEL::Channel, KSNODEPROPERTY_AUDIO_CHANNEL::Reserved
        sizeof(BOOL),                                                                                  // ULONG ValueCb;
        VT_BOOL                                                                                        // ULONG ValueType;
    },

    {
        &KSPROPSETID_Audio,                                               // const GUID * Set;
        KSPROPERTY_AUDIO_MIX_LEVEL_CAPS,
        ACX_PROPERTY_ITEM_FLAG_GET | ACX_PROPERTY_ITEM_FLAG_BASICSUPPORT, // ULONG Flags;
        Codec_EvtUSBAudioAcxDriverMixLevelCapsProcessRequest,             // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                                                // PVOID Reserved;
        0,                                                                // ULONG ControlCb;
        0,                                                                // ULONG ValueCb;
        VT_EMPTY                                                          // ULONG ValueType; (variable length)
    },
};

static ACX_PROPERTY_ITEM s_PinPropertyItems[] = {
    {
        &KSPROPSETID_Audio,                                               // const GUID * Set;
        KSPROPERTY_JACK_DESCRIPTION,
        ACX_PROPERTY_ITEM_FLAG_GET | ACX_PROPERTY_ITEM_FLAG_BASICSUPPORT, // ULONG Flags;
        Codec_EvtUSBAudioAcxDriverJackDescriptionProcessRequest,          // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                                                // PVOID Reserved;
        0,                                                                // ULONG ControlCb;
        0,                                                                // ULONG ValueCb;
        VT_EMPTY                                                          // ULONG ValueType; (variable length)
    },
};

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS Codec_AddAudioJackToBridgePin(
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
    jackCfg.Description.ConnectionType = AcxConnTypeRCA;
    jackCfg.Description.GeoLocation = AcxGeoLocFront;
    jackCfg.Description.GenLocation = AcxGenLocPrimaryBox;
    jackCfg.Description.PortConnection = AcxPortConnJack;
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

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS Codec_AddAudioDummyJackToBridgePin(
    _In_ ACXPIN Pin
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
    jackCfg.Description.ChannelMapping = 0;
    jackCfg.Description.Color = RGB(0, 0, 0);
    jackCfg.Description.ConnectionType = AcxConnTypeUnknown;
    jackCfg.Description.GeoLocation = AcxGeoLocNotApplicable;
    jackCfg.Description.GenLocation = AcxGenLocOther;
    jackCfg.Description.PortConnection = AcxPortConnUnknown;
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
_Success_(NT_SUCCESS(return))
static NTSTATUS AllocateElementContext(
    _In_ ACXELEMENT    Element,
    _In_ AudioNodeKind AudioNodeKind,
    _In_ ULONG         UnitID,
    _In_ ULONG         PinID
)
{
    WDF_OBJECT_ATTRIBUTES attributes{};
    PVOID                 context = nullptr;

    PAGED_CODE();

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, ELEMENT_CONTEXT);
    RETURN_NTSTATUS_IF_FAILED(WdfObjectAllocateContext(Element, &attributes, &context));
    ASSERT(context);
    PELEMENT_CONTEXT elementContext = (ELEMENT_CONTEXT *)context;
    elementContext->AudioNodeKind = toULONG(AudioNodeKind);
    elementContext->UnitID = (UCHAR)UnitID;
    elementContext->PinID = PinID;

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
    UNICODE_STRING retrievedName{};

    PAGED_CODE();

    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");

    CODEC_PIN_CONTEXT * pinContext = GetCodecPinContext(Pin);
    ASSERT(pinContext != nullptr);
    ASSERT(pinContext->AudioIsochronousEngine != nullptr);

    RETURN_NTSTATUS_IF_FAILED(pinContext->AudioIsochronousEngine->GetChannelName(pinContext->ChannelNames, pinContext->Channel, memory, channelName));
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
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    bool     muteState = (State != 0) ? true : false;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Entry, channel %u, mute %!bool!", Channel, muteState);
    PMUTE_ELEMENT_CONTEXT muteContext = GetMuteElementContext(Mute);
    ASSERT(muteContext);

    PDEVICE_CONTEXT deviceContext = GetDeviceContext(muteContext->Device);
    ASSERT(deviceContext != nullptr);

    if ((Channel != ALL_CHANNELS_ID) && (Channel < muteContext->NumberOfChannels))
    {
        status = deviceContext->UsbAudioConfiguration->SetCurrentMute(deviceContext, muteContext->EntityID, (UCHAR)(Channel + 1), muteState);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - set current mute %!bool!, entity ID 0x%02x, channel(USB) %d, %!STATUS!", (muteState != 0) ? true : false, muteContext->EntityID, Channel + 1, status);
        if (NT_SUCCESS(status))
        {
            muteContext->MuteStates[Channel + 1] = muteState;
        }
        else if (status == STATUS_NOT_SUPPORTED)
        {
            status = deviceContext->UsbAudioConfiguration->SetCurrentMute(deviceContext, muteContext->EntityID, 0, muteState);
            if (NT_SUCCESS(status))
            {
                muteContext->MuteStates[0] = muteState;
            }
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - set current mute %!bool!, entity ID 0x%02x, channel(USB) %d, %!STATUS!", (muteState != 0) ? true : false, muteContext->EntityID, 0, status);
        }
    }
    else if (Channel == ALL_CHANNELS_ID)
    {
        status = deviceContext->UsbAudioConfiguration->SetCurrentMute(deviceContext, muteContext->EntityID, 0, muteState);
        if (NT_SUCCESS(status))
        {
            muteContext->MuteStates[0] = muteState;
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - set current mute %!bool!, entity ID 0x%02x, channel(USB) %d, %!STATUS!", (muteState != 0) ? true : false, muteContext->EntityID, 0, status);
        }
        else if (status == STATUS_NOT_SUPPORTED)
        {
            for (ULONG i = 0; i < muteContext->NumberOfChannels; ++i)
            {
                status = deviceContext->UsbAudioConfiguration->SetCurrentMute(deviceContext, muteContext->EntityID, (UCHAR)(i + 1), muteState);
                if (NT_SUCCESS(status))
                {
                    muteContext->MuteStates[i + 1] = muteState;
                }
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - set current mute %!bool!, entity ID 0x%02x, channel(USB) %d, %!STATUS!", (muteState != 0) ? true : false, muteContext->EntityID, i + 1, status);
            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Exit, %!STATUS!", status);

    return status;
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
    NTSTATUS status = STATUS_SUCCESS;
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Entry!");

    bool                  mute = true;
    PMUTE_ELEMENT_CONTEXT muteContext = GetMuteElementContext(Mute);
    ASSERT(muteContext);

    PDEVICE_CONTEXT deviceContext = GetDeviceContext(muteContext->Device);
    ASSERT(deviceContext != nullptr);

    *State = 0;

    //
    // Use first channel for all channels setting.
    //
    if ((Channel != ALL_CHANNELS_ID) && (Channel < muteContext->NumberOfChannels))
    {
        status = deviceContext->UsbAudioConfiguration->ValidateMuteControl(muteContext->EntityID, (UCHAR)(Channel + 1));
        if (NT_SUCCESS(status))
        {
            *State = muteContext->MuteStates[Channel + 1];
        }
        else if (status == STATUS_NOT_SUPPORTED)
        {
            status = deviceContext->UsbAudioConfiguration->ValidateMuteControl(muteContext->EntityID, 0);
            if (NT_SUCCESS(status))
            {
                *State = muteContext->MuteStates[0];
            }
        }
    }
    else if (Channel == ALL_CHANNELS_ID)
    {
        status = deviceContext->UsbAudioConfiguration->ValidateMuteControl(muteContext->EntityID, 0);
        if (NT_SUCCESS(status))
        {
            *State = muteContext->MuteStates[0];
        }
        else if (status == STATUS_NOT_SUPPORTED)
        {
            for (ULONG i = 0; i < muteContext->NumberOfChannels; ++i)
            {
                status = deviceContext->UsbAudioConfiguration->ValidateMuteControl(muteContext->EntityID, (UCHAR)(i + 1));
                if (NT_SUCCESS(status))
                {
                    *State = muteContext->MuteStates[i + 1];
                    break;
                }
            }
        }
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - get current mute %!bool!, entity ID 0x%02x, channel %d, %!STATUS!", (mute != 0) ? true : false, muteContext->EntityID, Channel, status);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Exit, %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
VOID Codec_EvtMuteElementContextCleanup(
    WDFOBJECT Object
)
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");

    ACXMUTE muteElement = (ACXMUTE)Object;

    PMUTE_ELEMENT_CONTEXT muteContext = GetMuteElementContext(muteElement);
    ASSERT(muteContext);

    if ((muteContext != nullptr) && (muteContext->MuteStatesMemory != nullptr))
    {
        WdfObjectDelete(muteContext->MuteStatesMemory);
        muteContext->MuteStatesMemory = nullptr;
        muteContext->MuteStates = nullptr;
        muteContext->NumberOfChannels = 0;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");
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
    NTSTATUS status = STATUS_INVALID_PARAMETER;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Entry, channel %u, volume level %ld", Channel, VolumeLevel);

    PVOLUME_ELEMENT_CONTEXT volumeContext = GetVolumeElementContext(Volume);
    ASSERT(volumeContext);

    PDEVICE_CONTEXT deviceContext = GetDeviceContext(volumeContext->Device);
    ASSERT(deviceContext != nullptr);

    if ((Channel != ALL_CHANNELS_ID) && (Channel < volumeContext->NumberOfChannels))
    {
        status = deviceContext->UsbAudioConfiguration->SetCurrentVolume(deviceContext, volumeContext->EntityID, (UCHAR)(Channel + 1), VolumeLevel);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - set current volume %ld, entity ID 0x%02x, channel(USB) %d, %!STATUS!", VolumeLevel, volumeContext->EntityID, Channel + 1, status);
        if (NT_SUCCESS(status))
        {
            volumeContext->VolumeLevels[Channel + 1] = VolumeLevel;
        }
        else if (status == STATUS_NOT_SUPPORTED)
        {
            status = deviceContext->UsbAudioConfiguration->SetCurrentVolume(deviceContext, volumeContext->EntityID, 0, VolumeLevel);
            if (NT_SUCCESS(status))
            {
                volumeContext->VolumeLevels[0] = VolumeLevel;
            }
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - set current volume %ld, entity ID 0x%02x, channel(USB) %d, %!STATUS!", VolumeLevel, volumeContext->EntityID, 0, status);
        }
    }
    else if (Channel == ALL_CHANNELS_ID)
    {
        status = deviceContext->UsbAudioConfiguration->SetCurrentVolume(deviceContext, volumeContext->EntityID, 0, VolumeLevel);
        if (NT_SUCCESS(status))
        {
            volumeContext->VolumeLevels[0] = VolumeLevel;
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - set current volume %ld, entity ID 0x%02x, channel(USB) %d, %!STATUS!", VolumeLevel, volumeContext->EntityID, 0, status);
        }
        else if (status == STATUS_NOT_SUPPORTED)
        {
            for (ULONG i = 0; i < volumeContext->NumberOfChannels; ++i)
            {
                status = deviceContext->UsbAudioConfiguration->SetCurrentVolume(deviceContext, volumeContext->EntityID, (UCHAR)(i + 1), VolumeLevel);
                if (NT_SUCCESS(status))
                {
                    volumeContext->VolumeLevels[i + 1] = VolumeLevel;
                }
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - set current volume %ld, entity ID 0x%02x, channel(USB) %d, %!STATUS!", VolumeLevel, volumeContext->EntityID, i + 1, status);
            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Exit, %!STATUS!", status);

    return status;
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
    NTSTATUS status = STATUS_SUCCESS;
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Entry, channel %u", Channel);

    PVOLUME_ELEMENT_CONTEXT volumeContext = GetVolumeElementContext(Volume);
    ASSERT(volumeContext);

    PDEVICE_CONTEXT deviceContext = GetDeviceContext(volumeContext->Device);
    ASSERT(deviceContext != nullptr);

    *VolumeLevel = 0;

    if ((Channel != ALL_CHANNELS_ID) && (Channel < volumeContext->NumberOfChannels))
    {
        status = deviceContext->UsbAudioConfiguration->ValidateVolumeControl(volumeContext->EntityID, (UCHAR)(Channel + 1));
        if (NT_SUCCESS(status))
        {
            *VolumeLevel = volumeContext->VolumeLevels[Channel + 1];
        }
        else if (status == STATUS_NOT_SUPPORTED)
        {
            status = deviceContext->UsbAudioConfiguration->ValidateVolumeControl(volumeContext->EntityID, 0);
            if (NT_SUCCESS(status))
            {
                *VolumeLevel = volumeContext->VolumeLevels[0];
            }
        }
    }
    else if (Channel == ALL_CHANNELS_ID)
    {
        status = deviceContext->UsbAudioConfiguration->ValidateVolumeControl(volumeContext->EntityID, 0);
        if (NT_SUCCESS(status))
        {
            *VolumeLevel = volumeContext->VolumeLevels[0];
        }
        else if (status == STATUS_NOT_SUPPORTED)
        {
            for (ULONG i = 0; i < volumeContext->NumberOfChannels; ++i)
            {
                status = deviceContext->UsbAudioConfiguration->ValidateVolumeControl(volumeContext->EntityID, (UCHAR)(i + 1));
                if (NT_SUCCESS(status))
                {
                    *VolumeLevel = volumeContext->VolumeLevels[i + 1];
                    break;
                }
            }
        }
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - get current volume %ld, entity ID 0x%02x, channel %d, %!STATUS!", *VolumeLevel, volumeContext->EntityID, Channel, status);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Exit, %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
VOID Codec_EvtVolumeElementContextCleanup(
    WDFOBJECT Object
)
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");

    ACXVOLUME volumeElement = (ACXVOLUME)Object;

    PVOLUME_ELEMENT_CONTEXT volumeContext = GetVolumeElementContext(volumeElement);
    ASSERT(volumeContext);

    if ((volumeContext != nullptr) && (volumeContext->VolumeLevelsMemory != nullptr))
    {
        WdfObjectDelete(volumeContext->VolumeLevelsMemory);
        volumeContext->VolumeLevelsMemory = nullptr;
        volumeContext->VolumeLevels = nullptr;
        volumeContext->NumberOfChannels = 0;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
Codec_CreateVolumeElement(
    AudioIsochronousEngine * AudioIsochronousEngine,
    WDFDEVICE                Device,
    ACXCIRCUIT               Circuit,
    ACXELEMENT &             Element,
    UCHAR                    UnitID,
    UCHAR                    NumOfChannelsPerDevice
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

    if (NumOfChannelsPerDevice != 0)
    {
        numOfChannels = NumOfChannelsPerDevice;
    }

    volumeCfg.ChannelsCount = numOfChannels;
    volumeCfg.Name = &KSAUDFNAME_VOLUME_CONTROL;
    volumeCfg.Callbacks = &volumeCallbacks;
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - volume minimum %ld (0x%lx), maximum %ld (0x%lx), stepping delta %ld (0x%lx)", volumeCfg.Minimum, volumeCfg.Minimum, volumeCfg.Maximum, volumeCfg.Maximum, volumeCfg.SteppingDelta, volumeCfg.SteppingDelta);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, VOLUME_ELEMENT_CONTEXT);
    attributes.ParentObject = Circuit;
    attributes.EvtCleanupCallback = Codec_EvtVolumeElementContextCleanup;

    RETURN_NTSTATUS_IF_FAILED(AcxVolumeCreate(Circuit, &attributes, &volumeCfg, (ACXVOLUME *)&volumeElement));

    PVOLUME_ELEMENT_CONTEXT volumeContext = GetVolumeElementContext(volumeElement);
    ASSERT(volumeContext);

    RtlZeroMemory(volumeContext, sizeof(VOLUME_ELEMENT_CONTEXT));
    volumeContext->Device = Device;
    volumeContext->EntityID = UnitID;
    volumeContext->VolumeLevelsMemory = nullptr;
    volumeContext->VolumeLevels = nullptr;

    attributes.ParentObject = volumeElement;
    RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(LONG) * (numOfChannels + 1), &volumeContext->VolumeLevelsMemory, nullptr));
    volumeContext->VolumeLevels = (LONG *)WdfMemoryGetBuffer(volumeContext->VolumeLevelsMemory, nullptr);
    RtlZeroMemory(volumeContext->VolumeLevels, sizeof(LONG) * (numOfChannels + 1));
    volumeContext->NumberOfChannels = numOfChannels;

    PDEVICE_CONTEXT deviceContext = GetDeviceContext(Device);
    ASSERT(deviceContext);

    for (ULONG i = 0; i < volumeContext->NumberOfChannels; ++i)
    {
        LONG     volume = 0;
        NTSTATUS status = deviceContext->UsbAudioConfiguration->GetCurrentVolume(deviceContext, volumeContext->EntityID, (UCHAR)i, volume);
        if (NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - get current volume %ld, entity ID 0x%02x, channel(USB) %d", volume, volumeContext->EntityID, i);
            volumeContext->VolumeLevels[i] = volume;
        }
    }

    Element = volumeElement;

    RETURN_NTSTATUS_IF_FAILED(AllocateElementContext(Element, AudioNodeKind::VolumeElement, UnitID, (ULONG)-1));

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
Codec_CreateMuteElement(
    AudioIsochronousEngine * AudioIsochronousEngine,
    WDFDEVICE                Device,
    ACXCIRCUIT               Circuit,
    ACXELEMENT &             Element,
    UCHAR                    UnitID,
    UCHAR                    NumOfChannelsPerDevice
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

    if (NumOfChannelsPerDevice != 0)
    {
        numOfChannels = NumOfChannelsPerDevice;
    }

    muteCfg.ChannelsCount = numOfChannels;
    muteCfg.Name = &KSAUDFNAME_WAVE_MUTE;
    muteCfg.Callbacks = &muteCallbacks;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, MUTE_ELEMENT_CONTEXT);
    attributes.ParentObject = Circuit;
    attributes.EvtCleanupCallback = Codec_EvtVolumeElementContextCleanup;

    RETURN_NTSTATUS_IF_FAILED(AcxMuteCreate(Circuit, &attributes, &muteCfg, (ACXMUTE *)&muteElement));

    PMUTE_ELEMENT_CONTEXT muteContext = GetMuteElementContext(muteElement);
    ASSERT(muteContext);

    RtlZeroMemory(muteContext, sizeof(MUTE_ELEMENT_CONTEXT));
    muteContext->Device = Device;
    muteContext->EntityID = UnitID;
    muteContext->MuteStatesMemory = nullptr;
    muteContext->MuteStates = nullptr;

    attributes.ParentObject = muteElement;
    RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(bool) * (numOfChannels + 1), &muteContext->MuteStatesMemory, nullptr));
    muteContext->MuteStates = (bool *)WdfMemoryGetBuffer(muteContext->MuteStatesMemory, nullptr);
    RtlZeroMemory(muteContext->MuteStates, sizeof(bool) * (numOfChannels + 1));
    muteContext->NumberOfChannels = numOfChannels;

    PDEVICE_CONTEXT deviceContext = GetDeviceContext(Device);
    ASSERT(deviceContext);

    for (ULONG i = 0; i < (muteContext->NumberOfChannels + 1); ++i)
    {
        bool     mute = false;
        NTSTATUS status = deviceContext->UsbAudioConfiguration->GetCurrentMute(deviceContext, muteContext->EntityID, (UCHAR)i, mute);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - get current mute %!bool!, entity ID 0x%02x, channel(USB) %d", (mute != 0) ? true : false, muteContext->EntityID, i);
        if (NT_SUCCESS(status))
        {
            muteContext->MuteStates[i] = mute;
        }
    }

    Element = muteElement;

    RETURN_NTSTATUS_IF_FAILED(AllocateElementContext(Element, AudioNodeKind::MuteElement, UnitID, (ULONG)-1));

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
_Use_decl_annotations_
VOID Codec_EvtUSBAudioAcxDriverMuxProcessRequest(
    WDFOBJECT  Object,
    WDFREQUEST Request
)
{
    NTSTATUS               status = STATUS_NOT_SUPPORTED;
    ACX_REQUEST_PARAMETERS params{};

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Entry");

    ACX_REQUEST_PARAMETERS_INIT(&params);
    AcxRequestGetParameters(Request, &params);

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - Size = 0x%x, MajorFunction = 0x%x, MinorFunction = 0x%x, Type = 0x%x", params.Size, params.MajorFunction, params.MinorFunction, params.Type);
    if (params.Type == AcxRequestTypeProperty)
    {
        // GUID              Set;
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - Id = 0x%x, Verb = 0x%x, ItemType = 0x%x, ItemId = 0x%x, Control = %p, ControlCb = 0x%x, Value = 0x%p, ValueCb = 0x%x", params.Parameters.Property.Id, params.Parameters.Property.Verb, params.Parameters.Property.ItemType, params.Parameters.Property.ItemId, params.Parameters.Property.Control, params.Parameters.Property.ControlCb, params.Parameters.Property.Value, params.Parameters.Property.ValueCb);
    }

    ASSERT(params.Type == AcxRequestTypeProperty);

    IF_TRUE_ACTION_JUMP((params.Type != AcxRequestTypeProperty) ||
                            (!IsEqualGUID(params.Parameters.Property.Set, KSPROPSETID_Audio) ||
                             (params.Parameters.Property.Id != KSPROPERTY_AUDIO_MUX_SOURCE)),
                        ASSERT(FALSE);
                        status = STATUS_INVALID_PARAMETER;,
                                                          Exit);

    // ACXCIRCUIT circuit = (ACXCIRCUIT)AcxElementGetContainer((ACXELEMENT)Object);
    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, " - ACXCIRCUIT %p", circuit);

    PMUX_ELEMENT_CONTEXT muxContext = GetMuxElementContext((ACXELEMENT)Object);
    ASSERT(muxContext);

    PDEVICE_CONTEXT deviceContext = GetDeviceContext(muxContext->Device);
    ASSERT(deviceContext != nullptr);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, " - muxContext %p, entity ID 0x%02x, %d input pins", muxContext, muxContext->EntityID, muxContext->NumberOfInputPins);

    if (params.Parameters.Property.Verb == AcxPropertyVerbGet)
    {
        UCHAR selectorIndex = 0;
        *(PULONG(params.Parameters.Property.Value)) = 0;
        status = deviceContext->UsbAudioConfiguration->GetCurrentSelector(deviceContext, muxContext->EntityID, selectorIndex);
        if (NT_SUCCESS(status))
        {
            if (selectorIndex != 0)
            {
                *(PULONG(params.Parameters.Property.Value)) = selectorIndex - 1;
            }
            else
            {
                *(PULONG(params.Parameters.Property.Value)) = 0;
            }
        }
        params.Parameters.Property.ValueCb = sizeof(ULONG);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - AcxPropertyVerbGet ");
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - Value   = 0x%08x", *(PULONG)(params.Parameters.Property.Value));
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - ValueCb = 0x%08x", params.Parameters.Property.ValueCb);
    }
    else if (params.Parameters.Property.Verb == AcxPropertyVerbSet)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - AcxPropertyVerbSet ");
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - Value   = 0x%08x", *(PULONG)(params.Parameters.Property.Value));
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - ValueCb = 0x%08x", params.Parameters.Property.ValueCb);
        status = deviceContext->UsbAudioConfiguration->SetCurrentSelector(deviceContext, muxContext->EntityID, (UCHAR) * (PULONG)(params.Parameters.Property.Value) + 1);
    }
    else if (params.Parameters.Property.Verb == AcxPropertyVerbBasicSupport)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - AcxPropertyVerbBasicSupport ");
        status = ProcessRequestHandler_BasicSupport(&params, KSPROPERTY_TYPE_ALL, VT_UI4);

        status = STATUS_SUCCESS;
    }

Exit:
    WdfRequestComplete(Request, status);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Exit %!STATUS!", status);
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS Codec_CreateMuxElement(
    _In_ AudioIsochronousEngine * AudioIsochronousEngine,
    _In_ WDFDEVICE                Device,
    _In_ ACXCIRCUIT               Circuit,
    _Inout_ ACXELEMENT &          Element,
    _In_ UCHAR                    UnitID
)
{
    NTSTATUS              status = STATUS_SUCCESS;
    UCHAR                 numOfChannels = 0;
    UCHAR                 numOfInputPins = 0;
    ACXELEMENT            muxElement = nullptr;
    WDF_OBJECT_ATTRIBUTES attributes{};
    ACX_ELEMENT_CONFIG    config{};

    PAGED_CODE();
    ASSERT(Device != nullptr);
    ASSERT(Circuit != nullptr);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_FAILED(AudioIsochronousEngine->GetInformationForMuxElement(UnitID, numOfChannels, numOfInputPins));
    ASSERT(numOfInputPins != 0);

    ACX_ELEMENT_CONFIG_INIT(&config);
    config.Type = &KSNODETYPE_MUX;
    config.Name = &KSNODETYPE_MUX;
    config.Properties = &(s_PropertyItems[toULONG(PropertyItemIndex::MuxProperty)]);
    config.PropertiesCount = 1;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, MUX_ELEMENT_CONTEXT);
    attributes.ParentObject = Circuit;

    RETURN_NTSTATUS_IF_FAILED(AcxElementCreate(Circuit, &attributes, &config, &muxElement));

    PMUX_ELEMENT_CONTEXT muxContext = GetMuxElementContext(muxElement);
    ASSERT(muxContext);

    RtlZeroMemory(muxContext, sizeof(MUX_ELEMENT_CONTEXT));
    muxContext->Device = Device;
    muxContext->EntityID = UnitID;
    muxContext->NumberOfChannels = numOfChannels;
    muxContext->NumberOfInputPins = numOfInputPins;
    muxContext->ConnectPinIndex = 0;

    Element = muxElement;

    RETURN_NTSTATUS_IF_FAILED(AllocateElementContext(Element, AudioNodeKind::MuxElement, UnitID, (ULONG)-1));

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Exit");

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
VOID Codec_EvtUSBAudioAcxDriverAgcProcessRequest(
    WDFOBJECT  Object,
    WDFREQUEST Request
)
{
    NTSTATUS               status = STATUS_NOT_SUPPORTED;
    ACX_REQUEST_PARAMETERS params{};

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Entry");

    ACX_REQUEST_PARAMETERS_INIT(&params);
    AcxRequestGetParameters(Request, &params);

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - Size = 0x%x, MajorFunction = 0x%x, MinorFunction = 0x%x, Type = 0x%x", params.Size, params.MajorFunction, params.MinorFunction, params.Type);
    if (params.Type == AcxRequestTypeProperty)
    {
        // GUID              Set;
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - Id = 0x%x, Verb = 0x%x, ItemType = 0x%x, ItemId = 0x%x, Control = %p, ControlCb = 0x%x, Value = 0x%p, ValueCb = 0x%x", params.Parameters.Property.Id, params.Parameters.Property.Verb, params.Parameters.Property.ItemType, params.Parameters.Property.ItemId, params.Parameters.Property.Control, params.Parameters.Property.ControlCb, params.Parameters.Property.Value, params.Parameters.Property.ValueCb);
    }

    ULONG Channel = ALL_CHANNELS_ID;
    if (params.Parameters.Property.ControlCb == (sizeof(LONG) + sizeof(ULONG)))
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - Control[0] = %d (0x%x), Control[1] = 0x%x", ((LONG *)params.Parameters.Property.Control)[0], ((ULONG *)params.Parameters.Property.Control)[0], ((ULONG *)params.Parameters.Property.Control)[1]);
        if (params.Parameters.Property.Control != nullptr)
        {
            ((ULONG *)params.Parameters.Property.Control)[0];
        }
    }

    ASSERT(params.Type == AcxRequestTypeProperty);

    IF_TRUE_ACTION_JUMP((params.Type != AcxRequestTypeProperty) || (!IsEqualGUID(params.Parameters.Property.Set, KSPROPSETID_Audio) || (params.Parameters.Property.Id != KSPROPERTY_AUDIO_AGC)),
                        ASSERT(FALSE);
                        status = STATUS_INVALID_PARAMETER;,
                                                          Exit);

    PAGC_ELEMENT_CONTEXT agcContext = GetAgcElementContext((ACXELEMENT)Object);
    ASSERT(agcContext);

    PDEVICE_CONTEXT deviceContext = GetDeviceContext(agcContext->Device);
    ASSERT(deviceContext != nullptr);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, " - agcContext %p, entity ID 0x%02x", agcContext, agcContext->EntityID);

    if (params.Parameters.Property.Verb == AcxPropertyVerbGet)
    {
        bool autoGain = false;
        // Returns the ON/OFF status of Auto Gain Control.
        if ((Channel != ALL_CHANNELS_ID) && (Channel < agcContext->NumberOfChannels))
        {
            status = deviceContext->UsbAudioConfiguration->GetCurrentAutoGain(deviceContext, agcContext->EntityID, (UCHAR)(Channel + 1), autoGain);
            if (NT_SUCCESS(status))
            {
                *(BOOL *)(params.Parameters.Property.Value) = autoGain ? TRUE : FALSE;
            }
            else if (status == STATUS_NOT_SUPPORTED)
            {
                status = deviceContext->UsbAudioConfiguration->GetCurrentAutoGain(deviceContext, agcContext->EntityID, 0, autoGain);
                if (NT_SUCCESS(status))
                {
                    *(BOOL *)(params.Parameters.Property.Value) = autoGain ? TRUE : FALSE;
                }
            }
        }
        else if (Channel == ALL_CHANNELS_ID)
        {
            status = deviceContext->UsbAudioConfiguration->GetCurrentAutoGain(deviceContext, agcContext->EntityID, 0, autoGain);
            if (NT_SUCCESS(status))
            {
                *(BOOL *)(params.Parameters.Property.Value) = autoGain ? TRUE : FALSE;
            }
            else if (status == STATUS_NOT_SUPPORTED)
            {
                for (ULONG i = 0; i < agcContext->NumberOfChannels; ++i)
                {
                    status = deviceContext->UsbAudioConfiguration->GetCurrentAutoGain(deviceContext, agcContext->EntityID, (UCHAR)(i + 1), autoGain);
                    if (NT_SUCCESS(status))
                    {
                        *(BOOL *)(params.Parameters.Property.Value) = autoGain ? TRUE : FALSE;
                        break;
                    }
                }
            }
        }
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - AcxPropertyVerbGet ");
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - Value   = %d", *(BOOL *)(params.Parameters.Property.Value));
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - ValueCb = 0x%08x", params.Parameters.Property.ValueCb);
    }
    else if (params.Parameters.Property.Verb == AcxPropertyVerbSet)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - AcxPropertyVerbSet ");
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - Value   = %d", *(BOOL *)(params.Parameters.Property.Value));
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - ValueCb = 0x%08x", params.Parameters.Property.ValueCb);

        bool autoGain = *(BOOL *)(params.Parameters.Property.Value) ? true : false;

        if ((Channel != ALL_CHANNELS_ID) && (Channel < agcContext->NumberOfChannels))
        {
            status = deviceContext->UsbAudioConfiguration->SetCurrentAutoGain(deviceContext, agcContext->EntityID, (UCHAR)(Channel + 1), autoGain);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - set current agc %!bool!, entity ID 0x%02x, channel(USB) %d, %!STATUS!", (autoGain != 0) ? true : false, agcContext->EntityID, Channel + 1, status);
            if (status == STATUS_NOT_SUPPORTED)
            {
                status = deviceContext->UsbAudioConfiguration->SetCurrentAutoGain(deviceContext, agcContext->EntityID, 0, autoGain);
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - set current agc %!bool!, entity ID 0x%02x, channel(USB) %d, %!STATUS!", (autoGain != 0) ? true : false, agcContext->EntityID, 0, status);
            }
        }
        else if (Channel == ALL_CHANNELS_ID)
        {
            // Set Auto Gain Control to ON/OFF.
            status = deviceContext->UsbAudioConfiguration->SetCurrentAutoGain(deviceContext, agcContext->EntityID, 0, autoGain);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - set current agc %!bool!, entity ID 0x%02x, channel(USB) %d, %!STATUS!", autoGain, agcContext->EntityID, 0, status);
            if (status == STATUS_NOT_SUPPORTED)
            {
                for (ULONG i = 0; i < agcContext->NumberOfChannels; ++i)
                {
                    status = deviceContext->UsbAudioConfiguration->SetCurrentAutoGain(deviceContext, agcContext->EntityID, (UCHAR)(i + 1), autoGain);
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - set current agc %!bool!, entity ID 0x%02x, channel(USB) %d, %!STATUS!", autoGain, agcContext->EntityID, i + 1, status);
                }
            }
        }
    }
    else if (params.Parameters.Property.Verb == AcxPropertyVerbBasicSupport)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - AcxPropertyVerbBasicSupport ");
        status = ProcessRequestHandler_BasicSupport(&params, KSPROPERTY_TYPE_ALL, VT_BOOL);
    }

Exit:
    WdfRequestComplete(Request, status);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Exit %!STATUS!", status);
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS Codec_CreateAgcElement(
    AudioIsochronousEngine * AudioIsochronousEngine,
    WDFDEVICE                Device,
    ACXCIRCUIT               Circuit,
    ACXELEMENT &             Element,
    UCHAR                    UnitID
)
{
    NTSTATUS              status = STATUS_SUCCESS;
    UCHAR                 numOfChannels = 0;
    ACXELEMENT            agcElement = nullptr;
    WDF_OBJECT_ATTRIBUTES attributes{};
    ACX_ELEMENT_CONFIG    config{};

    PAGED_CODE();
    ASSERT(Device != nullptr);
    ASSERT(Circuit != nullptr);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_FAILED(AudioIsochronousEngine->GetInformationForAgcElement(UnitID, numOfChannels));

    ACX_ELEMENT_CONFIG_INIT(&config);
    config.Type = &KSNODETYPE_AGC;
    config.Name = &KSNODETYPE_AGC;
    config.Properties = &(s_PropertyItems[toULONG(PropertyItemIndex::AgcProperty)]);
    config.PropertiesCount = 1;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, AGC_ELEMENT_CONTEXT);
    attributes.ParentObject = Circuit;

    RETURN_NTSTATUS_IF_FAILED(AcxElementCreate(Circuit, &attributes, &config, &agcElement));

    PAGC_ELEMENT_CONTEXT agcContext = GetAgcElementContext(agcElement);
    ASSERT(agcContext);

    RtlZeroMemory(agcContext, sizeof(AGC_ELEMENT_CONTEXT));
    agcContext->Device = Device;
    agcContext->EntityID = UnitID;
    agcContext->NumberOfChannels = numOfChannels;

    Element = agcElement;

    RETURN_NTSTATUS_IF_FAILED(AllocateElementContext(Element, AudioNodeKind::AgcElement, UnitID, (ULONG)-1));

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Exit");

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
VOID Codec_EvtUSBAudioAcxDriverMixLevelCapsProcessRequest(
    WDFOBJECT /* Object */,
    WDFREQUEST Request
)
{
    NTSTATUS               status = STATUS_NOT_SUPPORTED;
    ACX_REQUEST_PARAMETERS params{};

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Entry");

    ACX_REQUEST_PARAMETERS_INIT(&params);
    AcxRequestGetParameters(Request, &params);

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - Size = 0x%x, MajorFunction = 0x%x, MinorFunction = 0x%x, Type = 0x%x", params.Size, params.MajorFunction, params.MinorFunction, params.Type);
    if (params.Type == AcxRequestTypeProperty)
    {
        // GUID              Set;
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - Id = 0x%x, Verb = 0x%x, ItemType = 0x%x, ItemId = 0x%x, Control = %p, ControlCb = 0x%x, Value = 0x%p, ValueCb = 0x%x", params.Parameters.Property.Id, params.Parameters.Property.Verb, params.Parameters.Property.ItemType, params.Parameters.Property.ItemId, params.Parameters.Property.Control, params.Parameters.Property.ControlCb, params.Parameters.Property.Value, params.Parameters.Property.ValueCb);
    }

    ASSERT(params.Type == AcxRequestTypeProperty);

    IF_TRUE_ACTION_JUMP((params.Type != AcxRequestTypeProperty) ||
                            (!IsEqualGUID(params.Parameters.Property.Set, KSPROPSETID_Audio) ||
                             (params.Parameters.Property.Id != KSPROPERTY_AUDIO_MIX_LEVEL_CAPS)),
                        ASSERT(FALSE);
                        status = STATUS_INVALID_PARAMETER;,
                                                          Exit);

#if 0
//	ACXCIRCUIT circuit = AcxElementGetContainer((ACXELEMENT)Object);
//    ASSERT(circuit != nullptr);

//    WDFDEVICE device = AcxCircuitGetWdfDevice((ACXCIRCUIT)circuit);
//    ASSERT(device != nullptr);
#else
//    WDFDEVICE       device = AcxCircuitGetWdfDevice((ACXCIRCUIT)Object);
#endif

    //    PDEVICE_CONTEXT deviceContext = GetDeviceContext(device);
    //    ASSERT(deviceContext != nullptr);

    if (params.Parameters.Property.Verb == AcxPropertyVerbGet)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - AcxPropertyVerbGet ");
        status = STATUS_SUCCESS;
    }
    else if (params.Parameters.Property.Verb == AcxPropertyVerbSet)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - AcxPropertyVerbSet ");
        status = STATUS_NOT_SUPPORTED;
    }
    else if (params.Parameters.Property.Verb == AcxPropertyVerbBasicSupport)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - AcxPropertyVerbBasicSupport ");
        status = ProcessRequestHandler_BasicSupport(&params, KSPROPERTY_TYPE_ALL, VT_EMPTY);
        status = STATUS_SUCCESS;
    }

Exit:
    WdfRequestComplete(Request, status);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Exit %!STATUS!", status);
}

PAGED_CODE_SEG
_Use_decl_annotations_
VOID Codec_EvtUSBAudioAcxDriverJackDescriptionProcessRequest(
    WDFOBJECT /* Object */,
    WDFREQUEST Request
)
{
    NTSTATUS               status = STATUS_NOT_SUPPORTED;
    ACX_REQUEST_PARAMETERS params{};

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Entry");

    ACX_REQUEST_PARAMETERS_INIT(&params);
    AcxRequestGetParameters(Request, &params);

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - Size = 0x%x, MajorFunction = 0x%x, MinorFunction = 0x%x, Type = 0x%x", params.Size, params.MajorFunction, params.MinorFunction, params.Type);
    if (params.Type == AcxRequestTypeProperty)
    {
        // GUID              Set;
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - Id = 0x%x, Verb = 0x%x, ItemType = 0x%x, ItemId = 0x%x, Control = %p, ControlCb = 0x%x, Value = 0x%p, ValueCb = 0x%x", params.Parameters.Property.Id, params.Parameters.Property.Verb, params.Parameters.Property.ItemType, params.Parameters.Property.ItemId, params.Parameters.Property.Control, params.Parameters.Property.ControlCb, params.Parameters.Property.Value, params.Parameters.Property.ValueCb);
    }

    ASSERT(params.Type == AcxRequestTypeProperty);

    IF_TRUE_ACTION_JUMP((params.Type != AcxRequestTypeProperty) ||
                            (!IsEqualGUID(params.Parameters.Property.Set, KSPROPSETID_Audio) ||
                             (params.Parameters.Property.Id != KSPROPERTY_JACK_DESCRIPTION)),
                        ASSERT(FALSE);
                        status = STATUS_INVALID_PARAMETER;,
                                                          Exit);

#if 0
//	ACXCIRCUIT circuit = AcxElementGetContainer((ACXELEMENT)Object);
//    ASSERT(circuit != nullptr);

//    WDFDEVICE device = AcxCircuitGetWdfDevice((ACXCIRCUIT)circuit);
//    ASSERT(device != nullptr);
#else
//    WDFDEVICE       device = AcxCircuitGetWdfDevice((ACXCIRCUIT)Object);
#endif

    //    PDEVICE_CONTEXT deviceContext = GetDeviceContext(device);
    //    ASSERT(deviceContext != nullptr);

    if (params.Parameters.Property.Verb == AcxPropertyVerbGet)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - AcxPropertyVerbGet ");
        status = STATUS_SUCCESS;
    }
    else if (params.Parameters.Property.Verb == AcxPropertyVerbSet)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - AcxPropertyVerbSet ");
        status = STATUS_NOT_SUPPORTED;
    }
    else if (params.Parameters.Property.Verb == AcxPropertyVerbBasicSupport)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - AcxPropertyVerbBasicSupport ");
        status = ProcessRequestHandler_BasicSupport(&params, KSPROPERTY_TYPE_ALL, VT_EMPTY);
        status = STATUS_SUCCESS;
    }

Exit:
    WdfRequestComplete(Request, status);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Exit %!STATUS!", status);
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS Codec_CreateSuperMixElement(
    _In_ AudioIsochronousEngine * AudioIsochronousEngine,
    _In_ WDFDEVICE                Device,
    _In_ ACXCIRCUIT               Circuit,
    _Inout_ ACXELEMENT &          Element,
    _In_ UCHAR                    UnitID
)
{
    NTSTATUS              status = STATUS_SUCCESS;
    UCHAR                 numOfInputChannels = 0;
    UCHAR                 numOfOutputChannels = 0;
    ACXELEMENT            superMixElement = nullptr;
    WDF_OBJECT_ATTRIBUTES attributes{};
    ACX_ELEMENT_CONFIG    config{};

    PAGED_CODE();
    ASSERT(Device != nullptr);
    ASSERT(Circuit != nullptr);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_FAILED(AudioIsochronousEngine->GetInformationForSuperMixElement(UnitID, numOfInputChannels, numOfOutputChannels));

    ACX_ELEMENT_CONFIG_INIT(&config);
    config.Type = &KSNODETYPE_SUPERMIX;
    config.Name = &KSNODETYPE_SUPERMIX;
    config.Properties = &(s_PropertyItems[toULONG(PropertyItemIndex::MixLevelCapsProperty)]);
    config.PropertiesCount = 1;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, SUPERMIX_ELEMENT_CONTEXT);
    attributes.ParentObject = Circuit;

    RETURN_NTSTATUS_IF_FAILED(AcxElementCreate(Circuit, &attributes, &config, &superMixElement));

    PSUPERMIX_ELEMENT_CONTEXT superMixContext = GetSuperMixElementContext(superMixElement);
    ASSERT(superMixContext);

    RtlZeroMemory(superMixContext, sizeof(SUPERMIX_ELEMENT_CONTEXT));
    superMixContext->Device = Device;
    superMixContext->EntityID = UnitID;
    superMixContext->NumberOfInputChannels = numOfInputChannels;
    superMixContext->NumberOfOutputChannels = numOfOutputChannels;
    superMixContext->ConnectPinIndex = 0;

    Element = superMixElement;

    RETURN_NTSTATUS_IF_FAILED(AllocateElementContext(Element, AudioNodeKind::SuperMixElement, UnitID, (ULONG)-1));

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Exit");

    return status;
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

    //
    // Register ACXDATAFORMAT objects in ascending order to ensure that Windows correctly evaluates KSDATARANGE_AUDIO.
    //
    for (ULONG formatIndex = 0; formatIndex < UsbAudioDataFormatManager->GetNumOfUSBAudioDataFormats(); formatIndex++)
    {
        for (ULONG mask = 1, index = 0; mask != 0; mask <<= 1, index++)
        {
            if (mask & SupportedSampleRate)
            {
                ULONG sampleRate = GetSampleRateFromIndex(index);

                ///////////////////////////////////////////////////////////
                //
                // Allocate the formats this circuit supports.
                //
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
    UCHAR                 channelNames = 0;
    WDF_OBJECT_ATTRIBUTES attributes{};

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry, id %u, device index %u, channel %u, channels count %u", Id, DeviceIndex, Channel, ChannelsCount);

    ///////////////////////////////////////////////////////////
    //
    // Create Device Bridge Pin.
    //
    ACX_PIN_CALLBACKS_INIT(&pinCallbacks);
    channelNames = AudioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.ChannelNames;
    pinCallbacks.EvtAcxPinRetrieveName = CodecR_EvtAcxPinRetrieveName;

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
    if (IsEqualGUID(*ConvertTerminalType(TerminalType), KSNODETYPE_SPEAKER))
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
    pinContext->ChannelNames = channelNames;
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
    UCHAR                 channelNames = 0;
    WDF_OBJECT_ATTRIBUTES attributes{};

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry, id %u, device index %u, channel %u, channels count %u", Id, DeviceIndex, Channel, ChannelsCount);

    ///////////////////////////////////////////////////////////
    //
    // Create capture endpoint pin.
    //
    ACX_PIN_CALLBACKS_INIT(&pinCallbacks);
    channelNames = AudioIsochronousEngine->GetAudioStreamPropertySet().InputProperty.ChannelNames;
    pinCallbacks.EvtAcxPinRetrieveName = CodecC_EvtAcxPinRetrieveName;

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
    pinContext->ChannelNames = channelNames;
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
    ACXPIN &                 Pin,
    UCHAR                    UnitID,
    const ULONG              SupportedSampleRate
)
{
    NTSTATUS              status = STATUS_SUCCESS;
    ACXPIN                pin = nullptr;
    ACX_PIN_CONFIG        pinCfg{};
    CODEC_PIN_CONTEXT *   pinContext = nullptr;
    WDF_OBJECT_ATTRIBUTES attributes{};
    UCHAR                 numOfChannels = 0;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry, unit id 0x%02x", UnitID);

    USBAudioDataFormatManager * usbAudioDataFormatManager = AudioIsochronousEngine->GetUSBAudioDataFormatManager(false);
    RETURN_NTSTATUS_IF_TRUE_ACTION(usbAudioDataFormatManager == nullptr, status = STATUS_INVALID_PARAMETER, status);

    RETURN_NTSTATUS_IF_FAILED(AudioIsochronousEngine->GetInformationForHostPin(UnitID, numOfChannels));

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
    Pin = pin;

    pinContext = GetCodecPinContext(pin);
    ASSERT(pinContext);
    pinContext->IsInput = false;
    pinContext->Device = Device;
    pinContext->CodecPinType = CodecPinTypeHost;
    pinContext->DeviceIndex = 0;                        // DeviceIndex;
    pinContext->Channel = 0;                            // Channel;
    pinContext->NumOfChannelsPerDevice = numOfChannels; // ChannelsCount;
    pinContext->AudioIsochronousEngine = AudioIsochronousEngine;
    pinContext->TerminalID = USBAudioConfiguration::InvalidID;

    RETURN_NTSTATUS_IF_FAILED(AllocateElementContext((ACXELEMENT)Pin, AudioNodeKind::RenderHostPin, UnitID, PinID));

    if (AudioIsochronousEngine->HasOutputIsochronousInterface())
    {
        RETURN_NTSTATUS_IF_FAILED(Codec_AllocateSupportedFormats(Device, pin, Circuit, SupportedSampleRate, numOfChannels, usbAudioDataFormatManager));
    }

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
    ACXPIN &                 Pin,
    UCHAR                    UnitID
)
{
    NTSTATUS                                      status = STATUS_SUCCESS;
    ACXPIN                                        pin = nullptr;
    ACX_PIN_CONFIG                                pinCfg{};
    ACX_PIN_CALLBACKS                             pinCallbacks{};
    CODEC_PIN_CONTEXT *                           pinContext = nullptr;
    WDF_OBJECT_ATTRIBUTES                         attributes{};
    USHORT                                        terminalType = 0;
    UCHAR                                         numOfChannels = 0;
    UCHAR                                         channelNames = 0;
    bool                                          isEnableConnector = false;
    NS_USBAudio::AUDIO_CHANNEL_CLUSTER_DESCRIPTOR connectorState{};

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry, unit id 0x%02x", UnitID);

    RETURN_NTSTATUS_IF_FAILED(AudioIsochronousEngine->GetInformationForBridgePin(UnitID, numOfChannels, terminalType, channelNames, isEnableConnector, connectorState));

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
    pinCfg.Properties = s_PinPropertyItems;
    pinCfg.PropertiesCount = ARRAYSIZE(s_PinPropertyItems);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, CODEC_PIN_CONTEXT);
    attributes.EvtCleanupCallback = Codec_EvtPinContextCleanup;
    attributes.ParentObject = Circuit;

    //
    // The driver uses this DDI to create one or more pins on the circuits.
    //
    RETURN_NTSTATUS_IF_FAILED(AcxPinCreate(Circuit, &attributes, &pinCfg, &pin));
    ASSERT(pinContext);
    Pin = pin;

    pinContext = GetCodecPinContext(pin);
    pinContext->IsInput = true;
    pinContext->Device = Device;
    pinContext->CodecPinType = CodecPinTypeDevice;
    pinContext->DeviceIndex = 0; // DeviceIndex;
    pinContext->Channel = 0;     // Channel;
    pinContext->NumOfChannelsPerDevice = numOfChannels;
    pinContext->AudioIsochronousEngine = AudioIsochronousEngine;
    pinContext->ChannelNames = channelNames;
    pinContext->TerminalID = UnitID;

    RETURN_NTSTATUS_IF_FAILED(AllocateElementContext((ACXELEMENT)Pin, AudioNodeKind::RenderBridgePin, UnitID, PinID));

    if (isEnableConnector)
    {
        RETURN_NTSTATUS_IF_FAILED(Codec_AddAudioJackToBridgePin(pin, ConverSpeakerPositions(connectorState.bmChannelConfig)));
    }
    else
    {
        RETURN_NTSTATUS_IF_FAILED(Codec_AddAudioDummyJackToBridgePin(pin));
    }

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
    ACXPIN &                 Pin,
    UCHAR                    UnitID,
    const ULONG              SupportedSampleRate
)
{
    NTSTATUS              status = STATUS_SUCCESS;
    ACXPIN                pin = nullptr;
    ACX_PIN_CONFIG        pinCfg{};
    CODEC_PIN_CONTEXT *   pinContext = nullptr;
    WDF_OBJECT_ATTRIBUTES attributes{};
    UCHAR                 numOfChannels = 0;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry, unit id 0x%02x", UnitID);

    USBAudioDataFormatManager * usbAudioDataFormatManager = AudioIsochronousEngine->GetUSBAudioDataFormatManager(true);
    RETURN_NTSTATUS_IF_TRUE_ACTION(usbAudioDataFormatManager == nullptr, status = STATUS_INVALID_PARAMETER, status);

    RETURN_NTSTATUS_IF_FAILED(AudioIsochronousEngine->GetInformationForHostPin(UnitID, numOfChannels));

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
    Pin = pin;

    pinContext = GetCodecPinContext(pin);
    ASSERT(pinContext);
    pinContext->Device = Device;
    pinContext->CodecPinType = CodecPinTypeHost;
    pinContext->DeviceIndex = 0;                        // DeviceIndex;
    pinContext->Channel = 0;                            // Channel;
    pinContext->NumOfChannelsPerDevice = numOfChannels; // ChannelsCount;
    pinContext->AudioIsochronousEngine = AudioIsochronousEngine;
    pinContext->TerminalID = USBAudioConfiguration::InvalidID;

    RETURN_NTSTATUS_IF_FAILED(AllocateElementContext((ACXELEMENT)Pin, AudioNodeKind::CaptureHostPin, UnitID, PinID));

    RETURN_NTSTATUS_IF_FAILED(Codec_AllocateSupportedFormats(Device, pin, Circuit, SupportedSampleRate, numOfChannels, usbAudioDataFormatManager));

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
    ACXPIN &                 Pin,
    UCHAR                    UnitID
)
{
    NTSTATUS                                      status = STATUS_SUCCESS;
    ACXPIN                                        pin = nullptr;
    ACX_PIN_CONFIG                                pinCfg{};
    ACX_PIN_CALLBACKS                             pinCallbacks{};
    CODEC_PIN_CONTEXT *                           pinContext = nullptr;
    WDF_OBJECT_ATTRIBUTES                         attributes{};
    USHORT                                        terminalType = 0;
    UCHAR                                         numOfChannels = 0;
    UCHAR                                         channelNames = 0;
    bool                                          isEnableConnector = false;
    NS_USBAudio::AUDIO_CHANNEL_CLUSTER_DESCRIPTOR connectorState{};

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry, unit id 0x%02x", UnitID);

    RETURN_NTSTATUS_IF_FAILED(AudioIsochronousEngine->GetInformationForBridgePin(UnitID, numOfChannels, terminalType, channelNames, isEnableConnector, connectorState));

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
    pinCfg.Properties = s_PinPropertyItems;
    pinCfg.PropertiesCount = ARRAYSIZE(s_PinPropertyItems);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, CODEC_PIN_CONTEXT);
    attributes.EvtCleanupCallback = Codec_EvtPinContextCleanup;
    attributes.ParentObject = Circuit;

    //
    // The driver uses this DDI to create one or more pins on the circuits.
    //
    RETURN_NTSTATUS_IF_FAILED(AcxPinCreate(Circuit, &attributes, &pinCfg, &pin));
    ASSERT(pin != nullptr);
    Pin = pin;

    pinContext = GetCodecPinContext(pin);
    ASSERT(pinContext);
    pinContext->Device = Device;
    pinContext->CodecPinType = CodecPinTypeDevice;
    pinContext->DeviceIndex = 0; // DeviceIndex;
    pinContext->Channel = 0;     // Channel;
    pinContext->NumOfChannelsPerDevice = numOfChannels;
    pinContext->AudioIsochronousEngine = AudioIsochronousEngine;
    pinContext->ChannelNames = channelNames;
    pinContext->TerminalID = UnitID;

    RETURN_NTSTATUS_IF_FAILED(AllocateElementContext((ACXELEMENT)Pin, AudioNodeKind::CaptureBridgePin, UnitID, PinID));

    if (isEnableConnector)
    {
        RETURN_NTSTATUS_IF_FAILED(Codec_AddAudioJackToBridgePin(pin, ConverSpeakerPositions(connectorState.bmChannelConfig)));
    }
    else
    {
        RETURN_NTSTATUS_IF_FAILED(Codec_AddAudioDummyJackToBridgePin(pin));
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS Codec_ConnectElement(
    ACXCIRCUIT       Circuit,
    ACX_CONNECTION & Connection,
    ACXELEMENT &     FromElement,
    ACXELEMENT &     ToElement
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    ELEMENT_CONTEXT * fromElementContext = GetElementContext(FromElement);
    ELEMENT_CONTEXT * toElementContext = GetElementContext(ToElement);

    if (fromElementContext->PinID != (ULONG)-1)
    {
        if (toElementContext->PinID != (ULONG)-1)
        {
            ACX_CONNECTION_INIT(&Connection, Circuit, Circuit);
            Connection.FromPin.Id = fromElementContext->PinID;
            Connection.ToPin.Id = toElementContext->PinID;
        }
        else
        {
            ACX_CONNECTION_INIT(&Connection, Circuit, ToElement);
            Connection.FromPin.Id = fromElementContext->PinID;
            if (toElementContext->AudioNodeKind == toULONG(AudioNodeKind::MuxElement))
            {
                PMUX_ELEMENT_CONTEXT muxContext = GetMuxElementContext(ToElement);
                Connection.ToPin.Id = (muxContext->ConnectPinIndex)++;
            }
            else if (toElementContext->AudioNodeKind == toULONG(AudioNodeKind::SuperMixElement))
            {
                PSUPERMIX_ELEMENT_CONTEXT superMixContext = GetSuperMixElementContext(ToElement);
                Connection.ToPin.Id = (superMixContext->ConnectPinIndex)++;
            }
        }
    }
    else
    {
        if (toElementContext->PinID != (ULONG)-1)
        {
            ACX_CONNECTION_INIT(&Connection, FromElement, Circuit);
            Connection.ToPin.Id = toElementContext->PinID;
        }
        else
        {
            ACX_CONNECTION_INIT(&Connection, FromElement, ToElement);
            if (toElementContext->AudioNodeKind == toULONG(AudioNodeKind::MuxElement))
            {
                PMUX_ELEMENT_CONTEXT muxContext = GetMuxElementContext(ToElement);
                Connection.ToPin.Id = (muxContext->ConnectPinIndex)++;
            }
            else if (toElementContext->AudioNodeKind == toULONG(AudioNodeKind::SuperMixElement))
            {
                PSUPERMIX_ELEMENT_CONTEXT superMixContext = GetSuperMixElementContext(ToElement);
                Connection.ToPin.Id = (superMixContext->ConnectPinIndex)++;
            }
        }
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - connect %s PinID 0x%02x, UnitID 0x%02x -> %s PinID 0x%02x, UnitID 0x%02x", GetAudioNodeKindString(AudioNodeKind((AudioNodeKind)fromElementContext->AudioNodeKind)), fromElementContext->PinID, fromElementContext->UnitID, GetAudioNodeKindString(AudioNodeKind((AudioNodeKind)toElementContext->AudioNodeKind)), toElementContext->PinID, toElementContext->UnitID);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - FromObject %p, Object %p, Id 0x%x, -> ToObject %p, Object %p, Id 0x%x", Connection.FromObject, Connection.FromPin.Object, Connection.FromPin.Id, Connection.ToObject, Connection.ToPin.Object, Connection.ToPin.Id);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
Codec_AllocateElements(
    WDFDEVICE                Device,
    ACXCIRCUIT               Circuit,
    bool                     IsInput,
    AudioIsochronousEngine * AudioIsochronousEngine,
    const ULONG              SupportedSampleRate
)
{
    enum
    {
        DefaultSizeOfArray = 0x10
    };

    NTSTATUS                                          status = STATUS_SUCCESS;
    WDF_OBJECT_ATTRIBUTES                             attributes{};
    VariableArray<ACXPIN, DefaultSizeOfArray>         pins;
    VariableArray<ACXELEMENT, DefaultSizeOfArray>     elements;
    VariableArray<ACX_CONNECTION, DefaultSizeOfArray> connections;
    bool                                              hasMoreData = true;
    TraversalDirection                                traversalDirection = TraversalDirection::Forward;
    AudioNodeKind                                     audioNodeKind = AudioNodeKind::Invalid;
    UCHAR                                             unitID = USBAudioConfiguration::InvalidID;
    UCHAR                                             nextUnitID = USBAudioConfiguration::InvalidID;
    ULONGLONG                                         pendingUnitMap[4] = {};
    ULONG                                             counter = 0;
    ACXELEMENT                                        currentElement{};
    ACXELEMENT                                        prevElement{};
    ULONG                                             pinID = 0;

    PAGED_CODE();

    auto allocateElementsScope = wil::scope_exit([&]() {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, " IsInput = %!bool!, %u pins, %u elements, %u connections", IsInput, pins.GetNumOfArray(), elements.GetNumOfArray(), connections.GetNumOfArray());
    });

    auto findElement = [&elements](ULONG unitID, AudioNodeKind audioNodeKind, ACXELEMENT & currentElement) noexcept -> bool {
        for (auto & element : elements)
        {
            ELEMENT_CONTEXT * elementContext = GetElementContext(element);
            ASSERT(elementContext);
            if ((elementContext->UnitID == unitID) && (elementContext->AudioNodeKind == toULONG(audioNodeKind)))
            {
                currentElement = element;
                return true;
            }
        }
        return false;
    };

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Entry");

    if (IsInput)
    {
        traversalDirection = TraversalDirection::Reverse;
    }
    else
    {
        traversalDirection = TraversalDirection::Forward;
    }

    PCODEC_CIRCUIT_CONTEXT circuitContext = GetCircuitContext(Circuit);
    ASSERT(circuitContext);

    circuitContext->NumOfVolumeElements = 0;
    circuitContext->NumOfMuteElements = 0;

    if ((IsInput && AudioIsochronousEngine->HasInputIsochronousInterface()) || (!IsInput && AudioIsochronousEngine->HasOutputIsochronousInterface()))
    {
        while (hasMoreData)
        {
            bool shouldConnect = true;
            RETURN_NTSTATUS_IF_FAILED(AudioIsochronousEngine->WalkNextUnit(IsInput, pendingUnitMap, audioNodeKind, unitID, nextUnitID, traversalDirection, hasMoreData));
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - IsInput = %!bool!, 0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx, %s, 0x%02x, 0x%02x, %s, hasMoreData = %!bool!", IsInput, pendingUnitMap[0], pendingUnitMap[1], pendingUnitMap[2], pendingUnitMap[3], GetAudioNodeKindString(audioNodeKind), unitID, nextUnitID, GetTraversalDirectionString(traversalDirection), hasMoreData);

            switch (audioNodeKind)
            {
            case AudioNodeKind::RenderHostPin: {
                ACXPIN pin{};
                RETURN_NTSTATUS_IF_FAILED(Codec_CreateRenderHostPin(AudioIsochronousEngine, Device, Circuit, pinID, pin, unitID, SupportedSampleRate));
                RETURN_NTSTATUS_IF_FAILED(pins.Append(Circuit, pin));
                currentElement = (ACXELEMENT)pin;
                pinID++;
            }
            break;
            case AudioNodeKind::RenderBridgePin: {
                ACXPIN pin{};
                RETURN_NTSTATUS_IF_FAILED(Codec_CreateRenderBridgePin(AudioIsochronousEngine, Device, Circuit, pinID, pin, unitID));
                RETURN_NTSTATUS_IF_FAILED(pins.Append(Circuit, pin));
                currentElement = (ACXELEMENT)pin;
                pinID++;
            }
            break;
            case AudioNodeKind::CaptureHostPin: {
                ACXPIN pin{};
                RETURN_NTSTATUS_IF_FAILED(Codec_CreateCaptureHostPin(AudioIsochronousEngine, Device, Circuit, pinID, pin, unitID, SupportedSampleRate));
                RETURN_NTSTATUS_IF_FAILED(pins.Append(Circuit, pin));
                currentElement = (ACXELEMENT)pin;
                pinID++;
            }
            break;
            case AudioNodeKind::CaptureBridgePin: {
                ACXPIN pin{};
                RETURN_NTSTATUS_IF_FAILED(Codec_CreateCaptureBridgePin(AudioIsochronousEngine, Device, Circuit, pinID, pin, unitID));
                RETURN_NTSTATUS_IF_FAILED(pins.Append(Circuit, pin));
                currentElement = (ACXELEMENT)pin;
                pinID++;
            }
            break;
            case AudioNodeKind::VolumeElement: { // Feature Unit (FU_VOLUME_CONTROL) : KSNODETYPE_VOLUME
                ACXELEMENT element{};
                RETURN_NTSTATUS_IF_FAILED(Codec_CreateVolumeElement(AudioIsochronousEngine, Device, Circuit, element, unitID, 0));
                RETURN_NTSTATUS_IF_FAILED(elements.Append(Circuit, element));
                currentElement = element;
                circuitContext->NumOfVolumeElements++;
            }
            break;
            case AudioNodeKind::MuteElement: { // Feature Unit (FU_MUTE_CONTROL) : KSNODETYPE_MUTE
                ACXELEMENT element{};
                RETURN_NTSTATUS_IF_FAILED(Codec_CreateMuteElement(AudioIsochronousEngine, Device, Circuit, element, unitID, 0));
                RETURN_NTSTATUS_IF_FAILED(elements.Append(Circuit, element));
                currentElement = element;
                circuitContext->NumOfMuteElements++;
            }
            break;
            case AudioNodeKind::AgcElement: { // Feature Unit (FU_AUTOMATIC_GAIN_CONTROL) : KSNODETYPE_AGC
                ACXELEMENT element{};
                RETURN_NTSTATUS_IF_FAILED(Codec_CreateAgcElement(AudioIsochronousEngine, Device, Circuit, element, unitID));
                RETURN_NTSTATUS_IF_FAILED(elements.Append(Circuit, element));
                currentElement = element;
                circuitContext->NumOfAgcElements++;
            }
            break;
            case AudioNodeKind::SuperMixElement: { // Mixer Unit : KSNODETYPE_SUPERMIX
                if (!findElement(unitID, AudioNodeKind::SuperMixElement, currentElement))
                {
                    ACXELEMENT element{};
                    RETURN_NTSTATUS_IF_FAILED(Codec_CreateSuperMixElement(AudioIsochronousEngine, Device, Circuit, element, unitID));
                    RETURN_NTSTATUS_IF_FAILED(elements.Append(Circuit, element));
                    currentElement = element;
                }
                else
                {
                    shouldConnect = false;
                }
            }
            break;
            case AudioNodeKind::MuxElement: { // Selector Unit : KSNODETYPE_MUX
                if (!findElement(unitID, AudioNodeKind::MuxElement, currentElement))
                {
                    ACXELEMENT element{};
                    RETURN_NTSTATUS_IF_FAILED(Codec_CreateMuxElement(AudioIsochronousEngine, Device, Circuit, element, unitID));
                    RETURN_NTSTATUS_IF_FAILED(elements.Append(Circuit, element));
                    currentElement = element;
                }
                else
                {
                    shouldConnect = false;
                }
            }
            break;
            case AudioNodeKind::SrcElement: { // Sampling Rate Converter Unit : KSNODETYPE_SRC
                // ACXELEMENT element{};
                // RETURN_NTSTATUS_IF_FAILED(Codec_CreateSrcElement(AudioIsochronousEngine, Device, Circuit, element, unitID, 0));
                // RETURN_NTSTATUS_IF_FAILED(elements.Append(Circuit, element));
                // currentElement = element;
                // circuitContext->NumOfSrcElements++;
            }
            break;
            case AudioNodeKind::EffectElement: { // Effect Unit : KSNODETYPE_3D_EFFECTS
                // ACXELEMENT element{};
                // RETURN_NTSTATUS_IF_FAILED(Codec_CreateEffectElement(AudioIsochronousEngine, Device, Circuit, element, unitID, 0));
                // RETURN_NTSTATUS_IF_FAILED(elements.Append(Circuit, element));
                // currentElement = element;
                // circuitContext->NumOfEffectElements++;
            }
            break;
            case AudioNodeKind::ProcessingElement: { // Processing Unit : KSNODETYPE_MICROPHONE_ARRAY_PROCESSOR ?
                // ACXELEMENT element{};
                // RETURN_NTSTATUS_IF_FAILED(Codec_CreateProessingElement(AudioIsochronousEngine, Device, Circuit, element, unitID, 0));
                // RETURN_NTSTATUS_IF_FAILED(elements.Append(Circuit, element));
                // currentElement = element;
                // circuitContext->NumOfProessingElements++;
            }
            break;
            default:
                break;
            }
            unitID = nextUnitID;
            if ((counter != 0) && shouldConnect)
            {
                ACX_CONNECTION connection{};
                if (traversalDirection == TraversalDirection::Forward)
                {
                    Codec_ConnectElement(Circuit, connection, prevElement, currentElement);
                }
                else
                {
                    Codec_ConnectElement(Circuit, connection, currentElement, prevElement);
                }
                RETURN_NTSTATUS_IF_FAILED(connections.Append(Circuit, connection));
            }
            counter++;
            if (counter > 0xff)
            {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_ENTITY, "%!FUNC! counter overflow.");
                break;
            }
            prevElement = currentElement;
        }

        if (elements.GetNumOfArray() != 0)
        {
            RETURN_NTSTATUS_IF_FAILED(AcxCircuitAddElements(Circuit, elements.begin(), elements.GetNumOfArray()));
        }
        if (pins.GetNumOfArray() != 0)
        {
            RETURN_NTSTATUS_IF_FAILED(AcxCircuitAddPins(Circuit, pins.begin(), pins.GetNumOfArray()));
        }

        for (auto & connection : connections)
        {
            ELEMENT_CONTEXT * elementContext = GetElementContext(connection.FromObject);
            if ((elementContext != nullptr) && (elementContext->AudioNodeKind == toULONG(AudioNodeKind::MuxElement)))
            {
                PMUX_ELEMENT_CONTEXT muxContext = GetMuxElementContext(connection.FromObject);
                connection.FromPin.Id = muxContext->ConnectPinIndex;
            }
        }

        if (connections.GetNumOfArray() != 0)
        {
            RETURN_NTSTATUS_IF_FAILED(AcxCircuitAddConnections(Circuit, connections.begin(), connections.GetNumOfArray()));
        }

        if (circuitContext->NumOfVolumeElements != 0)
        {
            WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
            attributes.ParentObject = Device;
            RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(ACXVOLUME) * circuitContext->NumOfVolumeElements, &circuitContext->VolumeElementsMemory, nullptr));
            circuitContext->VolumeElements = (ACXVOLUME *)WdfMemoryGetBuffer(circuitContext->VolumeElementsMemory, nullptr);
        }

        if (circuitContext->NumOfMuteElements != 0)
        {
            WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
            attributes.ParentObject = Device;
            RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(ACXMUTE) * circuitContext->NumOfMuteElements, &circuitContext->MuteElementsMemory, nullptr));
            circuitContext->MuteElements = (ACXMUTE *)WdfMemoryGetBuffer(circuitContext->MuteElementsMemory, nullptr);
        }

        if (circuitContext->NumOfAgcElements != 0)
        {
            WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
            attributes.ParentObject = Device;
            RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(ACXELEMENT) * circuitContext->NumOfAgcElements, &circuitContext->AgcElementsMemory, nullptr));
            circuitContext->AgcElements = (ACXELEMENT *)WdfMemoryGetBuffer(circuitContext->AgcElementsMemory, nullptr);
        }

        if ((circuitContext->NumOfVolumeElements != 0) || (circuitContext->NumOfMuteElements != 0) || (circuitContext->NumOfAgcElements != 0))
        {
            ULONG volumeIndex = 0, muteIndex = 0, agcIndex = 0;
            for (auto & element : elements)
            {
                ELEMENT_CONTEXT * elementContext = GetElementContext(element);
                ASSERT(elementContext);
                switch (elementContext->AudioNodeKind)
                {
                case AudioNodeKind::VolumeElement:
                    if (volumeIndex < circuitContext->NumOfVolumeElements)
                    {
                        circuitContext->VolumeElements[volumeIndex++] = (ACXVOLUME)element;
                    }
                    break;
                case AudioNodeKind::MuteElement:
                    if (muteIndex < circuitContext->NumOfMuteElements)
                    {
                        circuitContext->MuteElements[muteIndex++] = (ACXMUTE)element;
                    }
                    break;
                case AudioNodeKind::AgcElement:
                    if (agcIndex < circuitContext->NumOfAgcElements)
                    {
                        circuitContext->AgcElements[agcIndex++] = element;
                    }
                    break;
                default:
                    break;
                }
            }
        }
    }
    else
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, "%!FUNC! do nothing");
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
Codec_VolumeChangeLevelNotification(
    ACXCIRCUIT Circuit,
    UCHAR      EntityID
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Entry");

    CODEC_CIRCUIT_CONTEXT * circuitContext = GetCircuitContext(Circuit);
    ASSERT(circuitContext);

    for (ULONG index = 0; index < circuitContext->NumOfVolumeElements; index++)
    {
        if (circuitContext->VolumeElements[index] != nullptr)
        {
            PVOLUME_ELEMENT_CONTEXT volumeContext = GetVolumeElementContext(circuitContext->VolumeElements[index]);
            ASSERT(volumeContext);

            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - volume context entity ID 0x%02x, entity ID 0x%02x", volumeContext->EntityID, EntityID);
            if (volumeContext->EntityID == EntityID)
            {
                bool            notify = false;
                LONG            volume;
                PDEVICE_CONTEXT deviceContext = GetDeviceContext(volumeContext->Device);
                ASSERT(deviceContext != nullptr);

                for (ULONG i = 0; i < (volumeContext->NumberOfChannels + 1); ++i)
                {
                    status = deviceContext->UsbAudioConfiguration->GetCurrentVolume(deviceContext, volumeContext->EntityID, (UCHAR)i, volume);
                    if (NT_SUCCESS(status))
                    {
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - get current volume %ld, entity ID 0x%02x, channel(USB) %d", volume, volumeContext->EntityID, i);
                        if (volumeContext->VolumeLevels[i] != volume)
                        {
                            volumeContext->VolumeLevels[i] = volume;
                            notify |= true;
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

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
Codec_MuteChangeStateNotification(
    ACXCIRCUIT Circuit,
    UCHAR      EntityID
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Entry");

    CODEC_CIRCUIT_CONTEXT * circuitContext = GetCircuitContext(Circuit);
    ASSERT(circuitContext);

    for (ULONG index = 0; index < circuitContext->NumOfMuteElements; index++)
    {
        if (circuitContext->MuteElements[index] != nullptr)
        {
            PMUTE_ELEMENT_CONTEXT muteContext = GetMuteElementContext(circuitContext->MuteElements[index]);
            ASSERT(muteContext);

            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - mute context entity ID 0x%02x, entity ID 0x%02x", muteContext->EntityID, EntityID);
            if (muteContext->EntityID == EntityID)
            {
                bool            notify = false;
                bool            mute;
                PDEVICE_CONTEXT deviceContext = GetDeviceContext(muteContext->Device);
                ASSERT(deviceContext != nullptr);

                for (ULONG i = 0; i < (muteContext->NumberOfChannels + 1); ++i)
                {
                    status = deviceContext->UsbAudioConfiguration->GetCurrentMute(deviceContext, muteContext->EntityID, (UCHAR)i, mute);
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - get current mute %!bool!, entity ID 0x%02x, channel(USB) %d", (mute != 0) ? true : false, muteContext->EntityID, i);
                    if (NT_SUCCESS(status))
                    {
                        if (muteContext->MuteStates[i] != mute)
                        {
                            muteContext->MuteStates[i] = mute;
                            notify |= true;
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

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
Codec_ConnectorChangeStateNotification(
    ACXCIRCUIT Circuit,
    UCHAR      EntityID
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Entry");

    ULONG pinCount = AcxCircuitGetPinsCount(Circuit);

    for (ULONG pinID = 0; pinID < pinCount; pinID++)
    {
        ACXPIN pin = AcxCircuitGetPinById(Circuit, pinID);

        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - pinID %u, pin %p", pinID, pin);

        if (pin != nullptr)
        {
            PCODEC_PIN_CONTEXT pinContext = GetCodecPinContext(pin);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - pinContext %p, terminal ID 0x%02x, entity ID 0x%02x", pinContext, pinContext != nullptr ? pinContext->TerminalID : USBAudioConfiguration::InvalidID, EntityID);
            if ((pinContext != nullptr) && (pinContext->jack != nullptr) && (pinContext->TerminalID == EntityID))
            {
                PDEVICE_CONTEXT deviceContext = GetDeviceContext(pinContext->Device);
                ASSERT(deviceContext != nullptr);

                NS_USBAudio::AUDIO_CHANNEL_CLUSTER_DESCRIPTOR connectorState{};

                status = deviceContext->UsbAudioConfiguration->GetCurrentConnectorState(deviceContext, EntityID, connectorState);

                if (NT_SUCCESS(status))
                {
                    BOOLEAN isConnected = (0 < connectorState.bmChannelConfig) ? TRUE : FALSE;

                    PJACK_CONTEXT jackContext = GetJackContext(pinContext->jack);
                    ASSERT(jackContext != nullptr);

                    if (jackContext->IsConnected ^ isConnected)
                    {
                        jackContext->IsConnected = isConnected;
                        AcxJackChangeStateNotification(pinContext->jack);
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " call AcxJackChangeStateNotification");
                    }
                }
            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Exit %!STATUS!", status);

    return status;
}
