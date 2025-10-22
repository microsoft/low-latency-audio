// Copyright (c) Microsoft Corporation.
// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

/*++

Module Name:

    CircuitHelper.cpp

Abstract:

   This module contains helper functions for circuits.

Environment:

    Kernel mode

--*/

#include "Private.h"
#include "Public.h"
#include "CircuitHelper.h"
#include "USBAudio.h"

#ifndef __INTELLISENSE__
#include "CircuitHelper.tmh"
#endif

#if !defined(STATIC_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
#define STATIC_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT \
    DEFINE_WAVEFORMATEX_GUID(WAVE_FORMAT_IEEE_FLOAT)
DEFINE_GUIDSTRUCT("00000003-0000-0010-8000-00aa00389b71", KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
#define KSDATAFORMAT_SUBTYPE_IEEE_FLOAT DEFINE_GUIDNAMED(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
#endif

const ULONG _DSP_STREAM_PROPERTY_UI4_VALUE = 1;
const ULONG c_SampleRateList[] = {
    11025, 22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000, 705600, 768000
};
const ULONG c_SampleRateCount = SIZEOF_ARRAY(c_SampleRateList);

PAGED_CODE_SEG
NTSTATUS AllocateFormat(
    _In_ KSDATAFORMAT_WAVEFORMATEXTENSIBLE * WaveFormat,
    _In_ ACXCIRCUIT                          Circuit,
    _In_ WDFDEVICE                           Device,
    _Out_ ACXDATAFORMAT *                    Format
)
{
    PAGED_CODE();

    NTSTATUS status = STATUS_SUCCESS;

    RETURN_NTSTATUS_IF_TRUE(WaveFormat == nullptr, STATUS_INVALID_PARAMETER);

    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

    ACX_DATAFORMAT_CONFIG formatCfg;
    ACX_DATAFORMAT_CONFIG_INIT_KS(&formatCfg, WaveFormat);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, FORMAT_CONTEXT);
    attributes.ParentObject = Circuit;

    //
    // Creates an ACXDATAFORMAT handle for the given wave format.
    //
    RETURN_NTSTATUS_IF_FAILED(AcxDataFormatCreate(Device, &attributes, &formatCfg, Format));

    ASSERT((*Format) != nullptr);
    FORMAT_CONTEXT * formatContext;
    formatContext = GetFormatContext(*Format);
    ASSERT(formatContext);
    UNREFERENCED_PARAMETER(formatContext);

    return status;
}

struct AFX_FIND_KSATTRIBUTE_BY_ID
{
    const _GUID * Id;
    ULONG         Size;
    PKSATTRIBUTE  Attribute;
};

PAGED_CODE_SEG
NTSTATUS
EvtJackRetrievePresence(
    _In_          ACXJACK /* Jack */,
    _In_ PBOOLEAN IsConnected
)
{
    PAGED_CODE();

    NTSTATUS status = STATUS_SUCCESS;

    //
    // Because this is a sample we always return true (jack is present). A real driver should check
    // if the device is actually present before returning true.
    //
    *IsConnected = true;

    return status;
}

PAGED_CODE_SEG
NTSTATUS
CreateAudioJack(
    _In_ ULONG                    ChannelMapping,
    _In_ ULONG                    Color,
    _In_ ACX_JACK_CONNECTION_TYPE ConnectionType,
    _In_ ACX_JACK_GEO_LOCATION    GeoLocation,
    _In_ ACX_JACK_GEN_LOCATION    GenLocation,
    _In_ ACX_JACK_PORT_CONNECTION PortConnection,
    _In_ ULONG                    Flags,
    _In_ ACXPIN                   BridgePin
)
{
    PAGED_CODE();

    NTSTATUS           status = STATUS_SUCCESS;
    ACX_JACK_CONFIG    jackCfg;
    ACXJACK            jack;
    PJACK_CONTEXT      jackContext;
    ACX_JACK_CALLBACKS jackCallbacks;

    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

    ACX_JACK_CONFIG_INIT(&jackCfg);
    jackCfg.Description.ChannelMapping = ChannelMapping;
    jackCfg.Description.Color = Color;
    jackCfg.Description.ConnectionType = ConnectionType;
    jackCfg.Description.GeoLocation = GeoLocation;
    jackCfg.Description.GenLocation = GenLocation;
    jackCfg.Description.PortConnection = PortConnection;
    jackCfg.Flags = Flags;

    ACX_JACK_CALLBACKS_INIT(&jackCallbacks);
    jackCallbacks.EvtAcxJackRetrievePresenceState = EvtJackRetrievePresence;
    jackCfg.Callbacks = &jackCallbacks;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, JACK_CONTEXT);
    attributes.ParentObject = BridgePin;

    status = AcxJackCreate(BridgePin, &attributes, &jackCfg, &jack);
    if (!NT_SUCCESS(status))
    {
        goto exit;
    }

    ASSERT(jack != nullptr);

    jackContext = GetJackContext(jack);
    ASSERT(jackContext);
    jackContext->Dummy = 0;

    status = AcxPinAddJacks(BridgePin, &jack, 1);

exit:
    return status;
}

PAGED_CODE_SEG
VOID CpuResourcesCallbackHelper(
    _In_ WDFOBJECT  Object,
    _In_ WDFREQUEST Request,
    _In_ ACXELEMENT Element
)
{
    NTSTATUS               ntStatus = STATUS_NOT_SUPPORTED;
    ULONG_PTR              outDataCb = 0;
    ACX_REQUEST_PARAMETERS params;
    ULONG                  minSize = sizeof(ULONG);

    PAGED_CODE();

    ACX_REQUEST_PARAMETERS_INIT(&params);
    AcxRequestGetParameters(Request, &params);

    if ((params.Type != AcxRequestTypeProperty) ||
        (params.Parameters.Property.ItemType != AcxItemTypeElement))
    {
        // Return to acx
        (VOID) AcxCircuitDispatchAcxRequest((ACXCIRCUIT)Object, Request);
        Request = nullptr;
        goto exit;
    }

    if (Element == nullptr)
    {
        ntStatus = STATUS_NOT_SUPPORTED;
        goto exit;
    }

    ULONG elementId = params.Parameters.Property.ItemId;
    ULONG currentElementId = AcxElementGetId(Element);
    ULONG valueCb = params.Parameters.Property.ValueCb;

    if (valueCb != 0)
    {
        if (params.Parameters.Property.Value == nullptr)
        {
            ntStatus = STATUS_BUFFER_TOO_SMALL;
            goto exit;
        }
    }

    //
    // Check to see if the current node is the peakmeter node, if not then return the call to ACX
    //
    if (elementId != currentElementId)
    {
        (VOID) AcxCircuitDispatchAcxRequest((ACXCIRCUIT)Object, Request);
        Request = nullptr;
        goto exit;
    }

    if (params.Parameters.Property.Verb == AcxPropertyVerbGet)
    {

        if (valueCb == 0)
        {
            outDataCb = minSize;
            ntStatus = STATUS_BUFFER_OVERFLOW;
            goto exit;
        }
        else if (valueCb < minSize)
        {
            outDataCb = 0;
            ntStatus = STATUS_BUFFER_TOO_SMALL;
            goto exit;
        }
        else
        {
            *((PULONG)params.Parameters.Property.Value) = KSAUDIO_CPU_RESOURCES_NOT_HOST_CPU;
            params.Parameters.Property.ValueCb = sizeof(ULONG);
            outDataCb = params.Parameters.Property.ValueCb;
            ntStatus = STATUS_SUCCESS;
        }
    }
    else if (params.Parameters.Property.Verb == AcxPropertyVerbBasicSupport)
    {
        if ((valueCb != sizeof(ULONG)) && (valueCb != sizeof(KSPROPERTY_DESCRIPTION)))
        {
            outDataCb = minSize;
            ntStatus = STATUS_BUFFER_OVERFLOW;
            goto exit;
        }

        if (valueCb >= sizeof(KSPROPERTY_DESCRIPTION))
        {
            // if return buffer can hold a KSPROPERTY_DESCRIPTION, return it
            //
            PKSPROPERTY_DESCRIPTION PropDesc = (PKSPROPERTY_DESCRIPTION)params.Parameters.Property.Value;

            PropDesc->AccessFlags = KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_GET;
            PropDesc->DescriptionSize = sizeof(KSPROPERTY_DESCRIPTION);
            PropDesc->PropTypeSet.Set = KSPROPTYPESETID_General;
            PropDesc->PropTypeSet.Id = VT_UI4;
            PropDesc->PropTypeSet.Flags = 0;
            PropDesc->MembersListCount = 0;
            PropDesc->Reserved = 0;
            outDataCb = sizeof(KSPROPERTY_DESCRIPTION);
            ntStatus = STATUS_SUCCESS;
        }
        else if (valueCb >= sizeof(ULONG))
        {
            // if return buffer can hold a ULONG, return the access flags
            //
            *((PULONG)params.Parameters.Property.Value) = KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_GET;
            outDataCb = minSize;
            ntStatus = STATUS_SUCCESS;
        }
        else if (valueCb > 0)
        {
            outDataCb = 0;
            ntStatus = STATUS_BUFFER_TOO_SMALL;
        }
        else
        {
            outDataCb = minSize;
            ntStatus = STATUS_BUFFER_OVERFLOW;
        }
    }
    else
    {
        //
        // Just give it back to ACX. After this call the request is gone.
        //
        (VOID) AcxCircuitDispatchAcxRequest((ACXCIRCUIT)Object, Request);
        Request = nullptr;
        goto exit;
    }

exit:
    if (Request != nullptr)
    {
        WdfRequestCompleteWithInformation(Request, ntStatus, outDataCb);
    }
} // EvtAudioCpuResourcesCallback

PAGED_CODE_SEG
ULONG GetSampleRateFromIndex(_In_ ULONG Index)
{
    PAGED_CODE();

    ASSERT(Index < c_SampleRateCount);
    if (Index < c_SampleRateCount)
    {
        return c_SampleRateList[Index];
    }
    else
    {
        return 0; // Returns 0 to indicate an error.
    }
}

PAGED_CODE_SEG
NTSTATUS GetSampleRateMask(
    _In_ ULONG SampleRate
)
{
    PAGED_CODE();

    ULONG sampleRateMask = 0;
    for (ULONG frameRateListIndex = 0; frameRateListIndex < c_SampleRateCount; ++frameRateListIndex)
    {
        if (SampleRate == c_SampleRateList[frameRateListIndex])
        {
            sampleRateMask = 1 << frameRateListIndex;
        }
    }
    ASSERT(sampleRateMask != 0);

    return sampleRateMask;
}

PAGED_CODE_SEG
const GUID * ConvertTerminalType(
    _In_ USHORT TerminalType
)
{
    const GUID * pinCategory = nullptr;

    PAGED_CODE();

    switch (TerminalType)
    {
    case NS_USBAudio0200::MICROPHONE:
        pinCategory = &KSNODETYPE_MICROPHONE;
        break;
    case NS_USBAudio0200::DESKTOP_MICROPHONE:
        pinCategory = &KSNODETYPE_DESKTOP_MICROPHONE;
        break;
    case NS_USBAudio0200::PERSONAL_MICROPHONE:
        pinCategory = &KSNODETYPE_PERSONAL_MICROPHONE;
        break;
    case NS_USBAudio0200::OMNI_DIRECTIONAL_MICROPHONE:
        pinCategory = &KSNODETYPE_OMNI_DIRECTIONAL_MICROPHONE;
        break;
    case NS_USBAudio0200::MICROPHONE_ARRAY:
        pinCategory = &KSNODETYPE_MICROPHONE_ARRAY;
        break;
    case NS_USBAudio0200::PROCESSING_MICROPHONE_ARRAY:
        pinCategory = &KSNODETYPE_PROCESSING_MICROPHONE_ARRAY;
        break;
    case NS_USBAudio0200::SPEAKER:
        pinCategory = &KSNODETYPE_SPEAKER;
        break;
    case NS_USBAudio0200::HEADPHONES:
        pinCategory = &KSNODETYPE_HEADPHONES;
        break;
    case NS_USBAudio0200::HEAD_MOUNTED_DISPLAY_AUDIO:
        pinCategory = &KSNODETYPE_HEAD_MOUNTED_DISPLAY_AUDIO;
        break;
    case NS_USBAudio0200::DESKTOP_SPEAKER:
        pinCategory = &KSNODETYPE_DESKTOP_SPEAKER;
        break;
    case NS_USBAudio0200::ROOM_SPEAKER:
        pinCategory = &KSNODETYPE_ROOM_SPEAKER;
        break;
    case NS_USBAudio0200::COMMUNICATION_SPEAKER:
        pinCategory = &KSNODETYPE_COMMUNICATION_SPEAKER;
        break;
    case NS_USBAudio0200::LOW_FREQUENCY_EFFECTS_SPEAKER:
        pinCategory = &KSNODETYPE_LOW_FREQUENCY_EFFECTS_SPEAKER;
        break;
    case NS_USBAudio0200::HANDSET:
        pinCategory = &KSNODETYPE_HANDSET;
        break;
    case NS_USBAudio0200::HEADSET:
        pinCategory = &KSNODETYPE_HEADSET;
        break;
    case NS_USBAudio0200::SPEAKERPHONE_NO_ECHO_REDUCTION:
        pinCategory = &KSNODETYPE_SPEAKERPHONE_NO_ECHO_REDUCTION;
        break;
    case NS_USBAudio0200::ECHO_SUPPRESSING_SPEAKERPHONE:
        pinCategory = &KSNODETYPE_ECHO_SUPPRESSING_SPEAKERPHONE;
        break;
    case NS_USBAudio0200::ECHO_CANCELING_SPEAKERPHONE:
        pinCategory = &KSNODETYPE_ECHO_CANCELING_SPEAKERPHONE;
        break;
    case NS_USBAudio0200::PHONE_LINE:
        pinCategory = &KSNODETYPE_PHONE_LINE;
        break;
    case NS_USBAudio0200::TELEPHONE:
        pinCategory = &KSNODETYPE_TELEPHONE;
        break;
    case NS_USBAudio0200::DOWN_LINE_PHONE:
        pinCategory = &KSNODETYPE_DOWN_LINE_PHONE;
        break;
    case NS_USBAudio0200::ANALOG_CONNECTOR:
        pinCategory = &KSNODETYPE_ANALOG_CONNECTOR;
        break;
    case NS_USBAudio0200::DIGITAL_AUDIO_INTERFACE:
        pinCategory = &KSNODETYPE_DIGITAL_AUDIO_INTERFACE;
        break;
    case NS_USBAudio0200::LINE_CONNECTOR:
        pinCategory = &KSNODETYPE_LINE_CONNECTOR;
        break;
    case NS_USBAudio0200::LEGACY_AUDIO_CONNECTOR:
        pinCategory = &KSNODETYPE_LEGACY_AUDIO_CONNECTOR;
        break;
    case NS_USBAudio0200::SPDIF_INTERFACE:
        pinCategory = &KSNODETYPE_SPDIF_INTERFACE;
        break;
    case NS_USBAudio0200::_1394_DA_STREAM:
        pinCategory = &KSNODETYPE_1394_DA_STREAM;
        break;
    case NS_USBAudio0200::_1394_DV_STREAM_SOUNDTRACK:
        pinCategory = &KSNODETYPE_1394_DV_STREAM_SOUNDTRACK;
        break;
    default:
    case NS_USBAudio0200::ADAT_LIGHTPIPE:
    case NS_USBAudio0200::TDIF:
    case NS_USBAudio0200::MADI:
        pinCategory = &KSNODETYPE_LINE_CONNECTOR;
        break;
    }

    return pinCategory;
}

PAGED_CODE_SEG
const GUID * ConvertAudioDataFormat(
    _In_ ULONG FormatType,
    _In_ ULONG Format
)
{
    const GUID * ksDataFormatSubType = nullptr;

    PAGED_CODE();

    switch (FormatType)
    {
    case NS_USBAudio0200::FORMAT_TYPE_I:
        switch (Format)
        {
        case NS_USBAudio0200::PCM:
            ksDataFormatSubType = &KSDATAFORMAT_SUBTYPE_PCM;
            break;
        case NS_USBAudio0200::PCM8:
            // TBD
            break;
        case NS_USBAudio0200::IEEE_FLOAT:
            ksDataFormatSubType = &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
            break;
        default:
            break;
        }
        break;
    case NS_USBAudio0200::FORMAT_TYPE_III:
        switch (Format)
        {
        case NS_USBAudio0200::IEC61937_AC_3:
            ksDataFormatSubType = &KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_DIGITAL;
            break;
        case NS_USBAudio0200::IEC61937_MPEG_2_AAC_ADTS:
            ksDataFormatSubType = &KSDATAFORMAT_SUBTYPE_IEC61937_AAC;
            break;
        case NS_USBAudio0200::IEC61937_DTS_I:
            ksDataFormatSubType = &KSDATAFORMAT_SUBTYPE_IEC61937_DTS;
            break;
        case NS_USBAudio0200::IEC61937_DTS_II:
            ksDataFormatSubType = &KSDATAFORMAT_SUBTYPE_IEC61937_DTS_HD;
            break;
        case NS_USBAudio0200::IEC61937_DTS_III:
            ksDataFormatSubType = &KSDATAFORMAT_SUBTYPE_IEC61937_DTSX_E1;
            break;
        case NS_USBAudio0200::TYPE_III_WMA:
            ksDataFormatSubType = &KSDATAFORMAT_SUBTYPE_IEC61937_WMA_PRO;
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }

    return ksDataFormatSubType;
}

PAGED_CODE_SEG
NTSTATUS ConvertAudioDataFormat(
    _In_ const ACXDATAFORMAT & DataFormat,
    _Out_ ULONG &              FormatType,
    _Out_ ULONG &              Format
)
{
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    GUID     ksDataFormatSubType = AcxDataFormatGetSubFormat(DataFormat);

    PAGED_CODE();

    //
    // https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/subformat-guids-for-compressed-audio-formats
    //
    if (IsEqualGUIDAligned(ksDataFormatSubType, KSDATAFORMAT_SUBTYPE_PCM))
    {
        FormatType = NS_USBAudio0200::FORMAT_TYPE_I;
        Format = NS_USBAudio0200::PCM;
        status = STATUS_SUCCESS;
    }
    else if (IsEqualGUIDAligned(ksDataFormatSubType, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
    {
        FormatType = NS_USBAudio0200::FORMAT_TYPE_I;
        Format = NS_USBAudio0200::IEEE_FLOAT;
        status = STATUS_SUCCESS;
    }
    else if (IsEqualGUIDAligned(ksDataFormatSubType, KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_DIGITAL))
    {
        FormatType = NS_USBAudio0200::FORMAT_TYPE_III;
        Format = NS_USBAudio0200::IEC61937_AC_3;
        status = STATUS_SUCCESS;
    }
    else if (IsEqualGUIDAligned(ksDataFormatSubType, KSDATAFORMAT_SUBTYPE_IEC61937_AAC))
    {
        FormatType = NS_USBAudio0200::FORMAT_TYPE_III;
        Format = NS_USBAudio0200::IEC61937_MPEG_2_AAC_ADTS;
        status = STATUS_SUCCESS;
    }
    else if (IsEqualGUIDAligned(ksDataFormatSubType, KSDATAFORMAT_SUBTYPE_IEC61937_DTS))
    {
        FormatType = NS_USBAudio0200::FORMAT_TYPE_III;
        Format = NS_USBAudio0200::IEC61937_DTS_I;
        status = STATUS_SUCCESS;
    }
    else if (IsEqualGUIDAligned(ksDataFormatSubType, KSDATAFORMAT_SUBTYPE_IEC61937_DTS_HD))
    {
        FormatType = NS_USBAudio0200::FORMAT_TYPE_III;
        Format = NS_USBAudio0200::IEC61937_DTS_II;
        status = STATUS_SUCCESS;
    }
    else if (IsEqualGUIDAligned(ksDataFormatSubType, KSDATAFORMAT_SUBTYPE_IEC61937_DTSX_E1))
    {
        FormatType = NS_USBAudio0200::FORMAT_TYPE_III;
        Format = NS_USBAudio0200::IEC61937_DTS_III;
        status = STATUS_SUCCESS;
    }
    else if (IsEqualGUIDAligned(ksDataFormatSubType, KSDATAFORMAT_SUBTYPE_IEC61937_WMA_PRO))
    {
        FormatType = NS_USBAudio0200::FORMAT_TYPE_III;
        Format = NS_USBAudio0200::TYPE_III_WMA;
        status = STATUS_SUCCESS;
    }

    return status;
}

PAGED_CODE_SEG
NTSTATUS GetChannelsFromMask(
    _In_ DWORD ChannelMask
	)
{
    PAGED_CODE();

    ULONG channels = 0;
    ChannelMask &= ~SPEAKER_RESERVED;

    for (; ChannelMask != 0; ChannelMask >>= 1) {
        if (ChannelMask & 0x01) {
			channels++;
		}
    }

	ASSERT(channels != 0);

	return channels;
}

PAGED_CODE_SEG
NTSTATUS DuplicateAcxDataFormat(
    _In_ WDFDEVICE        Device,
    _In_ WDFOBJECT        ParentObject,
    _Out_ ACXDATAFORMAT & Destination,
    _In_ ACXDATAFORMAT    Source
)
{
    WDF_OBJECT_ATTRIBUTES attributes;

    PAGED_CODE();

    ACX_DATAFORMAT_CONFIG dataFormatConfig;
    ACX_DATAFORMAT_CONFIG_INIT(&dataFormatConfig);

    dataFormatConfig.Type = AcxDataFormatKsFormat;
    dataFormatConfig.u.KsFormat = (PKSDATAFORMAT)AcxDataFormatGetKsDataFormat(Source);
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = ParentObject;
    return AcxDataFormatCreate(Device, &attributes, &dataFormatConfig, &Destination);
}

PAGED_CODE_SEG
NTSTATUS SplitAcxDataFormatByDeviceChannels(
    _In_ WDFDEVICE        Device,
    _In_ ACXCIRCUIT       Circuit,
    _In_ ULONG            NumOfChannelsPerDevice,
    _Out_ ACXDATAFORMAT & Destination,
    _In_ ACXDATAFORMAT    Source
)
{
    KSDATAFORMAT_WAVEFORMATEXTENSIBLE pcmWaveFormatExtensible{};

    PAGED_CODE();

    UCHAR bytesPerSample = (UCHAR)(AcxDataFormatGetBitsPerSample(Source) / 8);
    UCHAR validBits = (UCHAR)AcxDataFormatGetValidBitsPerSample(Source);
    ULONG sampleRate = AcxDataFormatGetSampleRate(Source);

    pcmWaveFormatExtensible.DataFormat.FormatSize = sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE);
    pcmWaveFormatExtensible.DataFormat.MajorFormat = KSDATAFORMAT_TYPE_AUDIO;
    pcmWaveFormatExtensible.DataFormat.SubFormat = AcxDataFormatGetSubFormat(Source);
    pcmWaveFormatExtensible.DataFormat.Specifier = KSDATAFORMAT_SPECIFIER_WAVEFORMATEX;

    //
    // Compressed audio data formats such as IEC61937 are not supported.
    //
    ASSERT(IsEqualGUIDAligned(pcmWaveFormatExtensible.DataFormat.SubFormat, KSDATAFORMAT_SUBTYPE_PCM) || IsEqualGUIDAligned(pcmWaveFormatExtensible.DataFormat.SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT));

    pcmWaveFormatExtensible.WaveFormatExt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    pcmWaveFormatExtensible.WaveFormatExt.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    pcmWaveFormatExtensible.WaveFormatExt.dwChannelMask = (NumOfChannelsPerDevice == 1 ? KSAUDIO_SPEAKER_MONO : KSAUDIO_SPEAKER_STEREO);
    pcmWaveFormatExtensible.WaveFormatExt.SubFormat = AcxDataFormatGetSubFormat(Source);

    pcmWaveFormatExtensible.DataFormat.SampleSize = NumOfChannelsPerDevice * bytesPerSample;
    pcmWaveFormatExtensible.WaveFormatExt.Format.nChannels = static_cast<WORD>(NumOfChannelsPerDevice);
    pcmWaveFormatExtensible.WaveFormatExt.Format.nSamplesPerSec = sampleRate;
    pcmWaveFormatExtensible.WaveFormatExt.Format.nAvgBytesPerSec = NumOfChannelsPerDevice * bytesPerSample * sampleRate;
    pcmWaveFormatExtensible.WaveFormatExt.Format.nBlockAlign = static_cast<WORD>(NumOfChannelsPerDevice * bytesPerSample);
    pcmWaveFormatExtensible.WaveFormatExt.Format.wBitsPerSample = static_cast<WORD>(bytesPerSample * 8);
    pcmWaveFormatExtensible.WaveFormatExt.Samples.wValidBitsPerSample = validBits;

    RETURN_NTSTATUS_IF_FAILED(AllocateFormat(&pcmWaveFormatExtensible, Circuit, Device, &Destination));

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
const char * GetKsDataFormatSubTypeString(
    _In_ GUID     ksDataFormatSubType
	)
{
    PAGED_CODE();

    if (IsEqualGUIDAligned(ksDataFormatSubType, KSDATAFORMAT_SUBTYPE_PCM))
    {
		return "KSDATAFORMAT_SUBTYPE_PCM";
    }
    else if (IsEqualGUIDAligned(ksDataFormatSubType, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
    {
		return "KSDATAFORMAT_SUBTYPE_IEEE_FLOAT";
    }
    else if (IsEqualGUIDAligned(ksDataFormatSubType, KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_DIGITAL))
    {
		return "KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_DIGITAL";
    }
    else if (IsEqualGUIDAligned(ksDataFormatSubType, KSDATAFORMAT_SUBTYPE_IEC61937_AAC))
    {
		return "KSDATAFORMAT_SUBTYPE_IEC61937_AAC";
    }
    else if (IsEqualGUIDAligned(ksDataFormatSubType, KSDATAFORMAT_SUBTYPE_IEC61937_DTS))
    {
		return "KSDATAFORMAT_SUBTYPE_IEC61937_DTS";
    }
    else if (IsEqualGUIDAligned(ksDataFormatSubType, KSDATAFORMAT_SUBTYPE_IEC61937_DTS_HD))
    {
		return "KSDATAFORMAT_SUBTYPE_IEC61937_DTS_HD";
    }
    else if (IsEqualGUIDAligned(ksDataFormatSubType, KSDATAFORMAT_SUBTYPE_IEC61937_DTSX_E1))
    {
		return "KSDATAFORMAT_SUBTYPE_IEC61937_DTSX_E1";
    }
    else if (IsEqualGUIDAligned(ksDataFormatSubType, KSDATAFORMAT_SUBTYPE_IEC61937_WMA_PRO))
    {
		return "KSDATAFORMAT_SUBTYPE_IEC61937_WMA_PRO";
    }
	return "KSDATAFORMAT_SUBTYPE unknown";
}

PAGED_CODE_SEG
void
TraceAcxDataFormat(
    _In_ UCHAR         DebugPrintLevel,
	_In_ ACXDATAFORMAT DataFormat
	)
{
    PAGED_CODE();

    PWAVEFORMATEX                  waveFormatEx = static_cast<PWAVEFORMATEX>(AcxDataFormatGetWaveFormatEx(DataFormat));
    PWAVEFORMATEXTENSIBLE          waveFormatExtensible = static_cast<PWAVEFORMATEXTENSIBLE>(AcxDataFormatGetWaveFormatExtensible(DataFormat));
    PWAVEFORMATEXTENSIBLE_IEC61937 waveFormatExtensibleIEC61937 = static_cast<PWAVEFORMATEXTENSIBLE_IEC61937>(AcxDataFormatGetWaveFormatExtensibleIec61937(DataFormat));

	if (waveFormatExtensibleIEC61937) {
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::wFormatTag      0x%x", waveFormatExtensibleIEC61937->FormatExt.Format.wFormatTag);
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::nChannels       %u",   waveFormatExtensibleIEC61937->FormatExt.Format.nChannels);
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::nSamplesPerSec  %u",   waveFormatExtensibleIEC61937->FormatExt.Format.nSamplesPerSec);
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::nAvgBytesPerSec %u",   waveFormatExtensibleIEC61937->FormatExt.Format.nAvgBytesPerSec);
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::nBlockAlign     %u",   waveFormatExtensibleIEC61937->FormatExt.Format.nBlockAlign);
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::wBitsPerSample  %u",   waveFormatExtensibleIEC61937->FormatExt.Format.wBitsPerSample);
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::cbSize          %u",   waveFormatExtensibleIEC61937->FormatExt.Format.cbSize);
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEXTENSIBLE::Samples.wValidBitsPerSample %u", waveFormatExtensibleIEC61937->FormatExt.Samples.wValidBitsPerSample);
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEXTENSIBLE::dwChannelMask               %u", waveFormatExtensibleIEC61937->FormatExt.dwChannelMask);
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEXTENSIBLE::SubFormat                   %s", GetKsDataFormatSubTypeString(waveFormatExtensibleIEC61937->FormatExt.SubFormat));
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEXTENSIBLE_IEC61937::dwEncodedSamplesPerSec %u",  waveFormatExtensibleIEC61937->dwEncodedSamplesPerSec);
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEXTENSIBLE_IEC61937::dwEncodedChannelCount  %u",  waveFormatExtensibleIEC61937->dwEncodedChannelCount);
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEXTENSIBLE_IEC61937::dwAverageBytesPerSec   %u",  waveFormatExtensibleIEC61937->dwAverageBytesPerSec);
	} else if (waveFormatExtensible) {
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::wFormatTag      0x%x", waveFormatExtensible->Format.wFormatTag);
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::nChannels       %u",   waveFormatExtensible->Format.nChannels);
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::nSamplesPerSec  %u",   waveFormatExtensible->Format.nSamplesPerSec);
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::nAvgBytesPerSec %u",   waveFormatExtensible->Format.nAvgBytesPerSec);
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::nBlockAlign     %u",   waveFormatExtensible->Format.nBlockAlign);
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::wBitsPerSample  %u",   waveFormatExtensible->Format.wBitsPerSample);
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::cbSize          %u",   waveFormatExtensible->Format.cbSize);
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEXTENSIBLE::Samples.wValidBitsPerSample %u", waveFormatExtensible->Samples.wValidBitsPerSample);
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEXTENSIBLE::dwChannelMask               %u", waveFormatExtensible->dwChannelMask);
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEXTENSIBLE::SubFormat                   %s", GetKsDataFormatSubTypeString(waveFormatExtensible->SubFormat));
	} else if (waveFormatEx) {
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::wFormatTag      0x%x", waveFormatEx->wFormatTag);
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::nChannels       %u", waveFormatEx->nChannels);
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::nSamplesPerSec  %u", waveFormatEx->nSamplesPerSec);
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::nAvgBytesPerSec %u", waveFormatEx->nAvgBytesPerSec);
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::nBlockAlign     %u", waveFormatEx->nBlockAlign);
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::wBitsPerSample  %u", waveFormatEx->wBitsPerSample);
		TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::cbSize          %u", waveFormatEx->cbSize);
	}
}
