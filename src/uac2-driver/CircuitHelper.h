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
