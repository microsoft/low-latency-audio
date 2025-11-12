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

    Device.h

Abstract:

    This file contains the device definitions.

Environment:

    Kernel-mode Driver Framework

--*/

#ifndef _DEVICE_H_
#define _DEVICE_H_

#include <windef.h>
#include <ks.h>

#include "public.h"
#include "UAC_User.h"

#define UAC_MAX_IRP_NUMBER                  8
#define UAC_MAX_FRAMES_PER_MS               8    // USBAudioAcxDriver original

#define UAC_DEFAULT_SUGGESTED_BUFFER_PERIOD UAC_DEFAULT_ASIO_BUFFER_SIZE
#define UAC_DEFAULT_MAX_PACKET_SIZE         1024 // The minimum size is USB3 packet size * UAC_MAX_CLASSIC_FRAMES_PER_IRP * FramesPerMs.
#define UAC_DEFAULT_LOCK_DELAY              10

class CStreamEngine;
class ContiguousMemory;
class MixingEngineThread;
class RtPacketObject;
class StreamObject;
class TransferObject;
class AsioBufferObject;
class ErrorStatistics;
class USBAudioConfiguration;

EXTERN_C_START
#include "usbdi.h"
#include "usbdlib.h"
#include <wdfusb.h>

enum class IsoDirection
{
    In,
    Out,
    Feedback,
    NumOfIsoDirection
};

constexpr int toInt(IsoDirection direction)
{
    return static_cast<int>(direction);
}

constexpr ULONG toULONG(IsoDirection direction)
{
    return static_cast<ULONG>(direction);
}

typedef struct UAC_LATENCY_OFFSET_LIST_
{
    ULONG InputBufferOperationOffset;
    ULONG InputHubOffset;
    ULONG OutputBufferOperationOffset;
    ULONG OutputHubOffset;
} UAC_LATENCY_OFFSET_LIST, *PUAC_LATENCY_OFFSET_LIST;

typedef struct UAC_SUPPORTED_CONTROL_LIST_
{
    USHORT VendorId;
    USHORT ProductId;
    USHORT DeviceRelease;
    USHORT DeviceReleaseMask;
    bool   ClassRequestSupported;
    bool   VendorRequestSupported;
    bool   AvoidToSetSameAlternate;
    bool   SkipInitialSamples;
    ULONG  RequestTimeOut;
    ULONG  RequestRetry;
    ULONG  MaxBurstOverride;
} UAC_SUPPORTED_CONTROL_LIST, *PUAC_SUPPORTED_CONTROL_LIST;

typedef struct UAC_USB_LATENCY_
{
    ULONG InputOffsetMs;
    ULONG InputOffsetFrame;
    ULONG InputDriverBuffer;
    ULONG InputLatency;
    ULONG OutputOffsetMs;
    ULONG OutputOffsetFrame;
    ULONG OutputDriverBuffer;
    ULONG OutputLatency;
    ULONG OutputMinOffsetFrame;
} UAC_USB_LATENCY, *PUAC_USB_LATENCY;

typedef struct AC_CLOCK_SOURCE_INFO_
{
    UCHAR ClockId;
    UCHAR ClockSelectorId;
    UCHAR ClockSelectorIndex;
    UCHAR Attributes;
    UCHAR Controls;
    UCHAR AssociatedTerminal;
    UCHAR iClockSource;
} AC_CLOCK_SOURCE_INFO, *PAC_CLOCK_SOURCE_INFO;

typedef struct _UAC_DRIVER_PARAMETER
{
    ULONG ClassicFramesPerIrp;
    ULONG ClassicFramesPerIrp2;
    ULONG OutputBufferOperationOffset;
    ULONG InputBufferOperationOffset;
} UAC_DRIVER_PARAMETER;

typedef struct _UAC_DRIVER_FLAGS
{
    ULONG                PeriodFrames;
    UAC_DRIVER_PARAMETER Parameter;
} UAC_DRIVER_FLAGS;

//
// The device context performs the same job as
// a WDM device extension in the driver frameworks
//
typedef struct _DEVICE_CONTEXT
{
    struct SelectedInterfaceAndPipe
    {
        PUSB_INTERFACE_DESCRIPTOR InterfaceDescriptor;
        WDFUSBINTERFACE           UsbInterface;
        UCHAR                     SelectedAlternateSetting;
        UCHAR                     NumberConfiguredPipes;
        ULONG                     MaximumTransferSize;
        WDFUSBPIPE                Pipe;
        WDF_USB_PIPE_INFORMATION  PipeInfo;
    };

    typedef struct INTERNAL_PARAMETERS_
    {
        ULONG FirstPacketLatency;
        ULONG ClassicFramesPerIrp;
        ULONG MaxIrpNumber;
        ULONG PreSendFrames;
        LONG  OutputFrameDelay;
        ULONG DelayedOutputBufferSwitch;
        ULONG Reserved;
        ULONG InputBufferOperationOffset;
        ULONG InputHubOffset;
        ULONG OutputBufferOperationOffset;
        ULONG OutputHubOffset;
        ULONG BufferThreadPriority;
        ULONG BufferFlags;
        ULONG ClassicFramesPerIrp2;
        ULONG SuggestedBufferPeriod;
    } INTERNAL_PARAMETERS;

    typedef struct FEEDBACK_PROPERTY_
    {
        UCHAR FeedbackInterfaceNumber;
        UCHAR FeedbackAlternateSetting;
        UCHAR FeedbackEndpointNumber;
        UCHAR FeedbackInterval;
    } FEEDBACK_PROPERTY;

    ACXCIRCUIT                         Render;
    ACXCIRCUIT                         Capture;
    WDF_TRI_STATE                      ExcludeD3Cold;
    ULONG                              PrivateDeviceData; // just a placeholder
    USB_DEVICE_DESCRIPTOR              UsbDeviceDescriptor;
    PUSB_CONFIGURATION_DESCRIPTOR      UsbConfigurationDescriptor;
    WDFMEMORY                          UsbConfigurationDescriptorHandle;
    WDFDEVICE                          Device;
    WDFUSBDEVICE                       UsbDevice;
    bool                               IsDeviceRemoteWakeable;
    bool                               IsDeviceHighSpeed;
    bool                               IsDeviceSuperSpeed;
    SelectedInterfaceAndPipe           InputInterfaceAndPipe;
    SelectedInterfaceAndPipe           OutputInterfaceAndPipe;
    SelectedInterfaceAndPipe           FeedbackInterfaceAndPipe;
    WdfUsbTargetDeviceSelectConfigType SelectConfigType;
    PWDF_USB_INTERFACE_SETTING_PAIR    Pairs;
    UCHAR                              NumberOfConfiguredInterfaces;
    USBAudioConfiguration *            UsbAudioConfiguration;
    ContiguousMemory *                 ContiguousMemory;
    RtPacketObject *                   RtPacketObject;
    WDFWAITLOCK                        StreamWaitLock;
    CStreamEngine **                   RenderStreamEngine;
    CStreamEngine **                   CaptureStreamEngine;
    ULONG                              NumOfInputDevices;
    ULONG                              NumOfOutputDevices;
    WDFMEMORY                          RenderStreamEngineMemory;
    WDFMEMORY                          CaptureStreamEngineMemory;

    LARGE_INTEGER PerformanceCounterFrequency;

    PWSTR                      DeviceName;
    WDFMEMORY                  DeviceNameMemory;
    PWSTR                      SerialNumber;
    WDFMEMORY                  SerialNumberMemory;
    UAC_AUDIO_PROPERTY         AudioProperty;
    UAC_SUPPORTED_CONTROL_LIST SupportedControl;
    FEEDBACK_PROPERTY          FeedbackProperty;
    ULONG                      FramesPerMs;         // Number of (micro)frames per ms. 1 or 8
    ULONG                      ClassicFramesPerIrp;
    bool                       IsDeviceAdaptive;    // True if the output Endpoint is Adaptive
    bool                       IsDeviceSynchronous; // True if the output Endpoint is Synchronous
    UCHAR                      DeviceClass;
    UCHAR                      DeviceProtocol;
    ULONG                      InputUsbChannels;
    ULONG                      OutputUsbChannels;
    UCHAR                      InputChannelNames;
    UCHAR                      OutputChannelNames;
    LONG                       StartCounterAsio;
    LONG                       StartCounterWdmAudio;
    LONG                       StartCounterIsoStream;
    LONG                       IsIdleStopSucceeded;

    LARGE_INTEGER        LastVendorRequestTime;
    NTSTATUS             LastActivationStatus;
    ULONG                InputIsoPacketSize;
    ULONG                OutputIsoPacketSize;
    WCHAR                InputAsioChannelName[UAC_MAX_ASIO_CHANNEL][UAC_MAX_CHANNEL_NAME_LENGTH];
    WCHAR                OutputAsioChannelName[UAC_MAX_ASIO_CHANNEL][UAC_MAX_CHANNEL_NAME_LENGTH];
    ULONG                InputLockDelay;
    ULONG                OutputLockDelay;
    bool                 SuperSpeedCompatible;
    StreamObject *       StreamObject;
    AsioBufferObject *   AsioBufferObject;
    WDFFILEOBJECT        AsioBufferOwner;
    WDFFILEOBJECT        AsioOwner;
    WDFFILEOBJECT        ResetRequestOwner;
    UACSampleFormat      SampleFormatBackup;
    ErrorStatistics *    ErrorStatistics;
    UAC_USB_LATENCY      UsbLatency;
    UACSampleFormat      DesiredSampleFormat;
    UCHAR                ClockSelectorId;
    ULONG                AcClockSources;
    AC_CLOCK_SOURCE_INFO AcClockSourceInfo[UAC_MAX_CLOCK_SOURCE];
    WCHAR                ClockSourceName[UAC_MAX_CLOCK_SOURCE][UAC_MAX_CLOCK_SOURCE_NAME_LENGTH];
    ULONG                CurrentClockSource;
    KEVENT               ClockObservationThreadKillEvent;
    PKTHREAD             ClockObservationThread;
    LARGE_INTEGER        ResetEnableTime;

    INTERNAL_PARAMETERS             Params;
    const UAC_LATENCY_OFFSET_LIST * LatencyOffsetList;
    ULONG                           HubCount;

} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

//
// This macro will generate an inline function called GetDeviceContext
// which will be used to get a pointer to the device context memory
// in a type safe manner.
//
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext)

typedef struct _FILE_CONTEXT
{
    PDEVICE_CONTEXT DeviceContext;
} FILE_CONTEXT, *PFILE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(FILE_CONTEXT, GetFileContext)

//
// This context is associated with every pipe handle. In this sample,
// it used for isoch transfers.
//
typedef struct _PIPE_CONTEXT
{
    ULONG TransferSizePerMicroframe;

    ULONG TransferSizePerFrame;

    DEVICE_CONTEXT::SelectedInterfaceAndPipe * SelectedInterfaceAndPipe;
} PIPE_CONTEXT, *PPIPE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(PIPE_CONTEXT, GetPipeContext)

//
// This context is associated with every request received by the driver
// from the app.
//
typedef struct _REQUEST_CONTEXT
{

    WDFMEMORY UrbMemory;
    PMDL      Mdl;
    ULONG     Length;         // remaining to xfer
    ULONG     Numxfer;
    ULONG_PTR VirtualAddress; // va for next segment of xfer.
    BOOLEAN   Read;           // TRUE if Read
} REQUEST_CONTEXT, *PREQUEST_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(REQUEST_CONTEXT, GetRequestContext)

//
// This context is used in requests for isochronous transfers.
//
typedef struct _ISOCHRONOUS_TEST_REQUEST_CONTEXT
{
    WDFMEMORY       UrbMemory;
    PMDL            Mdl;
    ULONG           Length;         // remaining to xfer
    ULONG           Numxfer;
    ULONG_PTR       VirtualAddress; // va for next segment of xfer.
    BOOLEAN         Read;           // TRUE if Read
    PDEVICE_CONTEXT DeviceContext;
} ISOCHRONOUS_TEST_REQUEST_CONTEXT, *PISOCHRONOUS_TEST_REQUEST_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(ISOCHRONOUS_TEST_REQUEST_CONTEXT, GetIsochronousTestRequestContext)

//
// This context is used in requests for isochronous transfers.
//
typedef struct _ISOCHRONOUS_REQUEST_CONTEXT
{
    PDEVICE_CONTEXT  DeviceContext;
    StreamObject *   StreamObject;
    TransferObject * TransferObject;
    PVOID            IrpBuffer;
    PMDL             IrpMdl;
    PIRP             Irp;
    WDFMEMORY        UrbMemory;
} ISOCHRONOUS_REQUEST_CONTEXT, *PISOCHRONOUS_REQUEST_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(ISOCHRONOUS_REQUEST_CONTEXT, GetIsochronousRequestContext)

//
//
//
typedef struct _WORK_ITEM_CONTEXT
{
    PDEVICE_CONTEXT  DeviceContext;
    StreamObject *   StreamObject;
    TransferObject * TransferObject;
    NTSTATUS         IoStatusStatus;
} WORK_ITEM_CONTEXT, *PWORK_ITEM_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(WORK_ITEM_CONTEXT, GetWorkItemContext)

PAGED_CODE_SEG
__drv_requiresIRQL(PASSIVE_LEVEL)
    NTSTATUS
    CopyRegistrySettingsPath(
        _In_ PUNICODE_STRING registryPath
    );

EVT_WDF_DRIVER_DEVICE_ADD       USBAudioAcxDriverEvtDeviceAdd;                // PASSIVE_LEVEL
EVT_WDF_DEVICE_PREPARE_HARDWARE USBAudioAcxDriverEvtDevicePrepareHardware;    // PASSIVE_LEVEL
EVT_WDF_DEVICE_RELEASE_HARDWARE USBAudioAcxDriverEvtDeviceReleaseHardware;    // PASSIVE_LEVEL
EVT_WDF_DEVICE_SURPRISE_REMOVAL USBAudioAcxDriverEvtDeviceSurpriseRemoval;    // PASSIVE_LEVEL
EVT_WDF_DEVICE_D0_ENTRY         USBAudioAcxDriverEvtDeviceD0Entry;            // PASSIVE_LEVEL, but you should not make this callback function pageable.
EVT_WDF_DEVICE_D0_EXIT          USBAudioAcxDriverEvtDeviceD0Exit;             // PASSIVE_LEVEL
EVT_WDF_OBJECT_CONTEXT_CLEANUP  USBAudioAcxDriverEvtDeviceContextCleanup;     // PASSIVE_LEVEL, https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfobject/nc-wdfobject-evt_wdf_object_context_cleanup
EVT_WDF_OBJECT_CONTEXT_CLEANUP  USBAudioAcxDriverEvtPipeContextCleanup;       // IRQL <= DISPATCH_LEVEL, https://learn.microsoft.com/ja-jp/windows-hardware/drivers/ddi/wdfobject/nc-wdfobject-evt_wdf_object_context_cleanup
EVT_WDF_OBJECT_CONTEXT_CLEANUP  USBAudioAcxDriverEvtIsoRequestContextCleanup; // IRQL <= DISPATCH_LEVEL, https://learn.microsoft.com/ja-jp/windows-hardware/drivers/ddi/wdfobject/nc-wdfobject-evt_wdf_object_context_cleanup
EVT_WDF_DEVICE_CONTEXT_CLEANUP  Codec_EvtDeviceContextCleanup;                // IRQL <= DISPATCH_LEVEL, Conditionally IRQL = PASSIVE_LEVEL
EVT_WDF_OBJECT_CONTEXT_CLEANUP  USBAudioAcxDriverEvtFileCleanup;              // PASSIVE_LEVEL

__drv_maxIRQL(DISPATCH_LEVEL)
NONPAGED_CODE_SEG
NTSTATUS
SendUrbSync(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ PURB            urb
);

__drv_maxIRQL(DISPATCH_LEVEL)
NONPAGED_CODE_SEG
NTSTATUS
SendUrbSyncWithTimeout(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ PURB            urb,
    _In_ ULONG           msTimeout
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
ULONG
GetCurrentFrame(
    _In_ PDEVICE_CONTEXT deviceContext
);

__drv_maxIRQL(DISPATCH_LEVEL)
NONPAGED_CODE_SEG
ULONGLONG
USBAudioAcxDriverStreamGetCurrentTime(
    _In_ PDEVICE_CONTEXT deviceContext,
    _Out_opt_ PULONGLONG qpcPosition
);

__drv_maxIRQL(DISPATCH_LEVEL)
NONPAGED_CODE_SEG
ULONGLONG
USBAudioAcxDriverStreamGetCurrentTimeUs(
    _In_ PDEVICE_CONTEXT deviceContext,
    _Out_opt_ PULONGLONG qpcPosition
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS
USBAudioAcxDriverStreamPrepareHardware(
    _In_ bool            isInput,
    _In_ ULONG           deviceIndex,
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ CStreamEngine * streamEngine
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS
USBAudioAcxDriverStreamReleaseHardware(
    _In_ bool            isInput,
    _In_ ULONG           deviceIndex,
    _In_ PDEVICE_CONTEXT deviceContext
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS
USBAudioAcxDriverStreamSetDataFormat(
    _In_ bool            isInput,
    _In_ ULONG           deviceIndex,
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ ACXDATAFORMAT   dataFormat
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS
USBAudioAcxDriverStreamSetRtPackets(
    _In_ bool                                                               isInput,
    _In_ ULONG                                                              deviceIndex,
    _In_ PDEVICE_CONTEXT                                                    deviceContext,
    _Inout_updates_(packetsCount) _Inout_updates_bytes_(packetSize) PVOID * packets,
    _In_ ULONG                                                              packetsCount,
    _In_ ULONG                                                              packetSize,
    _In_ ULONG                                                              channel,
    _In_ ULONG                                                              numOfChannelsPerDevice
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
void USBAudioAcxDriverStreamUnsetRtPackets(
    _In_ bool            isInput,
    _In_ ULONG           deviceIndex,
    _In_ PDEVICE_CONTEXT deviceContext
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS
USBAudioAcxDriverStreamRun(
    _In_ bool            isInput,
    _In_ ULONG           deviceIndex,
    _In_ PDEVICE_CONTEXT deviceContext
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS
USBAudioAcxDriverStreamPause(
    _In_ bool            isInput,
    _In_ ULONG           deviceIndex,
    _In_ PDEVICE_CONTEXT deviceContext
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS
USBAudioAcxDriverStreamGetCurrentPacket(
    _In_ bool            isInput,
    _In_ ULONG           deviceIndex,
    _In_ PDEVICE_CONTEXT deviceContext,
    _Out_ PULONG         currentPacket
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS
USBAudioAcxDriverStreamResetCurrentPacket(
    _In_ bool            isInput,
    _In_ ULONG           deviceIndex,
    _In_ PDEVICE_CONTEXT deviceContext
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS
USBAudioAcxDriverStreamGetCapturePacket(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ ULONG           deviceIndex,
    _Out_ PULONG         lastCapturePacket,
    _Out_ PULONGLONG     qpcPacketStart
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS
USBAudioAcxDriverStreamGetPresentationPosition(
    _In_ bool            isInput,
    _In_ ULONG           deviceIndex,
    _In_ PDEVICE_CONTEXT deviceContext,
    _Out_ PULONGLONG     positionInBlocks,
    _Out_ PULONGLONG     qpcPosition
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS USBAudioAcxDriverGetCurrentDataFormat(
    _In_ PDEVICE_CONTEXT  deviceContext,
    _In_ bool             isInput,
    _Out_ ACXDATAFORMAT & dataFormat
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
bool USBAudioAcxDriverHasAsioOwnership(
    _In_ PDEVICE_CONTEXT deviceContext
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
VOID EvtUSBAudioAcxDriverGetAudioProperty(
    _In_ WDFOBJECT  object,
    _In_ WDFREQUEST request
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
VOID EvtUSBAudioAcxDriverGetChannelInfo(
    _In_ WDFOBJECT  object,
    _In_ WDFREQUEST request
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
VOID EvtUSBAudioAcxDriverGetClockInfo(
    _In_ WDFOBJECT  object,
    _In_ WDFREQUEST request
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
VOID EvtUSBAudioAcxDriverGetLatencyOffsetOfSampleRate(
    _In_ WDFOBJECT  object,
    _In_ WDFREQUEST request
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
VOID EvtUSBAudioAcxDriverSetClockSource(
    _In_ WDFOBJECT  object,
    _In_ WDFREQUEST request
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
VOID EvtUSBAudioAcxDriverSetFlags(
    _In_ WDFOBJECT  object,
    _In_ WDFREQUEST request
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
VOID EvtUSBAudioAcxDriverSetSampleFormat(
    _In_ WDFOBJECT  object,
    _In_ WDFREQUEST request
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
VOID EvtUSBAudioAcxDriverChangeSampleRate(
    _In_ WDFOBJECT  object,
    _In_ WDFREQUEST request
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
VOID EvtUSBAudioAcxDriverGetAsioOwnership(
    _In_ WDFOBJECT  object,
    _In_ WDFREQUEST request
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
VOID EvtUSBAudioAcxDriverStartAsioStream(
    _In_ WDFOBJECT  object,
    _In_ WDFREQUEST request
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
VOID EvtUSBAudioAcxDriverStopAsioStream(
    _In_ WDFOBJECT  object,
    _In_ WDFREQUEST request
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
VOID EvtUSBAudioAcxDriverSetAsioBuffer(
    _In_ WDFOBJECT  object,
    _In_ WDFREQUEST request
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
VOID EvtUSBAudioAcxDriverUnsetAsioBuffer(
    _In_ WDFOBJECT  object,
    _In_ WDFREQUEST request
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
VOID EvtUSBAudioAcxDriverReleaseAsioOwnership(
    _In_ WDFOBJECT  object,
    _In_ WDFREQUEST request
);

EVT_WDF_REQUEST_COMPLETION_ROUTINE USBAudioAcxDriverEvtIsoRequestCompletionRoutine;

__drv_maxIRQL(DISPATCH_LEVEL)
NONPAGED_CODE_SEG
const char *
GetDirectionString(
    _In_ IsoDirection direction
);

__drv_maxIRQL(DISPATCH_LEVEL)
NONPAGED_CODE_SEG
void DumpByteArray(
    _In_ LPCSTR                      label,
    _In_reads_bytes_(length) UCHAR * buffer,
    _In_ ULONG                       length
);

EXTERN_C_END

#endif // #ifndef _DEVICE_H_
