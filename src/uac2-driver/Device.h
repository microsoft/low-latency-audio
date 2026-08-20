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
class WorkerThread;
class MixingEngineThread;
class RtPacketObject;
class StreamObject;
class TransferObject;
class AsioBufferObject;
class ErrorStatistics;
class USBAudioConfiguration;
class AudioIsochronousEngine;

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

typedef struct AUDIO_PROPERTY_
{
    UCHAR          InterfaceNumber;     // Currently selected input interface number
    UCHAR          AlternateSetting;    // Currently selected input alternate setting number
    UCHAR          EndpointNumber;      // Currently selected input endpoint number
    ULONG          BytesPerBlock;       // Bytes per block for input (usually InChannels * BytesPerSample)
    ULONG          MaxSamplesPerPacket; // Number of frames transferable per micro frame for input
    ULONG          FormatType;
    ULONG          Format;
    ULONG          BytesPerSample;      // Bytes per sample
    ULONG          ValidBitsPerSample;  // Valid bits per sample
    volatile ULONG MeasuredSampleRate;  // Measured input sampling rate (1-second average)
    ULONG          PacketsPerSec;       // ISO (Micro) Frames per second
    ULONG          SamplesPerPacket;    // Number of samples per ISO Frame (truncated)
    ULONG          DeviceLatency;
    ULONG          UsbChannels;
    UCHAR          ChannelNames;
    ULONG          IsoPacketSize;
    ULONG          LockDelay;
    ULONG          CalcMaxSampleRate;
} AUDIO_PROPERTY;

typedef struct FEEDBACK_PROPERTY_
{
    UCHAR FeedbackInterfaceNumber;
    UCHAR FeedbackAlternateSetting;
    UCHAR FeedbackEndpointNumber;
    UCHAR FeedbackInterval;
} FEEDBACK_PROPERTY;

typedef struct _INTERNAL_PARAMETERS
{
    // Number of frames from the first ISO transfer request to the start of the transfer
    // Value range: 0~1000
    ULONG FirstPacketLatency;

    // Number of USB frames processed per IRP port on a USB 1.1 device
    // Value range: 1~16
    ULONG ClassicFramesPerIrp;

    // Number of USB frames processed per IRP on a USB 2.0 device
    // Value range: 1~16
    ULONG ClassicFramesPerIrp2;

    // Number of IRPs issued simultaneously
    // Value range: 2~8
    ULONG MaxIrpNumber;

    // offset of the buffer boundary timing of the output and the notification timing to the ASIO driver [samples].
    // Value range: 0~1000
    ULONG PreSendFrames;

    // If it is 0, the input and output frame numbers are the same at the time of IRP issuance. If it is not 0, the output frame number is delayed by the specified number of input frames.
    // Value range: 0~16
    LONG OutputFrameDelay;

    // When it is 0, input and output processing is performed independently, and a buffer switch is generated based on the completion of output processing.
    // In case 1, when input processing is complete, input/output processing is performed collectively, and a buffer switch is generated when input/output processing is complete.
    // For 1, the OutputFrameDelay must be at least 1 and (ClassicFramesPerIrp-1) or less.
    // Value range: 0~1
    ULONG DelayedOutputBufferSwitch;
    ULONG Reserved;

    // When processing input buffers in a thread, the target is the current frame position read from the USB controller to the position after this value is subtracted, and the value is read from the buffer.
    // When a value containing the OperationFlags::UseKernelOffset flag (0x10000000) and OR is written, the offset is calculated by the kernel driver based on the device model and connection status, and the value specified in this parameter is added and used.
    // If a value is written with the OperationFlags::UseKernelLatency flag (0x80000000) and OR, the latency value used by the ASIO driver is calculated in the kernel.
    // When using dropout detection, you need to specify this flag.
    // Value range: 0~128 and OperationFlags::UseKernelOffset flags (0x10000000), OperationFlags::UseKernelLatency flags (0x80000000)
    ULONG InputBufferOperationOffset;

    // When the OperationFlags::UseKernelOffset is specified in OutBufferOperationOffset, and when a hub is detected or its presence is unknown, the kernel driver internally adds this value to the value of InBufferOperationOffset.
    // The kernel driver adds the output latency correction value by fs/1000 and reports it to the ASIO driver.
    // Value range: 0~128
    ULONG InputHubOffset;

    // When performing output buffer processing on a thread, the buffer is written to the buffer with the current frame position read from the USB controller plus this value as the processing target.
    // When a value containing the OperationFlags::UseKernelOffset flag (0x10000000) and OR is written, the offset is calculated by the kernel driver based on the device model and connection status, and the value specified in this parameter is added and used.
    // If a value is written with the OperationFlags::UseKernelLatency flag (0x80000000) and OR, the latency value used by the ASIO driver is calculated in the kernel.
    // When using dropout detection, you need to specify this flag.
    // Value range: 0~128 and OperationFlags::UseKernelOffset flags (0x10000000), OperationFlags::UseKernelLatency flags (0x80000000)
    ULONG OutputBufferOperationOffset;

    // When the OperationFlags::UseKernelOffset is specified in OutBufferOperationOffset, and when a hub is detected or the presence or absence of a hub is unknown, the kernel driver internally adds this value to the OutBufferOperationOffset value.
    // The kernel driver adds the output latency correction value by fs/1000 and reports it to the ASIO driver.
    // Value range: 0~128
    ULONG OutputHubOffset;
    ULONG BufferThreadPriority;
    ULONG BufferFlags;

    // ASIO buffer size
	// The actual buffer size is determined by multiplying this period based on the device's sample rate:
	// 2x for sample rates >= 50 kHz and < 100 kHz,
	// 4x for >= 100 kHz and < 200 kHz,
	// 8x for >= 200 kHz and < 400 kHz,
	// and 16x for >= 400 kHz.
    ULONG SuggestedBufferPeriod;
} INTERNAL_PARAMETERS;

typedef struct _AUDIO_STREAM_PROPERTY_SET
{
    UAC_AUDIO_PROPERTY  AudioProperty;
    AUDIO_PROPERTY      InputProperty;
    AUDIO_PROPERTY      OutputProperty;
    FEEDBACK_PROPERTY   FeedbackProperty;
    INTERNAL_PARAMETERS InternalParameters;
    UACSampleFormat     SampleFormatBackup;
    UACSampleFormat     DesiredSampleFormat;
    ULONG               ClassicFramesPerIrp;
    bool                IsDeviceAdaptive;    // True if the output Endpoint is Adaptive
    bool                IsDeviceSynchronous; // True if the output Endpoint is Synchronous
} AUDIO_STREAM_PROPERTY_SET, *PAUDIO_STREAM_PROPERTY_SET;

typedef struct _SELECTED_INTERFACE_AND_PIPE
{
    PUSB_INTERFACE_DESCRIPTOR InterfaceDescriptor;
    WDFUSBINTERFACE           UsbInterface;
    UCHAR                     SelectedAlternateSetting;
    UCHAR                     NumberConfiguredPipes;
    ULONG                     MaximumTransferSize;
    WDFUSBPIPE                Pipe;
    WDF_USB_PIPE_INFORMATION  PipeInfo;
} SELECTED_INTERFACE_AND_PIPE, *PSELECTED_INTERFACE_AND_PIPE;

//
// The device context performs the same job as
// a WDM device extension in the driver frameworks
//
typedef struct _DEVICE_CONTEXT
{
    typedef struct INTERRUPT_MESSAGE_PROPERTY_
    {
        bool  IsValid;
        UCHAR InterfaceNumber;
        UCHAR EndpointNumber;
        UCHAR Interval;
    } INTERRUPT_MESSAGE_PROPERTY;

    USHORT                             VendorId;                                 // Vendor ID obtained from USB
    USHORT                             ProductId;                                // Product ID obtained from USB
    USHORT                             DeviceRelease;                            // Device Release Number obtained from USB
    WCHAR                              ProductName[UAC_MAX_PRODUCT_NAME_LENGTH]; // iProduct string obtained from USB
    WDF_TRI_STATE                      ExcludeD3Cold;
    ULONG                              PrivateDeviceData;                        // just a placeholder
    USB_DEVICE_DESCRIPTOR              UsbDeviceDescriptor;
    PUSB_CONFIGURATION_DESCRIPTOR      UsbConfigurationDescriptor;
    WDFMEMORY                          UsbConfigurationDescriptorHandle;
    WDFDEVICE                          Device;
    WDFUSBDEVICE                       UsbDevice;
    bool                               IsDeviceRemoteWakeable;
    bool                               IsDeviceHighSpeed;
    bool                               IsDeviceSuperSpeed;
    SELECTED_INTERFACE_AND_PIPE        InterruptInterfaceAndPipe;
    WdfUsbTargetDeviceSelectConfigType SelectConfigType;
    PWDF_USB_INTERFACE_SETTING_PAIR    Pairs;
    UCHAR                              NumberOfConfiguredInterfaces;
    USBAudioConfiguration *            UsbAudioConfiguration;
    AudioIsochronousEngine **          AudioIsochronousEngines;
    WDFMEMORY                          AudioIsochronousEnginesMemory;
    ULONG                              NumberOfAudioIsochronousEngines;
    LARGE_INTEGER                      PerformanceCounterFrequency;
    PWSTR                              DeviceName;
    WDFMEMORY                          DeviceNameMemory;
    PWSTR                              SerialNumber;
    WDFMEMORY                          SerialNumberMemory;
    UAC_SUPPORTED_CONTROL_LIST         SupportedControl;
    INTERRUPT_MESSAGE_PROPERTY         InterruptMessageProperty;
    WorkerThread *                     InterruptMessageWorkerThread;
    ULONG                              FramesPerMs; // Number of (micro)frames per ms. 1 or 8
    UCHAR                              DeviceClass;
    UCHAR                              DeviceProtocol;
    LONG                               IsIdleStopSucceeded;
    LARGE_INTEGER                      LastVendorRequestTime;
    bool                               IsPrepareHardwareSucceeded;

    bool              SuperSpeedCompatible;
    ErrorStatistics * ErrorStatistics;
    // UCHAR                              ClockSelectorId;
    // ULONG                              AcClockSources;
    // AC_CLOCK_SOURCE_INFO               AcClockSourceInfo[UAC_MAX_CLOCK_SOURCE];
    // WCHAR                              ClockSourceName[UAC_MAX_CLOCK_SOURCE][UAC_MAX_CLOCK_SOURCE_NAME_LENGTH];
    // ULONG                              CurrentClockSource;
    // KEVENT                             ClockObservationThreadKillEvent;
    // PKTHREAD                           ClockObservationThread;
    LARGE_INTEGER                   ResetEnableTime;
    UCHAR                           AudioControlInterfaceNumber; // Audio Control interface number
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

    SELECTED_INTERFACE_AND_PIPE * SelectedInterfaceAndPipe;
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
    PDEVICE_CONTEXT          DeviceContext;
    AudioIsochronousEngine * AudioIsochronousEngine;
    StreamObject *           StreamObject;
    TransferObject *         TransferObject;
    PVOID                    IrpBuffer;
    PMDL                     IrpMdl;
    PIRP                     Irp;
    WDFMEMORY                UrbMemory;
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

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS
RetrieveDeviceInformation(
    _In_ WDFDEVICE device
);

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

PAGED_CODE_SEG
EVT_ACX_OBJECT_PROCESS_REQUEST EvtUSBAudioAcxDriverGetAudioProperty;

PAGED_CODE_SEG
EVT_ACX_OBJECT_PROCESS_REQUEST EvtUSBAudioAcxDriverGetChannelInfo;

PAGED_CODE_SEG
EVT_ACX_OBJECT_PROCESS_REQUEST EvtUSBAudioAcxDriverGetClockInfo;

PAGED_CODE_SEG
EVT_ACX_OBJECT_PROCESS_REQUEST EvtUSBAudioAcxDriverGetLatencyOffsetOfSampleRate;

PAGED_CODE_SEG
EVT_ACX_OBJECT_PROCESS_REQUEST EvtUSBAudioAcxDriverSetClockSource;

PAGED_CODE_SEG
EVT_ACX_OBJECT_PROCESS_REQUEST EvtUSBAudioAcxDriverSetSampleFormat;

PAGED_CODE_SEG
EVT_ACX_OBJECT_PROCESS_REQUEST EvtUSBAudioAcxDriverChangeSampleRate;

PAGED_CODE_SEG
EVT_ACX_OBJECT_PROCESS_REQUEST EvtUSBAudioAcxDriverGetAsioOwnership;

PAGED_CODE_SEG
EVT_ACX_OBJECT_PROCESS_REQUEST EvtUSBAudioAcxDriverStartAsioStream;

PAGED_CODE_SEG
EVT_ACX_OBJECT_PROCESS_REQUEST EvtUSBAudioAcxDriverStopAsioStream;

PAGED_CODE_SEG
EVT_ACX_OBJECT_PROCESS_REQUEST EvtUSBAudioAcxDriverSetAsioBuffer;

PAGED_CODE_SEG
EVT_ACX_OBJECT_PROCESS_REQUEST EvtUSBAudioAcxDriverUnsetAsioBuffer;

PAGED_CODE_SEG
EVT_ACX_OBJECT_PROCESS_REQUEST EvtUSBAudioAcxDriverReleaseAsioOwnership;

PAGED_CODE_SEG
EVT_ACX_OBJECT_PROCESS_REQUEST EvtUSBAudioAcxDriverGetBufferPeriod;

PAGED_CODE_SEG
EVT_ACX_OBJECT_PROCESS_REQUEST EvtUSBAudioAcxDriverSetBufferPeriod;

PAGED_CODE_SEG
EVT_ACX_OBJECT_PROCESS_REQUEST EvtUSBAudioAcxDriverGetInputLatency;

PAGED_CODE_SEG
EVT_ACX_OBJECT_PROCESS_REQUEST EvtUSBAudioAcxDriverGetOutputLatency;

PAGED_CODE_SEG
EVT_ACX_OBJECT_PROCESS_REQUEST EvtUSBAudioAcxDriverSetAsioDevice;

PAGED_CODE_SEG
EVT_ACX_OBJECT_PROCESS_REQUEST EvtUSBAudioAcxDriverGetAsioDevice;

__drv_maxIRQL(DISPATCH_LEVEL)
NONPAGED_CODE_SEG
EVT_WDF_REQUEST_COMPLETION_ROUTINE USBAudioAcxDriverEvtIsoRequestCompletionRoutine;

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS USBAudioAcxDriverStartInterruptDataReception(
    _In_ PDEVICE_CONTEXT deviceContext
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS USBAudioAcxDriverStopInterruptDataReception(
    _In_ PDEVICE_CONTEXT deviceContext
);

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
