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
#include "AudioIsochronousEngine.h"

#ifndef __INTELLISENSE__
#include "RenderCircuit.tmh"
#endif

#pragma warning(disable : 4127)

//
//  Local function prototypes
//

static ACX_PROPERTY_ITEM s_PropertyItems[] = {
    {
        &KSPROPSETID_LowLatencyAudio,             // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::GetAudioProperty),
        ACX_PROPERTY_ITEM_FLAG_GET,               // ULONG Flags;
        EvtUSBAudioAcxDriverGetAudioProperty,     // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                        // PVOID Reserved;
        0,                                        // ULONG ControlCb;
        sizeof(UAC_AUDIO_PROPERTY),               // ULONG ValueCb;
    },
    {
        &KSPROPSETID_LowLatencyAudio,             // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::GetChannelInfo),
        ACX_PROPERTY_ITEM_FLAG_GET,               // ULONG Flags;
        EvtUSBAudioAcxDriverGetChannelInfo,       // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                        // PVOID Reserved;
        0,                                        // ULONG ControlCb;
        0,                                        // ULONG ValueCb; (variable length)
    },
    {
        &KSPROPSETID_LowLatencyAudio,             // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::GetClockInfo),
        ACX_PROPERTY_ITEM_FLAG_GET,               // ULONG Flags;
        EvtUSBAudioAcxDriverGetClockInfo,         // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                        // PVOID Reserved;
        0,                                        // ULONG ControlCb;
        0,                                        // ULONG ValueCb; (variable length)
    },
    {
        &KSPROPSETID_LowLatencyAudio,             // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::SetClockSource),
        ACX_PROPERTY_ITEM_FLAG_SET,               // ULONG Flags;
        EvtUSBAudioAcxDriverSetClockSource,       // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                        // PVOID Reserved;
        0,                                        // ULONG ControlCb;
        sizeof(UAC_SET_CLOCK_SOURCE_CONTEXT),     // ULONG ValueCb;
    },
    {
        &KSPROPSETID_LowLatencyAudio,             // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::SetSampleFormat),
        ACX_PROPERTY_ITEM_FLAG_SET,               // ULONG Flags;
        EvtUSBAudioAcxDriverSetSampleFormat,      // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                        // PVOID Reserved;
        0,                                        // ULONG ControlCb;
        sizeof(ULONG),                            // ULONG ValueCb;
    },
    {
        &KSPROPSETID_LowLatencyAudio,             // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::ChangeSampleRate),
        ACX_PROPERTY_ITEM_FLAG_SET,               // ULONG Flags;
        EvtUSBAudioAcxDriverChangeSampleRate,     // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                        // PVOID Reserved;
        0,                                        // ULONG ControlCb;
        sizeof(ULONG),                            // ULONG ValueCb;
    },
    {
        &KSPROPSETID_LowLatencyAudio,             // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::GetAsioOwnership),
        ACX_PROPERTY_ITEM_FLAG_SET,               // ULONG Flags;
        EvtUSBAudioAcxDriverGetAsioOwnership,     // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                        // PVOID Reserved;
        0,                                        // ULONG ControlCb;
        0,                                        // ULONG ValueCb;
    },
    {
        &KSPROPSETID_LowLatencyAudio,             // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::StartAsioStream),
        ACX_PROPERTY_ITEM_FLAG_SET,               // ULONG Flags;
        EvtUSBAudioAcxDriverStartAsioStream,      // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                        // PVOID Reserved;
        0,                                        // ULONG ControlCb;
        0,                                        // ULONG ValueCb;
    },
    {
        &KSPROPSETID_LowLatencyAudio,             // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::StopAsioStream),
        ACX_PROPERTY_ITEM_FLAG_SET,               // ULONG Flags;
        EvtUSBAudioAcxDriverStopAsioStream,       // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                        // PVOID Reserved;
        0,                                        // ULONG ControlCb;
        0,                                        // ULONG ValueCb;
    },
    {
        &KSPROPSETID_LowLatencyAudio,             // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::SetAsioBuffer),
        ACX_PROPERTY_ITEM_FLAG_SET,               // ULONG Flags;
        EvtUSBAudioAcxDriverSetAsioBuffer,        // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                        // PVOID Reserved;
        0,                                        // ULONG ControlCb;  (variable length)
        0,                                        // ULONG ValueCb;  (variable length)
    },
    {
        &KSPROPSETID_LowLatencyAudio,             // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::UnsetAsioBuffer),
        ACX_PROPERTY_ITEM_FLAG_SET,               // ULONG Flags;
        EvtUSBAudioAcxDriverUnsetAsioBuffer,      // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                        // PVOID Reserved;
        0,                                        // ULONG ControlCb;
        0,                                        // ULONG ValueCb;
    },
    {
        &KSPROPSETID_LowLatencyAudio,             // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::ReleaseAsioOwnership),
        ACX_PROPERTY_ITEM_FLAG_SET,               // ULONG Flags;
        EvtUSBAudioAcxDriverReleaseAsioOwnership, // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                        // PVOID Reserved;
        0,                                        // ULONG ControlCb;
        0,                                        // ULONG ValueCb;
    },
    {
        &KSPROPSETID_LowLatencyAudio,             // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::GetBufferPeriod),
        ACX_PROPERTY_ITEM_FLAG_GET,               // ULONG Flags;
        EvtUSBAudioAcxDriverGetBufferPeriod,      // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                        // PVOID Reserved;
        0,                                        // ULONG ControlCb;
        sizeof(ULONG),                            // ULONG ValueCb;
    },
    {
        &KSPROPSETID_LowLatencyAudio,             // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::SetBufferPeriod),
        ACX_PROPERTY_ITEM_FLAG_SET,               // ULONG Flags;
        EvtUSBAudioAcxDriverSetBufferPeriod,      // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                        // PVOID Reserved;
        0,                                        // ULONG ControlCb;
        sizeof(ULONG),                            // ULONG ValueCb;
    },
    {
        &KSPROPSETID_LowLatencyAudio,             // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::GetInputLatency),
        ACX_PROPERTY_ITEM_FLAG_GET,               // ULONG Flags;
        EvtUSBAudioAcxDriverGetInputLatency,      // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                        // PVOID Reserved;
        0,                                        // ULONG ControlCb;
        sizeof(LONG),                             // ULONG ValueCb;
    },
    {
        &KSPROPSETID_LowLatencyAudio,             // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::GetOutputLatency),
        ACX_PROPERTY_ITEM_FLAG_GET,               // ULONG Flags;
        EvtUSBAudioAcxDriverGetOutputLatency,     // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                        // PVOID Reserved;
        0,                                        // ULONG ControlCb;
        sizeof(LONG),                             // ULONG ValueCb;
    },
    {
        &KSPROPSETID_LowLatencyAudio,             // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::SetAsioDevice),
        ACX_PROPERTY_ITEM_FLAG_SET,               // ULONG Flags;
        EvtUSBAudioAcxDriverSetAsioDevice,        // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                        // PVOID Reserved;
        0,                                        // ULONG ControlCb;
        0,                                        // ULONG ValueCb;
    },
    {
        &KSPROPSETID_LowLatencyAudio,             // const GUID * Set;
        toInt(KsPropertyUACLowLatencyAudio::GetAsioDevice),
        ACX_PROPERTY_ITEM_FLAG_GET,               // ULONG Flags;
        EvtUSBAudioAcxDriverGetAsioDevice,        // PFN_ACX_OBJECT_PROCESS_REQUEST EvtAcxObjectProcessRequest;
        0,                                        // PVOID Reserved;
        0,                                        // ULONG ControlCb;
        0,                                        // ULONG ValueCb;
    }
};

PAGED_CODE_SEG
_Success_(NT_SUCCESS(return))
static NTSTATUS AddAudioJackToBridgePin(
    _In_ ACXPIN Pin,
    _In_ ULONG  ChannelsCount
)
{
    ACX_JACK_CALLBACKS    jackCallbacks{};
    ACX_JACK_CONFIG       jackCfg{};
    ACXJACK               jack{};
    PJACK_CONTEXT         jackContext = nullptr;
    WDF_OBJECT_ATTRIBUTES attributes{};

    PAGED_CODE();

    ACX_JACK_CALLBACKS_INIT(&jackCallbacks);
    jackCallbacks.EvtAcxJackRetrievePresenceState = EvtJackRetrievePresence;

    ACX_JACK_CONFIG_INIT(&jackCfg);
    jackCfg.Description.ChannelMapping = (ChannelsCount == 1 ? SPEAKER_FRONT_CENTER : SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
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
CodecR_EvtAcxPinRetrieveName(
    ACXPIN          Pin,
    PUNICODE_STRING Name
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
        RETURN_NTSTATUS_IF_FAILED(pinContext->AudioIsochronousEngine->GetChannelName(false, pinContext->Channel, memory, channelName));
    }
    else
    {
        RETURN_NTSTATUS_IF_FAILED(pinContext->AudioIsochronousEngine->GetStereoChannelName(false, pinContext->Channel, memory, channelName));
    }
    RtlInitUnicodeString(&retrievedName, channelName);

    *Name = retrievedName;

    WdfObjectDelete(memory);
    memory = nullptr;
    channelName = nullptr;

    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");

    return status;
}

PAGED_CODE_SEG
VOID CodecR_EvtCircuitCleanup(
    _In_ WDFOBJECT Object
)
{
    PCODEC_RENDER_CIRCUIT_CONTEXT circuitContext;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");

    ACXCIRCUIT circuit = (ACXCIRCUIT)Object;

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
    _In_ WDFDEVICE                Device,
    _In_ const GUID *             ComponentGuid,
    _In_ const UNICODE_STRING *   CircuitName,
    _In_ AudioIsochronousEngine * AudioIsochronousEngine

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

    For example, if the Circuit name is "RenderDevice000", the path to the device interface for this Circuit would be:
    "\\?\usb#vid_0499&pid_1509#5&3821233e&0&11#{6994ad04-93ef-11d0-a3cc-00a0c9223196}\RenderDevice000"

Return Value:

    NTSTATUS

--*/
{
    NTSTATUS   status = STATUS_SUCCESS;
    ACXCIRCUIT renderCircuit = nullptr;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");

    //
    // Create a render circuit associated with this child device.
    //
    RETURN_NTSTATUS_IF_FAILED(CodecR_CreateRenderCircuit(Device, ComponentGuid, CircuitName, AudioIsochronousEngine, AudioIsochronousEngine->GetAudioStreamPropertySet().AudioProperty.SupportedSampleRate /* & GetSampleRateMask(AudioIsochronousEngine->GetAudioStreamPropertySet().AudioProperty.SampleRate) */, &renderCircuit));

    AudioIsochronousEngine->SetRenderCircuit(renderCircuit);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
CodecR_CreateRenderCircuit(
    WDFDEVICE                Device,
    const GUID *             ComponentGuid,
    const UNICODE_STRING *   CircuitName,
    AudioIsochronousEngine * AudioIsochronousEngine,
    const ULONG              SupportedSampleRate,
    ACXCIRCUIT *             Circuit
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
    WDF_OBJECT_ATTRIBUTES          attributes{};
    ACXCIRCUIT                     circuit;
    CODEC_RENDER_CIRCUIT_CONTEXT * circuitContext;
    WDFMEMORY                      pinsMemory = nullptr;
    ACXPIN *                       pins = nullptr;
    WDFMEMORY                      elementsMemory = nullptr;
    ACXELEMENT *                   elements = nullptr;
    WDFMEMORY                      connectionsMemory = nullptr;
    ACX_CONNECTION *               connections = nullptr;
    UCHAR                          numOfChannels = 0;
    USHORT                         terminalType = 0;
    UCHAR                          terminalLink = USBAudioConfiguration::InvalidID;
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

        if (connectionsMemory != nullptr)
        {
            WdfObjectDelete(connectionsMemory);
            connectionsMemory = nullptr;
            connections = nullptr;
        }
    });

    deviceContext = GetDeviceContext(Device);
    ASSERT(deviceContext != nullptr);

    RETURN_NTSTATUS_IF_FAILED(AudioIsochronousEngine->GetStreamChannelInfoAdjusted(false, numOfChannels, terminalType, terminalLink, volumeUnitID, muteUnitID));
    RETURN_NTSTATUS_IF_FAILED(AudioIsochronousEngine->GetStreamDevicesAdjusted(false, numOfDevices));
    numOfRemainingChannels = numOfChannels;

    if (!deviceContext->UsbAudioConfiguration->IsEnableFeatureUnit(false))
    {
        volumeUnitID = muteUnitID = USBAudioConfiguration::InvalidID;
    }

    USBAudioDataFormatManager * usbAudioDataFormatManager = AudioIsochronousEngine->GetUSBAudioDataFormatManager(false);
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
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = Device;

    RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(ACX_CONNECTION) * numOfConnections, &connectionsMemory, nullptr));
    connections = (ACX_CONNECTION *)WdfMemoryGetBuffer(connectionsMemory, nullptr);
    RtlZeroMemory(connections, sizeof(ACX_CONNECTION) * numOfConnections);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - num of channels = %u, num of connections = %u, num of devices = %u", numOfChannels, numOfConnections, numOfDevices);

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
        circuitContext->AudioIsochronousEngine = AudioIsochronousEngine;

        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = circuit;
        RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(ACXVOLUME) * numOfDevices, &(circuitContext->VolumeElementsMemory), (PVOID *)&(circuitContext->VolumeElements)));
        RtlZeroMemory(circuitContext->VolumeElements, sizeof(ACXVOLUME) * numOfDevices);

        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = circuit;
        RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(ACXMUTE) * numOfDevices, &(circuitContext->MuteElementsMemory), (PVOID *)&(circuitContext->MuteElements)));
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

        if ((numOfRemainingChannels > 2) && deviceContext->UsbAudioConfiguration->IsDeviceSplittable())
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
                RETURN_NTSTATUS_IF_FAILED(Codec_CreateVolumeElement(Device, circuit, volumeUnitID, &KSAUDFNAME_VOLUME_CONTROL, numOfChannelsPerDevice, elements[elementIndex]));

                //
                // Saving the volume elements in the circuit context.
                //
                circuitContext->VolumeElements[index] = (ACXVOLUME)elements[elementIndex];

                elementIndex++;
            }

            if (muteUnitID != USBAudioConfiguration::InvalidID)
            { // Mute Enable
                RETURN_NTSTATUS_IF_FAILED(Codec_CreateMuteElement(Device, circuit, muteUnitID, &KSAUDFNAME_WAVE_MUTE, numOfChannelsPerDevice, elements[elementIndex]));

                //
                // Saving the mute elements in the circuit context.
                //
                circuitContext->MuteElements[index] = (ACXMUTE)elements[elementIndex];

                elementIndex++;
            }
        }

        ///////////////////////////////////////////////////////////
        //
        // Create the pins for the circuit.
        //

        ///////////////////////////////////////////////////////////
        //
        // Create Render Pin.
        //
        RETURN_NTSTATUS_IF_FAILED(Codec_CreateRenderPin(AudioIsochronousEngine, Device, circuit, pins[index * CodecRenderPinCount + CodecRenderHostPin], index * CodecRenderPinCount + CodecRenderHostPin, index, index * 2, numOfChannelsPerDevice));

        ///////////////////////////////////////////////////////////
        //
        // Create Device Bridge Pin.
        //
        RETURN_NTSTATUS_IF_FAILED(Codec_CreateBridgePin(AudioIsochronousEngine, Device, circuit, pins[index * CodecRenderPinCount + CodecRenderBridgePin], index * CodecRenderPinCount + CodecRenderBridgePin, index, index * 2, numOfChannelsPerDevice, terminalType, terminalLink));

        ///////////////////////////////////////////////////////////
        //
        // Add audio jack to bridge pin.
        // For more information on audio jack see: https://docs.microsoft.com/en-us/windows/win32/api/devicetopology/ns-devicetopology-ksjack_description
        //
        RETURN_NTSTATUS_IF_FAILED(AddAudioJackToBridgePin(pins[index * CodecRenderPinCount + CodecRenderBridgePin], numOfChannelsPerDevice));

        if (AudioIsochronousEngine->HasOutputIsochronousInterface())
        {
            RETURN_NTSTATUS_IF_FAILED(Codec_AllocateSupportedFormats(Device, pins[index * CodecRenderPinCount + CodecRenderHostPin], circuit, SupportedSampleRate, numOfChannelsPerDevice, usbAudioDataFormatManager));
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
    _In_ ACXCIRCUIT /* Circuit */,
    _In_ WDF_POWER_DEVICE_STATE /* PreviousState */
)
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");

    return STATUS_SUCCESS;
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
    WDF_OBJECT_ATTRIBUTES          attributes{};
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

    ASSERT(Circuit != nullptr);
    circuitContext = GetRenderCircuitContext(Circuit);
    ASSERT(circuitContext);
    ASSERT(circuitContext->AudioIsochronousEngine);

    deviceContext = GetDeviceContext(Device);
    ASSERT(deviceContext != nullptr);

    renderDeviceContext = GetRenderDeviceContext(Device);
    ASSERT(renderDeviceContext != nullptr);
    UNREFERENCED_PARAMETER(renderDeviceContext);

    pinContext = GetCodecPinContext(Pin);
    codecPinType = pinContext->CodecPinType;

    pinType = AcxPinGetType(Pin);

    if (circuitContext->AudioIsochronousEngine->HasAsioOwnership())
    {
        ACXDATAFORMAT dataFormat = nullptr;
        status = circuitContext->AudioIsochronousEngine->GetCurrentDataFormat(false, dataFormat);
        RETURN_NTSTATUS_IF_FAILED(status);

        ACXDATAFORMAT stereoDataFormat;
        RETURN_NTSTATUS_IF_FAILED(SplitAcxDataFormatByDeviceChannels(Device, Circuit, pinContext->NumOfChannelsPerDevice, stereoDataFormat, dataFormat));

        if (!AcxDataFormatIsEqual(stereoDataFormat, StreamFormat))
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
    renderStreamEngine = new (POOL_FLAG_NON_PAGED, DRIVER_TAG) CRenderStreamEngine(deviceContext, circuitContext->AudioIsochronousEngine, stream, StreamFormat, pinContext->DeviceIndex, pinContext->Channel, pinContext->NumOfChannelsPerDevice, FALSE /* , nullptr */);
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

    ULONG pinCount = AcxCircuitGetPinsCount(Circuit);

    for (ULONG pinID = 0; pinID < pinCount; pinID++)
    {
        ACXPIN pin = AcxCircuitGetPinById(Circuit, pinID);

        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - pinID %u, pin %p", pinID, pin);

        if (pin != nullptr)
        {
            PCODEC_PIN_CONTEXT pinContext = GetCodecPinContext(pin);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - pinContext %p, terminal ID 0x%02x, entity ID 0x%02x", pinContext, pinContext != nullptr ? pinContext->TerminalID : USBAudioConfiguration::InvalidID, EntityID);
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
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " call AcxJackChangeStateNotification");
                    }
                }
            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit %!STATUS!", status);

    return status;
}
