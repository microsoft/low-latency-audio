// Copyright (c) Microsoft Corporation.
// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

/*++

Module Name:

  CircuitHelper.h

Abstract:

  This module contains helper functions for endpoints.

Environment:

  Kernel mode

--*/

// size_t
// __inline
// CODEC_ALIGN_SIZE_DOWN_CONSTANT(
//     IN size_t Length,
//     IN size_t AlignTo
//     )
#define CODEC_ALIGN_SIZE_DOWN_CONSTANT(Length, AlignTo) ((Length) & ~((AlignTo) - 1))

#define CODEC_ALIGN_SIZE_DOWN                           CODEC_ALIGN_SIZE_DOWN_CONSTANT

// size_t
// __inline
// CODEC_ALIGN_SIZE_UP_CONSTANT(
//     IN size_t Length,
//     IN size_t AlignTo
//     )
#define CODEC_ALIGN_SIZE_UP_CONSTANT(Length, AlignTo)   CODEC_ALIGN_SIZE_DOWN_CONSTANT((Length) + (AlignTo) - 1, (AlignTo))

#define CODEC_ALIGN_SIZE_UP                             CODEC_ALIGN_SIZE_UP_CONSTANT

//
// Enumeration visitor callback.
//
typedef NTSTATUS(EVT_KSATTRIBUTES_VISITOR)(
    _In_ PKSATTRIBUTE AttributeHeader,
    _In_ PVOID        Context,
    _Out_ BOOLEAN *   bContinue
);

typedef EVT_KSATTRIBUTES_VISITOR * PFN_KSATTRIBUTES_VISITOR;

#define HNSTIME_PER_MILLISECOND 10000

typedef struct _DSP_DEVPROPERTY {
    const DEVPROPKEY* PropertyKey;
    DEVPROPTYPE Type;
    ULONG BufferSize;
    __field_bcount_opt(BufferSize) PVOID Buffer;
} DSP_DEVPROPERTY, PDSP_DEVPROPERTY;

static struct
{
    KSAUDIO_PACKETSIZE_CONSTRAINTS2                 TransportPacketConstraints;         // 1
    KSAUDIO_PACKETSIZE_PROCESSINGMODE_CONSTRAINT    AdditionalProcessingConstraints[1]; // 1 + 1 = 2
} s_PacketSizeConstraints =
{
    {
        ULONG(7.0 * (double)HNSTIME_PER_MILLISECOND),           // 7 ms minimum processing interval
        FILE_BYTE_ALIGNMENT,                                    // 1 byte packet size alignment
        0,                                                      // no maximum packet size constraint
        2,                                                      // 2 processing constraints follow
        {
            STATIC_AUDIO_SIGNALPROCESSINGMODE_RAW,              // constraint for raw processing mode
            0,                                                  // NA samples per processing frame
            ULONG(7.0 * (double)HNSTIME_PER_MILLISECOND),       // 70000 hns (7ms) per processing frame
        }
    },
    {
        {
            STATIC_AUDIO_SIGNALPROCESSINGMODE_DEFAULT,          // constraint for default processing mode
            0,                                                  // NA samples per processing frame
            ULONG(7.0 * (double)HNSTIME_PER_MILLISECOND),       // 70000 hns (7ms) per processing frame
        }
    }
};

const DSP_DEVPROPERTY c_InterfaceProperties[] =
{
    {
        &DEVPKEY_KsAudio_PacketSize_Constraints2,       // Key
        DEVPROP_TYPE_BINARY,                            // Type
        sizeof(s_PacketSizeConstraints),                // BufferSize
        &s_PacketSizeConstraints,                       // Buffer
    },
};

PAGED_CODE_SEG
NTSTATUS AllocateFormat(
    _In_ KSDATAFORMAT_WAVEFORMATEXTENSIBLE * WaveFormat,
    _In_ ACXCIRCUIT                          Circuit,
    _In_ WDFDEVICE                           Device,
    _Out_ ACXDATAFORMAT *                    Format
);

PAGED_CODE_SEG
NTSTATUS CreateAudioJack(
    _In_ ULONG                    ChannelMapping,
    _In_ ULONG                    Color,
    _In_ ACX_JACK_CONNECTION_TYPE ConnectionType,
    _In_ ACX_JACK_GEO_LOCATION    GeoLocation,
    _In_ ACX_JACK_GEN_LOCATION    GenLocation,
    _In_ ACX_JACK_PORT_CONNECTION PortConnection,
    _In_ ULONG                    Flags,
    _In_ ACXPIN                   BridgePin
);

PAGED_CODE_SEG
NTSTATUS EvtJackRetrievePresence(
    _In_ ACXJACK  Jack,
    _In_ PBOOLEAN IsConnected
);

PAGED_CODE_SEG
VOID CpuResourcesCallbackHelper(
    _In_ WDFOBJECT  Object,
    _In_ WDFREQUEST Request,
    _In_ ACXELEMENT Element
);

PAGED_CODE_SEG
ULONG GetSampleRateFromIndex(
    _In_ ULONG Index
);

PAGED_CODE_SEG
NTSTATUS GetSampleRateMask(
    _In_ ULONG SampleRate
);

PAGED_CODE_SEG
const GUID * ConvertTerminalType(
    _In_ USHORT TerminalType
);

PAGED_CODE_SEG
const GUID * ConvertAudioDataFormat(
    _In_ ULONG FormatType,
    _In_ ULONG Format
);

PAGED_CODE_SEG
NTSTATUS ConvertAudioDataFormat(
    _In_ const ACXDATAFORMAT & DataFormat,
    _Out_ ULONG &              FormatType,
    _Out_ ULONG &              Format
);

PAGED_CODE_SEG
NTSTATUS GetChannelsFromMask(
    _In_ DWORD ChannelMask
);

PAGED_CODE_SEG
NTSTATUS DuplicateAcxDataFormat(
    _In_ WDFDEVICE        Device,
    _In_ WDFOBJECT        ParentObject,
    _Out_ ACXDATAFORMAT & Destination,
    _In_ ACXDATAFORMAT    Source
);

PAGED_CODE_SEG
NTSTATUS SplitAcxDataFormatByDeviceChannels(
    _In_ WDFDEVICE        Device,
    _In_ ACXCIRCUIT       Circuit,
    _In_ ULONG            DeviceIndex,
    _Out_ ACXDATAFORMAT & Destination,
    _In_ ACXDATAFORMAT    Source
);

PAGED_CODE_SEG
const char * GetKsDataFormatSubTypeString(
    _In_ GUID ksDataFormatSubType
);

PAGED_CODE_SEG
void TraceAcxDataFormat(
    _In_ UCHAR         DebugPrintLevel,
    _In_ ACXDATAFORMAT DataFormat
);

PAGED_CODE_SEG
NTSTATUS AddPropertyToCircuitInterface(
    _In_ ACXCIRCUIT             Circuit,
    _In_ ULONG                  PropertyCount,
    _In_ const DSP_DEVPROPERTY* Properties
);

PAGED_CODE_SEG
WORD GetFormatTagFromAcxDataFormat(
    _In_    ACXDATAFORMAT   DataFormat
);

PAGED_CODE_SEG
NTSTATUS ConvertWaveFormatExToWaveFormatExtensible
(
    _In_    WDFDEVICE       Device,
    _In_    ACXCIRCUIT      Circuit,
    _In_    ACXDATAFORMAT   DataFormatEx,
    _Out_   ACXDATAFORMAT& DataFormatExtensible
);
