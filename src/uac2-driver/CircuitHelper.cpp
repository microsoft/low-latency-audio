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
#include <initguid.h>
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

typedef struct _TERMINALTYPE_TO_PINCATEGORY_ENTRY
{
    const USHORT TerminalType;
    const GUID * PinCategory;
} TERMINALTYPE_TO_PINCATEGORY_ENTRY, *PTERMINALTYPE_TO_PINCATEGORY_ENTRY;

static const TERMINALTYPE_TO_PINCATEGORY_ENTRY s_TerminalTypeToPinCategoryEntries[] = {
    {NS_USBAudio0200::MICROPHONE, &KSNODETYPE_MICROPHONE},
    {NS_USBAudio0200::DESKTOP_MICROPHONE, &KSNODETYPE_DESKTOP_MICROPHONE},
    {NS_USBAudio0200::PERSONAL_MICROPHONE, &KSNODETYPE_PERSONAL_MICROPHONE},
    {NS_USBAudio0200::OMNI_DIRECTIONAL_MICROPHONE, &KSNODETYPE_OMNI_DIRECTIONAL_MICROPHONE},
    {NS_USBAudio0200::MICROPHONE_ARRAY, &KSNODETYPE_MICROPHONE_ARRAY},
    {NS_USBAudio0200::PROCESSING_MICROPHONE_ARRAY, &KSNODETYPE_PROCESSING_MICROPHONE_ARRAY},
    {NS_USBAudio0200::SPEAKER, &KSNODETYPE_SPEAKER},
    {NS_USBAudio0200::HEADPHONES, &KSNODETYPE_HEADPHONES},
    {NS_USBAudio0200::HEAD_MOUNTED_DISPLAY_AUDIO, &KSNODETYPE_HEAD_MOUNTED_DISPLAY_AUDIO},
    {NS_USBAudio0200::DESKTOP_SPEAKER, &KSNODETYPE_DESKTOP_SPEAKER},
    {NS_USBAudio0200::ROOM_SPEAKER, &KSNODETYPE_ROOM_SPEAKER},
    {NS_USBAudio0200::COMMUNICATION_SPEAKER, &KSNODETYPE_COMMUNICATION_SPEAKER},
    {NS_USBAudio0200::LOW_FREQUENCY_EFFECTS_SPEAKER, &KSNODETYPE_LOW_FREQUENCY_EFFECTS_SPEAKER},
    {NS_USBAudio0200::HANDSET, &KSNODETYPE_HANDSET},
    {NS_USBAudio0200::HEADSET, &KSNODETYPE_HEADSET},
    {NS_USBAudio0200::SPEAKERPHONE_NO_ECHO_REDUCTION, &KSNODETYPE_SPEAKERPHONE_NO_ECHO_REDUCTION},
    {NS_USBAudio0200::ECHO_SUPPRESSING_SPEAKERPHONE, &KSNODETYPE_ECHO_SUPPRESSING_SPEAKERPHONE},
    {NS_USBAudio0200::ECHO_CANCELING_SPEAKERPHONE, &KSNODETYPE_ECHO_CANCELING_SPEAKERPHONE},
    {NS_USBAudio0200::PHONE_LINE, &KSNODETYPE_PHONE_LINE},
    {NS_USBAudio0200::TELEPHONE, &KSNODETYPE_TELEPHONE},
    {NS_USBAudio0200::DOWN_LINE_PHONE, &KSNODETYPE_DOWN_LINE_PHONE},
    {NS_USBAudio0200::ANALOG_CONNECTOR, &KSNODETYPE_ANALOG_CONNECTOR},
    {NS_USBAudio0200::DIGITAL_AUDIO_INTERFACE, &KSNODETYPE_DIGITAL_AUDIO_INTERFACE},
    {NS_USBAudio0200::LINE_CONNECTOR, &KSNODETYPE_LINE_CONNECTOR},
    {NS_USBAudio0200::LEGACY_AUDIO_CONNECTOR, &KSNODETYPE_LEGACY_AUDIO_CONNECTOR},
    {NS_USBAudio0200::SPDIF_INTERFACE, &KSNODETYPE_SPDIF_INTERFACE},
    {NS_USBAudio0200::_1394_DA_STREAM, &KSNODETYPE_1394_DA_STREAM},
    {NS_USBAudio0200::_1394_DV_STREAM_SOUNDTRACK, &KSNODETYPE_1394_DV_STREAM_SOUNDTRACK},
    {NS_USBAudio0200::EMBEDDED_UNDEFINED, &KSNODETYPE_EMBEDDED_UNDEFINED},
    {NS_USBAudio0200::LEVEL_CALIBRATION_NOISE_SOURCE, &KSNODETYPE_LEVEL_CALIBRATION_NOISE_SOURCE},
    {NS_USBAudio0200::EQUALIZATION_NOISE, &KSNODETYPE_EQUALIZATION_NOISE},
    {NS_USBAudio0200::CD_PLAYER, &KSNODETYPE_CD_PLAYER},
    {NS_USBAudio0200::DAT, &KSNODETYPE_DAT_IO_DIGITAL_AUDIO_TAPE},
    {NS_USBAudio0200::DCC, &KSNODETYPE_DCC_IO_DIGITAL_COMPACT_CASSETTE},
    {NS_USBAudio0200::COMPRESSED_AUDIO_PLAYER, &KSNODETYPE_MINIDISK},
    {NS_USBAudio0200::ANALOG_TAPE, &KSNODETYPE_ANALOG_TAPE},
    {NS_USBAudio0200::PHONOGRAPH, &KSNODETYPE_PHONOGRAPH},
    {NS_USBAudio0200::VCR_AUDIO, &KSNODETYPE_VCR_AUDIO},
    {NS_USBAudio0200::VIDEO_DISC_AUDIO, &KSNODETYPE_VIDEO_DISC_AUDIO},
    {NS_USBAudio0200::DVD_AUDIO, &KSNODETYPE_DVD_AUDIO},
    {NS_USBAudio0200::TV_TUNER_AUDIO, &KSNODETYPE_TV_TUNER_AUDIO},
    {NS_USBAudio0200::SATELLITE_RECEIVER_AUDIO, &KSNODETYPE_SATELLITE_RECEIVER_AUDIO},
    {NS_USBAudio0200::CABLE_TUNER_AUDIO, &KSNODETYPE_CABLE_TUNER_AUDIO},
    {NS_USBAudio0200::DSS_AUDIO, &KSNODETYPE_DSS_AUDIO},
    {NS_USBAudio0200::RADIO_RECEIVER, &KSNODETYPE_RADIO_RECEIVER},
    {NS_USBAudio0200::RADIO_TRANSMITTER, &KSNODETYPE_RADIO_TRANSMITTER},
    {NS_USBAudio0200::MULTITRACK_RECORDER, &KSNODETYPE_MULTITRACK_RECORDER},
    {NS_USBAudio0200::SYNTHESIZER, &KSNODETYPE_SYNTHESIZER},
    // {  NS_USBAudio0200::ADAT_LIGHTPIPE, nullptr },
    // {  NS_USBAudio0200::TDIF, nullptr },
    // {  NS_USBAudio0200::MADI, nullptr },
    // {  NS_USBAudio0200::PIANO, nullptr },
    // {  NS_USBAudio0200::GUITAR, nullptr },
    // {  NS_USBAudio0200::DRUMS_RHYTHM, nullptr },
    // {  NS_USBAudio0200::OTHER_MUSICAL_INSTRUMENT, nullptr },
};

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
    _In_ ACXJACK  Jack,
    _In_ PBOOLEAN IsConnected
)
{
    PAGED_CODE();

    PJACK_CONTEXT jackContext = GetJackContext(Jack);

    *IsConnected = jackContext->IsConnected;

    return STATUS_SUCCESS;
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
_Use_decl_annotations_
NTSTATUS
ProcessRequestHandler_BasicSupport(
    PACX_REQUEST_PARAMETERS Params,
    ULONG                   Flags,
    DWORD                   PropTypeSetId
)
{
    NTSTATUS status = STATUS_INVALID_PARAMETER;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Entry");

    ASSERT(Flags & KSPROPERTY_TYPE_BASICSUPPORT);

    if (Params->Parameters.Property.ValueCb >= sizeof(KSPROPERTY_DESCRIPTION))
    {
        // if return buffer can hold a KSPROPERTY_DESCRIPTION, return it
        //
        PKSPROPERTY_DESCRIPTION propertyDescription = (PKSPROPERTY_DESCRIPTION)Params->Parameters.Property.Value;

        propertyDescription->AccessFlags = Flags;
        propertyDescription->DescriptionSize = sizeof(KSPROPERTY_DESCRIPTION);
        if (VT_ILLEGAL != PropTypeSetId)
        {
            propertyDescription->PropTypeSet.Set = KSPROPTYPESETID_General;
            propertyDescription->PropTypeSet.Id = PropTypeSetId;
        }
        else
        {
            propertyDescription->PropTypeSet.Set = GUID_NULL;
            propertyDescription->PropTypeSet.Id = 0;
        }
        propertyDescription->PropTypeSet.Flags = 0;
        propertyDescription->MembersListCount = 0;
        propertyDescription->Reserved = 0;

        Params->Parameters.Property.ValueCb = sizeof(KSPROPERTY_DESCRIPTION);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - Description       = KSPROPERTY_DESCRIPTION ");
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - AccessFlags       = 0x%08x", propertyDescription->AccessFlags);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - DescriptionSize   = 0x%08x", propertyDescription->DescriptionSize);
        if (VT_ILLEGAL != PropTypeSetId)
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - PropertySet       = KSPROPERTYSETID_General %d", propertyDescription->PropTypeSet.Id);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " -  Set              = KSPROPTYPESETID_General");
        }
        else
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - PropertySet       = GUID_NULL 0");
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " -  Set              = GUID_NULL");
        }
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " -  Id               = 0x%08x", propertyDescription->PropTypeSet.Id);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " -  Flags            = 0x%08x", propertyDescription->PropTypeSet.Flags);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " -  MembersListCount = 0x%08x", propertyDescription->MembersListCount);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " -  Reserved         = 0x%08x", propertyDescription->Reserved);
        status = STATUS_SUCCESS;
    }
    else if (Params->Parameters.Property.ValueCb >= sizeof(ULONG))
    {
        // if return buffer can hold a ULONG, return the access flags
        //
        *(PULONG)(Params->Parameters.Property.Value) = Flags;

        Params->Parameters.Property.ValueCb = sizeof(ULONG);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - Value   = 0x%08x", *(PULONG)(Params->Parameters.Property.Value));
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ENTITY, " - ValueCb = 0x%08x", Params->Parameters.Property.ValueCb);
        status = STATUS_SUCCESS;
    }
    else
    {
        Params->Parameters.Property.ValueCb = 0;
        status = STATUS_BUFFER_TOO_SMALL;
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
ProcessRequestHandler_BasicSupportAgc(
    PACX_REQUEST_PARAMETERS /* Params */,
    ULONG /* Flags */,
    DWORD /* PropTypeSetId */
)
{
    NTSTATUS status = STATUS_INVALID_PARAMETER;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Entry");

    // ASSERT(Flags & KSPROPERTY_TYPE_BASICSUPPORT);

    // TBD

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ENTITY, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

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

    for (ULONG index = 0; index < SIZEOF_ARRAY(s_TerminalTypeToPinCategoryEntries); index++)
    {
        if (TerminalType == s_TerminalTypeToPinCategoryEntries[index].TerminalType)
        {
            pinCategory = s_TerminalTypeToPinCategoryEntries[index].PinCategory;
            break;
        }
    }
    if (pinCategory == nullptr)
    {
        pinCategory = &KSNODETYPE_LINE_CONNECTOR;
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
_Use_decl_annotations_
ULONG ConverSpeakerPositions(
    ULONG channelConfig
)
{
    PAGED_CODE();

    if (channelConfig < NS_USBAudio0200::TOP_FRONT_LEFT_OF_CENTER)
    {
        // NS_USBAudio0200::FRONT_LEFT                 , = 0x00000001, SPEAKER_FRONT_LEFT               = 0x00000001
        // NS_USBAudio0200::FRONT_RIGHT                , = 0x00000002, SPEAKER_FRONT_RIGHT              = 0x00000002
        // NS_USBAudio0200::FRONT_CENTER               , = 0x00000004, SPEAKER_FRONT_CENTER             = 0x00000004
        // NS_USBAudio0200::LOW_FREQUENCY_EFFECTS_LFE  , = 0x00000008, SPEAKER_LOW_FREQUENCY            = 0x00000008
        // NS_USBAudio0200::BACK_LEFT                  , = 0x00000010, SPEAKER_BACK_LEFT                = 0x00000010
        // NS_USBAudio0200::BACK_RIGHT                 , = 0x00000020, SPEAKER_BACK_RIGHT               = 0x00000020
        // NS_USBAudio0200::FRONT_LEFT_OF_CENTER       , = 0x00000040, SPEAKER_FRONT_LEFT_OF_CENTER     = 0x00000040
        // NS_USBAudio0200::FRONT_RIGHT_OF_CENTER      , = 0x00000080, SPEAKER_FRONT_RIGHT_OF_CENTER    = 0x00000080
        // NS_USBAudio0200::BACK_CENTER                , = 0x00000100, SPEAKER_BACK_CENTER              = 0x00000100
        // NS_USBAudio0200::SIDE_LEFT                  , = 0x00000200, SPEAKER_SIDE_LEFT                = 0x00000200
        // NS_USBAudio0200::SIDE_RIGHT                 , = 0x00000400, SPEAKER_SIDE_RIGHT               = 0x00000400
        // NS_USBAudio0200::TOP_CENTER                 , = 0x00000800, SPEAKER_TOP_CENTER               = 0x00000800
        // NS_USBAudio0200::TOP_FRONT_LEFT             , = 0x00001000, SPEAKER_TOP_FRONT_LEFT           = 0x00001000
        // NS_USBAudio0200::TOP_FRONT_CENTER           , = 0x00002000, SPEAKER_TOP_FRONT_CENTER         = 0x00002000
        // NS_USBAudio0200::TOP_FRONT_RIGHT            , = 0x00004000, SPEAKER_TOP_FRONT_RIGHT          = 0x00004000
        // NS_USBAudio0200::TOP_BACK_LEFT              , = 0x00008000, SPEAKER_TOP_BACK_LEFT            = 0x00008000
        // NS_USBAudio0200::TOP_BACK_CENTER            , = 0x00010000, SPEAKER_TOP_BACK_CENTER          = 0x00010000
        // NS_USBAudio0200::TOP_BACK_RIGHT             , = 0x00020000, SPEAKER_TOP_BACK_RIGHT           = 0x00020000
        return channelConfig;
    }
    return SPEAKER_ALL;
}

PAGED_CODE_SEG
NTSTATUS GetChannelsFromMask(
    _In_ DWORD ChannelMask
)
{
    PAGED_CODE();

    ULONG channels = 0;
    ChannelMask &= ~SPEAKER_RESERVED;

    for (; ChannelMask != 0; ChannelMask >>= 1)
    {
        if (ChannelMask & 0x01)
        {
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
NTSTATUS
NotifyDataFormatChange(
    _In_ WDFDEVICE     Device,
    _In_ ACXCIRCUIT    Circuit,
    _In_ ACXPIN        Pin,
    _In_ ACXDATAFORMAT OriginalDataFormat
)
{
    NTSTATUS      status = STATUS_SUCCESS;
    ACXDATAFORMAT desiredDataFormat = nullptr;

    PAGED_CODE();

    CODEC_PIN_CONTEXT * pinContext = GetCodecPinContext(Pin);
    ASSERT(pinContext != nullptr);

    status = SplitAcxDataFormatByDeviceChannels(Device, Circuit, pinContext->NumOfChannelsPerDevice, desiredDataFormat, OriginalDataFormat);
    RETURN_NTSTATUS_IF_FAILED(status);

    ACXDATAFORMATLIST dataFormatList = AcxPinGetRawDataFormatList(Pin);
    status = AcxDataFormatListAssignDefaultDataFormat(dataFormatList, desiredDataFormat);
    RETURN_NTSTATUS_IF_FAILED(status);

    status = AcxPinNotifyDataFormatChange(Pin);

    return status;
}

PAGED_CODE_SEG
const char * GetKsDataFormatSubTypeString(
    _In_ GUID ksDataFormatSubType
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
void TraceAcxDataFormat(
    _In_ UCHAR         DebugPrintLevel,
    _In_ ACXDATAFORMAT DataFormat
)
{
    PAGED_CODE();

    PWAVEFORMATEX                  waveFormatEx = static_cast<PWAVEFORMATEX>(AcxDataFormatGetWaveFormatEx(DataFormat));
    PWAVEFORMATEXTENSIBLE          waveFormatExtensible = static_cast<PWAVEFORMATEXTENSIBLE>(AcxDataFormatGetWaveFormatExtensible(DataFormat));
    PWAVEFORMATEXTENSIBLE_IEC61937 waveFormatExtensibleIEC61937 = static_cast<PWAVEFORMATEXTENSIBLE_IEC61937>(AcxDataFormatGetWaveFormatExtensibleIec61937(DataFormat));

    if (waveFormatExtensibleIEC61937)
    {
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::wFormatTag      0x%x", waveFormatExtensibleIEC61937->FormatExt.Format.wFormatTag);
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::nChannels       %u", waveFormatExtensibleIEC61937->FormatExt.Format.nChannels);
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::nSamplesPerSec  %u", waveFormatExtensibleIEC61937->FormatExt.Format.nSamplesPerSec);
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::nAvgBytesPerSec %u", waveFormatExtensibleIEC61937->FormatExt.Format.nAvgBytesPerSec);
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::nBlockAlign     %u", waveFormatExtensibleIEC61937->FormatExt.Format.nBlockAlign);
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::wBitsPerSample  %u", waveFormatExtensibleIEC61937->FormatExt.Format.wBitsPerSample);
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::cbSize          %u", waveFormatExtensibleIEC61937->FormatExt.Format.cbSize);
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEXTENSIBLE::Samples.wValidBitsPerSample %u", waveFormatExtensibleIEC61937->FormatExt.Samples.wValidBitsPerSample);
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEXTENSIBLE::dwChannelMask               %u", waveFormatExtensibleIEC61937->FormatExt.dwChannelMask);
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEXTENSIBLE::SubFormat                   %s", GetKsDataFormatSubTypeString(waveFormatExtensibleIEC61937->FormatExt.SubFormat));
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEXTENSIBLE_IEC61937::dwEncodedSamplesPerSec %u", waveFormatExtensibleIEC61937->dwEncodedSamplesPerSec);
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEXTENSIBLE_IEC61937::dwEncodedChannelCount  %u", waveFormatExtensibleIEC61937->dwEncodedChannelCount);
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEXTENSIBLE_IEC61937::dwAverageBytesPerSec   %u", waveFormatExtensibleIEC61937->dwAverageBytesPerSec);
    }
    else if (waveFormatExtensible)
    {
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::wFormatTag      0x%x", waveFormatExtensible->Format.wFormatTag);
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::nChannels       %u", waveFormatExtensible->Format.nChannels);
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::nSamplesPerSec  %u", waveFormatExtensible->Format.nSamplesPerSec);
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::nAvgBytesPerSec %u", waveFormatExtensible->Format.nAvgBytesPerSec);
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::nBlockAlign     %u", waveFormatExtensible->Format.nBlockAlign);
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::wBitsPerSample  %u", waveFormatExtensible->Format.wBitsPerSample);
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::cbSize          %u", waveFormatExtensible->Format.cbSize);
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEXTENSIBLE::Samples.wValidBitsPerSample %u", waveFormatExtensible->Samples.wValidBitsPerSample);
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEXTENSIBLE::dwChannelMask               %u", waveFormatExtensible->dwChannelMask);
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEXTENSIBLE::SubFormat                   %s", GetKsDataFormatSubTypeString(waveFormatExtensible->SubFormat));
    }
    else if (waveFormatEx)
    {
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::wFormatTag      0x%x", waveFormatEx->wFormatTag);
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::nChannels       %u", waveFormatEx->nChannels);
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::nSamplesPerSec  %u", waveFormatEx->nSamplesPerSec);
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::nAvgBytesPerSec %u", waveFormatEx->nAvgBytesPerSec);
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::nBlockAlign     %u", waveFormatEx->nBlockAlign);
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::wBitsPerSample  %u", waveFormatEx->wBitsPerSample);
        TraceEvents(DebugPrintLevel, TRACE_DEVICE, " - WAVEFORMATEX::cbSize          %u", waveFormatEx->cbSize);
    }
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS AddPropertyToCircuitInterface(
    ACXCIRCUIT              Circuit,
    ULONG                   PropertyCount,
    const DSP_DEVPROPERTY * Properties
)
{
    PAGED_CODE();

    UNICODE_STRING acxLink{};
    UNICODE_STRING audioLink{};
    WDFSTRING      wdfLink = AcxCircuitGetSymbolicLinkName(Circuit);
    bool           freeStr = false;

    auto exit = wil::scope_exit(
        [&]() {
            if (freeStr)
            {
                RtlFreeUnicodeString(&audioLink);
                freeStr = false;
            }
        }
    );

    // Get the underline unicode string.
    WdfStringGetUnicodeString(wdfLink, &acxLink);

    // Make sure there is a string.
    if (!acxLink.Length || !acxLink.Buffer)
    {
        RETURN_NTSTATUS_IF_FAILED(STATUS_INVALID_DEVICE_STATE);
    }

    // Get the audio interface.
    RETURN_NTSTATUS_IF_FAILED(IoGetDeviceInterfaceAlias(&acxLink, &KSCATEGORY_AUDIO, &audioLink));

    freeStr = true;

    // Set specified properties on the audio interface for the ACXCIRCUIT.
    for (ULONG i = 0; i < PropertyCount; ++i)
    {
        RETURN_NTSTATUS_IF_FAILED(IoSetDeviceInterfacePropertyData(
            &audioLink,
            Properties[i].PropertyKey,
            LOCALE_NEUTRAL,
            PLUGPLAY_PROPERTY_PERSISTENT,
            Properties[i].Type,
            Properties[i].BufferSize,
            Properties[i].Buffer
        ));
    }

    return STATUS_SUCCESS;
}
