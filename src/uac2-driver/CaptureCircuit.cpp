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

    CaptureCircuit.cpp

Abstract:

    Capture Circuit. This file contains routines to create and handle
    capture circuit.

Environment:

    Kernel mode

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
#include "CaptureCircuit.tmh"
#endif

#pragma warning(disable : 4127)

//
//  Local function prototypes
//

PAGED_CODE_SEG
NTSTATUS
CodecC_EvtAcxPinSetDataFormat(
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
    NTSTATUS status = STATUS_SUCCESS;
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");

    // NOTE: update device/mixed format here.

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
VOID CodecC_EvtAcxPinDataFormatChangeNotification(
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

PAGED_CODE_SEG
NTSTATUS
CodecC_EvtAcxPinRetrieveName(
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
        RETURN_NTSTATUS_IF_FAILED(deviceContext->UsbAudioConfiguration->GetChannelName(true, pinContext->Channel, memory, channelName));
    }
    else
    {
        RETURN_NTSTATUS_IF_FAILED(deviceContext->UsbAudioConfiguration->GetStereoChannelName(true, pinContext->Channel, memory, channelName));
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
VOID CodecC_EvtPinContextCleanup(
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
VOID CodecC_EvtCircuitCleanup(
    _In_ WDFOBJECT wdfObject
)
{
    PCODEC_CAPTURE_CIRCUIT_CONTEXT circuitContext;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");

    ACXCIRCUIT circuit = (ACXCIRCUIT)wdfObject;

    circuitContext = GetCaptureCircuitContext(circuit);
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

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");
}

PAGED_CODE_SEG
NTSTATUS
CodecC_AddStaticCapture(
    _In_ WDFDEVICE              Device,
    _In_ const GUID *           ComponentGuid,
    _In_ const GUID *           MicCustomName,
    _In_ const UNICODE_STRING * CircuitName
)
/*++

Routine Description:

    Creates the static capture circuit (pictured below)
    and adds it to the device context. This is called
    when a new device is detected and the AddDevice
    call is made by the pnp manager.

    ******************************************************
    * Capture Circuit                                    *
    *                                                    *
    *              +-----------------------+             *
    *              |                       |             *
    *              |    +-------------+    |             *
    * Host  ------>|    | Volume Node |    |---> Bridge  *
    * Pin          |    +-------------+    |      Pin    *
    *              |                       |             *
    *              +-----------------------+             *
    *                                                    *
    ******************************************************

    For example, if the Circuit name is "CaptureDevice0", the path to the device interface for this Circuit would be:
    "\\?\usb#vid_0499&pid_1509#5&3821233e&0&11#{6994ad04-93ef-11d0-a3cc-00a0c9223196}\CaptureDevice0"

Return Value:

    NTSTATUS

--*/
{
    NTSTATUS                status = STATUS_SUCCESS;
    PDEVICE_CONTEXT         deviceContext;
    PCAPTURE_DEVICE_CONTEXT captureDevContext;
    ACXCIRCUIT              captureCircuit = nullptr;
    WDF_OBJECT_ATTRIBUTES   attributes;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");

    deviceContext = GetDeviceContext(Device);
    ASSERT(deviceContext != nullptr);

    //
    // Alloc audio context to current device.
    //
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, CAPTURE_DEVICE_CONTEXT);
    RETURN_NTSTATUS_IF_FAILED(WdfObjectAllocateContext(Device, &attributes, (PVOID *)&captureDevContext));
    ASSERT(captureDevContext);

    //
    // Create a capture circuit associated with this child device.
    //
    RETURN_NTSTATUS_IF_FAILED(CodecC_CreateCaptureCircuit(Device, ComponentGuid, MicCustomName, CircuitName, deviceContext->AudioProperty.SupportedSampleRate /* & GetSampleRateMask(deviceContext->AudioProperty.SampleRate) */, &captureCircuit));

    deviceContext->Capture = captureCircuit;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");

    return status;
}

PAGED_CODE_SEG
NTSTATUS
Capture_AllocateSupportedFormats(
    _In_ WDFDEVICE                   Device,
    _In_ ACXPIN                      Pin,
    _In_ ACXCIRCUIT                  Circuit,
    _In_ const ULONG                 SupportedSampleRate,
    _In_ const ULONG                 Channels,
    _In_ USBAudioDataFormatManager * UsbAudioDataFormatManager
)
{
    NTSTATUS                          status = STATUS_SUCCESS;
    ACXDATAFORMAT                     acxDataFormat{};
    ACXDATAFORMATLIST                 formatList;
    KSDATAFORMAT_WAVEFORMATEXTENSIBLE pcmWaveFormatExtensible{};

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");

    ///////////////////////////////////////////////////////////
    //
    // Define supported formats for the host pin.
    //

    //
    // The raw processing mode list is associated with each single circuit
    // by ACX. A driver uses this DDI to retrieve the built-in raw
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
                UCHAR        bytesPerSample = UsbAudioDataFormatManager->GetBytesPerSample(formatIndex);
                UCHAR        validBits = UsbAudioDataFormatManager->GetValidBits(formatIndex);
                const GUID * ksDataFormatSubType = ConvertAudioDataFormat(UsbAudioDataFormatManager->GetFormatType(formatIndex), UsbAudioDataFormatManager->GetFormat(formatIndex));
                if (ksDataFormatSubType != nullptr)
                {
                    pcmWaveFormatExtensible.DataFormat.FormatSize = sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE);
                    pcmWaveFormatExtensible.DataFormat.MajorFormat = KSDATAFORMAT_TYPE_AUDIO;
                    pcmWaveFormatExtensible.DataFormat.SubFormat = *ksDataFormatSubType;
                    pcmWaveFormatExtensible.DataFormat.Specifier = KSDATAFORMAT_SPECIFIER_WAVEFORMATEX;

                    pcmWaveFormatExtensible.WaveFormatExt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
                    pcmWaveFormatExtensible.WaveFormatExt.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
                    pcmWaveFormatExtensible.WaveFormatExt.dwChannelMask = (Channels == 1 ? KSAUDIO_SPEAKER_MONO : KSAUDIO_SPEAKER_STEREO);
                    pcmWaveFormatExtensible.WaveFormatExt.SubFormat = *ksDataFormatSubType;

                    pcmWaveFormatExtensible.DataFormat.SampleSize = Channels * bytesPerSample;
                    pcmWaveFormatExtensible.WaveFormatExt.Format.nChannels = static_cast<WORD>(Channels);
                    pcmWaveFormatExtensible.WaveFormatExt.Format.nSamplesPerSec = sampleRate;
                    pcmWaveFormatExtensible.WaveFormatExt.Format.nAvgBytesPerSec = Channels * bytesPerSample * sampleRate;
                    pcmWaveFormatExtensible.WaveFormatExt.Format.nBlockAlign = static_cast<WORD>(Channels * bytesPerSample);
                    pcmWaveFormatExtensible.WaveFormatExt.Format.wBitsPerSample = static_cast<WORD>(bytesPerSample * 8);
                    pcmWaveFormatExtensible.WaveFormatExt.Samples.wValidBitsPerSample = validBits;

                    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%u %u %u %u %u %u %u", pcmWaveFormatExtensible.DataFormat.SampleSize, pcmWaveFormatExtensible.WaveFormatExt.Format.nChannels, pcmWaveFormatExtensible.WaveFormatExt.Format.nSamplesPerSec, pcmWaveFormatExtensible.WaveFormatExt.Format.nAvgBytesPerSec, pcmWaveFormatExtensible.WaveFormatExt.Format.nBlockAlign, pcmWaveFormatExtensible.WaveFormatExt.Format.wBitsPerSample, pcmWaveFormatExtensible.WaveFormatExt.Samples.wValidBitsPerSample);

                    RETURN_NTSTATUS_IF_FAILED(AllocateFormat(pcmWaveFormatExtensible, Circuit, Device, &acxDataFormat));
                    //
                    // The driver uses this DDI to add data formats to the raw
                    // processing mode list associated with the current circuit.
                    //
                    RETURN_NTSTATUS_IF_FAILED(AcxDataFormatListAddDataFormat(formatList, acxDataFormat));
                }
            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");

    return status;
}

PAGED_CODE_SEG
NTSTATUS
CodecC_CreateCaptureCircuit(
    _In_ WDFDEVICE    Device,
    _In_ const GUID * ComponentGuid,
    _In_ const GUID * /* MicCustomName */,
    _In_ const UNICODE_STRING * CircuitName,
    _In_ const ULONG            SupportedSampleRate,
    _Out_ ACXCIRCUIT *          Circuit
)
/*++

Routine Description:

    This routine builds the CODEC capture circuit.

Return Value:

    NT status value

--*/
{
    NTSTATUS                        status;
    PDEVICE_CONTEXT                 deviceContext;
    WDF_OBJECT_ATTRIBUTES           attributes;
    ACXCIRCUIT                      circuit;
    CODEC_CAPTURE_CIRCUIT_CONTEXT * circuitContext;
    WDFMEMORY                       pinsMemory = nullptr;
    ACXPIN *                        pins = nullptr;
    WDFMEMORY                       elementsMemory = nullptr;
    ACXELEMENT *                    elements = nullptr;
    ACX_CONNECTION *                connections = nullptr;
    UCHAR                           numOfChannels = 0;
    USHORT                          terminalType = 0;
    UCHAR                           volumeUnitID = USBAudioConfiguration::InvalidID;
    UCHAR                           muteUnitID = USBAudioConfiguration::InvalidID;
    ULONG                           numOfDevices = 0;
    ULONG                           numOfConnections = 0;
    ULONG                           numOfRemainingChannels = 0;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");

    auto createCaptureCircuitScope = wil::scope_exit([&]() {
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

    RETURN_NTSTATUS_IF_FAILED(deviceContext->UsbAudioConfiguration->GetStreamChannelInfo(true, numOfChannels, terminalType, volumeUnitID, muteUnitID));
    RETURN_NTSTATUS_IF_FAILED(deviceContext->UsbAudioConfiguration->GetStreamDevices(true, numOfDevices));
    numOfRemainingChannels = numOfChannels;

	if (numOfChannels == 0) {
		return STATUS_SUCCESS;
	}

    USBAudioDataFormatManager * usbAudioDataFormatManager = deviceContext->UsbAudioConfiguration->GetUSBAudioDataFormatManager(true);
    RETURN_NTSTATUS_IF_TRUE_ACTION(usbAudioDataFormatManager == nullptr, status = STATUS_INVALID_PARAMETER, status);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = Device;
    RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(ACXPIN) * CodecCapturePinCount * numOfDevices, &pinsMemory, (PVOID *)&pins));
    RtlZeroMemory(pins, sizeof(ACXPIN) * CodecCapturePinCount * numOfDevices);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = Device;
    RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(ACXELEMENT) * CaptureElementCount * numOfDevices, &elementsMemory, (PVOID *)&elements));
    RtlZeroMemory(elements, sizeof(ACXELEMENT) * CaptureElementCount * numOfDevices);

    numOfConnections = (CaptureElementCount + 1) * numOfDevices;

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
        // A driver uses this DDI to free the allocated
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
        // circuit type can be AcxCircuitTypeCapture, AcxCircuitTypeCapture,
        // AcxCircuitTypeOther, or AcxCircuitTypeMaximum (for validation).
        //
        AcxCircuitInitSetCircuitType(circuitInit, AcxCircuitTypeCapture);

        //
        // The driver uses this DDI to assign its (if any) power callbacks.
        //
        ACX_CIRCUIT_PNPPOWER_CALLBACKS_INIT(&powerCallbacks);
        powerCallbacks.EvtAcxCircuitPowerUp = CodecC_EvtCircuitPowerUp;
        powerCallbacks.EvtAcxCircuitPowerDown = CodecC_EvtCircuitPowerDown;
        AcxCircuitInitSetAcxCircuitPnpPowerCallbacks(circuitInit, &powerCallbacks);

        //
        // The driver uses this DDI to register for a stream-create callback.
        //
        RETURN_NTSTATUS_IF_FAILED(AcxCircuitInitAssignAcxCreateStreamCallback(circuitInit, CodecC_EvtCircuitCreateStream));

        //
        // The driver uses this DDI to create a new ACX circuit.
        //
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, CODEC_CAPTURE_CIRCUIT_CONTEXT);
        attributes.EvtCleanupCallback = CodecC_EvtCircuitCleanup;
        RETURN_NTSTATUS_IF_FAILED(AcxCircuitCreate(Device, &attributes, &circuitInit, &circuit));

        circuitContext = GetCaptureCircuitContext(circuit);
        ASSERT(circuitContext);

        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = circuit;
        RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(ACXVOLUME) * numOfDevices, &(circuitContext->VolumeElementsMemory), (PVOID *)&(circuitContext->VolumeElements)));
        RtlZeroMemory(circuitContext->VolumeElements, sizeof(ACXELEMENT) * numOfDevices);

        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = circuit;
        RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(ACXMUTE) * numOfDevices, &(circuitContext->MuteElementsMemory), (PVOID *)&(circuitContext->MuteElements)));
        RtlZeroMemory(circuitContext->MuteElements, sizeof(ACXELEMENT) * numOfDevices);

        circuitInitScope.release();
    }

    //
    // Post circuit creation initialization.
    //
    ULONG elementIndex = 0;
    for (ULONG index = 0; index < numOfDevices; index++)
    {
        ULONG numOfChannelsPerDevice;
        if (numOfRemainingChannels > 2)
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
        // Create mute and volume element.
        //
        {
            // If volume control is supported, enable this if-statement accordingly.
            // if (volumeUnitID != USBAudioConfiguration::InvalidID)
            if (false)
            { // Volume Enable

                // If the device is designed to support volume control,
                // the implementation should be added here.

                elementIndex++;
            }

            // If mute control is supported, enable this if-statement accordingly.
            // if (muteUnitID != USBAudioConfiguration::InvalidID)
            if (false)
            { // Mute Enable
                // If the device is designed to support mute control,
                // the implementation should be added here.

                elementIndex++;
            }
        }

        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! here");

        ///////////////////////////////////////////////////////////
        //
        // Create the pins for the circuit.
        //
        {
            ACX_PIN_CALLBACKS   pinCallbacks;
            ACX_PIN_CONFIG      pinCfg;
            CODEC_PIN_CONTEXT * pinContext;

            ///////////////////////////////////////////////////////////
            //
            // Create capture streaming pin.
            //
            ACX_PIN_CONFIG_INIT(&pinCfg);
            pinCfg.Type = AcxPinTypeSource;
            pinCfg.Communication = AcxPinCommunicationSink;
            pinCfg.Category = &KSCATEGORY_AUDIO;

            WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, CODEC_PIN_CONTEXT);
            attributes.EvtCleanupCallback = CodecC_EvtPinContextCleanup;
            attributes.ParentObject = circuit;

            //
            // The driver uses this DDI to create one or more pins on the circuits.
            //
            RETURN_NTSTATUS_IF_FAILED(AcxPinCreate(circuit, &attributes, &pinCfg, &(pins[index * CodecCapturePinCount + CodecCaptureHostPin])));
            ASSERT(pins[index * CodecCapturePinCount + CodecCaptureHostPin] != nullptr);
            pinContext = GetCodecPinContext(pins[index * CodecCapturePinCount + CodecCaptureHostPin]);
            ASSERT(pinContext);
            pinContext->Device = Device;
            pinContext->CodecPinType = CodecPinTypeHost;
            pinContext->DeviceIndex = index;
            pinContext->Channel = index * 2;
            pinContext->NumOfChannelsPerDevice = numOfChannelsPerDevice;

            ///////////////////////////////////////////////////////////
            //
            // Create capture endpoint pin.
            //
            ACX_PIN_CALLBACKS_INIT(&pinCallbacks);
            if (deviceContext->InputChannelNames != USBAudioConfiguration::InvalidString)
            {
                pinCallbacks.EvtAcxPinRetrieveName = CodecC_EvtAcxPinRetrieveName;
            }

            ACX_PIN_CONFIG_INIT(&pinCfg);

            pinCfg.Type = AcxPinTypeSink;
            pinCfg.Communication = AcxPinCommunicationNone;
            pinCfg.Category = ConvertTerminalType(terminalType);
            pinCfg.PinCallbacks = &pinCallbacks;

            WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, CODEC_PIN_CONTEXT);
            attributes.ParentObject = circuit;

            //
            // The driver uses this DDI to create one or more pins on the circuits.
            //
            RETURN_NTSTATUS_IF_FAILED(AcxPinCreate(circuit, &attributes, &pinCfg, &(pins[index * CodecCapturePinCount + CodecCaptureBridgePin])));
            ASSERT(pins[index * CodecCapturePinCount + CodecCaptureBridgePin] != nullptr);
            pinContext = GetCodecPinContext(pins[index * CodecCapturePinCount + CodecCaptureBridgePin]);
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
            ACX_JACK_CONFIG jackCfg;
            ACXJACK         jack;
            PJACK_CONTEXT   jackContext;

            ACX_JACK_CONFIG_INIT(&jackCfg);
            jackCfg.Description.ChannelMapping = (numOfChannelsPerDevice == 1 ? SPEAKER_FRONT_CENTER : SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
            jackCfg.Description.Color = RGB(0, 0, 0);
            jackCfg.Description.ConnectionType = AcxConnTypeAtapiInternal;
            jackCfg.Description.GeoLocation = AcxGeoLocFront;
            jackCfg.Description.GenLocation = AcxGenLocPrimaryBox;
            jackCfg.Description.PortConnection = AcxPortConnIntegratedDevice;

            WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, JACK_CONTEXT);
            attributes.ParentObject = pins[index * CodecCapturePinCount + CodecCaptureBridgePin];

            RETURN_NTSTATUS_IF_FAILED(AcxJackCreate(pins[index * CodecCapturePinCount + CodecCaptureBridgePin], &attributes, &jackCfg, &jack));

            ASSERT(jack != nullptr);

            jackContext = GetJackContext(jack);
            ASSERT(jackContext);
            jackContext->Dummy = 0;

            RETURN_NTSTATUS_IF_FAILED(AcxPinAddJacks(pins[index * CodecCapturePinCount + CodecCaptureBridgePin], &jack, 1));
        }
        RETURN_NTSTATUS_IF_FAILED(Capture_AllocateSupportedFormats(Device, pins[index * CodecCapturePinCount + CodecCaptureHostPin], circuit, SupportedSampleRate, numOfChannelsPerDevice, usbAudioDataFormatManager));
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
    RETURN_NTSTATUS_IF_FAILED(AcxCircuitAddPins(circuit, pins, CodecCapturePinCount * numOfDevices));

    {
        ULONG connectionIndex = 0;
        //                         Circuit layout
        //                 +---------------------------+
        //                 |   +--------+   +------+   |
        //  Bridge Pin -1->|---| volume |---| mute |---|-0-> Host
        //                 |   +--------+   +------+   |
        //                 |       0           1       |
        //                 |                +------+   |
        //  Bridge Pin -3->|----------------| mute |---|-2-> Host
        //                 |                +------+   |
        //                 |                   2       |
        //                 |   +--------+              |
        //  Bridge Pin -5->|---| volume |--------------|-4-> Host
        //                 |   +--------+              |
        //                 |       3                   |
        //                 |                           |
        //  Bridge Pin -7->|---------------------------|-6-> Host
        //                 |                           |
        //                 +---------------------------+
        elementIndex = 0;
        for (UCHAR index = 0; index < numOfDevices; index++)
        {
            if (circuitContext->VolumeElements[index] != nullptr)
            {
                if (circuitContext->MuteElements[index] != nullptr)
                {
                    ACX_CONNECTION_INIT(&connections[connectionIndex], circuit, circuitContext->VolumeElements[index]);
                    connections[connectionIndex].FromPin.Id = index * CodecCapturePinCount + CodecCaptureBridgePin;
                    connectionIndex++;

                    ACX_CONNECTION_INIT(&connections[connectionIndex], circuitContext->VolumeElements[index], circuitContext->MuteElements[index]);
                    connectionIndex++;

                    ACX_CONNECTION_INIT(&connections[connectionIndex], circuitContext->MuteElements[index], circuit);
                    connections[connectionIndex].ToPin.Id = index * CodecCapturePinCount + CodecCaptureHostPin;
                    connectionIndex++;
                }
                else
                {
                    ACX_CONNECTION_INIT(&connections[connectionIndex], circuit, circuitContext->VolumeElements[index]);
                    connections[connectionIndex].FromPin.Id = index * CodecCapturePinCount + CodecCaptureBridgePin;
                    connectionIndex++;

                    ACX_CONNECTION_INIT(&connections[connectionIndex], circuitContext->VolumeElements[index], circuit);
                    connections[connectionIndex].ToPin.Id = index * CodecCapturePinCount + CodecCaptureHostPin;
                    connectionIndex++;
                }
            }
            else
            {
                if (circuitContext->MuteElements[index] != nullptr)
                {
                    ACX_CONNECTION_INIT(&connections[connectionIndex], circuit, circuitContext->MuteElements[index]);
                    connections[connectionIndex].FromPin.Id = index * CodecCapturePinCount + CodecCaptureBridgePin;
                    connectionIndex++;

                    ACX_CONNECTION_INIT(&connections[connectionIndex], circuitContext->MuteElements[index], circuit);
                    connections[connectionIndex].ToPin.Id = (ULONG)(index * CodecCapturePinCount + CodecCaptureHostPin);
                    connectionIndex++;
                }
                else
                {
                    ACX_CONNECTION_INIT(&connections[connectionIndex], circuit, circuit);
                    connections[connectionIndex].FromPin.Id = index * CodecCapturePinCount + CodecCaptureBridgePin;
                    connections[connectionIndex].ToPin.Id = index * CodecCapturePinCount + CodecCaptureHostPin;
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
CodecC_EvtCircuitPowerUp(
    _In_ WDFDEVICE /* Device */,
    _In_ ACXCIRCUIT /* Circuit */,
    _In_ WDF_POWER_DEVICE_STATE /* PreviousState */
)
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
CodecC_EvtCircuitPowerDown(
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
CodecC_EvtCircuitCreateStream(
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
    NTSTATUS                        status;
    PDEVICE_CONTEXT                 deviceContext;
    PCAPTURE_DEVICE_CONTEXT         captureDeviceContext;
    WDF_OBJECT_ATTRIBUTES           attributes;
    ACXSTREAM                       stream;
    STREAMENGINE_CONTEXT *          streamContext;
    ACX_STREAM_CALLBACKS            streamCallbacks;
    ACX_RT_STREAM_CALLBACKS         rtCallbacks;
    CCaptureStreamEngine *          streamEngine = nullptr;
    CODEC_CAPTURE_CIRCUIT_CONTEXT * circuitContext;
    CODEC_PIN_CONTEXT *             pinContext;

    auto streamEngineScope = wil::scope_exit([&streamEngine]() {
        if (streamEngine)
        {
            delete streamEngine;
        }
    });

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry");
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - capture Pin Id %u", AcxPinGetId(Pin));

    // ASSERT(IsEqualGUID(*SignalProcessingMode, AUDIO_SIGNALPROCESSINGMODE_RAW));

    deviceContext = GetDeviceContext(Device);
    ASSERT(deviceContext != nullptr);

    captureDeviceContext = GetCaptureDeviceContext(Device);
    ASSERT(captureDeviceContext != nullptr);

    circuitContext = GetCaptureCircuitContext(Circuit);
    ASSERT(circuitContext != nullptr);

    pinContext = GetCodecPinContext(Pin);
    ASSERT(pinContext != nullptr);

    if (USBAudioAcxDriverHasAsioOwnership(deviceContext))
    {
        ACXDATAFORMAT dataFormat = nullptr;
        status = USBAudioAcxDriverGetCurrentDataFormat(deviceContext, true, dataFormat);
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
    rtCallbacks.EvtAcxStreamGetCapturePacket = CodecC_EvtStreamGetCapturePacket;
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

    streamContext = GetStreamEngineContext(stream);
    ASSERT(streamContext);

    //
    // Create the virtual streaming engine which will control
    // streaming logic for the capture circuit.
    //
    streamEngine = new (POOL_FLAG_NON_PAGED, DRIVER_TAG) CCaptureStreamEngine(deviceContext, stream, StreamFormat, pinContext->DeviceIndex, pinContext->Channel, pinContext->NumOfChannelsPerDevice);
    RETURN_NTSTATUS_IF_TRUE(streamEngine == nullptr, STATUS_INSUFFICIENT_RESOURCES);

    streamContext->StreamEngine = (PVOID)streamEngine;
    streamContext->DeviceIndex = pinContext->DeviceIndex;
    streamContext->Channel = pinContext->Channel;
    streamContext->NumOfChannelsPerDevice = pinContext->NumOfChannelsPerDevice;
    streamEngine = nullptr;

    //
    // Done.
    //
    status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
NTSTATUS
CodecC_EvtStreamGetCapturePacket(
    _In_ ACXSTREAM    Stream,
    _Out_ ULONG *     LastCapturePacket,
    _Out_ ULONGLONG * QPCPacketStart,
    _Out_ BOOLEAN *   MoreData
)
{
    PSTREAMENGINE_CONTEXT  context;
    CCaptureStreamEngine * streamEngine = nullptr;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry, Stream = %p", Stream);

    context = GetStreamEngineContext(Stream);

    streamEngine = static_cast<CCaptureStreamEngine *>(context->StreamEngine);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");

    return streamEngine->GetCapturePacket(LastCapturePacket, QPCPacketStart, MoreData);
}
