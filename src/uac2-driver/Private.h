// Copyright (c) Microsoft Corporation.
// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

/*++

Module Name:

    Private.h

Abstract:

    Contains structure definitions and function prototypes private to
    the driver.

    Implemented with reference to Windows-driver-samples/audio/Acx/Samples/Common/Private.h

Environment:

    Kernel

--*/

#ifndef _PRIVATE_H_
#define _PRIVATE_H_

#include <wdm.h>
#include <windef.h>

#pragma warning(push)
#pragma warning(disable : 28157)
#pragma warning(disable : 28158)
#pragma warning(disable : 28167)
#include <wil/resource.h>
#pragma warning(pop)
#include <mmsystem.h>
#include <ks.h>
#include <ksmedia.h>
#include "NewDelete.h"

#define DEFAULT_PRODUCT_NAME L"USBAudio2-ACX"

/* make prototypes usable from C++ */
#ifdef __cplusplus
EXTERN_C_START
#endif

#include <initguid.h>
#include <ntddk.h>
#include <ntstrsafe.h>
#include <ntintsafe.h>
#include "Trace.h"

#include <wdf.h>
#include <acx.h>

#ifndef PAGED_CODE_SEG
#define PAGED_CODE_SEG    __declspec(code_seg("PAGE"))
#define INIT_CODE_SEG     __declspec(code_seg("INIT"))
#define NONPAGED_CODE_SEG __declspec(code_seg("NOPAGE"))
#endif

#define MAX_SUPPORTED_PACKETS_FOR_SUPER_SPEED 1024
#define MAX_SUPPORTED_PACKETS_FOR_HIGH_SPEED  1024
#define MAX_SUPPORTED_PACKETS_FOR_FULL_SPEED  255

#define DISPATCH_LATENCY_IN_MS                10

// Diverted from Acx/Samples/AudioCodec/Driver/DriverSettings.h  Start
// Newly generated GUID

// Defining the component ID for the capture circuit. This ID uniquely identifies the circuit instance (vendor specific):
// {71476E6A-CCC7-44BB-9F33-B15CF4AD5628}
DEFINE_GUID(CODEC_CAPTURE_COMPONENT_GUID, 0x71476e6a, 0xccc7, 0x44bb, 0x9f, 0x33, 0xb1, 0x5c, 0xf4, 0xad, 0x56, 0x28);

// Defines a custom name for the capture circuit bridge pin:
// {AAFFD2FF-0130-4B3C-9D97-149E076033D3}
DEFINE_GUID(MIC_CUSTOM_NAME, 0xaaffd2ff, 0x130, 0x4b3c, 0x9d, 0x97, 0x14, 0x9e, 0x7, 0x60, 0x33, 0xd3);

// Defining the component ID for the render circuit. This ID uniquely identifies the circuit instance (vendor specific):
// {D9C140C2-AB01-4E83-9EAA-06C40F8CABEB}
DEFINE_GUID(CODEC_RENDER_COMPONENT_GUID, 0xd9c140c2, 0xab01, 0x4e83, 0x9e, 0xaa, 0x6, 0xc4, 0xf, 0x8c, 0xab, 0xeb);

// This is always the definition for the system container guid:
// {A11C91BC-6C56-44A4-832D-406BEE24DADE}
DEFINE_GUID(SYSTEM_CONTAINER_GUID, 0xa11c91bc, 0x6c56, 0x44a4, 0x83, 0x2d, 0x40, 0x6b, 0xee, 0x24, 0xda, 0xde);

// Driver developers should update this guid if the container is a device rather than a
// system. Otherwise, this GUID should stay the same:
// {99A15CBB-8ECF-4ED5-A3A1-D29926A1E03E}
DEFINE_GUID(DEVICE_CONTAINER_GUID, 0x99a15cbb, 0x8ecf, 0x4ed5, 0xa3, 0xa1, 0xd2, 0x99, 0x26, 0xa1, 0xe0, 0x3e);

// AudioCodec driver tag:
#define DRIVER_TAG        (ULONG)'DaAU'

#define MIXINGENGINE_TAG  (ULONG)'EmAU'

// The idle timeout in msec for power policy structure:
#define IDLE_TIMEOUT_MSEC (ULONG)10000

// The WPP control GUID defined in Trace.h should also be updated to be unique.

// This string must match the string defined in AudioCodec.inf for the microphone name:
DECLARE_CONST_UNICODE_STRING(captureCircuitName, L"CaptureDevice0");

// This string must match the string defined in AudioCodec.inf for the speaker name:
DECLARE_CONST_UNICODE_STRING(renderCircuitName, L"RenderDevice0");

// Diverted from Acx/Samples/AudioCodec/Driver/DriverSettings.h  End

//
// driver prototypes.
//
DRIVER_INITIALIZE DriverEntry;

/////////////////////////////////////////////////////////
//
// Driver wide definitions
//

// Copied from cfgmgr32.h
#if !defined(MAX_DEVICE_ID_LEN)
#define MAX_DEVICE_ID_LEN 200
#endif

// Number of millisecs per sec.
#define MS_PER_SEC              1000

// Number of hundred nanosecs per sec.
#define HNS_PER_SEC             10000000

#define REQUEST_TIMEOUT_SECONDS 5

#undef MIN
#undef MAX
#define MIN(a, b) ((a) > (b) ? (b) : (a))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#ifndef BOOL
typedef int BOOL;
#endif

#ifndef SIZEOF_ARRAY
#define SIZEOF_ARRAY(ar) (sizeof(ar) / sizeof((ar)[0]))
#endif // !defined(SIZEOF_ARRAY)

#ifndef RGB
#define RGB(r, g, b) (DWORD)(r << 16 | g << 8 | b)
#endif

#define ALL_CHANNELS_ID     UINT32_MAX
#define MAX_CHANNELS        2

//
// Ks support.
//
#define KSPROPERTY_TYPE_ALL KSPROPERTY_TYPE_BASICSUPPORT | \
                                KSPROPERTY_TYPE_GET |      \
                                KSPROPERTY_TYPE_SET

//
// Define struct to hold signal processing mode and corresponding
// list of supported formats.
//
typedef struct
{
    GUID                                SignalProcessingMode;
    KSDATAFORMAT_WAVEFORMATEXTENSIBLE * FormatList;
    ULONG                               FormatListCount;
} SUPPORTED_FORMATS_LIST;

//
// Define CAPTURE device context.
//
typedef struct _CAPTURE_DEVICE_CONTEXT
{
    ACXCIRCUIT Circuit;
    BOOLEAN    FirstTimePrepareHardware;
} CAPTURE_DEVICE_CONTEXT, *PCAPTURE_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(CAPTURE_DEVICE_CONTEXT, GetCaptureDeviceContext)

//
// Define RENDER device context.
//
typedef struct _RENDER_DEVICE_CONTEXT
{
    ACXCIRCUIT Circuit;
    BOOLEAN    FirstTimePrepareHardware;
} RENDER_DEVICE_CONTEXT, *PRENDER_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(RENDER_DEVICE_CONTEXT, GetRenderDeviceContext)

//
// Define circuit/stream element context.
//
typedef struct _ELEMENT_CONTEXT
{
    BOOLEAN Dummy;
} ELEMENT_CONTEXT, *PELEMENT_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(ELEMENT_CONTEXT, GetElementContext)

//
// Define circuit/stream element context.
//
typedef struct _MUTE_ELEMENT_CONTEXT
{
    BOOL MuteState[MAX_CHANNELS];
} MUTE_ELEMENT_CONTEXT, *PMUTE_ELEMENT_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(MUTE_ELEMENT_CONTEXT, GetMuteElementContext)

//
// Define circuit/stream element context.
//
typedef struct _VOLUME_ELEMENT_CONTEXT
{
    LONG VolumeLevel[MAX_CHANNELS];
} VOLUME_ELEMENT_CONTEXT, *PVOLUME_ELEMENT_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VOLUME_ELEMENT_CONTEXT, GetVolumeElementContext)

#define VOLUME_STEPPING      0x8000
#define VOLUME_LEVEL_MAXIMUM 0x00000000
#define VOLUME_LEVEL_MINIMUM (-96 * 0x10000)

//
// Define mute timer context.
//
typedef struct _MUTE_TIMER_CONTEXT
{
    ACXELEMENT MuteElement;
    ACXEVENT   Event;
} MUTE_TIMER_CONTEXT, *PMUTE_TIMER_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(MUTE_TIMER_CONTEXT, GetMuteTimerContext)

//
// Define format context.
//
typedef struct _FORMAT_CONTEXT
{
    BOOLEAN Dummy;
} FORMAT_CONTEXT, *PFORMAT_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(FORMAT_CONTEXT, GetFormatContext)

//
// Define jack context.
//
typedef struct _JACK_CONTEXT
{
    ULONG Dummy;
} JACK_CONTEXT, *PJACK_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(JACK_CONTEXT, GetJackContext)

//
// Define audio engine context.
//
typedef struct _ENGINE_CONTEXT
{
    ACXDATAFORMAT MixFormat;
} ENGINE_CONTEXT, *PENGINE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(ENGINE_CONTEXT, GetEngineContext)

//
// Define stream audio engine context.
//
typedef struct _STREAMAUDIOENGINE_CONTEXT
{
    BOOLEAN Dummy;
} STREAMAUDIOENGINE_CONTEXT, *PSTREAMAUDIOENGINE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(STREAMAUDIOENGINE_CONTEXT, GetStreamAudioEngineContext)

//
// Define keyword spotter context
//
typedef struct _KEYWORDSPOTTER_CONTEXT
{
    ACXPNPEVENT Event;
    PVOID       KeywordDetector;
} KEYWORDSPOTTER_CONTEXT, *PKEYWORDSPOTTER_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(KEYWORDSPOTTER_CONTEXT, GetKeywordSpotterContext)

//
// Define pnp event context.
//
typedef struct _PNPEVENT_CONTEXT
{
    BOOLEAN Dummy;
} PNPEVENT_CONTEXT, *PPNPEVENT_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(PNPEVENT_CONTEXT, GetPnpEventContext)

//
// Define stream engine context.
//
typedef struct _STREAMENGINE_CONTEXT
{
    PVOID StreamEngine;
    ULONG DeviceIndex;
    ULONG Channel;
    ULONG NumOfChannelsPerDevice;
} STREAMENGINE_CONTEXT, *PSTREAMENGINE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(STREAMENGINE_CONTEXT, GetStreamEngineContext)

/////////////////////////////////////////////////////////////
// Codec driver definitions
//
class CStreamEngine;

typedef enum _CODEC_PIN_TYPE
{
    CodecPinTypeHost,
    CodecPinTypeOffload,
    CodecPinTypeLoopback,
    CodecPinTypeKeyword,
    CodecPinTypeDevice
} CODEC_PIN_TYPE, *PCODEC_PIN_TYPE;

typedef struct _CODEC_PIN_CONTEXT
{
    WDFDEVICE      Device;
    CODEC_PIN_TYPE CodecPinType;
    ULONG          DeviceIndex;
    ULONG          Channel;
    ULONG          NumOfChannelsPerDevice;
} CODEC_PIN_CONTEXT, *PCODEC_PIN_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(CODEC_PIN_CONTEXT, GetCodecPinContext)

///////////////////////////////////////////////////////////
// Dsp driver definitions
//
typedef enum
{
    CaptureHostPin = 0,
    CaptureBridgePin = 1,
    CaptureKWSPin = 2,
    CapturePinCount = 3
} CAPTURE_PIN_TYPE;

typedef enum
{
    RenderHostPin = 0,
    RenderOffloadPin = 1,
    RenderLoopbackPin = 2,
    RenderBridgePin = 3,
    RenderPinCount = 4
} RENDER_PIN_TYPE;

typedef struct _DSP_PIN_CONTEXT
{
    ACXTARGETCIRCUIT TargetCircuit;
    ULONG            TargetPinId;
    RENDER_PIN_TYPE  RenderPinType;
    CAPTURE_PIN_TYPE CapturePinType;

    // The stream bridge below will only be valid for the Capture circuit Bridge Pin

    // Host stream bridge will be used to ensure host stream creations are passed
    // to the downlevel circuits. Since the HostStreamBridge won't have InModes set,
    // the ACX framework will not add streams automatically. We will add streams for
    // non KWS pin.
    ACXSTREAMBRIDGE HostStreamBridge;
} DSP_PIN_CONTEXT, *PDSP_PIN_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DSP_PIN_CONTEXT, GetDspPinContext)

/////////////////////////////////////////////////////////////
// Multicircuit Dsp driver definitions
//
// Codec Render (speaker) definitions
//

//
// Define render circuit context.
//
typedef struct _CODEC_RENDER_CIRCUIT_CONTEXT
{
    WDFMEMORY      VolumeElementsMemory;
    ACXVOLUME *    VolumeElements;
    WDFMEMORY      MuteElementsMemory;
    ACXMUTE *      MuteElements;
    ACXAUDIOENGINE AudioEngineElement;
} CODEC_RENDER_CIRCUIT_CONTEXT, *PCODEC_RENDER_CIRCUIT_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(CODEC_RENDER_CIRCUIT_CONTEXT, GetRenderCircuitContext)

typedef enum
{
    CodecRenderHostPin = 0,
    CodecRenderBridgePin = 1,
    CodecRenderPinCount = 2
} CODEC_RENDER_PINS;

typedef enum
{
    RenderVolumeIndex = 0,
    RenderMuteIndex = 1,
    RenderElementCount = 2
} CODEC_RENDER_ELEMENTS;

// Render callbacks.

PAGED_CODE_SEG
EVT_ACX_CIRCUIT_CREATE_STREAM CodecR_EvtCircuitCreateStream;
PAGED_CODE_SEG
EVT_ACX_CIRCUIT_POWER_UP CodecR_EvtCircuitPowerUp;
PAGED_CODE_SEG
EVT_ACX_CIRCUIT_POWER_DOWN CodecR_EvtCircuitPowerDown;
PAGED_CODE_SEG
EVT_ACX_STREAM_SET_RENDER_PACKET CodecR_EvtStreamSetRenderPacket;
// EVT_ACX_STREAM_GET_CAPTURE_PACKET   CodecR_EvtStreamGetLoopbackPacket;

PAGED_CODE_SEG
EVT_ACX_PIN_SET_DATAFORMAT CodecR_EvtAcxPinSetDataFormat;

NONPAGED_CODE_SEG
EVT_WDF_DEVICE_CONTEXT_CLEANUP CodecR_EvtPinContextCleanup;

PAGED_CODE_SEG
EVT_ACX_MUTE_ASSIGN_STATE CodecR_EvtMuteAssignState;

PAGED_CODE_SEG
EVT_ACX_MUTE_RETRIEVE_STATE CodecR_EvtMuteRetrieveState;
// EVT_ACX_VOLUME_ASSIGN_LEVEL         CodecR_EvtVolumeAssignLevel;
PAGED_CODE_SEG
EVT_ACX_VOLUME_RETRIEVE_LEVEL CodecR_EvtVolumeRetrieveLevel;
PAGED_CODE_SEG
EVT_ACX_RAMPED_VOLUME_ASSIGN_LEVEL CodecR_EvtRampedVolumeAssignLevel;
// EVT_ACX_AUDIOENGINE_RETRIEVE_BUFFER_SIZE_LIMITS CodecR_EvtAcxAudioEngineRetrieveBufferSizeLimits;
// EVT_ACX_AUDIOENGINE_RETRIEVE_EFFECTS_STATE      CodecR_EvtAcxAudioEngineRetrieveEffectsState;
// EVT_ACX_AUDIOENGINE_ASSIGN_EFFECTS_STATE        CodecR_EvtAcxAudioEngineAssignEffectsState;
// EVT_ACX_AUDIOENGINE_ASSIGN_ENGINE_FORMAT        CodecR_EvtAcxAudioEngineAssignEngineDeviceFormat;
// EVT_ACX_AUDIOENGINE_RETRIEVE_ENGINE_FORMAT      CodecR_EvtAcxAudioEngineRetrieveEngineMixFormat;
// EVT_ACX_STREAMAUDIOENGINE_RETRIEVE_EFFECTS_STATE            CodecR_EvtAcxStreamAudioEngineRetrieveEffectsState;
// EVT_ACX_STREAMAUDIOENGINE_ASSIGN_EFFECTS_STATE              CodecR_EvtAcxStreamAudioEngineAssignEffectsState;
// EVT_ACX_STREAMAUDIOENGINE_RETRIEVE_PRESENTATION_POSITION    CodecR_EvtAcxStreamAudioEngineRetrievePresentationPosition;
// EVT_ACX_STREAMAUDIOENGINE_ASSIGN_CURRENT_WRITE_POSITION     CodecR_EvtAcxStreamAudioEngineAssignCurrentWritePosition;
// EVT_ACX_STREAMAUDIOENGINE_RETRIEVE_LINEAR_BUFFER_POSITION   CodecR_EvtAcxStreamAudioEngineRetrieveLinearBufferPosition;
// EVT_ACX_STREAMAUDIOENGINE_ASSIGN_LAST_BUFFER_POSITION       CodecR_EvtAcxStreamAudioEngineAssignLastBufferPosition;
// EVT_ACX_STREAMAUDIOENGINE_ASSIGN_LOOPBACK_PROTECTION        CodecR_EvtAcxStreamAudioEngineAssignLoopbackProtection;

PAGED_CODE_SEG
NTSTATUS
CodecR_CreateRenderCircuit(
    _In_ WDFDEVICE              Device,
    _In_ const GUID *           ComponentGuid,
    _In_ const UNICODE_STRING * CircuitName,
    _In_ const ULONG            SupportedSampleRate,
    _Out_ ACXCIRCUIT *          Circuit
);

/////////////////////////////////////////////////////////
//
// Codec Capture (microphone) definitions
//

//
// Define capture circuit context.
//
typedef struct _CODEC_CAPTURE_CIRCUIT_CONTEXT
{
    WDFMEMORY   VolumeElementsMemory;
    ACXVOLUME * VolumeElements;
    WDFMEMORY   MuteElementsMemory;
    ACXMUTE *   MuteElements;
    // ACXKEYWORDSPOTTER KeywordSpotter;
} CODEC_CAPTURE_CIRCUIT_CONTEXT, *PCODEC_CAPTURE_CIRCUIT_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(CODEC_CAPTURE_CIRCUIT_CONTEXT, GetCaptureCircuitContext)

typedef enum
{
    CodecCaptureHostPin = 0,
    CodecCaptureBridgePin = 1,
    CodecCapturePinCount = 2
} CODEC_CAPTURE_PINS;

typedef enum
{
    CaptureVolumeIndex = 0,
    CaptureMuteIndex = 1,
    CaptureElementCount = 2
} CAPTURE_ELEMENTS;

// Capture callbacks.

PAGED_CODE_SEG
EVT_ACX_CIRCUIT_CREATE_STREAM CodecC_EvtCircuitCreateStream;

PAGED_CODE_SEG
EVT_ACX_CIRCUIT_POWER_UP CodecC_EvtCircuitPowerUp;

PAGED_CODE_SEG
EVT_ACX_CIRCUIT_POWER_DOWN CodecC_EvtCircuitPowerDown;

PAGED_CODE_SEG
EVT_ACX_VOLUME_ASSIGN_LEVEL CodecC_EvtVolumeAssignLevelCallback;

PAGED_CODE_SEG
EVT_ACX_VOLUME_RETRIEVE_LEVEL CodecC_EvtVolumeRetrieveLevelCallback;
// EVT_ACX_VOLUME_ASSIGN_LEVEL         CodecC_EvtBoostAssignLevelCallback;
// EVT_ACX_VOLUME_RETRIEVE_LEVEL       CodecC_EvtBoostRetrieveLevelCallback;
PAGED_CODE_SEG
EVT_ACX_STREAM_GET_CAPTURE_PACKET CodecC_EvtStreamGetCapturePacket;

PAGED_CODE_SEG
EVT_ACX_PIN_SET_DATAFORMAT CodecC_EvtAcxPinSetDataFormat;

PAGED_CODE_SEG
EVT_ACX_PIN_RETRIEVE_NAME CodecC_EvtAcxPinRetrieveName;

NONPAGED_CODE_SEG
EVT_WDF_DEVICE_CONTEXT_CLEANUP CodecC_EvtPinContextCleanup;
// EVT_ACX_KEYWORDSPOTTER_RETRIEVE_ARM     CodecC_EvtAcxKeywordSpotterRetrieveArm;
// EVT_ACX_KEYWORDSPOTTER_ASSIGN_ARM       CodecC_EvtAcxKeywordSpotterAssignArm;
// EVT_ACX_KEYWORDSPOTTER_ASSIGN_PATTERNS  CodecC_EvtAcxKeywordSpotterAssignPatterns;
// EVT_ACX_KEYWORDSPOTTER_ASSIGN_RESET     CodecC_EvtAcxKeywordSpotterAssignReset;

PAGED_CODE_SEG
NTSTATUS
CodecC_CreateCaptureCircuit(
    _In_ WDFDEVICE              Device,
    _In_ const GUID *           ComponentGuid,
    _In_ const GUID *           MicCustomName,
    _In_ const UNICODE_STRING * CircuitName,
    _In_ const ULONG            SupportedSampleRate,
    _Out_ ACXCIRCUIT *          Circuit
);

/* make internal prototypes usable from C++ */
#ifdef __cplusplus
EXTERN_C_END
#endif

// Used to store the registry settings path for the driver:
extern UNICODE_STRING g_RegistryPath;

// List of sample rates supported by this driver
extern const ULONG c_SampleRateList[];
extern const ULONG c_SampleRateCount;

#endif // _PRIVATE_H_
