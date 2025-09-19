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

    Device.cpp - Device handling events for example driver.

Abstract:

   This file contains the device entry points and callbacks.

Environment:

    Kernel-mode Driver Framework

--*/

#include "Driver.h"
#include "Device.h"
#include "DeviceControl.h"
#include "Public.h"
#include "Common.h"
#include "USBAudio.h"
#include "USBAudioConfiguration.h"
#include "ContiguousMemory.h"
#include "TransferObject.h"
#include "StreamObject.h"
#include "RtPacketObject.h"
#include "AsioBufferObject.h"
#include "StreamEngine.h"
#include "ErrorStatistics.h"
#include "CircuitHelper.h"

#ifndef __INTELLISENSE__
#include "Device.tmh"
#endif

//
//  Global variables
//
UNICODE_STRING g_RegistryPath{}; // This is used to store the registry settings path for the driver

//
// Static variables

//
// If operational changes are required for each model, they will be defined here.
//
// If there are differences in the control method for each USB Audio Device,
// they are added to this array to support them.
//
// At this time, all devices operate correctly with a unified behavior,
// so only the default parameters are defined.
//
static const UAC_SUPPORTED_CONTROL_LIST g_SupportedControlList[] = {
    {0xffff, 0xffff, 0x0000, 0x0000, true, true, true, false, 5000 /* 5sec */, 3, 1},
};

static const int g_SupportedControlCount = sizeof(g_SupportedControlList) / sizeof(g_SupportedControlList[0]);

//
// Latency offsets are defined according to the device's connection status.
//
static const UAC_LATENCY_OFFSET_LIST g_LatencyOffsetList[] = {
    {
        0,
        0,
        3,
        2,
    }, // for USB 1.1 device
    {
        0,
        0,
        3,
        0,
    }, // for USB 2.0 device
};

//
// Defines internal parameters corresponding to the specified ASIO Period Frames.
// These parameters affect not only ASIO but also USB isochronous transfer settings,
// and therefore influence the behavior of the ACX audio driver as well.
//
static const UAC_DRIVER_FLAGS g_DriverSettingsTable[] = {
    {8192, {4, 4, 0xb0000008, 0x90000000}},
    {4096, {4, 4, 0xb0000008, 0x90000000}},
    {2048, {4, 4, 0xb0000008, 0x90000000}},
    {1536, {4, 4, 0xb0000008, 0x90000000}},
    {1024, {4, 4, 0xb0000008, 0x90000000}},
    {768, {4, 4, 0xb0000008, 0x90000000}},
    {512, {4, 4, 0xb0000007, 0x90000000}},
    {384, {3, 3, 0xb0000006, 0x90000000}},
    {256, {3, 3, 0xb0000005, 0x90000000}},
    {192, {3, 3, 0xb0000004, 0x90000000}},
    {128, {3, 3, 0xb0000004, 0x90000000}},
    {96, {3, 2, 0xb0000003, 0x90000000}},
    {64, {3, 2, 0xb0000003, 0x90000000}},
    {48, {3, 1, 0xb0000002, 0x90000000}},
    {32, {3, 1, 0xb0000002, 0x90000000}},
    {24, {3, 1, 0xb0000002, 0x90000000}},
    {16, {3, 1, 0xb0000002, 0x90000000}},
    {12, {3, 1, 0xb0000002, 0x90000000}},
    {8, {3, 1, 0xb0000002, 0x90000000}},
    {4, {3, 1, 0xb0000002, 0x90000000}},
    {0, {4, 4, 0xb0000007, 0x90000000}},
};

static const int g_SettingsCount = sizeof(g_DriverSettingsTable) / sizeof(g_DriverSettingsTable[0]);

//
//  Local function prototypes
//

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static __drv_maxIRQL(PASSIVE_LEVEL)
NTSTATUS
USBAudioAcxDriverCreateDevice(
    _Inout_ PWDFDEVICE_INIT deviceInit
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS
Codec_SetPowerPolicy(
    _In_ WDFDEVICE device
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS
ReadAndSelectDescriptors(
    _In_ WDFDEVICE device
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS
ConfigureDevice(
    _In_ WDFDEVICE device
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS
AbortPipes(
    _In_ IsoDirection direction,
    _In_ WDFDEVICE    device
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS
InitializePipeContextForSuperSpeedDevice(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ WDFUSBINTERFACE interface,
    _In_ UCHAR           selectedAlternateSetting,
    _In_ WDFUSBPIPE      pipe
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS
RetrieveDeviceInformation(
    _In_ WDFDEVICE device
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS
InitializePipeContextForSuperSpeedIsochPipe(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           interfaceNumber,
    _In_ UCHAR           selectedAlternateSetting,
    _In_ WDFUSBPIPE      pipe
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS
InitializePipeContextForHighSpeedDevice(
    _In_ WDFUSBPIPE pipe
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS
InitializePipeContextForFullSpeedDevice(
    _In_ WDFUSBPIPE pipe
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static PUSB_ENDPOINT_DESCRIPTOR
GetEndpointDescriptorForEndpointAddress(
    _In_ PDEVICE_CONTEXT                                  deviceContext,
    _In_ UCHAR                                            interfaceNumber,
    _In_ UCHAR                                            selectedAlternateSetting,
    _In_ UCHAR                                            endpointAddress,
    _Out_ PUSB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR * endpointCompanionDescriptor
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS
ActivateAudioInterface(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ ULONG           desiredSampleRate,
    _In_ ULONG           desiredFormatType,
    _In_ ULONG           desiredFormat,
    _In_ ULONG           desiredBytesPerSampleIn,
    _In_ ULONG           desiredValidBitsPerSampleIn,
    _In_ ULONG           desiredBytesPerSampleOut,
    _In_ ULONG           desiredValidBitsPerSampleOut,
    _In_ bool            forceSetSampleRate = false

);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS
SelectConfiguration(
    _In_ PDEVICE_CONTEXT deviceContext
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS
SelectAlternateInterface(
    _In_ IsoDirection    direction,
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           interfaceNumber,
    _In_ UCHAR           alternateSetting
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS GetHubCount(
    _In_ PDEVICE_CONTEXT deviceContext,
    _Out_ ULONG &        hubCount
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS
GetStackCapability(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ const GUID *    capabilityType,
    _In_ ULONG           outputBufferLength,
    _When_(outputBufferLength == 0, _Pre_null_)
        _When_(outputBufferLength != 0, _Out_writes_bytes_(outputBufferLength))
            PUCHAR OutputBuffer
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static bool
IsValidFlags(
    _In_ PUAC_SET_FLAGS_CONTEXT flags
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS
ConvertFlags(
    _In_ PUAC_SET_FLAGS_CONTEXT flags
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS CalculateUsbLatency(
    _In_ PDEVICE_CONTEXT   deviceContext,
    _Out_ PUAC_USB_LATENCY usbLatency
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static void BuildChannelMap(
    _In_ PDEVICE_CONTEXT deviceContext
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS SetPipeInformation(
    _In_ PDEVICE_CONTEXT deviceContext
);

#if 0
__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS ResetPipeInformation(
    _In_ PDEVICE_CONTEXT deviceContext
);
#endif

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS StartIsoStream(
    _In_ PDEVICE_CONTEXT deviceContext
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS FreeTransferObject(
    _In_ TransferObject * transferObject
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS StartTransfer(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ StreamObject *  streamObject,
    _In_ ULONG           index,
    _In_ IsoDirection    direction
);

__drv_maxIRQL(DISPATCH_LEVEL)
NONPAGED_CODE_SEG
static NTSTATUS InitializeIsoUrbIn(
    _In_ PDEVICE_CONTEXT  deviceContext,
    _In_ StreamObject *   streamObject,
    _In_ TransferObject * transferObject,
    _In_ ULONG            numPackets
);

__drv_maxIRQL(DISPATCH_LEVEL)
NONPAGED_CODE_SEG
static NTSTATUS InitializeIsoUrbOut(
    _In_ PDEVICE_CONTEXT  deviceContext,
    _In_ StreamObject *   streamObject,
    _In_ TransferObject * transferObject,
    _In_ ULONG            numPackets
);

__drv_maxIRQL(DISPATCH_LEVEL)
NONPAGED_CODE_SEG
static NTSTATUS InitializeIsoUrbFeedback(
    _In_ PDEVICE_CONTEXT  deviceContext,
    _In_ StreamObject *   streamObject,
    _In_ TransferObject * transferObject,
    _In_ ULONG            numPackets
);

__drv_maxIRQL(DISPATCH_LEVEL)
NONPAGED_CODE_SEG
static NTSTATUS ProcessTransferIn(
    _In_ PDEVICE_CONTEXT  deviceContext,
    _In_ StreamObject *   streamObject,
    _In_ TransferObject * transferObject
);

__drv_maxIRQL(DISPATCH_LEVEL)
NONPAGED_CODE_SEG
static NTSTATUS ProcessTransferOut(
    _In_ PDEVICE_CONTEXT  deviceContext,
    _In_ StreamObject *   streamObject,
    _In_ TransferObject * transferObject
);

__drv_maxIRQL(DISPATCH_LEVEL)
NONPAGED_CODE_SEG
static NTSTATUS ProcessTransferFeedback(
    _In_ PDEVICE_CONTEXT  deviceContext,
    _In_ StreamObject *   streamObject,
    _In_ TransferObject * transferObject
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS StopIsoStream(
    _In_ PDEVICE_CONTEXT deviceContext
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS NotifyDataFormatChange(
    _In_ WDFDEVICE     device,
    _In_ ACXCIRCUIT    cirtuit,
    _In_ ACXPIN        pin,
    _In_ ACXDATAFORMAT originalDataFormat
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static NTSTATUS NotifyAllPinsDataFormatChange(
    _In_ bool            isInput,
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ ACXDATAFORMAT   dataFormatBeforeChange,
    _In_ ACXDATAFORMAT   dataFormatAfterChange
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
static void ReportInternalParameters(
    PDEVICE_CONTEXT deviceContext
);

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
CopyRegistrySettingsPath(
    PUNICODE_STRING registryPath
)
/*++

Routine Description:

Copies the following registry path to a global variable.

\REGISTRY\MACHINE\SYSTEM\ControlSetxxx\Services\<driver>\Parameters

Arguments:

registryPath - Registry path passed to DriverEntry

Returns:

NTSTATUS - SUCCESS if able to configure the framework

--*/

{
    PAGED_CODE();

    //
    // Initializing the unicode string, so that if it is not allocated it will not be deallocated too.
    //
    RtlInitUnicodeString(&g_RegistryPath, nullptr);

    g_RegistryPath.MaximumLength = registryPath->Length + sizeof(WCHAR);

    g_RegistryPath.Buffer = (PWCH)ExAllocatePool2(POOL_FLAG_PAGED, g_RegistryPath.MaximumLength, DRIVER_TAG);

    if (g_RegistryPath.Buffer == nullptr)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlAppendUnicodeToString(&g_RegistryPath, registryPath->Buffer);

    return STATUS_SUCCESS;
}

NONPAGED_CODE_SEG
_Use_decl_annotations_
void DumpByteArray(
    LPCSTR  label,
    UCHAR * buffer,
    ULONG   length
)
{
    CHAR outputString[100] = "";
    CHAR oneByte[10] = "";

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "<<%s %u bytes>>", label, length);

    ULONG i;
    for (i = 0; i < length; ++i)
    {
        if (i % 16 == 0)
        {
            sprintf_s(outputString, sizeof(outputString), "%04x: ", i);
        }
        sprintf_s(oneByte, sizeof(oneByte), "%02x ", buffer[i]);
        strcat_s(outputString, sizeof(outputString), oneByte);
        if (i % 16 == 15)
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "%s", outputString);
        }
    }
    if (i % 16 != 0)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "%s", outputString);
    }
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioAcxDriverEvtDeviceAdd(
    WDFDRIVER /* driver */,
    PWDFDEVICE_INIT deviceInit
)
/*++
Routine Description:

    EvtDeviceAdd is called by the framework in response to AddDevice
    call from the PnP manager. We create and initialize a device object to
    represent a new instance of the device.

Arguments:

    driver - Handle to a framework driver object created in DriverEntry

    deviceInit - Pointer to a framework-allocated WDFDEVICE_INIT structure.

Return Value:

    NTSTATUS

--*/
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    status = USBAudioAcxDriverCreateDevice(deviceInit);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
static NTSTATUS
USBAudioAcxDriverCreateDevice(
    PWDFDEVICE_INIT deviceInit
)
/*++

Routine Description:

    Worker routine called to create a device and its software resources.

Arguments:

    deviceInit - Pointer to an opaque init structure. Memory for this
                    structure will be freed by the framework when the WdfDeviceCreate
                    succeeds. So don't access the structure after that point.

Return Value:

    NTSTATUS

--*/
{
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    WDF_OBJECT_ATTRIBUTES        attributes;
    WDF_DEVICE_PNP_CAPABILITIES  pnpCapabilities;
    WDF_FILEOBJECT_CONFIG        fileConfig;
    ACX_DEVICEINIT_CONFIG        devInitConfig;
    ACX_DEVICE_CONFIG            deviceConfig;
    PDEVICE_CONTEXT              deviceContext;
    WDFDEVICE                    device = nullptr;
    NTSTATUS                     status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");
    //
    // The driver calls this DDI in its AddDevice callback before creating the PnP device.
    // ACX uses this call to add default/standard settings for the device to be created.
    //
    ACX_DEVICEINIT_CONFIG_INIT(&devInitConfig);
    RETURN_IF_FAILED(AcxDeviceInitInitialize(deviceInit, &devInitConfig));

    //
    //  Initialize the pnpPowerCallbacks structure.  Callback events for PNP
    //  and Power are specified here.  If you don't supply any callbacks,
    //  the Framework will take appropriate default actions based on whether
    //  deviceInit is initialized to be an FDO, a PDO or a filter device
    //  object.
    //
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDevicePrepareHardware = USBAudioAcxDriverEvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = USBAudioAcxDriverEvtDeviceReleaseHardware;
    pnpPowerCallbacks.EvtDeviceD0Entry = USBAudioAcxDriverEvtDeviceD0Entry;
    pnpPowerCallbacks.EvtDeviceD0Exit = USBAudioAcxDriverEvtDeviceD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(deviceInit, &pnpPowerCallbacks);

    //
    // Initialize the request attributes to specify the context size and type
    // for every request created by framework for this device.
    //
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, REQUEST_CONTEXT);

    WdfDeviceInitSetRequestAttributes(deviceInit, &attributes);

    //
    // Initialize fileConfig for the call to WdfDeviceInitSetFileObjectConfig.
    // Since callbacks for Create/Close/Cleanup are not needed, initialize with WDF_NO_EVENT_CALLBACK.
    //
    WDF_FILEOBJECT_CONFIG_INIT(
        &fileConfig,
        WDF_NO_EVENT_CALLBACK,
        WDF_NO_EVENT_CALLBACK,
        WDF_NO_EVENT_CALLBACK
    );

    //
    // Call WdfDeviceInitSetFileObjectConfig to register the cleanup process for the File Object controlled by the ASIO Driver.
    // This cleanup function is also effective for the ACX Driver.
    //
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, FILE_CONTEXT);
    attributes.EvtCleanupCallback = USBAudioAcxDriverEvtFileCleanup;
    WdfDeviceInitSetFileObjectConfig(deviceInit, &fileConfig, &attributes);

#if !defined(BUFFERED_READ_WRITE)
    //
    // I/O type is Buffered by default. We want to do direct I/O for Reads
    // and Writes so set it explicitly. Please note that this sample
    // can do isoch transfer only if the io type is directio.
    //
    WdfDeviceInitSetIoType(deviceInit, WdfDeviceIoDirect);

#endif

    //
    // Now specify the size of device extension where we track per device
    // context.DeviceInit is completely initialized. So call the framework
    // to create the device and attach it to the lower stack.
    //
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);
    attributes.EvtCleanupCallback = USBAudioAcxDriverEvtDeviceContextCleanup;

    RETURN_NTSTATUS_IF_FAILED(WdfDeviceCreate(&deviceInit, &attributes, &device));

    //
    // Get a pointer to the device context structure that we just associated
    // with the device object. We define this structure in the device.h
    // header file. GetDeviceContext is an inline function generated by
    // using the WDF_DECLARE_CONTEXT_TYPE_WITH_NAME macro in device.h.
    // This function will do the type checking and return the device context.
    // If you pass a wrong object handle it will return NULL and assert if
    // run under framework verifier mode.
    //
    deviceContext = GetDeviceContext(device);
    ASSERT(deviceContext != nullptr);
    //
    // Initialize the context.
    //
    deviceContext->PrivateDeviceData = 0;
    deviceContext->Device = device;
    KeQueryPerformanceCounter(&deviceContext->PerformanceCounterFrequency);

    deviceContext->Render = nullptr;
    deviceContext->Capture = nullptr;
    deviceContext->ExcludeD3Cold = WdfFalse;

    deviceContext->ContiguousMemory = ContiguousMemory::Create();
    RETURN_NTSTATUS_IF_TRUE(deviceContext->ContiguousMemory == nullptr, STATUS_INSUFFICIENT_RESOURCES);

    deviceContext->RtPacketObject = RtPacketObject::Create(deviceContext);
    RETURN_NTSTATUS_IF_TRUE(deviceContext->RtPacketObject == nullptr, STATUS_INSUFFICIENT_RESOURCES);

    deviceContext->ErrorStatistics = ErrorStatistics::Create();
    RETURN_NTSTATUS_IF_TRUE(deviceContext->ErrorStatistics == nullptr, STATUS_INSUFFICIENT_RESOURCES);

    //
    // The driver calls this DDI in its AddDevice callback after creating the PnP
    // device. ACX uses this call to apply any post device settings.
    //
    ACX_DEVICE_CONFIG_INIT(&deviceConfig);
    RETURN_NTSTATUS_IF_FAILED(AcxDeviceInitialize(device, &deviceConfig));

    //
    // Tell the framework to set the SurpriseRemovalOK in the DeviceCaps so
    // that you don't get the popup in usermode (on Win2K) when you surprise
    // remove the device.
    //
    WDF_DEVICE_PNP_CAPABILITIES_INIT(&pnpCapabilities);
    pnpCapabilities.SurpriseRemovalOK = WdfTrue;
    WdfDeviceSetPnpCapabilities(device, &pnpCapabilities);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioAcxDriverEvtDevicePrepareHardware(
    WDFDEVICE device,
    WDFCMRESLIST /* resourceList */,
    WDFCMRESLIST /* resourceListTranslated */
)
/*++

Routine Description:

    In this callback, the driver does whatever is necessary to make the
    hardware ready to use.  In the case of a USB device, this involves
    reading and selecting descriptors.

Arguments:

    device - handle to a device

Return Value:

    NT status value

--*/
{
    NTSTATUS                     status;
    PDEVICE_CONTEXT              deviceContext;
    WDF_OBJECT_ATTRIBUTES        attributes;
    WDF_USB_DEVICE_CREATE_CONFIG createParams;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    status = STATUS_SUCCESS;
    deviceContext = GetDeviceContext(device);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = device;

    status = WdfWaitLockCreate(&attributes, &deviceContext->StreamWaitLock);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "WdfWaitLockCreate failed %!STATUS!", status);
        return status;
    }

    status = ReadAndSelectDescriptors(device);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "ReadandSelectDescriptors failed %!STATUS!", status);
        return status;
    }

    {
        deviceContext->Params.FirstPacketLatency = UAC_DEFAULT_FIRST_PACKET_LATENCY;
        deviceContext->Params.ClassicFramesPerIrp = UAC_DEFAULT_CLASSIC_FRAMES_PER_IRP;
        deviceContext->Params.MaxIrpNumber = UAC_DEFAULT_MAX_IRP_NUMBER;
        deviceContext->Params.PreSendFrames = UAC_DEFAULT_PRE_SEND_FRAMES;
        deviceContext->Params.OutputFrameDelay = UAC_DEFAULT_OUTPUT_FRAME_DELAY;
        deviceContext->Params.DelayedOutputBufferSwitch = UAC_DEFAULT_DELAYED_OUTPUT_BUFFER_SWITCH;
        deviceContext->Params.InputBufferOperationOffset = UAC_DEFAULT_IN_BUFFER_OPERATION_OFFSET;
        deviceContext->Params.InputHubOffset = UAC_DEFAULT_IN_HUB_OFFSET;
        deviceContext->Params.OutputBufferOperationOffset = UAC_DEFAULT_OUT_BUFFER_OPERATION_OFFSET;
        deviceContext->Params.OutputHubOffset = UAC_DEFAULT_OUT_HUB_OFFSET;
        deviceContext->Params.BufferThreadPriority = UAC_DEFAULT_BUFFER_THREAD_PRIORITY;
        deviceContext->Params.ClassicFramesPerIrp2 = UAC_DEFAULT_CLASSIC_FRAMES_PER_IRP;
        deviceContext->Params.SuggestedBufferPeriod = UAC_DEFAULT_SUGGESTED_BUFFER_PERIOD;

        deviceContext->SupportedControl = g_SupportedControlList[0];
        for (int i = 1; i < g_SupportedControlCount; ++i)
        {
            if ((g_SupportedControlList[i].VendorId == deviceContext->UsbDeviceDescriptor.idVendor) &&
                (g_SupportedControlList[i].ProductId == deviceContext->UsbDeviceDescriptor.idProduct) &&
                (g_SupportedControlList[i].DeviceRelease ==
                 (deviceContext->UsbDeviceDescriptor.bcdDevice & g_SupportedControlList[i].DeviceReleaseMask)))
            {
                deviceContext->SupportedControl = g_SupportedControlList[i];
                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "This device %s CLASS control requests.", deviceContext->SupportedControl.ClassRequestSupported ? "supports" : "does not support");
                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "This device %s VENDOR control requests.", deviceContext->SupportedControl.VendorRequestSupported ? "supports" : "does not support");
            }
        }

        deviceContext->DesiredSampleFormat = UACSampleFormat::UAC_SAMPLE_FORMAT_PCM;
    }

    deviceContext->UsbAudioConfiguration = USBAudioConfiguration::Create(deviceContext, &deviceContext->UsbDeviceDescriptor);

    GetHubCount(deviceContext, deviceContext->HubCount);

    //
    //  Create a USB device handle so that we can communicate with the
    //  underlying USB stack. The WDFUSBDEVICE handle is used to query,
    //  configure, and manage all aspects of the USB device.
    //  These aspects include device properties, bus properties,
    //  and I/O creation and synchronization. We only create the device the first time
    //  PrepareHardware is called. If the device is restarted by pnp manager
    //  for resource re balance, we will use the same device handle but then select
    //  the interfaces again because the USB stack could reconfigure the device on
    //  restart.
    //
    if (deviceContext->UsbDevice == nullptr)
    {
        //
        // Specifying a client contract version of 602 enables us to query for
        // and use the new capabilities of the USB driver stack for Windows 8.
        // It also implies that we conform to rules mentioned in MSDN
        // documentation for WdfUsbTargetDeviceCreateWithParameters.
        //
        WDF_USB_DEVICE_CREATE_CONFIG_INIT(&createParams, USBD_CLIENT_CONTRACT_VERSION_602);

        status = WdfUsbTargetDeviceCreateWithParameters(device, &createParams, WDF_NO_OBJECT_ATTRIBUTES, &deviceContext->UsbDevice);

        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "WdfUsbTargetDeviceCreateWithParameters failed %!STATUS!", status);
            return status;
        }
    }

    status = SelectConfiguration(deviceContext);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "SelectConfiguration failed %!STATUS!", status);
        return status;
    }

    //
    //  Set power policy data.
    //
    RETURN_NTSTATUS_IF_FAILED(Codec_SetPowerPolicy(device));

    //
    // Updates the connection status of the USB Bus
    //
    RETURN_NTSTATUS_IF_FAILED(RetrieveDeviceInformation(device));

    //
    // Sets the LatencyOffsetList etc. for this device.
    //
    if (deviceContext->IsDeviceSuperSpeed)
    {
        deviceContext->FramesPerMs = 8;
        deviceContext->LatencyOffsetList = &(g_LatencyOffsetList[1]);
    }
    else if (deviceContext->IsDeviceHighSpeed)
    {
        deviceContext->FramesPerMs = 8;
        deviceContext->LatencyOffsetList = &(g_LatencyOffsetList[1]);
    }
    else
    {
        deviceContext->FramesPerMs = 1;
        deviceContext->LatencyOffsetList = &(g_LatencyOffsetList[0]);
    }

    if (deviceContext->AudioProperty.VendorId == 0)
    {
        ULONG       retryCount = 0;
        const ULONG maxRetry = 30;

        //
        // Parses USB CONFIGURATION DESCRIPTOR and holds the descriptors
        // required for creating an ACX Device and streaming USB Audio.
        //
        RETURN_NTSTATUS_IF_FAILED(deviceContext->UsbAudioConfiguration->ParseDescriptors(deviceContext->UsbConfigurationDescriptor));

        //
        // Queries all control settings for the current device.
        // Immediately after connecting the device, if you make an inquiry, it
        // may return STATUS_DEVICE_BUSY. In that case, retry.
        //
        while (retryCount < maxRetry)
        {
            status = deviceContext->UsbAudioConfiguration->QueryDeviceFeatures();
            if (status != STATUS_DEVICE_BUSY)
            {
                break;
            }
            ++retryCount;
        }

        // TBD
        // Normally it is read from the registry and written to the registry when the device is destroyed.
        //
        ULONG desiredSampleRate = UAC_DEFAULT_SAMPLE_RATE;

        // The default is PCM, but for devices that do not support PCM, the format closest to PCM will be selected.
        ULONG desiredFormatType = NS_USBAudio0200::FORMAT_TYPE_I;
        ULONG desiredFormat = NS_USBAudio0200::PCM;
        for (ULONG sampleFormat = 0; sampleFormat < toULong(UACSampleFormat::UAC_SAMPLE_FORMAT_LAST_ENTRY); sampleFormat++)
        {
            if ((deviceContext->AudioProperty.SupportedSampleFormats & (1 << sampleFormat)))
            {
                RETURN_NTSTATUS_IF_FAILED(USBAudioDataFormat::ConvertFormatToSampleFormat((UACSampleFormat)sampleFormat, desiredFormatType, desiredFormat));
                break;
            }
        }

        ULONG inputBytesPerSample = 0;
        ULONG inputValidBitsPerSample = 0;
        ULONG outputBytesPerSample = 0;
        ULONG outputValidBitsPerSample = 0;

        RETURN_NTSTATUS_IF_FAILED(deviceContext->UsbAudioConfiguration->GetMaxSupportedValidBitsPerSample(true, desiredFormatType, desiredFormat, inputBytesPerSample, inputValidBitsPerSample));
        RETURN_NTSTATUS_IF_FAILED(deviceContext->UsbAudioConfiguration->GetMaxSupportedValidBitsPerSample(false, desiredFormatType, desiredFormat, outputBytesPerSample, outputValidBitsPerSample));

        RETURN_NTSTATUS_IF_FAILED(ActivateAudioInterface(deviceContext, desiredSampleRate, desiredFormatType, desiredFormat, inputBytesPerSample, inputValidBitsPerSample, outputBytesPerSample, outputValidBitsPerSample, true));

        if (deviceContext->OutputInterfaceAndPipe.SelectedAlternateSetting != 0)
        {
            RETURN_NTSTATUS_IF_FAILED(SelectAlternateInterface(IsoDirection::Out, deviceContext, deviceContext->AudioProperty.OutputInterfaceNumber, 0));
        }
        if (deviceContext->InputInterfaceAndPipe.SelectedAlternateSetting != 0)
        {
            RETURN_NTSTATUS_IF_FAILED(SelectAlternateInterface(IsoDirection::In, deviceContext, deviceContext->AudioProperty.InputInterfaceNumber, 0));
        }

        ULONG numOfInputDevices = 0, numOfOutputDevices = 0;
        RETURN_NTSTATUS_IF_FAILED(deviceContext->UsbAudioConfiguration->GetStreamDevices(true, numOfInputDevices));
        RETURN_NTSTATUS_IF_FAILED(deviceContext->UsbAudioConfiguration->GetStreamDevices(false, numOfOutputDevices));

        RETURN_NTSTATUS_IF_FAILED(deviceContext->RtPacketObject->AssignDevices(numOfInputDevices, numOfOutputDevices));

        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = deviceContext->Device;

        RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(CStreamEngine *) * numOfInputDevices, &deviceContext->CaptureStreamEngineMemory, (PVOID *)&deviceContext->CaptureStreamEngine));
        RtlZeroMemory(deviceContext->CaptureStreamEngine, sizeof(CStreamEngine *) * numOfInputDevices);
        deviceContext->NumOfInputDevices = numOfInputDevices;

        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = deviceContext->Device;

        RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(CStreamEngine *) * numOfOutputDevices, &deviceContext->RenderStreamEngineMemory, (PVOID *)&deviceContext->RenderStreamEngine));
        RtlZeroMemory(deviceContext->RenderStreamEngine, sizeof(CStreamEngine *) * numOfOutputDevices);
        deviceContext->NumOfOutputDevices = numOfOutputDevices;
    }
    ReportInternalParameters(deviceContext);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "renderDeviceName = %wZ, DeviceName = %ws", &renderCircuitName, deviceContext->DeviceName);
    RETURN_NTSTATUS_IF_FAILED(CodecR_AddStaticRender(device, &CODEC_RENDER_COMPONENT_GUID, &renderCircuitName));

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "captureDeviceName = %wZ, DeviceName = %ws", &captureCircuitName, deviceContext->DeviceName);
    RETURN_NTSTATUS_IF_FAILED(CodecC_AddStaticCapture(device, &CODEC_CAPTURE_COMPONENT_GUID, &MIC_CUSTOM_NAME, &captureCircuitName));

    //
    // To prevent the DMA buffer from becoming a double buffer on a PC
    // with 4GB or more of memory, contiguous memory is allocated in
    // an area less than 4GB.
    //
    RETURN_NTSTATUS_IF_FAILED(deviceContext->ContiguousMemory->Allocate(deviceContext->UsbAudioConfiguration, deviceContext->SupportedControl.MaxBurstOverride, UAC_MAX_CLASSIC_FRAMES_PER_IRP, deviceContext->FramesPerMs));

    //
    // The driver uses this DDI to associate a circuit to a device. After
    // this call the circuit is not visible until the device goes in D0.
    // For a real driver there should be a check here to make sure the
    // circuit has not been added already (there could be a situation where
    // prepareHardware is called multiple times and releaseHardware is only
    // called once).
    //
    if (deviceContext->Render != nullptr)
    {
        RETURN_NTSTATUS_IF_FAILED(AcxDeviceAddCircuit(device, deviceContext->Render));
    }

    if (deviceContext->Capture != nullptr)
    {
        RETURN_NTSTATUS_IF_FAILED(AcxDeviceAddCircuit(device, deviceContext->Capture));
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioAcxDriverEvtDeviceReleaseHardware(
    WDFDEVICE device,
    WDFCMRESLIST /* resourceListTranslated */
)
/*++

Routine Description:

    In this callback, the driver releases the h/w resources allocated in the
    prepare h/w callback.

Arguments:

    device - handle to a device

Return Value:

    NT status value

--*/
{
    NTSTATUS status = STATUS_SUCCESS;
    ;
    PDEVICE_CONTEXT deviceContext;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    deviceContext = GetDeviceContext(device);
    NT_ASSERT(deviceContext != nullptr);

    if (deviceContext->ContiguousMemory != nullptr)
    {
        delete deviceContext->ContiguousMemory;
        deviceContext->ContiguousMemory = nullptr;
    }

    if (deviceContext->UsbAudioConfiguration != nullptr)
    {
        delete deviceContext->UsbAudioConfiguration;
        deviceContext->UsbAudioConfiguration = nullptr;
    }

    //
    // The driver uses this DDI to delete a circuit from the current device.
    //
    if (deviceContext->Render != nullptr)
    {
        RETURN_NTSTATUS_IF_FAILED(AcxDeviceRemoveCircuit(device, deviceContext->Render));
        deviceContext->Render = nullptr;
    }

    if (deviceContext->Capture != nullptr)
    {
        RETURN_NTSTATUS_IF_FAILED(AcxDeviceRemoveCircuit(device, deviceContext->Capture));
        deviceContext->Capture = nullptr;
    }

    if (deviceContext->UsbConfigurationDescriptorHandle != nullptr)
    {
        WdfObjectDelete(deviceContext->UsbConfigurationDescriptorHandle);
        deviceContext->UsbConfigurationDescriptorHandle = nullptr;
        deviceContext->UsbConfigurationDescriptor = nullptr;
    }

    if (deviceContext->DeviceNameMemory != nullptr)
    {
        WdfObjectDelete(deviceContext->DeviceNameMemory);
        deviceContext->DeviceNameMemory = nullptr;
    }
    deviceContext->DeviceName = nullptr;

    if (deviceContext->SerialNumberMemory != nullptr)
    {
        WdfObjectDelete(deviceContext->SerialNumberMemory);
        deviceContext->SerialNumberMemory = nullptr;
    }
    deviceContext->SerialNumber = nullptr;

    if (deviceContext->Pairs != nullptr)
    {
        delete[] deviceContext->Pairs;
        deviceContext->Pairs = nullptr;
    }

    if (deviceContext->CaptureStreamEngineMemory != nullptr)
    {
        WdfObjectDelete(deviceContext->CaptureStreamEngineMemory);
        deviceContext->CaptureStreamEngineMemory = nullptr;
    }

    if (deviceContext->RenderStreamEngineMemory != nullptr)
    {
        WdfObjectDelete(deviceContext->RenderStreamEngineMemory);
        deviceContext->RenderStreamEngineMemory = nullptr;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

NONPAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioAcxDriverEvtDeviceD0Entry(
    WDFDEVICE device,
    WDF_POWER_DEVICE_STATE /* previousState */
)
{
    PDEVICE_CONTEXT deviceContext;

    // PASSIVE_LEVEL, but you should not make this callback function pageable.
    // PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    deviceContext = GetDeviceContext(device);
    ASSERT(deviceContext != nullptr);

    deviceContext->AudioProperty.IsAccessible = TRUE;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioAcxDriverEvtDeviceD0Exit(
    WDFDEVICE              device,
    WDF_POWER_DEVICE_STATE targetState
)
{
    NTSTATUS        status = STATUS_SUCCESS;
    POWER_ACTION    powerAction;
    PDEVICE_CONTEXT deviceContext;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    powerAction = WdfDeviceGetSystemPowerAction(device);

    deviceContext = GetDeviceContext(device);
    ASSERT(deviceContext != nullptr);

    deviceContext->AudioProperty.IsAccessible = FALSE;

    //
    // Update the power policy D3-cold info for Connected Standby.
    //
    if (targetState == WdfPowerDeviceD3 && powerAction == PowerActionNone)
    {
        WDF_TRI_STATE       excludeD3Cold = WdfTrue;
        ACX_DX_EXIT_LATENCY latency;

        //
        // Get the current exit latency.
        //
        latency = AcxDeviceGetCurrentDxExitLatency(device, WdfDeviceGetSystemPowerAction(device), targetState);

        //
        // If the current exit latency for the ACX device is responsive
        // (not instant or fast) then D3-cold does not need to be excluded.
        // Otherwise, D3-cold should be excluded because if the hardware
        // goes into this state it will take too long to go back into D0
        // and respond.
        //
        if (latency == AcxDxExitLatencyResponsive)
        {
            excludeD3Cold = WdfFalse;
        }

        if (deviceContext->ExcludeD3Cold != excludeD3Cold)
        {
            deviceContext->ExcludeD3Cold = excludeD3Cold;

            RETURN_NTSTATUS_IF_FAILED(Codec_SetPowerPolicy(device));
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
static NTSTATUS
Codec_SetPowerPolicy(
    WDFDEVICE device
)
{
    NTSTATUS        status = STATUS_SUCCESS;
    PDEVICE_CONTEXT deviceContext;

    PAGED_CODE();

    deviceContext = GetDeviceContext(device);
    NT_ASSERT(deviceContext != nullptr);

    //
    // Init the idle policy structure.
    //
    WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS idleSettings;
    WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS_INIT(&idleSettings, IdleCannotWakeFromS0);
    idleSettings.IdleTimeout = IDLE_POWER_TIMEOUT;
    idleSettings.IdleTimeoutType = SystemManagedIdleTimeoutWithHint;
    idleSettings.ExcludeD3Cold = deviceContext->ExcludeD3Cold;

    RETURN_NTSTATUS_IF_FAILED(WdfDeviceAssignS0IdleSettings(device, &idleSettings));

    return status;
}

_Use_decl_annotations_
VOID Codec_EvtDeviceContextCleanup(
    WDFOBJECT wdfDevice
)
/*++

Routine Description:

    In this callback, it cleans up device context.

Arguments:

    wdfDevice - WDF device object

Return Value:

    nullptr

--*/
{
    WDFDEVICE       device;
    PDEVICE_CONTEXT deviceContext;

    device = (WDFDEVICE)wdfDevice;
    deviceContext = GetDeviceContext(device);
    NT_ASSERT(deviceContext != nullptr);

    // if (deviceContext->Capture)
    // {
    //     CodecC_CircuitCleanup(deviceContext->Capture);
    // }
}

PAGED_CODE_SEG
static _Use_decl_annotations_
NTSTATUS
ReadAndSelectDescriptors(
    WDFDEVICE device
)
/*++

Routine Description:

    This routine configures the USB device.
    In this routines we get the device descriptor,
    the configuration descriptor and select the
    configuration.

Arguments:

    device - Handle to a framework device

Return Value:

    NTSTATUS - NT status value.

--*/
{
    NTSTATUS        status;
    PDEVICE_CONTEXT deviceContext;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    //
    //  initialize variables
    //
    deviceContext = GetDeviceContext(device);

    //
    //  Create a USB device handle so that we can communicate with the
    //  underlying USB stack. The WDFUSBDEVICE handle is used to query,
    //  configure, and manage all aspects of the USB device.
    //  These aspects include device properties, bus properties,
    //  and I/O creation and synchronization. We only create device the first
    //  the PrepareHardware is called. If the device is restarted by pnp manager
    //  for resource re balance, we will use the same device handle but then select
    //  the interfaces again because the USB stack could reconfigure the device on
    //  restart.
    //
    if (deviceContext->UsbDevice == nullptr)
    {
        WDF_USB_DEVICE_CREATE_CONFIG config;

        WDF_USB_DEVICE_CREATE_CONFIG_INIT(&config, USBD_CLIENT_CONTRACT_VERSION_602);

        status = WdfUsbTargetDeviceCreateWithParameters(device, &config, WDF_NO_OBJECT_ATTRIBUTES, &deviceContext->UsbDevice);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfUsbTargetDeviceCreateWithParameters failed with Status code %!STATUS!", status);
            return status;
        }
    }

    WdfUsbTargetDeviceGetDeviceDescriptor(deviceContext->UsbDevice, &deviceContext->UsbDeviceDescriptor);

    NT_ASSERT(deviceContext->UsbDeviceDescriptor.bNumConfigurations);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "bNumConfigurations 0x%x", deviceContext->UsbDeviceDescriptor.bNumConfigurations);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "bcdDevice		   0x%x", deviceContext->UsbDeviceDescriptor.bcdDevice);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "iProduct		   0x%x", deviceContext->UsbDeviceDescriptor.iProduct);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "iSerialNumber	   0x%x", deviceContext->UsbDeviceDescriptor.iSerialNumber);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "idProduct		   0x%x", deviceContext->UsbDeviceDescriptor.idProduct);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "idVendor           0x%x", deviceContext->UsbDeviceDescriptor.idVendor);

    status = ConfigureDevice(device);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
static _Use_decl_annotations_
NTSTATUS
ConfigureDevice(
    WDFDEVICE device
)
/*++

Routine Description:

    This helper routine reads the configuration descriptor
    for the device in couple of steps.

Arguments:

    device - Handle to a framework device

Return Value:

    NTSTATUS - NT status value

--*/
{
    USHORT                        size = 0;
    NTSTATUS                      status;
    PDEVICE_CONTEXT               deviceContext;
    PUSB_CONFIGURATION_DESCRIPTOR configurationDescriptor = nullptr;
    WDF_OBJECT_ATTRIBUTES         attributes;
    WDFMEMORY                     memory = nullptr;
    PUCHAR                        offset = nullptr;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    //
    //  initialize the variables
    //
    configurationDescriptor = nullptr;
    deviceContext = GetDeviceContext(device);

    deviceContext->UsbConfigurationDescriptor = nullptr;
    deviceContext->UsbConfigurationDescriptorHandle = nullptr;

    auto configureDeviceScope = wil::scope_exit([&]() {
        if (!NT_SUCCESS(status) && (memory != nullptr))
        {
            WdfObjectDelete(memory);
        }
    });

    //
    //  Read the first configuration descriptor
    //  This requires two steps:
    //  1. Ask the WDFUSBDEVICE how big it is
    //  2. Allocate it and get it from the WDFUSBDEVICE
    //
    status = WdfUsbTargetDeviceRetrieveConfigDescriptor(deviceContext->UsbDevice, nullptr, &size);

    if (status != STATUS_BUFFER_TOO_SMALL || size == 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! failed %!STATUS!", status);
        return status;
    }

    //
    //  Create a memory object and specify usbdevice as the parent so that
    //  it will be freed automatically.
    //
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

    attributes.ParentObject = deviceContext->UsbDevice;

    status = WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, size, &memory, (PVOID *)&configurationDescriptor);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! failed %!STATUS!", status);
        return status;
    }
    RtlZeroMemory(configurationDescriptor, size);

    status = WdfUsbTargetDeviceRetrieveConfigDescriptor(deviceContext->UsbDevice, configurationDescriptor, &size);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! failed %!STATUS!", status);
        return status;
    }

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! Descriptor validation failed with Status code %x and at the offset %p", status, offset);
        return status;
    }

    deviceContext->UsbConfigurationDescriptor = configurationDescriptor;
    deviceContext->UsbConfigurationDescriptorHandle = memory;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

PAGED_CODE_SEG
static _Use_decl_annotations_
NTSTATUS
AbortPipes(
    IsoDirection direction,
    WDFDEVICE    device
)
/*++

Routine Description

    sends an abort pipe request on all open pipes.

Arguments:

    device - Handle to a framework device

Return Value:

    NT status value

--*/
{
    ULONG           count;
    NTSTATUS        status;
    PDEVICE_CONTEXT deviceContext;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");
    //
    // initialize variables
    //
    deviceContext = GetDeviceContext(device);

    DEVICE_CONTEXT::SelectedInterfaceAndPipe & selectedInterfaceAndPipe = (direction == IsoDirection::In) ? deviceContext->InputInterfaceAndPipe : (direction == IsoDirection::Out) ? deviceContext->OutputInterfaceAndPipe
                                                                                                                                                                                    : deviceContext->FeedbackInterfaceAndPipe;

    count = selectedInterfaceAndPipe.NumberConfiguredPipes;

    if (selectedInterfaceAndPipe.UsbInterface != nullptr)
    {
        for (UCHAR pipeIndex = 0; pipeIndex < count; pipeIndex++)
        {
            WDFUSBPIPE pipe;
            pipe = WdfUsbInterfaceGetConfiguredPipe(selectedInterfaceAndPipe.UsbInterface, pipeIndex, nullptr);

            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "Aborting open pipe %d", pipeIndex);

            status = WdfUsbTargetPipeAbortSynchronously(pipe,
                                                        WDF_NO_HANDLE, // WDFREQUEST
                                                        nullptr);      // PWDF_REQUEST_SEND_OPTIONS

            if (!NT_SUCCESS(status))
            {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! failed WdfUsbTargetPipeAbortSynchronously failed %!STATUS!", status);
                break;
            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
static _Use_decl_annotations_
NTSTATUS
InitializePipeContextForSuperSpeedDevice(
    PDEVICE_CONTEXT deviceContext,
    WDFUSBINTERFACE Interface,
    UCHAR           selectedAlternateSetting,
    WDFUSBPIPE      pipe
)
/*++

Routine Description

    This function initialize pipe context for super speed isoch and
    bulk endpoints.

Return Value:

    NT status value

--*/
{
    WDF_USB_PIPE_INFORMATION pipeInfo;
    NTSTATUS                 status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    WDF_USB_PIPE_INFORMATION_INIT(&pipeInfo);
    WdfUsbTargetPipeGetInformation(pipe, &pipeInfo);

    //
    // We only use pipe context for super speed isoch and bulk speed bulk endpoints.
    //
    if ((WdfUsbPipeTypeIsochronous == pipeInfo.PipeType))
    {

        status = InitializePipeContextForSuperSpeedIsochPipe(deviceContext, WdfUsbInterfaceGetInterfaceNumber(Interface), selectedAlternateSetting, pipe);
    }
    else if (WdfUsbPipeTypeBulk == pipeInfo.PipeType)
    {

        ASSERT(WdfUsbPipeTypeBulk != pipeInfo.PipeType);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
static _Use_decl_annotations_
PUSB_ENDPOINT_DESCRIPTOR
GetEndpointDescriptorForEndpointAddress(
    PDEVICE_CONTEXT                                 deviceContext,
    UCHAR                                           InterfaceNumber,
    UCHAR                                           selectedAlternateSetting,
    UCHAR                                           endpointAddress,
    PUSB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR * endpointCompanionDescriptor
)
/*++

Routine Description:

    The helper routine gets the Endpoint Descriptor matched with endpointAddress and return
    its Endpoint Companion Descriptor if it has.

    USBAudioAcxDriverValidateConfigurationDescriptor already validates that descriptors lie within
    allocated buffer.

Arguments:

    deviceContext - pointer to the device context which includes configuration descriptor

    interfaceNumber - interfaceNumber of selected interface

    endpointAddress - endpointAddress of the Pipe

    endpointCompanionDescriptor - pointer to the Endpoint Companion Descriptor pointer

Return Value:

    Pointer to Endpoint Descriptor

--*/
{

    PUSB_COMMON_DESCRIPTOR        pCommonDescriptorHeader = nullptr;
    PUSB_CONFIGURATION_DESCRIPTOR pConfigurationDescriptor = nullptr;
    PUSB_INTERFACE_DESCRIPTOR     pInterfaceDescriptor = nullptr;
    PUSB_ENDPOINT_DESCRIPTOR      pEndpointDescriptor = nullptr;
    PUCHAR                        startingPosition;
    ULONG                         index;
    bool                          found = false;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - interface %u, alternate %u, endpoint %u", InterfaceNumber, selectedAlternateSetting, endpointAddress);

    pConfigurationDescriptor = deviceContext->UsbConfigurationDescriptor;

    *endpointCompanionDescriptor = nullptr;

    //
    // Parse the ConfigurationDescriptor (including all Interface and
    // Endpoint Descriptors) and locate a Interface Descriptor which
    // matches the interfaceNumber, AlternateSetting, InterfaceClass,
    // InterfaceSubClass, and InterfaceProtocol parameters.
    //
    pInterfaceDescriptor = USBD_ParseConfigurationDescriptorEx(
        pConfigurationDescriptor,
        pConfigurationDescriptor,
        InterfaceNumber,
        selectedAlternateSetting,
        -1, // InterfaceClass, don't care
        -1, // InterfaceSubClass, don't care
        -1  // InterfaceProtocol, don't care
    );

    if (pInterfaceDescriptor == nullptr)
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! USBD_ParseConfigurationDescriptorEx failed to retrieve Interface Descriptor.");
        goto End;
    }

    startingPosition = (PUCHAR)pInterfaceDescriptor;

    for (index = 0; index < pInterfaceDescriptor->bNumEndpoints; index++)
    {
        pCommonDescriptorHeader = USBD_ParseDescriptors(pConfigurationDescriptor, pConfigurationDescriptor->wTotalLength, startingPosition, USB_ENDPOINT_DESCRIPTOR_TYPE);
        if (pCommonDescriptorHeader == nullptr)
        {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! USBD_ParseDescriptors failed to retrieve SuperSpeed Endpoint Descriptor unexpectedly");
            goto End;
        }

        //
        // USBAudioAcxDriverValidateConfigurationDescriptor validates all descriptors.
        // This means that the descriptor pointed to by pCommonDescriptorHeader( received above ) is completely
        // contained within the buffer representing ConfigurationDescriptor and
        // it also verifies that pCommonDescriptorHeader->bLength is equal to sizeof(USB_ENDPOINT_DESCRIPTOR).
        //

        pEndpointDescriptor = (PUSB_ENDPOINT_DESCRIPTOR)pCommonDescriptorHeader;

        //
        // Search an Endpoint Descriptor that matches the endpointAddress
        //
        if (pEndpointDescriptor->bEndpointAddress == endpointAddress)
        {

            found = true;

            break;
        }

        //
        // Skip the current Endpoint Descriptor and search for the next.
        //
        startingPosition = (PUCHAR)pCommonDescriptorHeader + pCommonDescriptorHeader->bLength;
    }

    if (found)
    {
        //
        // Locate the SuperSpeed Endpoint Companion Descriptor associated with the endpoint descriptor
        //
        pCommonDescriptorHeader = USBD_ParseDescriptors(pConfigurationDescriptor, pConfigurationDescriptor->wTotalLength, pEndpointDescriptor, USB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR_TYPE);
        if (pCommonDescriptorHeader != nullptr)
        {

            //
            // USBAudioAcxDriverValidateConfigurationDescriptor validates all descriptors.
            // This means that the descriptor pointed to by pCommonDescriptorHeader( received above ) is completely
            // contained within the buffer representing ConfigurationDescriptor and
            // it also verifies that pCommonDescriptorHeader->bLength is >= sizeof(USB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR)
            //

            *endpointCompanionDescriptor =
                (PUSB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR)pCommonDescriptorHeader;
        }
        else
        {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! USBD_ParseDescriptors failed to retrieve SuperSpeed Endpoint Companion Descriptor unexpectedly");
        }
    }

End:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
    return pEndpointDescriptor;
}

PAGED_CODE_SEG
static _Use_decl_annotations_
NTSTATUS
InitializePipeContextForSuperSpeedIsochPipe(
    PDEVICE_CONTEXT deviceContext,
    UCHAR           interfaceNumber,
    UCHAR           selectedAlternateSetting,
    WDFUSBPIPE      pipe
)
/*++

Routine Description

    This function validates all the isoch related fields in the endpoint descriptor
    to make sure it's in conformance with the spec and Microsoft core stack
    implementation and initializes the pipe context.

    The TransferSizePerMicroframe and TransferSizePerFrame values will be
    used in the I/O path to do read and write transfers.

Return Value:

    NT status value

-*/
{
    WDF_USB_PIPE_INFORMATION                      pipeInfo;
    PPIPE_CONTEXT                                 pipeContext;
    UCHAR                                         endpointAddress;
    PUSB_ENDPOINT_DESCRIPTOR                      pEndpointDescriptor;
    PUSB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR pEndpointCompanionDescriptor;
    USHORT                                        wMaxPacketSize;
    UCHAR                                         bMaxBurst;
    UCHAR                                         bMult;
    USHORT                                        wBytesPerInterval;

    PAGED_CODE();

    WDF_USB_PIPE_INFORMATION_INIT(&pipeInfo);
    WdfUsbTargetPipeGetInformation(pipe, &pipeInfo);

    //
    // We use the pipe context only for isoch endpoints.
    //
    if ((WdfUsbPipeTypeIsochronous != pipeInfo.PipeType))
    {

        return STATUS_SUCCESS;
    }

    pipeContext = GetPipeContext(pipe);

    endpointAddress = pipeInfo.EndpointAddress;

    pEndpointDescriptor = GetEndpointDescriptorForEndpointAddress(
        deviceContext,
        interfaceNumber,
        selectedAlternateSetting,
        endpointAddress,
        &pEndpointCompanionDescriptor
    );

    if (pEndpointDescriptor == nullptr || pEndpointCompanionDescriptor == nullptr)
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! failed. pEndpointDescriptor or pEndpointCompanionDescriptor is invalid (nullptr)");
        return STATUS_INVALID_PARAMETER;
    }

    //
    // For SuperSpeed isoch endpoint, it uses wBytesPerInterval from its
    // endpoint companion descriptor. If bMaxBurst field in its endpoint
    // companion descriptor is greater than zero, wMaxPacketSize must be
    // 1024. If the value in the bMaxBurst field is set to zero then
    // wMaxPacketSize can have any value from 0 to 1024.
    //
    wBytesPerInterval = pEndpointCompanionDescriptor->wBytesPerInterval;
    wMaxPacketSize = pEndpointDescriptor->wMaxPacketSize;
    bMaxBurst = pEndpointCompanionDescriptor->bMaxBurst;
    bMult = pEndpointCompanionDescriptor->bmAttributes.Isochronous.Mult;

    if (wBytesPerInterval > (wMaxPacketSize * (bMaxBurst + 1) * (bMult + 1)))
    {

        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! failed. SuperSpeed isochronous endpoints's wBytesPerInterval value (%d) is greater than wMaxPacketSize * (bMaxBurst+1) * (Mult +1) (%d) ", wBytesPerInterval, (wMaxPacketSize * (bMaxBurst + 1) * (bMult + 1)));
        return STATUS_INVALID_PARAMETER;
    }

    if (bMaxBurst > 0)
    {

        if (wMaxPacketSize != USB_ENDPOINT_SUPERSPEED_ISO_MAX_PACKET_SIZE)
        {

            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! failed. SuperSpeed isochronous endpoints must have wMaxPacketSize value of %d bytes when bMaxpBurst is %d ", USB_ENDPOINT_SUPERSPEED_ISO_MAX_PACKET_SIZE, bMaxBurst);
            return STATUS_INVALID_PARAMETER;
        }
    }
    else
    {

        if (wMaxPacketSize > USB_ENDPOINT_SUPERSPEED_ISO_MAX_PACKET_SIZE)
        {

            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! failed. SuperSpeed isochronous endpoints must have wMaxPacketSize value no more than %d bytes when bMaxpBurst is %d ", USB_ENDPOINT_SUPERSPEED_ISO_MAX_PACKET_SIZE, bMaxBurst);
            return STATUS_INVALID_PARAMETER;
        }
    }

    //
    // This sample demos how to use wBytesPerInterval from its Endpoint
    // Companion Descriptor. Actually, for Superspeed isochronous endpoints,
    // MaximumPacketSize in WDF_USB_PIPE_INFORMATION and USBD_PIPE_INFORMATION
    // is returned with the value of wBytesPerInterval in the endpoint
    // companion descriptor. This is different than the true MaxPacketSize of
    // the endpoint descriptor.
    //
    NT_ASSERT(pipeInfo.MaximumPacketSize == wBytesPerInterval);
    pipeContext->TransferSizePerMicroframe = wBytesPerInterval;

    //
    // Microsoft USB 3.0 stack only supports bInterval value of 1, 2, 3 and 4
    // (or polling period of 1, 2, 4 and 8).
    // For super-speed isochronous endpoints, the bInterval value is used as
    // the exponent for a 2^(bInterval-1) value expressed in microframes;
    // e.g., a bInterval of 4 means a period of 8 (2^(4-1)) microframes.
    //
    if (pipeInfo.Interval == 0 || pipeInfo.Interval > 4)
    {

        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! failed. bInterval value %u in pipeInfo is invalid (0 or > 4)", pipeInfo.Interval);
        return STATUS_INVALID_PARAMETER;
    }

    switch (pipeInfo.Interval)
    {
    case 1:
        //
        // Transfer period is every microframe (8 times a frame).
        //
        pipeContext->TransferSizePerFrame = pipeContext->TransferSizePerMicroframe * 8;
        break;

    case 2:
        //
        // Transfer period is every 2 microframes (4 times a frame).
        //
        pipeContext->TransferSizePerFrame = pipeContext->TransferSizePerMicroframe * 4;
        break;

    case 3:
        //
        // Transfer period is every 4 microframes (2 times a frame).
        //
        pipeContext->TransferSizePerFrame = pipeContext->TransferSizePerMicroframe * 2;
        break;

    case 4:
        //
        // Transfer period is every 8 microframes (1 times a frame).
        //
        pipeContext->TransferSizePerFrame = pipeContext->TransferSizePerMicroframe;
        break;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "MaxPacketSize = %d, bInterval = %d", pipeInfo.MaximumPacketSize, pipeInfo.Interval);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "TransferSizePerFrame = %d, TransferSizePerMicroframe = %d", pipeContext->TransferSizePerFrame, pipeContext->TransferSizePerMicroframe);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
static _Use_decl_annotations_
NTSTATUS
InitializePipeContextForHighSpeedDevice(
    WDFUSBPIPE pipe
)
/*++

Routine Description

    This function validates all the isoch related fields in the endpoint descriptor
    to make sure it's in conformance with the spec and Microsoft core stack
    implementation and initializes the pipe context.

    The TransferSizePerMicroframe and TransferSizePerFrame values will be
    used in the I/O path to do read and write transfers.

Return Value:

    NT status value

--*/
{
    WDF_USB_PIPE_INFORMATION pipeInfo;
    PPIPE_CONTEXT            pipeContext;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    WDF_USB_PIPE_INFORMATION_INIT(&pipeInfo);
    WdfUsbTargetPipeGetInformation(pipe, &pipeInfo);

    //
    // We use the pipe context only for isoch endpoints.
    //
    if ((WdfUsbPipeTypeIsochronous != pipeInfo.PipeType))
    {
        return STATUS_SUCCESS;
    }

    pipeContext = GetPipeContext(pipe);

    if (pipeInfo.MaximumPacketSize == 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! failed. MaximumPacketSize in the pipeInfo is invalid (zero)");
        return STATUS_INVALID_PARAMETER;
    }

    //
    // Universal Serial Bus Specification Revision 2.0 5.6.3 Isochronous Transfer
    // Packet Size Constraints: High-speed endpoints are allowed up to 1024-byte data
    // payloads per microframe and allowed up to a maximum of 3 transactions per microframe.
    //
    // For highspeed isoch endpoints, bits 12-11 of wMaxPacketSize in the endpoint descriptor
    // specify the number of additional transactions oppurtunities per microframe.
    // 00 - None (1 transaction per microframe)
    // 01 - 1 additional (2 per microframe)
    // 10 - 2 additional (3 per microframe)
    // 11 - Reserved.
    //
    // Note: MaximumPacketSize of WDF_USB_PIPE_INFORMATION is already adjusted to include
    // additional transactions if it is a high bandwidth pipe.
    //

    if (pipeInfo.MaximumPacketSize > 1024 * 3)
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! failed. MaximumPacketSize in the endpoint descriptor is invalid (>1024*3)");
        return STATUS_INVALID_PARAMETER;
    }

    //
    // Microsoft USB stack only supports bInterval value of 1, 2, 3 and 4 (or polling period of 1, 2, 4 and 8).
    //
    if (pipeInfo.Interval == 0 || pipeInfo.Interval > 4)
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! failed. bInterval value %u in pipeInfo is invalid (0 or > 4)", pipeInfo.Interval);
        return STATUS_INVALID_PARAMETER;
    }

    pipeContext->TransferSizePerMicroframe = pipeInfo.MaximumPacketSize;

    //
    // For high-speed isochronous endpoints, the bInterval value is used
    // as the exponent for a 2^(bInterval-1) value expressed in
    // microframes; e.g., a bInterval of 4 means a period of 8 (2^(4-1))
    // microframes. The bInterval value must be from 1 to 16.  NOTE: The
    // USBPORT.SYS driver only supports high-speed isochronous bInterval
    // values of {1, 2, 3, 4}.
    //
    switch (pipeInfo.Interval)
    {
    case 1:
        //
        // Transfer period is every microframe (8 times a frame).
        //
        pipeContext->TransferSizePerFrame = pipeContext->TransferSizePerMicroframe * 8;
        break;

    case 2:
        //
        // Transfer period is every 2 microframes (4 times a frame).
        //
        pipeContext->TransferSizePerFrame = pipeContext->TransferSizePerMicroframe * 4;
        break;

    case 3:
        //
        // Transfer period is every 4 microframes (2 times a frame).
        //
        pipeContext->TransferSizePerFrame = pipeContext->TransferSizePerMicroframe * 2;
        break;

    case 4:
        //
        // Transfer period is every 8 microframes (1 times a frame).
        //
        pipeContext->TransferSizePerFrame = pipeContext->TransferSizePerMicroframe;
        break;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "MaxPacketSize = %d, bInterval = %d", pipeInfo.MaximumPacketSize, pipeInfo.Interval);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "TransferSizePerFrame = %d, TransferSizePerMicroframe = %d", pipeContext->TransferSizePerFrame, pipeContext->TransferSizePerMicroframe);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
static _Use_decl_annotations_
NTSTATUS
InitializePipeContextForFullSpeedDevice(
    WDFUSBPIPE pipe
)
/*++

Routine Description

    This function validates all the isoch related fields in the endpoint descriptor
    to make sure it's in conformance with the spec and Microsoft core stack
    implementation and initializes the pipe context.

    The TransferSizePerMicroframe and TransferSizePerFrame values will be
    used in the I/O path to do read and write transfers.

Return Value:

    NT status value

--*/
{
    WDF_USB_PIPE_INFORMATION pipeInfo;
    PPIPE_CONTEXT            pipeContext;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    WDF_USB_PIPE_INFORMATION_INIT(&pipeInfo);
    WdfUsbTargetPipeGetInformation(pipe, &pipeInfo);

    //
    // We use the pipe context only for isoch endpoints.
    //
    if ((WdfUsbPipeTypeIsochronous != pipeInfo.PipeType))
    {
        return STATUS_SUCCESS;
    }

    pipeContext = GetPipeContext(pipe);

    if (pipeInfo.MaximumPacketSize == 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! failed. MaximumPacketSize in the endpoint descriptor is invalid");
        return STATUS_INVALID_PARAMETER;
    }

    //
    // Universal Serial Bus Specification Revision 2.0
    // 5.6.3 Isochronous Transfer Packet Size Constraints
    //
    // The USB limits the maximum data payload size to 1,023 bytes
    // for each full-speed isochronous endpoint.
    //
    if (pipeInfo.MaximumPacketSize > 1023)
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! failed. MaximumPacketSize in the endpoint descriptor is invalid");
        return STATUS_INVALID_PARAMETER;
    }

    //
    // Microsoft USB stack only supports bInterval value of 1 for
    // full-speed isochronous endpoints.
    //
    if (pipeInfo.Interval != 1)
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! failed. bInterval value in endpoint descriptor is invalid");
        return STATUS_INVALID_PARAMETER;
    }

    pipeContext->TransferSizePerFrame = pipeInfo.MaximumPacketSize;
    pipeContext->TransferSizePerMicroframe = 0;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "TransferSizePerFrame = %d", pipeContext->TransferSizePerFrame);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
static _Use_decl_annotations_
NTSTATUS
RetrieveDeviceInformation(
    WDFDEVICE device
)
{
    PDEVICE_CONTEXT            deviceContext;
    WDF_USB_DEVICE_INFORMATION info;
    NTSTATUS                   status;
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    deviceContext = GetDeviceContext(device);

    WDF_USB_DEVICE_INFORMATION_INIT(&info);

    //
    // Retrieve USBD version information, port driver capabilities and device
    // capabilities such as speed, power, etc.
    //
    status = WdfUsbTargetDeviceRetrieveInformation(deviceContext->UsbDevice, &info);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! failed %!STATUS!", status);
        return status;
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - HcdPortCapabilities = 0x%x, Traits = 0x%x", info.HcdPortCapabilities, info.Traits);
    deviceContext->IsDeviceHighSpeed = (info.Traits & WDF_USB_DEVICE_TRAIT_AT_HIGH_SPEED) ? true : false;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, " - DeviceIsHighSpeed: %!bool!", deviceContext->IsDeviceHighSpeed);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, " - IsDeviceSelfPowered: %!bool!", (info.Traits & WDF_USB_DEVICE_TRAIT_SELF_POWERED) ? TRUE : FALSE);

    deviceContext->IsDeviceRemoteWakeable = (info.Traits & WDF_USB_DEVICE_TRAIT_REMOTE_WAKE_CAPABLE) ? true : false;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, " - IsDeviceRemoteWakeable: %!bool!", deviceContext->IsDeviceRemoteWakeable);

    status = GetStackCapability(deviceContext, &GUID_USB_CAPABILITY_DEVICE_CONNECTION_SUPER_SPEED_COMPATIBLE, 0, nullptr);
    if (NT_SUCCESS(status))
    {
        deviceContext->IsDeviceSuperSpeed = true;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, " - DeviceIsSuperSpeed: %!bool!", deviceContext->IsDeviceSuperSpeed);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
_Use_decl_annotations_
VOID USBAudioAcxDriverEvtDeviceContextCleanup(
    WDFOBJECT wdfDevice
)
/*++

Routine Description:

    In this callback, it cleans up device context.

Arguments:

    wdfDevice - WDF device object

Return Value:

    nullptr

--*/
{
    WDFDEVICE       device;
    PDEVICE_CONTEXT pDevContext;

    //
    // EvtCleanupCallback for WDFDEVICE is always called at PASSIVE_LEVEL
    //
    _IRQL_limited_to_(PASSIVE_LEVEL);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    device = (WDFDEVICE)wdfDevice;

    pDevContext = GetDeviceContext(device);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

VOID USBAudioAcxDriverEvtPipeContextCleanup(
    IN WDFOBJECT wdfObject
)
/*++

Routine Description:

    In this callback, it cleans up pipe context.

Arguments:

    wdfObject - WDFUSBPIPE object

Return Value:

    nullptr

--*/
{
    //
    // EvtCleanupCallback for WDFUSBPIPE is always called at IRQL <= DISPATCH_LEVEL
    //
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    WDFUSBPIPE    pipe;
    PPIPE_CONTEXT pipeContext = nullptr;

    pipe = (WDFUSBPIPE)wdfObject;
    pipeContext = GetPipeContext(pipe);
    if ((pipeContext != nullptr) && (pipeContext->SelectedInterfaceAndPipe != nullptr))
    {
        pipeContext->SelectedInterfaceAndPipe->UsbInterface = 0;
        pipeContext->SelectedInterfaceAndPipe->SelectedAlternateSetting = 0;
        pipeContext->SelectedInterfaceAndPipe->NumberConfiguredPipes = 0;
        pipeContext->SelectedInterfaceAndPipe->MaximumTransferSize = 0;
        pipeContext->SelectedInterfaceAndPipe->Pipe = nullptr;
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

NONPAGED_CODE_SEG
_Use_decl_annotations_
const char *
GetDirectionString(
    _In_ IsoDirection direction
)
{
    const char * directionNames[] = {
        "In",
        "Out",
        "Feedback",
    };

    ASSERT(toULONG(direction) < toULONG(IsoDirection::NumOfIsoDirection));

    return directionNames[toULONG(direction)];
}

PAGED_CODE_SEG
static _Use_decl_annotations_
NTSTATUS SelectConfiguration(
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS                            status = STATUS_SUCCESS;
    PWDF_USB_INTERFACE_SETTING_PAIR     settingPairs = nullptr;
    WDF_USB_DEVICE_SELECT_CONFIG_PARAMS configParams;

    _IRQL_limited_to_(PASSIVE_LEVEL);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    auto selectConfigurationScope = wil::scope_exit([&]() {
        if (settingPairs != nullptr)
        {
            delete settingPairs;
            settingPairs = nullptr;
        }
    });

    // Select the first configuration of the device, using the first alternate
    // setting of each interface
    UCHAR numInterfaces = WdfUsbTargetDeviceGetNumInterfaces(deviceContext->UsbDevice);

    NT_ASSERT(numInterfaces > 0);
    if (numInterfaces == 1)
    {
        WDF_USB_DEVICE_SELECT_CONFIG_PARAMS_INIT_SINGLE_INTERFACE(&configParams);
    }
    else
    {
        settingPairs = new (POOL_FLAG_NON_PAGED, DRIVER_TAG) WDF_USB_INTERFACE_SETTING_PAIR[numInterfaces];
        RETURN_NTSTATUS_IF_TRUE_ACTION(settingPairs == nullptr, status = STATUS_INSUFFICIENT_RESOURCES, status);
        RtlZeroMemory(settingPairs, sizeof(WDF_USB_INTERFACE_SETTING_PAIR) * numInterfaces);
        for (UCHAR interfaceIndex = 0; interfaceIndex < numInterfaces; interfaceIndex++)
        {
            settingPairs[interfaceIndex].UsbInterface = WdfUsbTargetDeviceGetInterface(deviceContext->UsbDevice, interfaceIndex);

            //
            //  Select alternate setting zero on all interfaces.
            //
            settingPairs[interfaceIndex].SettingIndex = 0;
        }
        WDF_USB_DEVICE_SELECT_CONFIG_PARAMS_INIT_MULTIPLE_INTERFACES(&configParams, numInterfaces, settingPairs);
    }
    status = WdfUsbTargetDeviceSelectConfig(deviceContext->UsbDevice, WDF_NO_OBJECT_ATTRIBUTES, &configParams);
    RETURN_NTSTATUS_IF_FAILED_MSG(status, "WdfUsbTargetDeviceSelectConfig failed");

    if (numInterfaces == 1)
    {
        deviceContext->SelectConfigType = WdfUsbTargetDeviceSelectConfigTypeSingleInterface;
        deviceContext->Pairs = static_cast<PWDF_USB_INTERFACE_SETTING_PAIR>(ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(WDF_USB_INTERFACE_SETTING_PAIR), DRIVER_TAG));
        RETURN_NTSTATUS_IF_TRUE_ACTION(deviceContext->Pairs == nullptr, status = STATUS_INSUFFICIENT_RESOURCES, status);
        deviceContext->Pairs->UsbInterface = configParams.Types.SingleInterface.ConfiguredUsbInterface;
        deviceContext->Pairs->SettingIndex = 0;
        deviceContext->NumberOfConfiguredInterfaces = 1;
    }
    else
    {
        deviceContext->SelectConfigType = WdfUsbTargetDeviceSelectConfigTypeMultiInterface;
        deviceContext->Pairs = settingPairs;
        settingPairs = nullptr;
        deviceContext->NumberOfConfiguredInterfaces = configParams.Types.MultiInterface.NumberOfConfiguredInterfaces;
    }

    //
    // Since Configuration is selected in
    // WdfUsbTargetDeviceSelectConfig,
    // USBD_CreateConfigurationRequestEx is not necessary.
    //

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");

    return status;
}

PAGED_CODE_SEG
static _Use_decl_annotations_
NTSTATUS SelectAlternateInterface(
    IsoDirection    direction,
    PDEVICE_CONTEXT deviceContext,
    UCHAR           interfaceNumber,
    UCHAR           alternateSetting
)
{
    NTSTATUS                                   status = STATUS_SUCCESS;
    DEVICE_CONTEXT::SelectedInterfaceAndPipe & selectedInterfaceAndPipe = (direction == IsoDirection::In) ? deviceContext->InputInterfaceAndPipe : (direction == IsoDirection::Out) ? deviceContext->OutputInterfaceAndPipe
                                                                                                                                                                                    : deviceContext->FeedbackInterfaceAndPipe;
    ASSERT(direction != IsoDirection::Feedback);

    _IRQL_limited_to_(PASSIVE_LEVEL);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry, interfaceNumber = %u, alternateSetting = %u", interfaceNumber, alternateSetting);

    if (deviceContext->SupportedControl.AvoidToSetSameAlternate && (selectedInterfaceAndPipe.SelectedAlternateSetting == alternateSetting))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "Skipping (already selected) Interface %u, Alternate %u.", interfaceNumber, alternateSetting);
        return STATUS_SUCCESS;
    }

    //
    // Get the interface descriptor for the specified interface number
    // and alternate setting.
    //
    PUSB_CONFIGURATION_DESCRIPTOR configDesc = deviceContext->UsbConfigurationDescriptor;
    PUSB_INTERFACE_DESCRIPTOR     interfaceDesc = USBD_ParseConfigurationDescriptorEx(
        configDesc,
        configDesc,
        interfaceNumber,
        alternateSetting,
        -1,
        -1,
        -1
    );

    IF_TRUE_ACTION_JUMP(interfaceDesc == nullptr, status = STATUS_INVALID_PARAMETER, SelectAlternateInterface_Exit);

    WDF_USB_INTERFACE_SELECT_SETTING_PARAMS selectSettingParams;
    UCHAR                                   numberAlternateSettings = 0;
    UCHAR                                   numberConfiguredPipes = 0;
    WDF_OBJECT_ATTRIBUTES                   pipeAttributes;

    if (WdfUsbTargetDeviceGetNumInterfaces(deviceContext->UsbDevice) > 0)
    {
        status = RetrieveDeviceInformation(deviceContext->Device);
        RETURN_NTSTATUS_IF_FAILED_MSG(status, "RetrieveDeviceInformation failed");
    }
    WDFUSBINTERFACE usbInterface = nullptr;

    UCHAR numInterfaces = WdfUsbTargetDeviceGetNumInterfaces(deviceContext->UsbDevice);
    for (UCHAR interfaceIndex = 0; interfaceIndex < numInterfaces; interfaceIndex++)
    {
        if (WdfUsbInterfaceGetInterfaceNumber(deviceContext->Pairs[interfaceIndex].UsbInterface) == interfaceNumber)
        {
            usbInterface = deviceContext->Pairs[interfaceIndex].UsbInterface;
            break;
        }
    }

    IF_TRUE_ACTION_JUMP(usbInterface == nullptr, status = STATUS_INVALID_PARAMETER, SelectAlternateInterface_Exit);

    numberAlternateSettings = WdfUsbInterfaceGetNumSettings(usbInterface);

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - interfaceNumber %u, WdfUsbInterfaceGetInterfaceNumber %u, AlternateSetting %u", interfaceNumber, WdfUsbInterfaceGetInterfaceNumber(usbInterface), alternateSetting);

    ASSERT(numberAlternateSettings > 0);

    WDF_USB_INTERFACE_SELECT_SETTING_PARAMS_INIT_SETTING(&selectSettingParams, alternateSetting);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&pipeAttributes, PIPE_CONTEXT);

    pipeAttributes.EvtCleanupCallback = USBAudioAcxDriverEvtPipeContextCleanup;

    //
    // If an alternate setting has already been specified, this call
    // will invoke USBAudioAcxDriverEvtPipeContextCleanup() and
    // initialize DEVICE_CONTEXT::SelectedInterfaceAndPipe.
    // Therefore, DEVICE_CONTEXT::SelectedInterfaceAndPipe should not
    // be used until it has been updated.
    //
    status = WdfUsbInterfaceSelectSetting(usbInterface, &pipeAttributes, &selectSettingParams);

    if (NT_SUCCESS(status))
    {
        numberConfiguredPipes = WdfUsbInterfaceGetNumConfiguredPipes(usbInterface);

        selectedInterfaceAndPipe.UsbInterface = usbInterface;
        selectedInterfaceAndPipe.InterfaceDescriptor = interfaceDesc;
        selectedInterfaceAndPipe.SelectedAlternateSetting = alternateSetting;
        selectedInterfaceAndPipe.NumberConfiguredPipes = numberConfiguredPipes;
        if (numberConfiguredPipes > 0)
        {

            switch (direction)
            {
            case IsoDirection::In:
                selectedInterfaceAndPipe.MaximumTransferSize = deviceContext->InputIsoPacketSize * UAC_MAX_CLASSIC_FRAMES_PER_IRP * deviceContext->FramesPerMs;
                break;
            case IsoDirection::Out:
                selectedInterfaceAndPipe.MaximumTransferSize = deviceContext->OutputIsoPacketSize * UAC_MAX_CLASSIC_FRAMES_PER_IRP * deviceContext->FramesPerMs;
                break;
            case IsoDirection::Feedback:
                selectedInterfaceAndPipe.MaximumTransferSize = deviceContext->OutputIsoPacketSize * UAC_MAX_CLASSIC_FRAMES_PER_IRP * deviceContext->FramesPerMs;
                ASSERT(false);
                break;
            default:
                ASSERT(false);
                break;
            }

            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - SelectedAlternateSettings %u, NumberConfiguredPipes %u", selectedInterfaceAndPipe.SelectedAlternateSetting, selectedInterfaceAndPipe.NumberConfiguredPipes);
        }
    }
SelectAlternateInterface_Exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

PAGED_CODE_SEG
static _Use_decl_annotations_
NTSTATUS
ActivateAudioInterface(
    PDEVICE_CONTEXT deviceContext,
    ULONG           desiredSampleRate,
    ULONG           desiredFormatType,
    ULONG           desiredFormat,
    ULONG           desiredBytesPerSampleIn,
    ULONG           desiredValidBitsPerSampleIn,
    ULONG           desiredBytesPerSampleOut,
    ULONG           desiredValidBitsPerSampleOut,
    bool            forceSetSampleRate /* = false */
)
{
    NTSTATUS                      status = STATUS_SUCCESS;
    PUAC_AUDIO_PROPERTY           audioProp = &deviceContext->AudioProperty;
    PUSB_CONFIGURATION_DESCRIPTOR configDescriptor = deviceContext->UsbConfigurationDescriptor;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry, %u, %u, %u, %u, %u, %u, %u, %!bool!", desiredSampleRate, desiredFormatType, desiredFormat, desiredBytesPerSampleIn, desiredValidBitsPerSampleIn, desiredBytesPerSampleOut, desiredValidBitsPerSampleOut, forceSetSampleRate);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "<PID %04x>", audioProp->ProductId);

    _IRQL_limited_to_(PASSIVE_LEVEL);

    auto activateAudioInterfaceScope = wil::scope_exit([&]() {
        deviceContext->LastActivationStatus = status;
    });

    deviceContext->LastActivationStatus = STATUS_UNSUCCESSFUL;
    if (audioProp == nullptr || configDescriptor == nullptr)
    {
        status = STATUS_DEVICE_NOT_READY;
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! failed %!STATUS!", status);
        return status;
    }

    {
        status = deviceContext->UsbAudioConfiguration->ActivateAudioInterface(desiredSampleRate, desiredFormatType, desiredFormat, desiredBytesPerSampleIn, desiredValidBitsPerSampleIn, desiredBytesPerSampleOut, desiredValidBitsPerSampleOut, forceSetSampleRate);
        RETURN_NTSTATUS_IF_FAILED_MSG(status, "ActivateAudioInterface failed");

        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "<PID %04x>", audioProp->ProductId);

        RtlZeroMemory(&deviceContext->UsbLatency, sizeof(UAC_USB_LATENCY));
        CalculateUsbLatency(deviceContext, &deviceContext->UsbLatency);

        audioProp->InputLatencyOffset = deviceContext->UsbLatency.InputLatency;
        audioProp->OutputLatencyOffset = deviceContext->UsbLatency.OutputLatency;

        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "<PID %04x>", audioProp->ProductId);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - Re-calculated Latency Offset In %d samples, Out %d samples", audioProp->InputLatencyOffset, audioProp->OutputLatencyOffset);

        // For some USB devices, switching the sample rate before SetInterface
        // can cause a STATUS_UNSUCCESSFUL error and a Code 10 error when
        // selecting the alternate interface.
        status = SetPipeInformation(deviceContext);
    }
    RETURN_NTSTATUS_IF_FAILED_MSG(status, "SetPipeInformation failed");

    BuildChannelMap(deviceContext);

    if (audioProp->InputBytesPerBlock != 0 && audioProp->OutputBytesPerBlock != 0)
    {
        status = STATUS_SUCCESS;
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! ActivateAudioInterface() failed. InputBytesPerBlock %u, OutputBytesPerBlock %u", audioProp->InputBytesPerBlock, audioProp->OutputBytesPerBlock);
        status = STATUS_UNSUCCESSFUL;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
static _Use_decl_annotations_
NTSTATUS CalculateUsbLatency(
    PDEVICE_CONTEXT  deviceContext,
    PUAC_USB_LATENCY usbLatency
)
{
    ULONG classicFramesPerIrp1 = deviceContext->Params.ClassicFramesPerIrp;
    ULONG classicFramesPerIrp2 = deviceContext->Params.ClassicFramesPerIrp2;
    ULONG classicFramesPerIrp = deviceContext->FramesPerMs > 1 ? classicFramesPerIrp2 : classicFramesPerIrp1;
    ULONG inBufferOperationOffset = deviceContext->Params.InputBufferOperationOffset;
    ULONG inHubOffset = deviceContext->Params.InputHubOffset;
    ULONG outBufferOperationOffset = deviceContext->Params.OutputBufferOperationOffset;
    ULONG outHubOffset = deviceContext->Params.OutputHubOffset;
    ULONG sampleRate = deviceContext->AudioProperty.SampleRate;
    bool  hub = deviceContext->HubCount > 1;
    ULONG inRawOffset = (inBufferOperationOffset & 0x0fffffffUL);
    ULONG inHardwareMs = 0;
    ULONG inHubMs = 0;
    ULONG outRawOffset = (outBufferOperationOffset & 0x0fffffffUL);
    ULONG outHardwareMs = 0;
    ULONG outHubMs = 0;

    PAGED_CODE();

    switch ((inBufferOperationOffset & 0x30000000UL) >> 28)
    {
    case 0x00:
        inHubMs = hub ? inHubOffset : 0;
        break;
    case 0x01:
        inHardwareMs = deviceContext->LatencyOffsetList->InputBufferOperationOffset;
        inHubMs = hub ? deviceContext->LatencyOffsetList->InputHubOffset : 0;
        break;
    case 0x02:
        break;
    case 0x03:
        inHubMs = hub ? deviceContext->LatencyOffsetList->InputHubOffset : 0;
        break;
    default:
        break;
    }

    if ((inBufferOperationOffset & 0x40000000UL) != 0)
    {
        usbLatency->InputOffsetFrame = (inHardwareMs + inHubMs) * deviceContext->FramesPerMs + (inRawOffset * deviceContext->FramesPerMs / 8);
        usbLatency->InputOffsetMs = usbLatency->InputOffsetFrame / deviceContext->FramesPerMs;
    }
    else
    {
        usbLatency->InputOffsetMs = inHardwareMs + inHubMs + inRawOffset;
        usbLatency->InputOffsetFrame = usbLatency->InputOffsetMs * deviceContext->FramesPerMs;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "In  Offset : %ums, %uframes", usbLatency->InputOffsetMs, usbLatency->InputOffsetFrame);

    switch ((outBufferOperationOffset & 0x30000000UL) >> 28)
    {
    case 0x00:
        outHubMs = hub ? outHubOffset : 0;
        break;
    case 0x01:
        outHardwareMs = deviceContext->LatencyOffsetList->OutputBufferOperationOffset;
        outHubMs = hub ? deviceContext->LatencyOffsetList->OutputHubOffset : 0;
        break;
    case 0x02:
        break;
    case 0x03:
        outHubMs = hub ? deviceContext->LatencyOffsetList->OutputHubOffset : 0;
        break;
    default:
        break;
    }

    if ((outBufferOperationOffset & 0x40000000UL) != 0)
    {
        usbLatency->OutputOffsetFrame = (outHardwareMs + outHubMs) * deviceContext->FramesPerMs + (outRawOffset * deviceContext->FramesPerMs / 8);
        usbLatency->OutputOffsetMs = usbLatency->OutputOffsetFrame / deviceContext->FramesPerMs;
        if (outHardwareMs != 0)
        {
            usbLatency->OutputMinOffsetFrame = (outHubMs + 1) * deviceContext->FramesPerMs + (outRawOffset * 8 / deviceContext->FramesPerMs);
        }
        else
        {
            usbLatency->OutputMinOffsetFrame = 1;
        }
    }
    else
    {
        usbLatency->OutputOffsetMs = outHardwareMs + outHubMs + outRawOffset;
        usbLatency->OutputOffsetFrame = usbLatency->OutputOffsetMs * deviceContext->FramesPerMs;
        if (outHardwareMs != 0)
        {
            usbLatency->OutputMinOffsetFrame = (outHubMs + outRawOffset + 1) * deviceContext->FramesPerMs;
        }
        else
        {
            usbLatency->OutputMinOffsetFrame = 1;
        }
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "Out Offset : %ums, %uframes, %uframes minimum", usbLatency->OutputOffsetMs, usbLatency->OutputOffsetFrame, usbLatency->OutputMinOffsetFrame);

    usbLatency->InputDriverBuffer = (ULONG)((double)(sampleRate * (classicFramesPerIrp * deviceContext->FramesPerMs + usbLatency->InputOffsetFrame)) / (double)(deviceContext->FramesPerMs * 1000));
    usbLatency->OutputDriverBuffer = (ULONG)((double)(sampleRate * usbLatency->OutputOffsetFrame /* - usbLatency->InputOffsetFrame */) / (double)(deviceContext->FramesPerMs * 1000));

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "Driver Internal Buffer : In %usamples, Out %usamples", usbLatency->InputDriverBuffer, usbLatency->OutputDriverBuffer);

    if ((inBufferOperationOffset & 0x80000000UL) != 0)
    {
        usbLatency->InputLatency = usbLatency->InputDriverBuffer;
    }
    else
    {
        usbLatency->InputLatency = (inHardwareMs + inHubMs) * sampleRate / 1000;
    }
    if ((outBufferOperationOffset & 0x80000000UL) != 0)
    {
        usbLatency->OutputLatency = usbLatency->OutputDriverBuffer;
    }
    else
    {
        usbLatency->OutputLatency = (outHardwareMs + outHubMs) * sampleRate / 1000;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "Total Latency : In %usamples, Out %usamples", usbLatency->InputLatency, usbLatency->OutputLatency);

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
static _Use_decl_annotations_
void BuildChannelMap(
    PDEVICE_CONTEXT deviceContext
)
{
    PAGED_CODE();

    deviceContext->AudioProperty.InputAsioChannels = deviceContext->InputUsbChannels;
    deviceContext->AudioProperty.OutputAsioChannels = deviceContext->OutputUsbChannels;

    for (ULONG asioInChannel = 0; asioInChannel < deviceContext->AudioProperty.InputAsioChannels; asioInChannel++)
    {
        WDFMEMORY memory = nullptr;
        PWSTR     channelName = nullptr;
        NTSTATUS  status = deviceContext->UsbAudioConfiguration->GetChannelName(true, asioInChannel, memory, channelName);

        if (NT_SUCCESS(status))
        {
            RtlStringCchCopyW(deviceContext->InputAsioChannelName[asioInChannel], UAC_MAX_CHANNEL_NAME_LENGTH, channelName);
            WdfObjectDelete(memory);
            memory = nullptr;
            channelName = nullptr;
        }
        else
        {
            RtlStringCchCopyW(deviceContext->InputAsioChannelName[asioInChannel], UAC_MAX_CHANNEL_NAME_LENGTH, deviceContext->AudioProperty.ProductName);
        }
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - in asio channel name [%d] %ws", asioInChannel, deviceContext->InputAsioChannelName[asioInChannel]);
    }

    for (ULONG asioOutChannel = 0; asioOutChannel < deviceContext->AudioProperty.OutputAsioChannels; asioOutChannel++)
    {
        WDFMEMORY memory = nullptr;
        PWSTR     channelName = nullptr;
        NTSTATUS  status = deviceContext->UsbAudioConfiguration->GetChannelName(false, asioOutChannel, memory, channelName);

        if (NT_SUCCESS(status))
        {
            RtlStringCchCopyW(deviceContext->OutputAsioChannelName[asioOutChannel], UAC_MAX_CHANNEL_NAME_LENGTH, channelName);
            WdfObjectDelete(memory);
            memory = nullptr;
            channelName = nullptr;
        }
        else
        {
            RtlStringCchCopyW(deviceContext->OutputAsioChannelName[asioOutChannel], UAC_MAX_CHANNEL_NAME_LENGTH, deviceContext->AudioProperty.ProductName);
        }
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - out asio channel name [%d] %ws", asioOutChannel, deviceContext->OutputAsioChannelName[asioOutChannel]);
    }
}

PAGED_CODE_SEG
static _Use_decl_annotations_
NTSTATUS SetPipeInformation(
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    // deviceContext->PipeInformationIn       = nullptr;
    // deviceContext->PipeInformationOut      = nullptr;
    // deviceContext->PipeInformationFeedback = nullptr;
    bool failed = false;
    status = SelectAlternateInterface(IsoDirection::Out, deviceContext, deviceContext->AudioProperty.OutputInterfaceNumber, deviceContext->AudioProperty.OutputAlternateSetting);

    if (NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - OutputInterfaceAndPipe.NumberConfiguredPipes %u", deviceContext->OutputInterfaceAndPipe.NumberConfiguredPipes);
        for (UCHAR pipeIndex = 0; pipeIndex < deviceContext->OutputInterfaceAndPipe.NumberConfiguredPipes; pipeIndex++)
        {
            WDFUSBPIPE               pipe;
            WDF_USB_PIPE_INFORMATION pipeInfo;

            pipe = WdfUsbInterfaceGetConfiguredPipe(deviceContext->OutputInterfaceAndPipe.UsbInterface, pipeIndex, nullptr);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] %p", pipeIndex, pipe);
            if (pipe != nullptr)
            {
                WDF_USB_PIPE_INFORMATION_INIT(&pipeInfo);
                WdfUsbTargetPipeGetInformation(pipe, &pipeInfo);
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u], EndpointAddress 0x%x OutputEndpointNumber 0x%x", pipeIndex, pipeInfo.EndpointAddress, deviceContext->AudioProperty.OutputEndpointNumber);
                if (pipeInfo.EndpointAddress == deviceContext->AudioProperty.OutputEndpointNumber)
                {
                    deviceContext->OutputInterfaceAndPipe.Pipe = pipe;
                    deviceContext->OutputInterfaceAndPipe.PipeInfo = pipeInfo;
                    PPIPE_CONTEXT pipeContext = GetPipeContext(pipe);
                    pipeContext->SelectedInterfaceAndPipe = &(deviceContext->OutputInterfaceAndPipe);
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - set OutputInterfaceAndPipe.Pipe");
                }
                else if (pipeInfo.EndpointAddress == deviceContext->FeedbackProperty.FeedbackEndpointNumber)
                {
                    deviceContext->FeedbackInterfaceAndPipe.InterfaceDescriptor = deviceContext->OutputInterfaceAndPipe.InterfaceDescriptor;
                    deviceContext->FeedbackInterfaceAndPipe.UsbInterface = deviceContext->OutputInterfaceAndPipe.UsbInterface;
                    deviceContext->FeedbackInterfaceAndPipe.SelectedAlternateSetting = deviceContext->OutputInterfaceAndPipe.SelectedAlternateSetting;
                    deviceContext->FeedbackInterfaceAndPipe.NumberConfiguredPipes = deviceContext->OutputInterfaceAndPipe.NumberConfiguredPipes;
                    deviceContext->FeedbackInterfaceAndPipe.MaximumTransferSize = deviceContext->OutputInterfaceAndPipe.MaximumTransferSize;
                    deviceContext->FeedbackInterfaceAndPipe.Pipe = pipe;
                    deviceContext->FeedbackInterfaceAndPipe.PipeInfo = pipeInfo;
                    PPIPE_CONTEXT pipeContext = GetPipeContext(pipe);
                    pipeContext->SelectedInterfaceAndPipe = &(deviceContext->FeedbackInterfaceAndPipe);
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - set FeedbackInterfaceAndPipe.Pipe");
                }
            }
        }
    }
    else
    {
        failed = true;
    }

    status = SelectAlternateInterface(IsoDirection::In, deviceContext, deviceContext->AudioProperty.InputInterfaceNumber, deviceContext->AudioProperty.InputAlternateSetting);

    if (NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - InputInterfaceAndPipe.NumberConfiguredPipes %u", deviceContext->InputInterfaceAndPipe.NumberConfiguredPipes);
        for (UCHAR pipeIndex = 0; pipeIndex < deviceContext->InputInterfaceAndPipe.NumberConfiguredPipes; pipeIndex++)
        {
            WDFUSBPIPE               pipe;
            WDF_USB_PIPE_INFORMATION pipeInfo;

            WDF_USB_PIPE_INFORMATION_INIT(&pipeInfo);
            pipe = WdfUsbInterfaceGetConfiguredPipe(deviceContext->InputInterfaceAndPipe.UsbInterface, pipeIndex, &pipeInfo);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] %p", pipeIndex, pipe);
            if (pipe != nullptr)
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u], EndpointAddress 0x%x OutputEndpointNumber 0x%x", pipeIndex, pipeInfo.EndpointAddress, deviceContext->AudioProperty.InputEndpointNumber);
                if (pipeInfo.EndpointAddress == deviceContext->AudioProperty.InputEndpointNumber)
                {
                    deviceContext->InputInterfaceAndPipe.Pipe = pipe;
                    deviceContext->InputInterfaceAndPipe.PipeInfo = pipeInfo;
                    PPIPE_CONTEXT pipeContext = GetPipeContext(pipe);
                    pipeContext->SelectedInterfaceAndPipe = &(deviceContext->InputInterfaceAndPipe);
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - set InputInterfaceAndPipe.Pipe");
                }
            }
        }
    }
    else
    {
        failed = true;
    }

    if (failed)
    {
        deviceContext->ErrorStatistics->SetBandWidthError();
        status = STATUS_UNSUCCESSFUL;
    }
    else
    {
        deviceContext->ErrorStatistics->ClearBandWidthError();
        status = STATUS_SUCCESS;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

#if 0
PAGED_CODE_SEG
static _Use_decl_annotations_
NTSTATUS ResetPipeInformation(
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    // deviceContext->PipeInformationFeedback = nullptr;
    // deviceContext->PipeInformationIn       = nullptr;
    // deviceContext->PipeInformationOut      = nullptr;
    // deviceContext->PipeInformationFeedback = nullptr;

    deviceContext->InputInterfaceAndPipe.Pipe = nullptr;
    RtlZeroMemory(&(deviceContext->InputInterfaceAndPipe.PipeInfo), sizeof(deviceContext->InputInterfaceAndPipe.PipeInfo));
    deviceContext->OutputInterfaceAndPipe.Pipe = nullptr;
    RtlZeroMemory(&(deviceContext->OutputInterfaceAndPipe.PipeInfo), sizeof(deviceContext->OutputInterfaceAndPipe.PipeInfo));

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
    return status;
}
#endif

NONPAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS SendUrbSync(
    PDEVICE_CONTEXT deviceContext,
    PURB            urb
)
{
    NTSTATUS status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    status = WdfUsbTargetDeviceSendUrbSynchronously(deviceContext->UsbDevice, nullptr, nullptr, urb);

    // status = SendUrbSyncWithTimeout(deviceContext, urb, 1000);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

NONPAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS SendUrbSyncWithTimeout(
    PDEVICE_CONTEXT deviceContext,
    PURB            urb,
    ULONG           msTimeout
)
{
    NTSTATUS                 status = STATUS_SUCCESS;
    WDF_REQUEST_SEND_OPTIONS sendOptions;

    // PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    WDF_REQUEST_SEND_OPTIONS_INIT(&sendOptions, WDF_REQUEST_SEND_OPTION_TIMEOUT);

    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&sendOptions, WDF_REL_TIMEOUT_IN_MS(msTimeout));

    status = WdfUsbTargetDeviceSendUrbSynchronously(deviceContext->UsbDevice, nullptr, &sendOptions, urb);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
static _Use_decl_annotations_
NTSTATUS GetHubCount(
    PDEVICE_CONTEXT deviceContext,
    ULONG &         hubCount
)
{
    NTSTATUS                 status = STATUS_SUCCESS;
    WDF_MEMORY_DESCRIPTOR    memoryDescriptor{};
    WDF_REQUEST_SEND_OPTIONS options{};

    PAGED_CODE();

    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memoryDescriptor, &hubCount, sizeof(hubCount));
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_SYNCHRONOUS);

    status = WdfIoTargetSendInternalIoctlSynchronously(
        WdfDeviceGetIoTarget(deviceContext->Device),
        nullptr,
        IOCTL_INTERNAL_USB_GET_HUB_COUNT,
        &memoryDescriptor,
        nullptr,
        &options,
        nullptr
    );

    if (!NT_SUCCESS(status) || hubCount == 0)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "USB hub count might not be obtained, status %!STATUS!, count %d", status, hubCount);
        hubCount = 2;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "USB hub count is %u", hubCount);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
ULONG
GetCurrentFrame(
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG    currentFrameNumber = 0;

    PAGED_CODE();

    status = WdfUsbTargetDeviceRetrieveCurrentFrameNumber(deviceContext->UsbDevice, &currentFrameNumber);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "WdfUsbTargetDeviceRetrieveCurrentFrameNumber failed %!STATUS!", status);
        currentFrameNumber = 0;
        goto GetCurrentFrame_Exit;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "%!FUNC! frameNumber %u", currentFrameNumber);
GetCurrentFrame_Exit:
    return currentFrameNumber;
}

PAGED_CODE_SEG
static _Use_decl_annotations_
NTSTATUS
GetStackCapability(
    PDEVICE_CONTEXT deviceContext,
    const GUID *    capabilityType,
    ULONG           outputBufferLength,
    PUCHAR          outputBuffer
)
/*++

Routine Description:

    The helper routine gets stack's capability.

Arguments:

    deviceContext -

    capabilityType - Pointer to capability type GUID

    outputBufferLength - Length of output buffer

    OutPutBuffer - Output buffer

Return Value:

    NTSTATUS

--*/
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    status = WdfUsbTargetDeviceQueryUsbCapability(deviceContext->UsbDevice, capabilityType, outputBufferLength, outputBuffer, nullptr);
    if (NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "WdfUsbTargetDeviceQueryUsbCapability %x", status);
    }

    return status;
}

NONPAGED_CODE_SEG
_Use_decl_annotations_
ULONGLONG
USBAudioAcxDriverStreamGetCurrentTime(
    PDEVICE_CONTEXT deviceContext,
    PULONGLONG      qpcPosition
)
{
    ULONGLONG     currentTime = 0ULL;
    LARGE_INTEGER qpc = KeQueryPerformanceCounter(nullptr);

    if (deviceContext != nullptr)
    {
        currentTime = KSCONVERT_PERFORMANCE_TIME(deviceContext->PerformanceCounterFrequency.QuadPart, qpc);
        if (qpcPosition != nullptr)
        {
            *qpcPosition = (ULONGLONG)qpc.QuadPart;
        }
    }

    return currentTime;
}

NONPAGED_CODE_SEG
_Use_decl_annotations_
ULONGLONG
USBAudioAcxDriverStreamGetCurrentTimeUs(
    PDEVICE_CONTEXT deviceContext,
    PULONGLONG      qpcCPosition
)
{
    ULONGLONG currentTime = USBAudioAcxDriverStreamGetCurrentTime(deviceContext, qpcCPosition) / 10;

    return currentTime;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioAcxDriverStreamPrepareHardware(
    bool            isInput,
    ULONG           deviceIndex,
    PDEVICE_CONTEXT deviceContext,
    CStreamEngine * streamEngine
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    _IRQL_limited_to_(PASSIVE_LEVEL);

    auto prepareHardwareScope = wil::scope_exit([&]() {
        WdfWaitLockRelease(deviceContext->StreamWaitLock);
    });

    WdfWaitLockAcquire(deviceContext->StreamWaitLock, nullptr);

    if (isInput)
    {
        RETURN_NTSTATUS_IF_TRUE(deviceContext->CaptureStreamEngine == nullptr, STATUS_UNSUCCESSFUL);
        RETURN_NTSTATUS_IF_TRUE(deviceIndex >= deviceContext->NumOfInputDevices, STATUS_INVALID_PARAMETER);
        RETURN_NTSTATUS_IF_TRUE(deviceContext->CaptureStreamEngine[deviceIndex] != nullptr, STATUS_UNSUCCESSFUL);
        deviceContext->CaptureStreamEngine[deviceIndex] = streamEngine;
    }
    else
    {
        RETURN_NTSTATUS_IF_TRUE(deviceContext->RenderStreamEngine == nullptr, STATUS_UNSUCCESSFUL);
        RETURN_NTSTATUS_IF_TRUE(deviceIndex >= deviceContext->NumOfOutputDevices, STATUS_INVALID_PARAMETER);
        RETURN_NTSTATUS_IF_TRUE(deviceContext->RenderStreamEngine[deviceIndex] != nullptr, STATUS_UNSUCCESSFUL);
        deviceContext->RenderStreamEngine[deviceIndex] = streamEngine;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioAcxDriverStreamReleaseHardware(
    bool            isInput,
    ULONG           deviceIndex,
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    _IRQL_limited_to_(PASSIVE_LEVEL);

    auto releaseHardwareScope = wil::scope_exit([&]() {
        WdfWaitLockRelease(deviceContext->StreamWaitLock);
    });

    WdfWaitLockAcquire(deviceContext->StreamWaitLock, nullptr);
    if (isInput)
    {
        RETURN_NTSTATUS_IF_TRUE(deviceContext->CaptureStreamEngine == nullptr, STATUS_UNSUCCESSFUL);
        RETURN_NTSTATUS_IF_TRUE(deviceIndex >= deviceContext->NumOfInputDevices, STATUS_INVALID_PARAMETER);
        deviceContext->CaptureStreamEngine[deviceIndex] = nullptr;
    }
    else
    {
        RETURN_NTSTATUS_IF_TRUE(deviceContext->RenderStreamEngine == nullptr, STATUS_UNSUCCESSFUL);
        RETURN_NTSTATUS_IF_TRUE(deviceIndex >= deviceContext->NumOfOutputDevices, STATUS_INVALID_PARAMETER);
        deviceContext->RenderStreamEngine[deviceIndex] = nullptr;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioAcxDriverStreamSetDataFormat(
    bool            isInput,
    ULONG           deviceIndex,
    PDEVICE_CONTEXT deviceContext,
    ACXDATAFORMAT   dataFormat
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry, %!bool!", isInput);

    _IRQL_limited_to_(PASSIVE_LEVEL);

    WdfWaitLockAcquire(deviceContext->StreamWaitLock, nullptr);

    if (deviceContext->RtPacketObject != nullptr)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - data format %u, %llu, %u, %u, %u, %u, %u, %u, %u", AcxDataFormatGetChannelsCount(dataFormat), AcxDataFormatGetChannelMask(dataFormat), AcxDataFormatGetSampleSize(dataFormat), AcxDataFormatGetBitsPerSample(dataFormat), AcxDataFormatGetValidBitsPerSample(dataFormat), AcxDataFormatGetSamplesPerBlock(dataFormat), AcxDataFormatGetBlockAlign(dataFormat), AcxDataFormatGetSampleRate(dataFormat), AcxDataFormatGetAverageBytesPerSec(dataFormat));

        {
            PWAVEFORMATEXTENSIBLE waveFormatExtensible = (PWAVEFORMATEXTENSIBLE)AcxDataFormatGetWaveFormatExtensible(dataFormat);

            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - Format.wFormatTag           = %u\n", waveFormatExtensible->Format.wFormatTag);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - Format.nChannels            = %u\n", waveFormatExtensible->Format.nChannels);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - Format.nSamplesPerSec       = %u\n", waveFormatExtensible->Format.nSamplesPerSec);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - Format.nAvgBytesPerSec      = %u\n", waveFormatExtensible->Format.nAvgBytesPerSec);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - Format.nBlockAlign          = %u\n", waveFormatExtensible->Format.nBlockAlign);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - Format.wBitsPerSample       = %u\n", waveFormatExtensible->Format.wBitsPerSample);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - Format.cbSize               = %u\n", waveFormatExtensible->Format.cbSize);
            if (waveFormatExtensible->Format.wBitsPerSample != 0)
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - Samples.wValidBitsPerSample = %u\n", waveFormatExtensible->Samples.wValidBitsPerSample);
            }
            else
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - Samples.wSamplesPerBlock    = %u\n", waveFormatExtensible->Samples.wSamplesPerBlock); /* valid if wBitsPerSample==0 */
            }
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - dwChannelMask               = 0x%x\n", waveFormatExtensible->dwChannelMask);
        }

        status = deviceContext->RtPacketObject->SetDataFormat(isInput, dataFormat);
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

        ACXDATAFORMAT inputDataFormatBeforeChange = nullptr;
        ACXDATAFORMAT outputDataFormatBeforeChange = nullptr;
        ACXDATAFORMAT inputDataFormatAfterChange = nullptr;
        ACXDATAFORMAT outputDataFormatAfterChange = nullptr;
        ULONG         formatType, format;

        status = USBAudioAcxDriverGetCurrentDataFormat(deviceContext, true, inputDataFormatBeforeChange);
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

        status = USBAudioAcxDriverGetCurrentDataFormat(deviceContext, false, outputDataFormatBeforeChange);
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

        status = ConvertAudioDataFormat(dataFormat, formatType, format);
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

        if (isInput)
        {
            ULONG desiredBytesPerSampleOut = deviceContext->AudioProperty.OutputBytesPerSample;
            ULONG desiredValidBitsPerSampleOut = deviceContext->AudioProperty.OutputValidBitsPerSample;

            status = deviceContext->UsbAudioConfiguration->GetNearestSupportedValidBitsPerSamples(isInput, formatType, format, desiredBytesPerSampleOut, desiredValidBitsPerSampleOut);
            IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

            status = ActivateAudioInterface(deviceContext, AcxDataFormatGetSampleRate(dataFormat), formatType, format, AcxDataFormatGetBitsPerSample(dataFormat) / 8, AcxDataFormatGetValidBitsPerSample(dataFormat), desiredBytesPerSampleOut, desiredValidBitsPerSampleOut);
        }
        else
        {
            ULONG desiredBytesPerSampleIn = deviceContext->AudioProperty.InputBytesPerSample;
            ULONG desiredValidBitsPerSampleIn = deviceContext->AudioProperty.InputValidBitsPerSample;

            status = deviceContext->UsbAudioConfiguration->GetNearestSupportedValidBitsPerSamples(isInput, formatType, format, desiredBytesPerSampleIn, desiredValidBitsPerSampleIn);
            IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

            status = ActivateAudioInterface(deviceContext, AcxDataFormatGetSampleRate(dataFormat), formatType, format, desiredBytesPerSampleIn, desiredValidBitsPerSampleIn, AcxDataFormatGetBitsPerSample(dataFormat) / 8, AcxDataFormatGetValidBitsPerSample(dataFormat));
        }
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

        status = USBAudioAcxDriverGetCurrentDataFormat(deviceContext, true, inputDataFormatAfterChange);
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

        status = USBAudioAcxDriverGetCurrentDataFormat(deviceContext, false, outputDataFormatAfterChange);
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

        if ((deviceContext->Render != nullptr) && (outputDataFormatBeforeChange != nullptr) && (outputDataFormatAfterChange != nullptr) && !AcxDataFormatIsEqual(outputDataFormatBeforeChange, outputDataFormatAfterChange))
        {
            for (ULONG renderDeviceIndex = 0; renderDeviceIndex < deviceContext->NumOfOutputDevices; renderDeviceIndex++)
            {
                if (isInput || (!isInput && (renderDeviceIndex != deviceIndex)))
                {
                    ACXPIN pin = AcxCircuitGetPinById(deviceContext->Render, renderDeviceIndex * CodecRenderPinCount + CodecRenderHostPin);
                    if (pin != nullptr)
                    {
                        status = NotifyDataFormatChange(deviceContext->Device, deviceContext->Render, pin, outputDataFormatAfterChange);
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - render pin %u, PinNotifyDataFormatChange %!STATUS!", renderDeviceIndex * 2, status);
                        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);
                    }
                }
            }
        }
        if ((deviceContext->Capture != nullptr) && (inputDataFormatBeforeChange != nullptr) && (inputDataFormatAfterChange != nullptr) && !AcxDataFormatIsEqual(inputDataFormatBeforeChange, inputDataFormatAfterChange))
        {
            for (ULONG captureDeviceIndex = 0; captureDeviceIndex < deviceContext->NumOfInputDevices; captureDeviceIndex++)
            {
                if (!isInput || (isInput && (captureDeviceIndex != deviceIndex)))
                {
                    ACXPIN pin = AcxCircuitGetPinById(deviceContext->Capture, captureDeviceIndex * CodecCapturePinCount + CodecCaptureHostPin);
                    if (pin != nullptr)
                    {
                        status = NotifyDataFormatChange(deviceContext->Device, deviceContext->Capture, pin, inputDataFormatAfterChange);
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - capture pin %u, AcxPinNotifyDataFormatChange %!STATUS!", captureDeviceIndex * 2, status);
                        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);
                    }
                }
            }
        }
    }
Exit_BeforeWaitLockRelease:
    WdfWaitLockRelease(deviceContext->StreamWaitLock);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

NONPAGED_CODE_SEG
_Use_decl_annotations_
VOID USBAudioAcxDriverEvtIsoRequestContextCleanup(
    WDFOBJECT request
)
{
    PISOCHRONOUS_REQUEST_CONTEXT requestContext;

    //
    // EvtCleanupCallback for WDFDEVICE is always called at PASSIVE_LEVEL
    //
    // _IRQL_limited_to_(PASSIVE_LEVEL);

    // PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    requestContext = GetIsochronousRequestContext(request);

    // Do not release it here, but do it with ProcessTransferOut / ProcessTransferIn.
    // if ((requestContext != nullptr) && (requestContext->transferObject != nullptr))
    // {
    //     requestContext->transferObject->Free();
    // }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioAcxDriverStreamSetRtPackets(
    bool            isInput,
    ULONG           deviceIndex,
    PDEVICE_CONTEXT deviceContext,
    PVOID *         packets,
    ULONG           packetsCount,
    ULONG           packetSize,
    ULONG           channel,
    ULONG           numOfChannelsPerDevice
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry, %s, packetsCount = %d, packetSize = %d", isInput ? "Input" : "Output", packetsCount, packetSize);

    WdfWaitLockAcquire(deviceContext->StreamWaitLock, nullptr);

    if (deviceContext->RtPacketObject != nullptr)
    {
        status = deviceContext->RtPacketObject->SetRtPackets(isInput, deviceIndex, packets, packetsCount, packetSize, channel, numOfChannelsPerDevice);
    }

    WdfWaitLockRelease(deviceContext->StreamWaitLock);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
void USBAudioAcxDriverStreamUnsetRtPackets(
    bool            isInput,
    ULONG           deviceIndex,
    PDEVICE_CONTEXT deviceContext
)
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    WdfWaitLockAcquire(deviceContext->StreamWaitLock, nullptr);

    if (deviceContext->RtPacketObject != nullptr)
    {
        deviceContext->RtPacketObject->UnsetRtPackets(isInput, deviceIndex);
    }

    WdfWaitLockRelease(deviceContext->StreamWaitLock);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioAcxDriverStreamRun(
    bool            isInput,
    ULONG           deviceIndex,
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    WdfWaitLockAcquire(deviceContext->StreamWaitLock, nullptr);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_MULTICLIENT, " - start counter asio %ld, start counter acx audio %ld, start counter iso stream %ld", deviceContext->StartCounterAsio, deviceContext->StartCounterWdmAudio, deviceContext->StartCounterIsoStream);
    if ((deviceContext->StartCounterAsio == 0) && (deviceContext->StartCounterWdmAudio == 0))
    {
        status = StartIsoStream(deviceContext);
    }
    else
    {
        if (deviceContext->RtPacketObject != nullptr)
        {
            deviceContext->RtPacketObject->ResetCurrentPacket(isInput, deviceIndex);
        }
        status = STATUS_SUCCESS;
    }
    if (NT_SUCCESS(status))
    {
        InterlockedIncrement(&deviceContext->StartCounterWdmAudio);
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_MULTICLIENT, " - start counter asio %ld, start counter acx audio %ld, start counter iso stream %ld", deviceContext->StartCounterAsio, deviceContext->StartCounterWdmAudio, deviceContext->StartCounterIsoStream);
    }

    WdfWaitLockRelease(deviceContext->StreamWaitLock);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioAcxDriverStreamPause(
    bool /* isInput */,
    ULONG /* deviceIndex */,
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    _IRQL_limited_to_(PASSIVE_LEVEL);

    WdfWaitLockAcquire(deviceContext->StreamWaitLock, nullptr);
    // AbortPipes(IsoDirection::In, deviceContext->Device);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_MULTICLIENT, " - start counter asio %ld, start counter acx audio %ld, start counter iso stream %ld", deviceContext->StartCounterAsio, deviceContext->StartCounterWdmAudio, deviceContext->StartCounterIsoStream);
    if (deviceContext->StartCounterWdmAudio)
    {
        InterlockedDecrement(&deviceContext->StartCounterWdmAudio);
        if ((deviceContext->StartCounterAsio == 0) && (deviceContext->StartCounterWdmAudio == 0))
        {
            status = StopIsoStream(deviceContext);
        }
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_MULTICLIENT, " - start counter asio %ld, start counter acx audio %ld, start counter iso stream %ld", deviceContext->StartCounterAsio, deviceContext->StartCounterWdmAudio, deviceContext->StartCounterIsoStream);
    }

    WdfWaitLockRelease(deviceContext->StreamWaitLock);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioAcxDriverStreamGetCurrentPacket(
    bool            isInput,
    ULONG           deviceIndex,
    PDEVICE_CONTEXT deviceContext,
    PULONG          currentPacket
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    _IRQL_limited_to_(PASSIVE_LEVEL);

    IF_TRUE_ACTION_JUMP(deviceContext == nullptr, status = STATUS_INVALID_PARAMETER, USBAudioAcxDriverStreamGetCurrentPacket_Exit);
    IF_TRUE_ACTION_JUMP(deviceContext->RtPacketObject == nullptr, status = STATUS_INVALID_PARAMETER, USBAudioAcxDriverStreamGetCurrentPacket_Exit);
    IF_TRUE_ACTION_JUMP(currentPacket == nullptr, status = STATUS_INVALID_PARAMETER, USBAudioAcxDriverStreamGetCurrentPacket_Exit);

    status = deviceContext->RtPacketObject->GetCurrentPacket(isInput, deviceIndex, currentPacket);

USBAudioAcxDriverStreamGetCurrentPacket_Exit:

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioAcxDriverStreamResetCurrentPacket(
    bool            isInput,
    ULONG           deviceIndex,
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    _IRQL_limited_to_(PASSIVE_LEVEL);

    IF_TRUE_ACTION_JUMP(deviceContext == nullptr, status = STATUS_INVALID_PARAMETER, USBAudioAcxDriverStreamResetCurrentPacket_Exit);
    IF_TRUE_ACTION_JUMP(deviceContext->RtPacketObject == nullptr, status = STATUS_INVALID_PARAMETER, USBAudioAcxDriverStreamResetCurrentPacket_Exit);

    status = deviceContext->RtPacketObject->ResetCurrentPacket(isInput, deviceIndex);

USBAudioAcxDriverStreamResetCurrentPacket_Exit:

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioAcxDriverStreamGetCapturePacket(
    PDEVICE_CONTEXT deviceContext,
    ULONG           deviceIndex,
    PULONG          lastCapturePacket,
    PULONGLONG      qpcPacketStart
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    _IRQL_limited_to_(PASSIVE_LEVEL);

    IF_TRUE_ACTION_JUMP(deviceContext == nullptr, status = STATUS_INVALID_PARAMETER, USBAudioAcxDriverStreamGetCapturePacket_Exit);
    IF_TRUE_ACTION_JUMP(deviceContext->RtPacketObject == nullptr, status = STATUS_INVALID_PARAMETER, USBAudioAcxDriverStreamGetCapturePacket_Exit);
    IF_TRUE_ACTION_JUMP(lastCapturePacket == nullptr, status = STATUS_INVALID_PARAMETER, USBAudioAcxDriverStreamGetCapturePacket_Exit);
    IF_TRUE_ACTION_JUMP(qpcPacketStart == nullptr, status = STATUS_INVALID_PARAMETER, USBAudioAcxDriverStreamGetCapturePacket_Exit);

    status = deviceContext->RtPacketObject->GetCapturePacket(deviceIndex, lastCapturePacket, qpcPacketStart);

USBAudioAcxDriverStreamGetCapturePacket_Exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioAcxDriverStreamGetPresentationPosition(
    bool            isInput,
    ULONG           deviceIndex,
    PDEVICE_CONTEXT deviceContext,
    PULONGLONG      positionInBlocks,
    PULONGLONG      qpcPosition
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    _IRQL_limited_to_(PASSIVE_LEVEL);

    IF_TRUE_ACTION_JUMP(deviceContext == nullptr, status = STATUS_INVALID_PARAMETER, USBAudioAcxDriverStreamGetCapturePacket_Exit);
    IF_TRUE_ACTION_JUMP(deviceContext->RtPacketObject == nullptr, status = STATUS_INVALID_PARAMETER, USBAudioAcxDriverStreamGetCapturePacket_Exit);
    IF_TRUE_ACTION_JUMP(positionInBlocks == nullptr, status = STATUS_INVALID_PARAMETER, USBAudioAcxDriverStreamGetCapturePacket_Exit);
    IF_TRUE_ACTION_JUMP(qpcPosition == nullptr, status = STATUS_INVALID_PARAMETER, USBAudioAcxDriverStreamGetCapturePacket_Exit);

    status = deviceContext->RtPacketObject->GetPresentationPosition(isInput, deviceIndex, positionInBlocks, qpcPosition);

USBAudioAcxDriverStreamGetCapturePacket_Exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudioAcxDriverGetCurrentDataFormat(
    PDEVICE_CONTEXT deviceContext,
    bool            isInput,
    ACXDATAFORMAT & dataFormat
)
{
    UCHAR                             numOfChannels = 0;
    KSDATAFORMAT_WAVEFORMATEXTENSIBLE pcmWaveFormatExtensible{};

    PAGED_CODE();

    ASSERT(deviceContext->Device != nullptr);

    RETURN_NTSTATUS_IF_FAILED(deviceContext->UsbAudioConfiguration->GetStreamChannels(isInput, numOfChannels));

    if (isInput)
    {
        ASSERT(deviceContext->Capture != nullptr);

        RETURN_NTSTATUS_IF_FAILED(USBAudioDataFormat::BuildWaveFormatExtensible(
            deviceContext->AudioProperty.SampleRate,
            numOfChannels,
            (UCHAR)deviceContext->AudioProperty.InputBytesPerSample,
            (UCHAR)deviceContext->AudioProperty.InputValidBitsPerSample,
            deviceContext->AudioProperty.InputFormatType,
            deviceContext->AudioProperty.InputFormat,
            pcmWaveFormatExtensible
        ));
        RETURN_NTSTATUS_IF_FAILED(AllocateFormat(pcmWaveFormatExtensible, deviceContext->Capture, deviceContext->Device, &dataFormat));
    }
    else
    {
        ASSERT(deviceContext->Render != nullptr);

        RETURN_NTSTATUS_IF_FAILED(USBAudioDataFormat::BuildWaveFormatExtensible(
            deviceContext->AudioProperty.SampleRate,
            numOfChannels,
            (UCHAR)deviceContext->AudioProperty.OutputBytesPerSample,
            (UCHAR)deviceContext->AudioProperty.OutputValidBitsPerSample,
            deviceContext->AudioProperty.OutputFormatType,
            deviceContext->AudioProperty.OutputFormat,
            pcmWaveFormatExtensible
        ));
        RETURN_NTSTATUS_IF_FAILED(AllocateFormat(pcmWaveFormatExtensible, deviceContext->Render, deviceContext->Device, &dataFormat));
    }

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioAcxDriverHasAsioOwnership(
    _In_ PDEVICE_CONTEXT deviceContext
)
{
    bool hasAsioOwnership = false;

    ASSERT(deviceContext != nullptr);

    PAGED_CODE();

    WdfWaitLockAcquire(deviceContext->StreamWaitLock, nullptr);

    hasAsioOwnership = (deviceContext->AsioOwner != nullptr);

    WdfWaitLockRelease(deviceContext->StreamWaitLock);

    return hasAsioOwnership;
}

PAGED_CODE_SEG
static _Use_decl_annotations_
bool IsValidFlags(
    PUAC_SET_FLAGS_CONTEXT flags
)
{
    bool isValid = false;

    PAGED_CODE();

    if ((flags->FirstPacketLatency > USBD_ISO_START_FRAME_RANGE) ||
        /* (flags->ClassicFramesPerIrp < UAC_MIN_CLASSIC_FRAMES_PER_IRP) || */
        (flags->ClassicFramesPerIrp > UAC_MAX_CLASSIC_FRAMES_PER_IRP) ||
        (flags->MaxIrpNumber < UAC_MIN_MAX_IRP_NUMBER) ||
        (flags->MaxIrpNumber > UAC_MAX_IRP_NUMBER) ||
        (flags->PreSendFrames > UAC_MAX_PRE_SEND_FRAMES) ||
        (flags->OutputFrameDelay < UAC_MIN_OUTPUT_FRAME_DELAY) ||
        (flags->OutputFrameDelay > UAC_MAX_OUTPUT_FRAME_DELAY) ||
        //(flags->BufferOperationThread > UAC_MAX_BUFFER_OPERATION_THREAD) ||
        ((flags->InputBufferOperationOffset & 0xfffffff) > UAC_MAX_CLASSIC_FRAMES_PER_IRP * UAC_MAX_IRP_NUMBER * 8) ||
        (flags->InputHubOffset > UAC_MAX_CLASSIC_FRAMES_PER_IRP * UAC_MAX_IRP_NUMBER * 8) ||
        ((flags->OutputBufferOperationOffset & 0xfffffff) > UAC_MAX_CLASSIC_FRAMES_PER_IRP * UAC_MAX_IRP_NUMBER * 8) ||
        (flags->OutputHubOffset > UAC_MAX_CLASSIC_FRAMES_PER_IRP * UAC_MAX_IRP_NUMBER * 8) ||
        (flags->BufferThreadPriority > HIGH_PRIORITY))
    {
        isValid = false;
    }
    else
    {
        isValid = true;
    }

    return isValid;
}

PAGED_CODE_SEG
static _Use_decl_annotations_
NTSTATUS ConvertFlags(
    PUAC_SET_FLAGS_CONTEXT flags
)
{
    PAGED_CODE();

    if (flags == nullptr)
    {
        return STATUS_INVALID_PARAMETER;
    }

    int bufferSizeIndex = 0;
    for (; bufferSizeIndex < g_SettingsCount - 1; ++bufferSizeIndex)
    {
        if (g_DriverSettingsTable[bufferSizeIndex].PeriodFrames == flags->SuggestedBufferPeriod)
        {
            break;
        }
    }
    flags->ClassicFramesPerIrp = g_DriverSettingsTable[bufferSizeIndex].Parameter.ClassicFramesPerIrp;
    flags->ClassicFramesPerIrp2 = g_DriverSettingsTable[bufferSizeIndex].Parameter.ClassicFramesPerIrp2;
    flags->OutputBufferOperationOffset = g_DriverSettingsTable[bufferSizeIndex].Parameter.OutputBufferOperationOffset;
    flags->InputBufferOperationOffset = g_DriverSettingsTable[bufferSizeIndex].Parameter.InputBufferOperationOffset;

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
_Use_decl_annotations_
VOID EvtUSBAudioAcxDriverGetAudioProperty(
    WDFOBJECT  object,
    WDFREQUEST request
)
{
    NTSTATUS               status = STATUS_NOT_SUPPORTED;
    ACX_REQUEST_PARAMETERS params{};
    ULONG_PTR              outDataCb = 0;
    // ACXSTREAM              stream = static_cast<ACXSTREAM>(object);
    // ASSERT(stream != nullptr);

    WDFDEVICE device = AcxCircuitGetWdfDevice((ACXCIRCUIT)object);
    ASSERT(device != nullptr);

    PDEVICE_CONTEXT deviceContext = GetDeviceContext(device);
    ASSERT(deviceContext != nullptr);

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    ACX_REQUEST_PARAMETERS_INIT(&params);
    AcxRequestGetParameters(request, &params);

    ASSERT(params.Type == AcxRequestTypeProperty);
    ASSERT(params.Parameters.Property.Verb == AcxPropertyVerbGet);
    ASSERT(params.Parameters.Property.Control == nullptr);
    ASSERT(params.Parameters.Property.ControlCb == 0);
    ASSERT(params.Parameters.Property.Value != nullptr);
    ASSERT(params.Parameters.Property.ValueCb == sizeof(UAC_AUDIO_PROPERTY));

    IF_TRUE_ACTION_JUMP(((params.Parameters.Property.Control != nullptr) ||
                         (params.Parameters.Property.ControlCb != 0) ||
                         (params.Parameters.Property.Value == nullptr) ||
                         (params.Parameters.Property.ValueCb < sizeof(UAC_AUDIO_PROPERTY))),
                        ASSERT(FALSE);
                        outDataCb = 0; status = STATUS_INVALID_PARAMETER;,
                                                                         Exit);

    ULONG               minValueSize = sizeof(UAC_AUDIO_PROPERTY);
    PUAC_AUDIO_PROPERTY audioProperty = static_cast<PUAC_AUDIO_PROPERTY>(params.Parameters.Property.Value);

    deviceContext->AudioProperty.InputDriverBuffer = deviceContext->UsbLatency.InputDriverBuffer;
    deviceContext->AudioProperty.OutputDriverBuffer = deviceContext->UsbLatency.OutputDriverBuffer;
    // deviceContext->AudioProperty.CurrentSampleFormat = deviceContext->CurrentSampleFormat;

    *audioProperty = deviceContext->AudioProperty;

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - InputAsioChannels  %d", audioProperty->InputAsioChannels);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - OutputAsioChannels %d", audioProperty->OutputAsioChannels);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - IsAccessible    %!bool!", audioProperty->IsAccessible);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - PowerState      %d", WdfDeviceGetDevicePowerState(device));

    outDataCb = minValueSize;

    status = STATUS_SUCCESS;
Exit:
    WdfRequestCompleteWithInformation(request, status, outDataCb);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
}

PAGED_CODE_SEG
_Use_decl_annotations_
VOID EvtUSBAudioAcxDriverGetChannelInfo(
    WDFOBJECT  object,
    WDFREQUEST request
)
{
    NTSTATUS               status = STATUS_NOT_SUPPORTED;
    ACX_REQUEST_PARAMETERS params{};
    ULONG_PTR              outDataCb = 0;
    // ACXSTREAM              stream = static_cast<ACXSTREAM>(object);
    // ASSERT(stream != nullptr);

    WDFDEVICE device = AcxCircuitGetWdfDevice((ACXCIRCUIT)object);
    ASSERT(device != nullptr);

    PDEVICE_CONTEXT deviceContext = GetDeviceContext(device);
    ASSERT(deviceContext != nullptr);

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    ACX_REQUEST_PARAMETERS_INIT(&params);
    AcxRequestGetParameters(request, &params);

    ASSERT(params.Type == AcxRequestTypeProperty);
    ASSERT(params.Parameters.Property.Verb == AcxPropertyVerbGet);
    ASSERT(params.Parameters.Property.Control == nullptr);
    ASSERT(params.Parameters.Property.ControlCb == 0);

    IF_TRUE_ACTION_JUMP(((params.Parameters.Property.Control != nullptr) ||
                         (params.Parameters.Property.ControlCb != 0) ||
                         ((params.Parameters.Property.ValueCb != 0) && (params.Parameters.Property.Value == nullptr))),
                        ASSERT(FALSE);
                        outDataCb = 0; status = STATUS_INVALID_PARAMETER;,
                                                                         Exit);

    ULONG numChannels = deviceContext->AudioProperty.InputAsioChannels + deviceContext->AudioProperty.OutputAsioChannels;
    ULONG minValueSize = offsetof(UAC_GET_CHANNEL_INFO_CONTEXT, Channel) + (sizeof(UAC_CHANNEL_INFO) * numChannels);
    if (params.Parameters.Property.ValueCb == 0)
    {
        outDataCb = minValueSize;
        status = STATUS_BUFFER_OVERFLOW;
    }
    else if (params.Parameters.Property.ValueCb < minValueSize)
    {
        outDataCb = 0;
        status = STATUS_BUFFER_TOO_SMALL;
    }
    else
    {
        PUAC_GET_CHANNEL_INFO_CONTEXT channelInfo = static_cast<PUAC_GET_CHANNEL_INFO_CONTEXT>(params.Parameters.Property.Value);
        channelInfo->NumChannels = numChannels;
        BOOL  input = TRUE;
        ULONG asioCh = 0;
        for (ULONG i = 0; i < numChannels; ++i)
        {
            RtlStringCchCopyW(channelInfo->Channel[i].Name, UAC_MAX_CHANNEL_NAME_LENGTH, input ? deviceContext->InputAsioChannelName[asioCh] : deviceContext->OutputAsioChannelName[asioCh]);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - channel info. channel name [%d] %ws", i, channelInfo->Channel[i].Name);
            channelInfo->Channel[i].Index = asioCh;
            channelInfo->Channel[i].IsInput = input;
            channelInfo->Channel[i].IsActive = 0;     // not used
            channelInfo->Channel[i].ChannelGroup = 0; // not used
            ++asioCh;
            if (input && asioCh >= deviceContext->AudioProperty.InputAsioChannels)
            {
                input = false;
                asioCh = 0;
            }
        }
        outDataCb = minValueSize;
        status = STATUS_SUCCESS;
    }
Exit:
    WdfRequestCompleteWithInformation(request, status, outDataCb);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
}

PAGED_CODE_SEG
_Use_decl_annotations_
VOID EvtUSBAudioAcxDriverGetClockInfo(
    WDFOBJECT  object,
    WDFREQUEST request
)
{
    NTSTATUS               status = STATUS_NOT_SUPPORTED;
    ACX_REQUEST_PARAMETERS params{};
    ULONG_PTR              outDataCb = 0;
    // ACXSTREAM              stream = static_cast<ACXSTREAM>(object);
    // ASSERT(stream != nullptr);

    WDFDEVICE device = AcxCircuitGetWdfDevice((ACXCIRCUIT)object);
    ASSERT(device != nullptr);

    PDEVICE_CONTEXT deviceContext = GetDeviceContext(device);
    ASSERT(deviceContext != nullptr);

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    ACX_REQUEST_PARAMETERS_INIT(&params);
    AcxRequestGetParameters(request, &params);

    ASSERT(params.Type == AcxRequestTypeProperty);
    ASSERT(params.Parameters.Property.Verb == AcxPropertyVerbGet);
    ASSERT(params.Parameters.Property.Control == nullptr);
    ASSERT(params.Parameters.Property.ControlCb == 0);

    IF_TRUE_ACTION_JUMP(((params.Parameters.Property.Control != nullptr) ||
                         (params.Parameters.Property.ControlCb != 0) ||
                         ((params.Parameters.Property.ValueCb != 0) && (params.Parameters.Property.Value == nullptr))),
                        ASSERT(FALSE);
                        outDataCb = 0; status = STATUS_INVALID_PARAMETER;,
                                                                         Exit);

    ULONG numClockSources = deviceContext->AcClockSources;
    ULONG minValueSize = offsetof(UAC_GET_CLOCK_INFO_CONTEXT, ClockSource) + (sizeof(UAC_CLOCK_INFO) * numClockSources);

    if (params.Parameters.Property.ValueCb == 0)
    {
        outDataCb = minValueSize;
        status = STATUS_BUFFER_OVERFLOW;
    }
    else if (params.Parameters.Property.ValueCb < minValueSize)
    {
        outDataCb = 0;
        status = STATUS_BUFFER_TOO_SMALL;
    }
    else
    {
        PUAC_GET_CLOCK_INFO_CONTEXT clockInfo = (PUAC_GET_CLOCK_INFO_CONTEXT)(params.Parameters.Property.Value);
        clockInfo->NumClockSource = numClockSources;
        for (ULONG i = 0; i < numClockSources; ++i)
        {
            clockInfo->ClockSource[i].Index = i;
            clockInfo->ClockSource[i].AssociatedChannel = 0; // not used
            clockInfo->ClockSource[i].AssociatedGroup = 0;   // not used
            clockInfo->ClockSource[i].IsCurrentSource = (i == deviceContext->CurrentClockSource);
            clockInfo->ClockSource[i].IsLocked = 0;          // not used
            RtlStringCchCopyW(clockInfo->ClockSource[i].Name, UAC_MAX_CLOCK_SOURCE_NAME_LENGTH, deviceContext->ClockSourceName[i]);
        }
        outDataCb = minValueSize;
        status = STATUS_SUCCESS;
    }

Exit:
    WdfRequestCompleteWithInformation(request, status, outDataCb);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
}

PAGED_CODE_SEG
_Use_decl_annotations_
VOID EvtUSBAudioAcxDriverGetLatencyOffsetOfSampleRate(
    WDFOBJECT /* object */,
    WDFREQUEST request
)
{
    NTSTATUS               status = STATUS_NOT_SUPPORTED;
    ACX_REQUEST_PARAMETERS params{};
    ULONG_PTR              outDataCb = 0;
    // ACXSTREAM              stream = static_cast<ACXSTREAM>(object);
    // ASSERT(stream != nullptr);

    // WDFDEVICE device = AcxCircuitGetWdfDevice((ACXCIRCUIT)object);
    // ASSERT(device != nullptr);

    // PDEVICE_CONTEXT deviceContext = GetDeviceContext(device);
    // ASSERT(deviceContext != nullptr);

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    ACX_REQUEST_PARAMETERS_INIT(&params);
    AcxRequestGetParameters(request, &params);

    ASSERT(params.Type == AcxRequestTypeProperty);
    ASSERT(params.Parameters.Property.Verb == AcxPropertyVerbGet);
    ASSERT(params.Parameters.Property.Control != nullptr);
    ASSERT(params.Parameters.Property.ControlCb >= sizeof(UAC_SET_FLAGS_CONTEXT));

    IF_TRUE_ACTION_JUMP(((params.Parameters.Property.Control == nullptr) ||
                         (params.Parameters.Property.ControlCb < sizeof(UAC_SET_FLAGS_CONTEXT)) ||
                         ((params.Parameters.Property.ValueCb != 0) && (params.Parameters.Property.Value == nullptr))),
                        ASSERT(FALSE);
                        outDataCb = 0; status = STATUS_INVALID_PARAMETER;,
                                                                         Exit);

#if false
	// TBD
    if ((deviceContext->LatencyOffsetList != nullptr) && (deviceContext->LatencyOffsetList->NumLatencyOffsetTable != 0))
    {
        ULONG minValueSize = sizeof(UAC_LATENCY_OFFSET_OF_SAMPLERATE_CONTEXT) * deviceContext->LatencyOffsetList->NumLatencyOffsetTable;
        if (params.Parameters.Property.ValueCb == 0)
        {
            // 0 buffer length requests the buffer size needed.
            outDataCb = minValueSize;
            status    = STATUS_BUFFER_OVERFLOW;
        }
        else if (params.Parameters.Property.ValueCb < minValueSize)
        {
            outDataCb = 0;
            status    = STATUS_BUFFER_TOO_SMALL;
        }
        else
        {
            UAC_SET_FLAGS_CONTEXT flags = {0};
            if (params.Parameters.Property.ControlCb >= sizeof(UAC_SET_FLAGS_CONTEXT))
            {
                PUAC_SET_FLAGS_CONTEXT flagsInput = (PUAC_SET_FLAGS_CONTEXT)params.Parameters.Property.Control;
                if (IsValidFlags(flagsInput))
                {
                    flags = *flagsInput;
                    ConvertFlags(&flags);
                }
            }
            PUAC_LATENCY_OFFSET_OF_SAMPLERATE_CONTEXT latencyOffset = (PUAC_LATENCY_OFFSET_OF_SAMPLERATE_CONTEXT)params.Parameters.Property.Value;
            const UAC_LATENCY_OFFSET_LIST *           list          = deviceContext->LatencyOffsetList;
            for (int sampleRateIndex = 0; sampleRateIndex < list->NumLatencyOffsetTable; ++sampleRateIndex)
            {
                UAC_USB_LATENCY latency = {0};
                CalculateUsbLatency(deviceContext, &flags, list->LatencyOffsetTable[sampleRateIndex].SampleRate, &latency);
                latencyOffset[sampleRateIndex].SampleRate          = list->LatencyOffsetTable[sampleRateIndex].SampleRate;
                latencyOffset[sampleRateIndex].InputLatencyOffset  = latency.InLatency;
                latencyOffset[sampleRateIndex].OutputLatencyOffset = latency.OutputLatency;
            }
            outDataCb = minValueSize;
            status    = STATUS_SUCCESS;
        }
        else
        {
            status = STATUS_INVALID_PARAMETER;
        }
    }
    else
#endif
    {
        status = STATUS_DEVICE_NOT_READY;
    }

Exit:
    WdfRequestCompleteWithInformation(request, status, outDataCb);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
}

PAGED_CODE_SEG
_Use_decl_annotations_
VOID EvtUSBAudioAcxDriverSetClockSource(
    WDFOBJECT  object,
    WDFREQUEST request
)
{
    NTSTATUS               status = STATUS_NOT_SUPPORTED;
    ACX_REQUEST_PARAMETERS params{};
    ULONG_PTR              outDataCb = 0;
    // ACXSTREAM              stream = static_cast<ACXSTREAM>(object);
    // ASSERT(stream != nullptr);

    WDFDEVICE device = AcxCircuitGetWdfDevice((ACXCIRCUIT)object);
    ASSERT(device != nullptr);

    PDEVICE_CONTEXT deviceContext = GetDeviceContext(device);
    ASSERT(deviceContext != nullptr);

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    ACX_REQUEST_PARAMETERS_INIT(&params);
    AcxRequestGetParameters(request, &params);

    ASSERT(params.Type == AcxRequestTypeProperty);
    ASSERT(params.Parameters.Property.Verb == AcxPropertyVerbSet);
    ASSERT(params.Parameters.Property.Control == nullptr);
    ASSERT(params.Parameters.Property.ControlCb == 0);
    ASSERT(params.Parameters.Property.Value != nullptr);
    ASSERT(params.Parameters.Property.ValueCb >= sizeof(UAC_SET_CLOCK_SOURCE_CONTEXT));

    IF_TRUE_ACTION_JUMP(((params.Parameters.Property.Control != nullptr) ||
                         (params.Parameters.Property.ControlCb != 0 ||
                          (params.Parameters.Property.Value == nullptr) ||
                          (params.Parameters.Property.ValueCb < sizeof(UAC_SET_CLOCK_SOURCE_CONTEXT)))),
                        ASSERT(FALSE);
                        outDataCb = 0; status = STATUS_INVALID_PARAMETER;,
                                                                         Exit);

    PUAC_SET_CLOCK_SOURCE_CONTEXT context = (PUAC_SET_CLOCK_SOURCE_CONTEXT)params.Parameters.Property.Value;
    if (context->Index == deviceContext->CurrentClockSource)
    {
        status = STATUS_SUCCESS;
    }
    else if (deviceContext->AcClockSources > 1)
    {
        status = ControlRequestSetClockSelector(deviceContext, deviceContext->AudioProperty.AudioControlInterfaceNumber, deviceContext->ClockSelectorId, deviceContext->AcClockSourceInfo[(USHORT)context->Index].ClockSelectorIndex);

        WdfWaitLockAcquire(deviceContext->StreamWaitLock, nullptr);
        if (deviceContext->ClockObservationThread != nullptr && NT_SUCCESS(status))
        {
            deviceContext->CurrentClockSource = context->Index;
            ULONG newRate = deviceContext->AudioProperty.SampleRate;

            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_MULTICLIENT, " - start counter asio %ld, start counter acx audio %ld, start counter iso stream %ld", deviceContext->StartCounterAsio, deviceContext->StartCounterWdmAudio, deviceContext->StartCounterIsoStream);
            if ((deviceContext->StartCounterAsio != 0) || (deviceContext->StartCounterWdmAudio != 0))
            {
                StopIsoStream(deviceContext);
            }
            deviceContext->ResetRequestOwner = deviceContext->AsioOwner;

            ULONG desiredFormatType = NS_USBAudio0200::FORMAT_TYPE_I;
            ULONG desiredFormat = NS_USBAudio0200::PCM;

            USBAudioDataFormat::ConvertFormatToSampleFormat(deviceContext->AudioProperty.CurrentSampleFormat, desiredFormatType, desiredFormat);

            status = ActivateAudioInterface(deviceContext, newRate, desiredFormatType, desiredFormat, deviceContext->AudioProperty.InputBytesPerSample, deviceContext->AudioProperty.InputValidBitsPerSample, deviceContext->AudioProperty.OutputBytesPerSample, deviceContext->AudioProperty.OutputValidBitsPerSample);
            if ((deviceContext->StartCounterAsio != 0) || (deviceContext->StartCounterWdmAudio != 0))
            {
                StartIsoStream(deviceContext);
            }
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_MULTICLIENT, " - start counter asio %ld, start counter acx audio %ld, start counter iso stream %ld", deviceContext->StartCounterAsio, deviceContext->StartCounterWdmAudio, deviceContext->StartCounterIsoStream);
        }
        WdfWaitLockRelease(deviceContext->StreamWaitLock);
    }
    else
    {
    }
Exit:
    WdfRequestCompleteWithInformation(request, status, outDataCb);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
}

PAGED_CODE_SEG
_Use_decl_annotations_
VOID EvtUSBAudioAcxDriverSetFlags(
    WDFOBJECT  object,
    WDFREQUEST request
)
{
    NTSTATUS               status = STATUS_NOT_SUPPORTED;
    ACX_REQUEST_PARAMETERS params{};
    ULONG_PTR              outDataCb = 0;
    // ACXSTREAM              stream = static_cast<ACXSTREAM>(object);
    // ASSERT(stream != nullptr);

    WDFDEVICE device = AcxCircuitGetWdfDevice((ACXCIRCUIT)object);
    ASSERT(device != nullptr);

    PDEVICE_CONTEXT deviceContext = GetDeviceContext(device);
    ASSERT(deviceContext != nullptr);

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    ACX_REQUEST_PARAMETERS_INIT(&params);
    AcxRequestGetParameters(request, &params);

    ASSERT(params.Type == AcxRequestTypeProperty);
    ASSERT(params.Parameters.Property.Verb == AcxPropertyVerbSet);
    ASSERT(params.Parameters.Property.Control == nullptr);
    ASSERT(params.Parameters.Property.ControlCb == 0);
    ASSERT(params.Parameters.Property.Value != nullptr);
    ASSERT(params.Parameters.Property.ValueCb >= sizeof(UAC_SET_FLAGS_CONTEXT));

    IF_TRUE_ACTION_JUMP(((params.Parameters.Property.Control != nullptr) ||
                         (params.Parameters.Property.ControlCb != 0 ||
                          (params.Parameters.Property.Value == nullptr) ||
                          (params.Parameters.Property.ValueCb < sizeof(UAC_SET_FLAGS_CONTEXT)))),
                        ASSERT(FALSE);
                        outDataCb = 0; status = STATUS_INVALID_PARAMETER;,
                                                                         Exit);

    PUAC_SET_FLAGS_CONTEXT flags = (PUAC_SET_FLAGS_CONTEXT)params.Parameters.Property.Value;
    if (!IsValidFlags(flags))
    {
        status = STATUS_INVALID_PARAMETER;
    }
    else if ((deviceContext->Params.FirstPacketLatency != flags->FirstPacketLatency) ||
             (deviceContext->Params.ClassicFramesPerIrp != flags->ClassicFramesPerIrp) ||
             (deviceContext->Params.ClassicFramesPerIrp2 != flags->ClassicFramesPerIrp2) ||
             (deviceContext->Params.MaxIrpNumber != flags->MaxIrpNumber) ||
             (deviceContext->Params.PreSendFrames != flags->PreSendFrames) ||
             (deviceContext->Params.OutputFrameDelay != flags->OutputFrameDelay) ||
             (deviceContext->Params.DelayedOutputBufferSwitch != flags->DelayedOutputBufferSwitch) ||
             //(deviceContext->Params.BufferOperationThread != flags->BufferOperationThread) ||
             (deviceContext->Params.InputBufferOperationOffset != flags->InputBufferOperationOffset) ||
             (deviceContext->Params.InputHubOffset != flags->InputHubOffset) ||
             (deviceContext->Params.OutputBufferOperationOffset != flags->OutputBufferOperationOffset) ||
             (deviceContext->Params.OutputHubOffset != flags->OutputHubOffset) ||
             (deviceContext->Params.BufferThreadPriority != flags->BufferThreadPriority) ||
             (deviceContext->Params.SuggestedBufferPeriod != flags->SuggestedBufferPeriod))
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - FirstPacketLatency        = %u -> %u", deviceContext->Params.FirstPacketLatency, flags->FirstPacketLatency);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - ClassicFramesPerIrp       = %u -> %u", deviceContext->Params.ClassicFramesPerIrp, flags->ClassicFramesPerIrp);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - ClassicFramesPerIrp2      = %u -> %u", deviceContext->Params.ClassicFramesPerIrp2, flags->ClassicFramesPerIrp2);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - MaxIrpNumber              = %u -> %u", deviceContext->Params.MaxIrpNumber, flags->MaxIrpNumber);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - PreSendFrames             = %u -> %u", deviceContext->Params.PreSendFrames, flags->PreSendFrames);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - OutputFrameDelay          = %d -> %d", deviceContext->Params.OutputFrameDelay, flags->OutputFrameDelay);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - DelayedOutputBufferSwitch = %u -> %u", deviceContext->Params.DelayedOutputBufferSwitch, flags->DelayedOutputBufferSwitch);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - InputBufferOperationOffset   = %u -> %u", deviceContext->Params.InputBufferOperationOffset, flags->InputBufferOperationOffset);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - InputHubOffset               = %u -> %u", deviceContext->Params.InputHubOffset, flags->InputHubOffset);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - OutputBufferOperationOffset  = %u -> %u", deviceContext->Params.OutputBufferOperationOffset, flags->OutputBufferOperationOffset);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - OutputHubOffset              = %u -> %u", deviceContext->Params.OutputHubOffset, flags->OutputHubOffset);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - BufferThreadPriority      = %u -> %u", deviceContext->Params.BufferThreadPriority, flags->BufferThreadPriority);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - SuggestedBufferPeriod     = %u -> %u", deviceContext->Params.SuggestedBufferPeriod, flags->SuggestedBufferPeriod);

        WdfWaitLockAcquire(deviceContext->StreamWaitLock, nullptr);
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_MULTICLIENT, " - start counter asio %ld, start counter acx audio %ld, start counter iso stream %ld", deviceContext->StartCounterAsio, deviceContext->StartCounterWdmAudio, deviceContext->StartCounterIsoStream);
        if ((deviceContext->StartCounterAsio != 0) || (deviceContext->StartCounterWdmAudio != 0))
        {
            StopIsoStream(deviceContext);
        }
        UAC_SET_FLAGS_CONTEXT tempFlags;
        RtlCopyMemory(&tempFlags, flags, sizeof(tempFlags));

        ConvertFlags(&tempFlags);

        deviceContext->Params.FirstPacketLatency = tempFlags.FirstPacketLatency;
        deviceContext->Params.ClassicFramesPerIrp = tempFlags.ClassicFramesPerIrp;
        deviceContext->Params.MaxIrpNumber = tempFlags.MaxIrpNumber;
        deviceContext->Params.PreSendFrames = tempFlags.PreSendFrames;
        deviceContext->Params.OutputFrameDelay = tempFlags.OutputFrameDelay;
        deviceContext->Params.DelayedOutputBufferSwitch = tempFlags.DelayedOutputBufferSwitch;
        // deviceContext->Params.BufferOperationThread = tempFlags.BufferOperationThread;
        deviceContext->Params.InputBufferOperationOffset = tempFlags.InputBufferOperationOffset;
        deviceContext->Params.InputHubOffset = tempFlags.InputHubOffset;
        deviceContext->Params.OutputBufferOperationOffset = tempFlags.OutputBufferOperationOffset;
        deviceContext->Params.OutputHubOffset = tempFlags.OutputHubOffset;
        deviceContext->Params.BufferThreadPriority = tempFlags.BufferThreadPriority;
        deviceContext->Params.ClassicFramesPerIrp2 = tempFlags.ClassicFramesPerIrp2;
        deviceContext->Params.SuggestedBufferPeriod = tempFlags.SuggestedBufferPeriod;

        ULONG desiredFormatType = NS_USBAudio0200::FORMAT_TYPE_I;
        ULONG desiredFormat = NS_USBAudio0200::PCM;

        USBAudioDataFormat::ConvertFormatToSampleFormat(deviceContext->AudioProperty.CurrentSampleFormat, desiredFormatType, desiredFormat);

        status = ActivateAudioInterface(deviceContext, deviceContext->AudioProperty.SampleRate, desiredFormatType, desiredFormat, deviceContext->AudioProperty.InputBytesPerSample, deviceContext->AudioProperty.InputValidBitsPerSample, deviceContext->AudioProperty.OutputBytesPerSample, deviceContext->AudioProperty.OutputValidBitsPerSample);
        //					WriteDeviceParams(deviceContext);
        if (NT_SUCCESS(status))
        {
            if ((deviceContext->StartCounterAsio != 0) || (deviceContext->StartCounterWdmAudio != 0))
            {
                StartIsoStream(deviceContext);
            }
            else
            {
                ASSERT(NT_SUCCESS(status));
            }
        }
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_MULTICLIENT, " - start counter asio %ld, start counter acx audio %ld, start counter iso stream %ld", deviceContext->StartCounterAsio, deviceContext->StartCounterWdmAudio, deviceContext->StartCounterIsoStream);
        WdfWaitLockRelease(deviceContext->StreamWaitLock);
        status = STATUS_SUCCESS;
    }
    else
    {
        // Nothing is done because there is no change in flag.
        status = STATUS_SUCCESS;
    }
Exit:
    WdfRequestCompleteWithInformation(request, status, outDataCb);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
}

PAGED_CODE_SEG
_Use_decl_annotations_
VOID EvtUSBAudioAcxDriverSetSampleFormat(
    WDFOBJECT  object,
    WDFREQUEST request
)
{
    NTSTATUS               status = STATUS_NOT_SUPPORTED;
    ACX_REQUEST_PARAMETERS params{};
    ULONG_PTR              outDataCb = 0;
    // ACXSTREAM              stream = static_cast<ACXSTREAM>(object);
    // ASSERT(stream != nullptr);

    WDFDEVICE device = AcxCircuitGetWdfDevice((ACXCIRCUIT)object);
    ASSERT(device != nullptr);

    PDEVICE_CONTEXT deviceContext = GetDeviceContext(device);
    ASSERT(deviceContext != nullptr);

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    ACX_REQUEST_PARAMETERS_INIT(&params);
    AcxRequestGetParameters(request, &params);

    ASSERT(params.Type == AcxRequestTypeProperty);
    ASSERT(params.Parameters.Property.Verb == AcxPropertyVerbSet);
    ASSERT(params.Parameters.Property.Control == nullptr);
    ASSERT(params.Parameters.Property.ControlCb == 0);
    ASSERT(params.Parameters.Property.Value != nullptr);
    ASSERT(params.Parameters.Property.ValueCb >= sizeof(ULONG));

    IF_TRUE_ACTION_JUMP(((params.Parameters.Property.Control != nullptr) ||
                         (params.Parameters.Property.ControlCb != 0 ||
                          (params.Parameters.Property.Value == nullptr) ||
                          (params.Parameters.Property.ValueCb < sizeof(ULONG)))),
                        ASSERT(FALSE);
                        outDataCb = 0; status = STATUS_INVALID_PARAMETER;,
                                                                         Exit);

    UACSampleFormat sampleFormat = (UACSampleFormat)(*(PULONG)params.Parameters.Property.Value);
    if ((deviceContext->AudioProperty.SupportedSampleFormats & (1 << toULong(sampleFormat))) == 0)
    {
        status = STATUS_INVALID_PARAMETER;
    }
    else if (sampleFormat == deviceContext->AudioProperty.CurrentSampleFormat)
    {
        status = STATUS_SUCCESS;
    }
    else
    {
        ULONG formatType = 0;
        ULONG format = 0;

        WdfWaitLockAcquire(deviceContext->StreamWaitLock, nullptr);

        StopIsoStream(deviceContext);
        deviceContext->DesiredSampleFormat = sampleFormat;
        status = USBAudioDataFormat::ConvertFormatToSampleFormat(sampleFormat, formatType, format);
        if (NT_SUCCESS(status))
        {
            status = ActivateAudioInterface(deviceContext, deviceContext->AudioProperty.SampleRate, formatType, format, deviceContext->AudioProperty.InputBytesPerSample, deviceContext->AudioProperty.InputValidBitsPerSample, deviceContext->AudioProperty.OutputBytesPerSample, deviceContext->AudioProperty.OutputValidBitsPerSample);
        }
        WdfWaitLockRelease(deviceContext->StreamWaitLock);
        status = STATUS_SUCCESS;
    }

Exit:
    WdfRequestCompleteWithInformation(request, status, outDataCb);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
}

PAGED_CODE_SEG
_Use_decl_annotations_
VOID EvtUSBAudioAcxDriverChangeSampleRate(
    WDFOBJECT  object,
    WDFREQUEST request
)
{
    NTSTATUS               status = STATUS_NOT_SUPPORTED;
    ACX_REQUEST_PARAMETERS params{};
    ULONG_PTR              outDataCb = 0;
    // ACXSTREAM              stream = static_cast<ACXSTREAM>(object);
    // ASSERT(stream != nullptr);

    WDFDEVICE device = AcxCircuitGetWdfDevice((ACXCIRCUIT)object);
    ASSERT(device != nullptr);

    PDEVICE_CONTEXT deviceContext = GetDeviceContext(device);
    ASSERT(deviceContext != nullptr);

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    ACX_REQUEST_PARAMETERS_INIT(&params);
    AcxRequestGetParameters(request, &params);

    ASSERT(params.Type == AcxRequestTypeProperty);
    ASSERT(params.Parameters.Property.Verb == AcxPropertyVerbSet);
    ASSERT(params.Parameters.Property.Control == nullptr);
    ASSERT(params.Parameters.Property.ControlCb == 0);
    ASSERT(params.Parameters.Property.Value != nullptr);
    ASSERT(params.Parameters.Property.ValueCb >= sizeof(ULONG));

    IF_TRUE_ACTION_JUMP(((params.Parameters.Property.Control != nullptr) ||
                         (params.Parameters.Property.ControlCb != 0 ||
                          (params.Parameters.Property.Value == nullptr) ||
                          (params.Parameters.Property.ValueCb < sizeof(ULONG)))),
                        ASSERT(FALSE);
                        outDataCb = 0; status = STATUS_INVALID_PARAMETER;,
                                                                         Exit);

    ULONG desiredRate = *((ULONG *)params.Parameters.Property.Value);
    bool  streamRunning = false;
    WdfWaitLockAcquire(deviceContext->StreamWaitLock, nullptr);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_MULTICLIENT, " - start counter asio %ld, start counter acx audio %ld, start counter iso stream %ld", deviceContext->StartCounterAsio, deviceContext->StartCounterWdmAudio, deviceContext->StartCounterIsoStream);
    if (deviceContext->StreamObject != nullptr)
    {
        if (deviceContext->AsioBufferObject == nullptr)
        {
            streamRunning = true;
        }
        if ((deviceContext->StartCounterAsio != 0) || (deviceContext->StartCounterWdmAudio != 0))
        {
            StopIsoStream(deviceContext);
        }
    }
    ACXDATAFORMAT inputDataFormatBeforeChange = nullptr;
    ACXDATAFORMAT outputDataFormatBeforeChange = nullptr;
    ACXDATAFORMAT inputDataFormatAfterChange = nullptr;
    ACXDATAFORMAT outputDataFormatAfterChange = nullptr;

    status = USBAudioAcxDriverGetCurrentDataFormat(deviceContext, true, inputDataFormatBeforeChange);
    IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

    status = USBAudioAcxDriverGetCurrentDataFormat(deviceContext, false, outputDataFormatBeforeChange);
    IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

    if (NT_SUCCESS(status))
    {
        ULONG desiredFormatType = NS_USBAudio0200::FORMAT_TYPE_I;
        ULONG desiredFormat = NS_USBAudio0200::PCM;

        USBAudioDataFormat::ConvertFormatToSampleFormat(deviceContext->AudioProperty.CurrentSampleFormat, desiredFormatType, desiredFormat);

        status = ActivateAudioInterface(deviceContext, desiredRate, desiredFormatType, desiredFormat, deviceContext->AudioProperty.InputBytesPerSample, deviceContext->AudioProperty.InputValidBitsPerSample, deviceContext->AudioProperty.OutputBytesPerSample, deviceContext->AudioProperty.OutputValidBitsPerSample);
        if (streamRunning && NT_SUCCESS(status))
        {
            if ((deviceContext->StartCounterAsio != 0) || (deviceContext->StartCounterWdmAudio != 0))
            {
                StartIsoStream(deviceContext);
            }
        }
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_MULTICLIENT, " - start counter asio %ld, start counter acx audio %ld, start counter iso stream %ld", deviceContext->StartCounterAsio, deviceContext->StartCounterWdmAudio, deviceContext->StartCounterIsoStream);
    }

    IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

    status = USBAudioAcxDriverGetCurrentDataFormat(deviceContext, true, inputDataFormatAfterChange);
    IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

    status = USBAudioAcxDriverGetCurrentDataFormat(deviceContext, false, outputDataFormatAfterChange);
    IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

    status = NotifyAllPinsDataFormatChange(false, deviceContext, outputDataFormatBeforeChange, outputDataFormatAfterChange);
    IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

    status = NotifyAllPinsDataFormatChange(true, deviceContext, inputDataFormatBeforeChange, inputDataFormatAfterChange);
    IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

    status = STATUS_SUCCESS;

Exit_BeforeWaitLockRelease:
    WdfWaitLockRelease(deviceContext->StreamWaitLock);

Exit:
    WdfRequestCompleteWithInformation(request, status, outDataCb);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
}

PAGED_CODE_SEG
_Use_decl_annotations_
VOID EvtUSBAudioAcxDriverGetAsioOwnership(
    WDFOBJECT  object,
    WDFREQUEST request
)
/*++

Routine Description:

    This routine acquires ASIO ownership.

Return Value:

    VOID

--*/
{
    NTSTATUS               status = STATUS_NOT_SUPPORTED;
    ACX_REQUEST_PARAMETERS params{};
    ULONG_PTR              outDataCb = 0;
    LARGE_INTEGER          systemTime = {0};
    // ACXSTREAM              stream = static_cast<ACXSTREAM>(object);
    // ASSERT(stream != nullptr);

    WDFDEVICE device = AcxCircuitGetWdfDevice((ACXCIRCUIT)object);
    ASSERT(device != nullptr);

    PDEVICE_CONTEXT deviceContext = GetDeviceContext(device);
    ASSERT(deviceContext != nullptr);

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    ACX_REQUEST_PARAMETERS_INIT(&params);
    AcxRequestGetParameters(request, &params);

    ASSERT(params.Type == AcxRequestTypeProperty);
    ASSERT(params.Parameters.Property.Verb == AcxPropertyVerbSet);
    ASSERT(params.Parameters.Property.Control == nullptr);
    ASSERT(params.Parameters.Property.ControlCb == 0);
    ASSERT(params.Parameters.Property.Value == nullptr);
    ASSERT(params.Parameters.Property.ValueCb == 0);

    IF_TRUE_ACTION_JUMP(((params.Parameters.Property.Control != nullptr) ||
                         (params.Parameters.Property.ControlCb != 0) ||
                         (params.Parameters.Property.Value != nullptr) ||
                         (params.Parameters.Property.ValueCb != 0)),
                        ASSERT(FALSE);
                        outDataCb = 0; status = STATUS_INVALID_PARAMETER;,
                                                                         Exit);

    KeQuerySystemTime(&systemTime);
    if (deviceContext->AsioOwner != nullptr || systemTime.QuadPart < deviceContext->ResetEnableTime.QuadPart)
    {
        status = STATUS_ACCESS_DENIED;
    }
    else
    {
        ULONG         inputBytesPerSample = 0;
        ULONG         inputValidBitsPerSample = 0;
        ULONG         outputBytesPerSample = 0;
        ULONG         outputValidBitsPerSample = 0;
        ULONG         desiredFormatType = NS_USBAudio0200::FORMAT_TYPE_I;
        ULONG         desiredFormat = NS_USBAudio0200::PCM;
        ACXDATAFORMAT inputDataFormatBeforeChange = nullptr;
        ACXDATAFORMAT outputDataFormatBeforeChange = nullptr;
        ACXDATAFORMAT inputDataFormatAfterChange = nullptr;
        ACXDATAFORMAT outputDataFormatAfterChange = nullptr;

        WdfWaitLockAcquire(deviceContext->StreamWaitLock, nullptr);

        status = USBAudioAcxDriverGetCurrentDataFormat(deviceContext, true, inputDataFormatBeforeChange);
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

        status = USBAudioAcxDriverGetCurrentDataFormat(deviceContext, false, outputDataFormatBeforeChange);
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

        status = USBAudioDataFormat::ConvertFormatToSampleFormat(deviceContext->AudioProperty.CurrentSampleFormat, desiredFormatType, desiredFormat);
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

        if (deviceContext->AudioProperty.SupportedSampleFormats & (1 << toULong(UACSampleFormat::UAC_SAMPLE_FORMAT_IEEE_FLOAT)))
        {
            deviceContext->SampleFormatBackup = deviceContext->AudioProperty.CurrentSampleFormat;
            desiredFormatType = NS_USBAudio0200::FORMAT_TYPE_I;
            desiredFormat = NS_USBAudio0200::IEEE_FLOAT;
        }
        status = deviceContext->UsbAudioConfiguration->GetMaxSupportedValidBitsPerSample(true, desiredFormatType, desiredFormat, inputBytesPerSample, inputValidBitsPerSample);
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

        status = deviceContext->UsbAudioConfiguration->GetMaxSupportedValidBitsPerSample(false, desiredFormatType, desiredFormat, outputBytesPerSample, outputValidBitsPerSample);
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

        //
        // When using ASIO, the maximum bit depth is used independently for input and output.
        //
        status = ActivateAudioInterface(deviceContext, deviceContext->AudioProperty.SampleRate, desiredFormatType, desiredFormat, inputBytesPerSample, inputValidBitsPerSample, outputBytesPerSample, outputValidBitsPerSample);

        WDFFILEOBJECT fileObject = WdfRequestGetFileObject(request);
        if (fileObject != nullptr)
        {
            deviceContext->AsioOwner = fileObject;

            PFILE_CONTEXT fileContext = GetFileContext(fileObject);
            if (fileContext != nullptr)
            {
                fileContext->DeviceContext = deviceContext;
            }
            status = STATUS_SUCCESS;
        }
        else
        {
            status = STATUS_INVALID_DEVICE_REQUEST;
        }
        status = USBAudioAcxDriverGetCurrentDataFormat(deviceContext, true, inputDataFormatAfterChange);
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

        status = USBAudioAcxDriverGetCurrentDataFormat(deviceContext, false, outputDataFormatAfterChange);
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

        status = NotifyAllPinsDataFormatChange(false, deviceContext, outputDataFormatBeforeChange, outputDataFormatAfterChange);
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

        status = NotifyAllPinsDataFormatChange(true, deviceContext, inputDataFormatBeforeChange, inputDataFormatAfterChange);
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

    Exit_BeforeWaitLockRelease:
        WdfWaitLockRelease(deviceContext->StreamWaitLock);
    }
Exit:
    WdfRequestCompleteWithInformation(request, status, outDataCb);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
}

PAGED_CODE_SEG
_Use_decl_annotations_
VOID EvtUSBAudioAcxDriverStartAsioStream(
    WDFOBJECT  object,
    WDFREQUEST request
)
{
    NTSTATUS               status = STATUS_NOT_SUPPORTED;
    ACX_REQUEST_PARAMETERS params{};
    ULONG_PTR              outDataCb = 0;
    // ACXSTREAM              stream = static_cast<ACXSTREAM>(object);
    // ASSERT(stream != nullptr);

    WDFDEVICE device = AcxCircuitGetWdfDevice((ACXCIRCUIT)object);
    ASSERT(device != nullptr);

    PDEVICE_CONTEXT deviceContext = GetDeviceContext(device);
    ASSERT(deviceContext != nullptr);

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    ACX_REQUEST_PARAMETERS_INIT(&params);
    AcxRequestGetParameters(request, &params);

    ASSERT(params.Type == AcxRequestTypeProperty);
    ASSERT(params.Parameters.Property.Verb == AcxPropertyVerbSet);
    ASSERT(params.Parameters.Property.Control == nullptr);
    ASSERT(params.Parameters.Property.ControlCb = 0);
    ASSERT(params.Parameters.Property.Value == nullptr);
    ASSERT(params.Parameters.Property.ValueCb == 0);

    IF_TRUE_ACTION_JUMP(((params.Parameters.Property.Control != nullptr) ||
                         (params.Parameters.Property.ControlCb != 0) ||
                         (params.Parameters.Property.Value != nullptr) ||
                         (params.Parameters.Property.ValueCb != 0)),
                        ASSERT(FALSE);
                        outDataCb = 0; status = STATUS_INVALID_PARAMETER;,
                                                                         Exit);

    WdfWaitLockAcquire(deviceContext->StreamWaitLock, nullptr);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_MULTICLIENT, " - start counter asio %ld, start counter acx audio %ld, start counter iso stream %ld", deviceContext->StartCounterAsio, deviceContext->StartCounterWdmAudio, deviceContext->StartCounterIsoStream);
    if (deviceContext->StartCounterAsio == 0)
    {
        if (deviceContext->StartCounterWdmAudio == 0)
        {
            status = StartIsoStream(deviceContext);
        }
        else
        {
            if (deviceContext->AsioBufferObject != nullptr)
            {
                deviceContext->AsioBufferObject->SetReady();
                status = STATUS_SUCCESS;
            }
            else
            {
                status = STATUS_UNSUCCESSFUL;
            }
        }
        if (NT_SUCCESS(status))
        {
            InterlockedIncrement(&deviceContext->StartCounterAsio);
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_MULTICLIENT, " - start counter asio %ld, start counter acx audio %ld, start counter iso stream %ld", deviceContext->StartCounterAsio, deviceContext->StartCounterWdmAudio, deviceContext->StartCounterIsoStream);
        }
    }
    else
    {
        status = STATUS_SUCCESS;
    }
    WdfWaitLockRelease(deviceContext->StreamWaitLock);
Exit:
    WdfRequestCompleteWithInformation(request, status, outDataCb);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
}

PAGED_CODE_SEG
_Use_decl_annotations_
VOID EvtUSBAudioAcxDriverStopAsioStream(
    WDFOBJECT  object,
    WDFREQUEST request
)
{
    NTSTATUS               status = STATUS_NOT_SUPPORTED;
    ACX_REQUEST_PARAMETERS params{};
    ULONG_PTR              outDataCb = 0;
    // ACXSTREAM              stream = static_cast<ACXSTREAM>(object);
    // ASSERT(stream != nullptr);

    WDFDEVICE device = AcxCircuitGetWdfDevice((ACXCIRCUIT)object);
    ASSERT(device != nullptr);

    PDEVICE_CONTEXT deviceContext = GetDeviceContext(device);
    ASSERT(deviceContext != nullptr);

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    ACX_REQUEST_PARAMETERS_INIT(&params);
    AcxRequestGetParameters(request, &params);

    ASSERT(params.Type == AcxRequestTypeProperty);
    ASSERT(params.Parameters.Property.Verb == AcxPropertyVerbSet);
    ASSERT(params.Parameters.Property.Control == nullptr);
    ASSERT(params.Parameters.Property.ControlCb == 0);
    ASSERT(params.Parameters.Property.Value == nullptr);
    ASSERT(params.Parameters.Property.ValueCb == 0);

    IF_TRUE_ACTION_JUMP(((params.Parameters.Property.Control != nullptr) ||
                         (params.Parameters.Property.ControlCb != 0) ||
                         (params.Parameters.Property.Value != nullptr) ||
                         (params.Parameters.Property.ValueCb != 0)),
                        ASSERT(FALSE);
                        outDataCb = 0; status = STATUS_INVALID_PARAMETER;,
                                                                         Exit);

    WdfWaitLockAcquire(deviceContext->StreamWaitLock, nullptr);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_MULTICLIENT, " - start counter asio %ld, start counter acx audio %ld, start counter iso stream %ld", deviceContext->StartCounterAsio, deviceContext->StartCounterWdmAudio, deviceContext->StartCounterIsoStream);
    if (deviceContext->StartCounterAsio)
    {
        InterlockedDecrement(&deviceContext->StartCounterAsio);
        if ((deviceContext->StartCounterAsio == 0) && (deviceContext->StartCounterWdmAudio == 0))
        {
            status = StopIsoStream(deviceContext);
        }
        else
        {
            status = STATUS_SUCCESS;
        }
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_MULTICLIENT, " - start counter asio %ld, start counter acx audio %ld, start counter iso stream %ld", deviceContext->StartCounterAsio, deviceContext->StartCounterWdmAudio, deviceContext->StartCounterIsoStream);
    }
    else
    {
        status = STATUS_SUCCESS;
    }

    WdfWaitLockRelease(deviceContext->StreamWaitLock);

Exit:
    WdfRequestCompleteWithInformation(request, status, outDataCb);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
}

PAGED_CODE_SEG
_Use_decl_annotations_
VOID EvtUSBAudioAcxDriverSetAsioBuffer(
    WDFOBJECT  object,
    WDFREQUEST request
)
{
    NTSTATUS               status = STATUS_NOT_SUPPORTED;
    ACX_REQUEST_PARAMETERS params{};
    ULONG_PTR              outDataCb = 0;
    // ACXSTREAM              stream = static_cast<ACXSTREAM>(object);
    // ASSERT(stream != nullptr);

    WDFDEVICE device = AcxCircuitGetWdfDevice((ACXCIRCUIT)object);
    ASSERT(device != nullptr);

    PDEVICE_CONTEXT deviceContext = GetDeviceContext(device);
    ASSERT(deviceContext != nullptr);

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    ACX_REQUEST_PARAMETERS_INIT(&params);
    AcxRequestGetParameters(request, &params);

    ASSERT(params.Type == AcxRequestTypeProperty);
    ASSERT(params.Parameters.Property.Verb == AcxPropertyVerbSet);
    ASSERT(params.Parameters.Property.Control != nullptr);
    ASSERT(params.Parameters.Property.ControlCb >= sizeof(UAC_ASIO_PLAY_BUFFER_HEADER));
    ASSERT(params.Parameters.Property.Value != nullptr);
    ASSERT(params.Parameters.Property.ValueCb >= sizeof(UAC_ASIO_REC_BUFFER_HEADER));

    WdfWaitLockAcquire(deviceContext->StreamWaitLock, nullptr);

    IF_TRUE_ACTION_JUMP(((params.Parameters.Property.Control == nullptr) ||
                         (params.Parameters.Property.ControlCb < sizeof(UAC_ASIO_PLAY_BUFFER_HEADER)) ||
                         (params.Parameters.Property.Value == nullptr) ||
                         (params.Parameters.Property.ValueCb < sizeof(UAC_ASIO_REC_BUFFER_HEADER))),
                        ASSERT(FALSE);
                        outDataCb = 0; status = STATUS_INVALID_PARAMETER;,
                                                                         Exit);

    IF_TRUE_ACTION_JUMP((deviceContext->AsioBufferOwner != nullptr) || (deviceContext->AsioBufferObject != nullptr),
                        outDataCb = 0;
                        status = STATUS_DEVICE_BUSY;, Exit);

    deviceContext->AsioBufferObject = AsioBufferObject::Create(deviceContext);
    IF_TRUE_ACTION_JUMP(deviceContext->AsioBufferObject == nullptr,
                        outDataCb = 0;
                        STATUS_INSUFFICIENT_RESOURCES, Exit);

    PIRP irp = WdfRequestWdmGetIrp(request);

    IF_TRUE_ACTION_JUMP(irp == nullptr, ASSERT(FALSE); outDataCb = 0; status = STATUS_INVALID_PARAMETER;, Exit);

    PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(irp);
    PBYTE              inBuffer = (PBYTE)irpStack->Parameters.DeviceIoControl.Type3InputBuffer;
    PBYTE              outBuffer = (PBYTE)irp->UserBuffer;
    ULONG              inBufferLength = irpStack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG              outBufferLength = irpStack->Parameters.DeviceIoControl.OutputBufferLength;

    outDataCb = params.Parameters.Property.ValueCb;

    status = deviceContext->AsioBufferObject->SetBuffer(
        static_cast<ULONG>(outBufferLength),
        (PBYTE)outBuffer,
        0,
        static_cast<ULONG>(inBufferLength),
        (PBYTE)inBuffer,
        sizeof(KSPROPERTY)
    );
Exit:
    WdfWaitLockRelease(deviceContext->StreamWaitLock);

    WdfRequestCompleteWithInformation(request, status, outDataCb);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
}

PAGED_CODE_SEG
_Use_decl_annotations_
VOID EvtUSBAudioAcxDriverUnsetAsioBuffer(
    WDFOBJECT  object,
    WDFREQUEST request
)
{
    NTSTATUS               status = STATUS_NOT_SUPPORTED;
    ACX_REQUEST_PARAMETERS params{};
    ULONG_PTR              outDataCb = 0;
    // ACXSTREAM              stream = static_cast<ACXSTREAM>(object);
    // ASSERT(stream != nullptr);

    WDFDEVICE device = AcxCircuitGetWdfDevice((ACXCIRCUIT)object);
    ASSERT(device != nullptr);

    PDEVICE_CONTEXT deviceContext = GetDeviceContext(device);
    ASSERT(deviceContext != nullptr);

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    ACX_REQUEST_PARAMETERS_INIT(&params);
    AcxRequestGetParameters(request, &params);

    ASSERT(params.Type == AcxRequestTypeProperty);
    ASSERT(params.Parameters.Property.Verb == AcxPropertyVerbSet);
    ASSERT(params.Parameters.Property.Control == nullptr);
    ASSERT(params.Parameters.Property.ControlCb == 0);
    ASSERT(params.Parameters.Property.Value == nullptr);
    ASSERT(params.Parameters.Property.ValueCb == 0);

    WdfWaitLockAcquire(deviceContext->StreamWaitLock, nullptr);

    IF_TRUE_ACTION_JUMP(((params.Parameters.Property.Control != nullptr) ||
                         (params.Parameters.Property.ControlCb != 0) ||
                         (params.Parameters.Property.Value != nullptr) ||
                         (params.Parameters.Property.ValueCb != 0)),
                        ASSERT(FALSE);
                        outDataCb = 0; status = STATUS_INVALID_PARAMETER;,
                                                                         Exit);

    if (deviceContext->AsioBufferObject != nullptr)
    {
        status = deviceContext->AsioBufferObject->UnsetBuffer();
        delete deviceContext->AsioBufferObject;
        deviceContext->AsioBufferObject = nullptr;
    }
    else
    {
        status = STATUS_SUCCESS;
    }
Exit:
    WdfWaitLockRelease(deviceContext->StreamWaitLock);

    WdfRequestCompleteWithInformation(request, status, outDataCb);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
}

PAGED_CODE_SEG
_Use_decl_annotations_
VOID EvtUSBAudioAcxDriverReleaseAsioOwnership(
    WDFOBJECT  object,
    WDFREQUEST request
)
/*++

Routine Description:

    This routine releases ASIO ownership.

Return Value:

    VOID

--*/
{
    NTSTATUS               status = STATUS_NOT_SUPPORTED;
    ACX_REQUEST_PARAMETERS params{};
    ULONG_PTR              outDataCb = 0;
    // ACXSTREAM              stream = static_cast<ACXSTREAM>(object);
    // ASSERT(stream != nullptr);

    WDFDEVICE device = AcxCircuitGetWdfDevice((ACXCIRCUIT)object);
    ASSERT(device != nullptr);

    PDEVICE_CONTEXT deviceContext = GetDeviceContext(device);
    ASSERT(deviceContext != nullptr);

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    ACX_REQUEST_PARAMETERS_INIT(&params);
    AcxRequestGetParameters(request, &params);

    ASSERT(params.Type == AcxRequestTypeProperty);
    ASSERT(params.Parameters.Property.Verb == AcxPropertyVerbSet);
    ASSERT(params.Parameters.Property.Control == nullptr);
    ASSERT(params.Parameters.Property.ControlCb == 0);
    ASSERT(params.Parameters.Property.Value == nullptr);
    ASSERT(params.Parameters.Property.ValueCb == 0);

    WdfWaitLockAcquire(deviceContext->StreamWaitLock, nullptr);

    IF_TRUE_ACTION_JUMP(((params.Parameters.Property.Control != nullptr) ||
                         (params.Parameters.Property.ControlCb != 0) ||
                         (params.Parameters.Property.Value != nullptr) ||
                         (params.Parameters.Property.ValueCb != 0)),
                        ASSERT(FALSE);
                        outDataCb = 0; status = STATUS_INVALID_PARAMETER;,
                                                                         Exit);

    if (deviceContext->AsioOwner != nullptr)
    {
        if (deviceContext->AsioOwner == WdfRequestGetFileObject(request))
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "clear asio owner");
            deviceContext->AsioOwner = nullptr;
        }
    }
    status = STATUS_SUCCESS;

    if ((deviceContext->AudioProperty.SupportedSampleFormats & (1 << toULong(UACSampleFormat::UAC_SAMPLE_FORMAT_IEEE_FLOAT))) && (deviceContext->SampleFormatBackup != deviceContext->AudioProperty.CurrentSampleFormat))
    {
        ULONG         desiredFormatType = NS_USBAudio0200::FORMAT_TYPE_I;
        ULONG         desiredFormat = NS_USBAudio0200::PCM;
        ULONG         inputBytesPerSample = 0;
        ULONG         inputValidBitsPerSample = 0;
        ULONG         outputBytesPerSample = 0;
        ULONG         outputValidBitsPerSample = 0;
        ACXDATAFORMAT inputDataFormatBeforeChange = nullptr;
        ACXDATAFORMAT outputDataFormatBeforeChange = nullptr;
        ACXDATAFORMAT inputDataFormatAfterChange = nullptr;
        ACXDATAFORMAT outputDataFormatAfterChange = nullptr;

        status = USBAudioAcxDriverGetCurrentDataFormat(deviceContext, true, inputDataFormatBeforeChange);
        IF_FAILED_JUMP(status, Exit);

        status = USBAudioAcxDriverGetCurrentDataFormat(deviceContext, false, outputDataFormatBeforeChange);
        IF_FAILED_JUMP(status, Exit);

        status = USBAudioDataFormat::ConvertFormatToSampleFormat(deviceContext->SampleFormatBackup, desiredFormatType, desiredFormat);
        IF_FAILED_JUMP(status, Exit);

        status = deviceContext->UsbAudioConfiguration->GetMaxSupportedValidBitsPerSample(true, desiredFormatType, desiredFormat, inputBytesPerSample, inputValidBitsPerSample);
        IF_FAILED_JUMP(status, Exit);

        status = deviceContext->UsbAudioConfiguration->GetMaxSupportedValidBitsPerSample(false, desiredFormatType, desiredFormat, outputBytesPerSample, outputValidBitsPerSample);
        IF_FAILED_JUMP(status, Exit);

        status = ActivateAudioInterface(deviceContext, deviceContext->AudioProperty.SampleRate, desiredFormatType, desiredFormat, inputBytesPerSample, inputValidBitsPerSample, outputBytesPerSample, outputValidBitsPerSample);
        IF_FAILED_JUMP(status, Exit);

        status = USBAudioAcxDriverGetCurrentDataFormat(deviceContext, true, inputDataFormatAfterChange);
        IF_FAILED_JUMP(status, Exit);

        status = USBAudioAcxDriverGetCurrentDataFormat(deviceContext, false, outputDataFormatAfterChange);
        IF_FAILED_JUMP(status, Exit);

        status = NotifyAllPinsDataFormatChange(false, deviceContext, outputDataFormatBeforeChange, outputDataFormatAfterChange);
        IF_FAILED_JUMP(status, Exit);

        status = NotifyAllPinsDataFormatChange(true, deviceContext, inputDataFormatBeforeChange, inputDataFormatAfterChange);
        IF_FAILED_JUMP(status, Exit);
    }

Exit:
    WdfWaitLockRelease(deviceContext->StreamWaitLock);

    WdfRequestCompleteWithInformation(request, status, outDataCb);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
}

NONPAGED_CODE_SEG
_Use_decl_annotations_
VOID USBAudioAcxDriverEvtIsoRequestCompletionRoutine(
    WDFREQUEST /* request */,
    WDFIOTARGET /* target */,
    PWDF_REQUEST_COMPLETION_PARAMS completionParams,
    WDFCONTEXT                     context
)
/*++

Routine Description:

    Completion Routine

Arguments:

    context - Driver supplied context
    target - Target handle
    request - Request handle
    completionParams - request completion params


Return Value:

    VOID

--*/
{
    NTSTATUS                     status = STATUS_SUCCESS;
    USBD_STATUS                  usbdStatus = STATUS_SUCCESS;
    PISOCHRONOUS_REQUEST_CONTEXT requestContext = (PISOCHRONOUS_REQUEST_CONTEXT)context;
    PDEVICE_CONTEXT              deviceContext = requestContext->DeviceContext;
    StreamObject *               streamObject = requestContext->StreamObject;
    TransferObject *             transferObject = requestContext->TransferObject;
    ULONGLONG                    currentTimeUs = 0ULL;
    ULONGLONG                    qpcPosition = 0ULL;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry, %p", requestContext);

    ASSERT(deviceContext);
    ASSERT(transferObject);
    ASSERT(streamObject);

    currentTimeUs = USBAudioAcxDriverStreamGetCurrentTimeUs(deviceContext, &qpcPosition);

    status = completionParams->IoStatus.Status;
    if (!NT_SUCCESS(status) && (status != STATUS_CANCELLED))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "frame %u : completion failed with status %08x", transferObject->GetStartFrame(), status);
    }

    usbdStatus = transferObject->GetUSBDStatus();
    if (!USBD_SUCCESS(usbdStatus) && (usbdStatus != USBD_STATUS_CANCELED))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "frame %u : urb failed with status %08x", transferObject->GetStartFrame(), usbdStatus);
        deviceContext->ErrorStatistics->LogErrorOccurrence(ErrorStatus::UrbFailed, usbdStatus);
#if false
		// TBD Add a recovery process
#else
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "irp at index %d failed (%!STATUS!), but will be reused.", transferObject->GetIndex(), status);
        usbdStatus = USBD_STATUS_SUCCESS;
        status = STATUS_SUCCESS;
#endif
    }

    if (requestContext->TransferObject != nullptr)
    {
        ULONGLONG periodUs = 0ULL;
        ULONGLONG periodQPC = 0ULL;
        requestContext->StreamObject->CompleteRequest(transferObject->GetDirection(), currentTimeUs, qpcPosition, periodUs, periodQPC);
        transferObject->CompleteRequest(currentTimeUs, qpcPosition, periodUs, periodQPC);
    }

    if (NT_SUCCESS(status) && USBD_SUCCESS(usbdStatus) && (deviceContext->StartCounterIsoStream != 0))
    {
        //		WdfWaitLockAcquire(deviceContext->StreamWaitLock, nullptr);

        switch (transferObject->GetDirection())
        {
        case IsoDirection::In: {
            status = ProcessTransferIn(deviceContext, streamObject, transferObject);
            if (!NT_SUCCESS(status))
            {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "ProcessTransferIn failed %!STATUS!", status);

                goto USBAudioAcxDriverEvtIsoRequestCompletionRoutine_Exit;
            }
            // Since the URB is referenced in ProcessTransferIn, the parent request is released here.
            status = transferObject->FreeRequest();
            if (!NT_SUCCESS(status))
            {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "FreeRequest failed %!STATUS!", status);

                goto USBAudioAcxDriverEvtIsoRequestCompletionRoutine_Exit;
            }
            status = InitializeIsoUrbIn(deviceContext, streamObject, transferObject, transferObject->GetNumPackets());
            if (!NT_SUCCESS(status))
            {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "InitializeIsoUrbIn failed %!STATUS!", status);

                goto USBAudioAcxDriverEvtIsoRequestCompletionRoutine_Exit;
            }
        }
        break;
        case IsoDirection::Out: {
            status = ProcessTransferOut(deviceContext, streamObject, transferObject);
            if (!NT_SUCCESS(status))
            {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "ProcessTransferOut failed %!STATUS!", status);

                goto USBAudioAcxDriverEvtIsoRequestCompletionRoutine_Exit;
            }

            streamObject->SetOutputStreaming(transferObject->GetIndex(), transferObject->GetLockDelayCount());

            // Since the URB is referenced in ProcessTransferOut, the parent request is released here.
            status = transferObject->FreeRequest();
            if (!NT_SUCCESS(status))
            {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "FreeRequest failed %!STATUS!", status);

                goto USBAudioAcxDriverEvtIsoRequestCompletionRoutine_Exit;
            }
            status = InitializeIsoUrbOut(deviceContext, streamObject, transferObject, transferObject->GetNumPackets());
            if (!NT_SUCCESS(status))
            {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "InitializeIsoUrbOut failed %!STATUS!", status);

                goto USBAudioAcxDriverEvtIsoRequestCompletionRoutine_Exit;
            }
        }
        break;
        case IsoDirection::Feedback: {
            status = ProcessTransferFeedback(deviceContext, streamObject, transferObject);
            if (!NT_SUCCESS(status))
            {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "ProcessTransferFeedback failed %!STATUS!", status);

                goto USBAudioAcxDriverEvtIsoRequestCompletionRoutine_Exit;
            }
            // Since the URB is referenced in ProcessTransferFeedback, the parent request is released here.
            status = transferObject->FreeRequest();
            if (!NT_SUCCESS(status))
            {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "FreeRequest failed %!STATUS!", status);

                goto USBAudioAcxDriverEvtIsoRequestCompletionRoutine_Exit;
            }

            status = InitializeIsoUrbFeedback(deviceContext, streamObject, transferObject, transferObject->GetNumPackets());
            if (!NT_SUCCESS(status))
            {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "InitializeIsoUrbFeedback failed %!STATUS!", status);

                goto USBAudioAcxDriverEvtIsoRequestCompletionRoutine_Exit;
            }
        }
        break;
        default:
            break;
        }

        status = transferObject->SendIsochronousRequest(transferObject->GetDirection(), USBAudioAcxDriverEvtIsoRequestCompletionRoutine);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "SendIsochronousRequest failed %!STATUS!", status);

            goto USBAudioAcxDriverEvtIsoRequestCompletionRoutine_Exit;
        }

        //		WdfWaitLockRelease(deviceContext->StreamWaitLock);
    }

USBAudioAcxDriverEvtIsoRequestCompletionRoutine_Exit:

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return;
}

PAGED_CODE_SEG
static _Use_decl_annotations_
NTSTATUS StartIsoStream(
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    auto startIsoStreamScope = wil::scope_exit([&]() {
        if (!NT_SUCCESS(status) && (deviceContext->StreamObject != nullptr) && (status != STATUS_DEVICE_BUSY))
        {
            delete deviceContext->StreamObject;
            deviceContext->StreamObject = nullptr;
        }
        else
        {
            InterlockedIncrement(&deviceContext->StartCounterIsoStream);
            if (deviceContext->AsioBufferObject != nullptr)
            {
                deviceContext->AsioBufferObject->SetReady();
            }
        }

        if (deviceContext->StreamObject != nullptr)
        {
            NTSTATUS statusTemp = WdfDeviceStopIdle(deviceContext->Device, TRUE);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "WdfDeviceStopIdle %!STATUS!", statusTemp);
        }
    });

    RETURN_NTSTATUS_IF_TRUE_ACTION(deviceContext->StreamObject != nullptr, status = STATUS_DEVICE_BUSY, status);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_MULTICLIENT, " - start counter asio %ld, start counter acx audio %ld, start counter iso stream %ld", deviceContext->StartCounterAsio, deviceContext->StartCounterWdmAudio, deviceContext->StartCounterIsoStream);
    status = SetPipeInformation(deviceContext);
    RETURN_NTSTATUS_IF_FAILED_MSG(status, "SetPipeInformation failed");

    DEVICE_CONTEXT::SelectedInterfaceAndPipe * interfaceAndPipe[] = {
        &deviceContext->InputInterfaceAndPipe,
        &deviceContext->OutputInterfaceAndPipe,
        &deviceContext->FeedbackInterfaceAndPipe,
    };

    for (ULONG index = 0; index < (sizeof(interfaceAndPipe) / sizeof(interfaceAndPipe[0])); index++)
    {
        if (interfaceAndPipe[index]->MaximumTransferSize != 0)
        {
            if (deviceContext->IsDeviceSuperSpeed && deviceContext->SuperSpeedCompatible)
            {
                status = InitializePipeContextForSuperSpeedDevice(deviceContext, interfaceAndPipe[index]->UsbInterface, interfaceAndPipe[index]->SelectedAlternateSetting, interfaceAndPipe[index]->Pipe);
            }
            else if (deviceContext->IsDeviceHighSpeed)
            {
                status = InitializePipeContextForHighSpeedDevice(interfaceAndPipe[index]->Pipe);
            }
            else
            {
                status = InitializePipeContextForFullSpeedDevice(interfaceAndPipe[index]->Pipe);
            }
            if (!NT_SUCCESS(status))
            {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "InitializePipeContext failed ");
                break;
            }
            if ((WdfUsbPipeTypeIsochronous != interfaceAndPipe[index]->PipeInfo.PipeType))
            {
                status = STATUS_INVALID_DEVICE_REQUEST;
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "Pipe type is not Isochronous");
                break;
            }

            if (interfaceAndPipe[index] == &deviceContext->InputInterfaceAndPipe)
            {
                if (WdfUsbTargetPipeIsInEndpoint(interfaceAndPipe[index]->Pipe) == FALSE)
                {
                    status = STATUS_INVALID_PARAMETER;
                    TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "Invalid pipe - not an input pipe");
                    break;
                }
            }
            else if (interfaceAndPipe[index] == &deviceContext->OutputInterfaceAndPipe)
            {
                if (WdfUsbTargetPipeIsOutEndpoint(interfaceAndPipe[index]->Pipe) == FALSE)
                {
                    status = STATUS_INVALID_PARAMETER;
                    TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "Invalid pipe - not an output pipe");
                    break;
                }
            }
        }
    }
    RETURN_NTSTATUS_IF_FAILED(status);

    deviceContext->StreamObject = StreamObject::Create(deviceContext);
    RETURN_NTSTATUS_IF_TRUE_ACTION(deviceContext->StreamObject == nullptr, status = STATUS_INSUFFICIENT_RESOURCES, status);

    deviceContext->StreamObject->ResetNextMeasureFrames(deviceContext->AudioProperty.PacketsPerSec);

    // Before measurement, initialize with the nominal sample rate.
    deviceContext->AudioProperty.InputMeasuredSampleRate = deviceContext->AudioProperty.SampleRate;
    deviceContext->AudioProperty.OutputMeasuredSampleRate = deviceContext->AudioProperty.SampleRate;

    status = deviceContext->StreamObject->CreateMixingEngineThread(HIGH_PRIORITY, 1000);
    RETURN_NTSTATUS_IF_FAILED(status);

    if (deviceContext->RtPacketObject != nullptr)
    {
        deviceContext->RtPacketObject->Reset(TRUE);
        deviceContext->RtPacketObject->Reset(FALSE);
    }

    deviceContext->StreamObject->SetStartIsoFrame(GetCurrentFrame(deviceContext), deviceContext->Params.OutputFrameDelay);
    deviceContext->StreamObject->SetIsoFrameDelay(deviceContext->Params.FirstPacketLatency);
    deviceContext->StreamObject->ResetIsoRequestCompletionTime();
    deviceContext->StreamObject->SaveStartPCUs();

    for (ULONG i = 0; i < deviceContext->Params.MaxIrpNumber; i++)
    {
        if (deviceContext->FeedbackInterfaceAndPipe.Pipe != nullptr)
        {
            status = StartTransfer(deviceContext, deviceContext->StreamObject, i, IsoDirection::Feedback);
            RETURN_NTSTATUS_IF_FAILED(status);
        }
        if (deviceContext->InputInterfaceAndPipe.Pipe != nullptr)
        {
            status = StartTransfer(deviceContext, deviceContext->StreamObject, i, IsoDirection::In);
            RETURN_NTSTATUS_IF_FAILED(status);
        }
        status = StartTransfer(deviceContext, deviceContext->StreamObject, i, IsoDirection::Out);
        RETURN_NTSTATUS_IF_FAILED(status);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
    return status;
}

#if false
PAGED_CODE_SEG
static _Use_decl_annotations_
NTSTATUS FreeTransferObject(
    TransferObject * /* transferObject */
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
    return status;
}
#endif

PAGED_CODE_SEG
static _Use_decl_annotations_
NTSTATUS StartTransfer(
    PDEVICE_CONTEXT deviceContext,
    StreamObject *  streamObject,
    ULONG           index,
    IsoDirection    direction
)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG    maxXferSize = 0;
    ULONG    isoPacketSize = 0;
    ULONG    numIsoPackets = 0;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    IF_TRUE_ACTION_JUMP(streamObject == nullptr, status = STATUS_INVALID_PARAMETER, StartTransfer_Exit);
    IF_TRUE_ACTION_JUMP(deviceContext->ContiguousMemory == nullptr, status = STATUS_INVALID_PARAMETER, StartTransfer_Exit);
    IF_TRUE_ACTION_JUMP(!deviceContext->ContiguousMemory->IsValid(index, direction), status = STATUS_INVALID_PARAMETER, StartTransfer_Exit);

    switch (direction)
    {
    case IsoDirection::In:
        maxXferSize = deviceContext->InputInterfaceAndPipe.MaximumTransferSize;
        isoPacketSize = deviceContext->InputInterfaceAndPipe.PipeInfo.MaximumPacketSize * deviceContext->SupportedControl.MaxBurstOverride;
        numIsoPackets = deviceContext->ClassicFramesPerIrp * deviceContext->FramesPerMs;
        if (numIsoPackets > 128)
        { // Ensure the number of packets is within the WDK limit.
            numIsoPackets = 128;
            maxXferSize = isoPacketSize * numIsoPackets;
        }
        break;
    case IsoDirection::Out:
        maxXferSize = deviceContext->OutputInterfaceAndPipe.MaximumTransferSize;
        // isoPacketSize is not used.
        isoPacketSize = deviceContext->OutputInterfaceAndPipe.PipeInfo.MaximumPacketSize * deviceContext->SupportedControl.MaxBurstOverride;
        numIsoPackets = deviceContext->ClassicFramesPerIrp * deviceContext->FramesPerMs;
        break;
    case IsoDirection::Feedback:
        maxXferSize = deviceContext->FeedbackInterfaceAndPipe.MaximumTransferSize;
        isoPacketSize = deviceContext->FeedbackInterfaceAndPipe.PipeInfo.MaximumPacketSize;
        numIsoPackets = deviceContext->ClassicFramesPerIrp * deviceContext->FramesPerMs;
        numIsoPackets >>= (deviceContext->FeedbackProperty.FeedbackInterval - 1);
        break;
    default:
        ASSERT(false);
        break;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "num packets = %u, Classic frames per irp = %u, frames per ms = %u", numIsoPackets, deviceContext->ClassicFramesPerIrp, deviceContext->FramesPerMs);

    TransferObject * transferObject = streamObject->GetTransferObject(index, direction);
    if (transferObject == nullptr)
    {
        transferObject = TransferObject::Create(deviceContext, streamObject, index, direction);
        IF_TRUE_ACTION_JUMP(transferObject == nullptr, status = STATUS_INSUFFICIENT_RESOURCES, StartTransfer_Exit);

        transferObject->AttachDataBuffer(deviceContext->ContiguousMemory->GetDataBuffer(index, direction), numIsoPackets, isoPacketSize, maxXferSize);

        streamObject->SetTransferObject(index, direction, transferObject);
    }

    transferObject->Reset();

    ULONG lockDelayCount = 0;
    if (!deviceContext->SupportedControl.SkipInitialSamples)
    {
        lockDelayCount = 0;
    }
    else
    {
        switch (direction)
        {
        case IsoDirection::In:
            if (deviceContext->InputLockDelay != 0)
            {
                lockDelayCount = (deviceContext->InputLockDelay + deviceContext->Params.MaxIrpNumber - 1) / deviceContext->Params.MaxIrpNumber;
            }
            else
            {
                lockDelayCount = UAC_DEFAULT_LOCK_DELAY;
            }
            break;
        case IsoDirection::Out:
        case IsoDirection::Feedback:
            if (deviceContext->OutputLockDelay != 0)
            {
                lockDelayCount = (deviceContext->OutputLockDelay + deviceContext->Params.MaxIrpNumber - 1) / deviceContext->Params.MaxIrpNumber;
            }
            else
            {
                lockDelayCount = UAC_DEFAULT_LOCK_DELAY;
            }
            break;
            break;
        default:
            break;
        }
    }
    transferObject->SetLockDelayCount(lockDelayCount);

    switch (direction)
    {
    case IsoDirection::In:
        if (index == 0)
        {
            deviceContext->RtPacketObject->SetIsoPacketInfo(direction, isoPacketSize, numIsoPackets);
        }
        status = InitializeIsoUrbIn(deviceContext, streamObject, transferObject, numIsoPackets);
        RETURN_NTSTATUS_IF_FAILED_MSG(status, "InitializeIsoUrbIn failed");
        break;
    case IsoDirection::Out:
        if (index == 0)
        {
            deviceContext->RtPacketObject->SetIsoPacketInfo(direction, isoPacketSize, numIsoPackets);
        }
        status = InitializeIsoUrbOut(deviceContext, streamObject, transferObject, numIsoPackets);
        RETURN_NTSTATUS_IF_FAILED_MSG(status, "InitializeIsoUrbOut failed");

        //
        // Advance half a screen as the initial transfer position. If
        // playback starts late, reconsider this position.
        //
        deviceContext->RtPacketObject->FeedOutputWriteBytes(numIsoPackets * isoPacketSize / 2);
        break;
    case IsoDirection::Feedback:
        status = InitializeIsoUrbFeedback(deviceContext, streamObject, transferObject, numIsoPackets);
        RETURN_NTSTATUS_IF_FAILED_MSG(status, "InitializeIsoUrbFeedback failed");
        break;
    default:
        break;
    }

    status = transferObject->SendIsochronousRequest(direction, USBAudioAcxDriverEvtIsoRequestCompletionRoutine);
    RETURN_NTSTATUS_IF_FAILED_MSG(status, "SendIsochronousRequest failed");

StartTransfer_Exit:

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

NONPAGED_CODE_SEG
static _Use_decl_annotations_
NTSTATUS InitializeIsoUrbIn(
    PDEVICE_CONTEXT  deviceContext,
    StreamObject *   streamObject,
    TransferObject * transferObject,
    ULONG            numPackets
)
{
    NTSTATUS status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    ULONG startFrame = streamObject->GetStartFrame(IsoDirection::In, numPackets);

    bool asap = false;

    if (streamObject->IsIoSteady())
    {
        asap = TRUE;
    }
    status = transferObject->SetUrbIsochronousParametersInput(startFrame, deviceContext->InputInterfaceAndPipe.Pipe, asap, USBAudioAcxDriverEvtIsoRequestContextCleanup);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

NONPAGED_CODE_SEG
static _Use_decl_annotations_
NTSTATUS InitializeIsoUrbOut(
    PDEVICE_CONTEXT  deviceContext,
    StreamObject *   streamObject,
    TransferObject * transferObject,
    ULONG            numPackets
)
{
    NTSTATUS status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    ULONG startFrame = streamObject->GetStartFrame(IsoDirection::Out, numPackets);

    bool asap = false;

    if (streamObject->IsIoSteady())
    {
        asap = true;
    }

    status = transferObject->SetUrbIsochronousParametersOutput(startFrame, deviceContext->OutputInterfaceAndPipe.Pipe, asap, USBAudioAcxDriverEvtIsoRequestContextCleanup);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

NONPAGED_CODE_SEG
static _Use_decl_annotations_
NTSTATUS InitializeIsoUrbFeedback(
    PDEVICE_CONTEXT  deviceContext,
    StreamObject *   streamObject,
    TransferObject * transferObject,
    ULONG            numPackets
)
{
    NTSTATUS status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    ULONG startFrame = streamObject->GetStartFrame(IsoDirection::Feedback, numPackets);

    bool asap = false;

    if (streamObject->IsIoSteady())
    {
        asap = true;
    }

    status = transferObject->SetUrbIsochronousParametersFeedback(startFrame, deviceContext->FeedbackInterfaceAndPipe.Pipe, asap, USBAudioAcxDriverEvtIsoRequestContextCleanup);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

NONPAGED_CODE_SEG
static _Use_decl_annotations_
NTSTATUS ProcessTransferIn(
    PDEVICE_CONTEXT  deviceContext,
    StreamObject *   streamObject,
    TransferObject * transferObject
)
{
    NTSTATUS status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    USBD_STATUS usbdStatus = transferObject->GetUSBDStatus();
    if (!USBD_SUCCESS(usbdStatus))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "in frame %u : urb failed with status %08x", transferObject->GetStartFrame(), usbdStatus);
    }

    ULONG transferredBytesInThisIrp = 0;
    ULONG invalidPacket = 0;
    status = transferObject->UpdateTransferredBytesInThisIrp(transferredBytesInThisIrp, &invalidPacket);
    ULONG transferredSamplesInThisIrp = transferredBytesInThisIrp / deviceContext->AudioProperty.InputBytesPerBlock;
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "in frame %u : transfer bytes in this irp = %d", transferObject->GetStartFrame(), transferredBytesInThisIrp);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "Input irp at index %d failed (%!STATUS!), but will be reused.", transferObject->GetIndex(), status);
        status = STATUS_SUCCESS;
    }

    if (NT_SUCCESS(status))
    {
        // Update the number of completed packets recorded in the streamObject
        streamObject->UpdateCompletedPacket(TRUE, transferObject->GetIndex(), transferObject->GetNumberOfPacketsInThisIrp());

        transferObject->RecordIsoPacketLength();
    }

    bool isLockDelay = transferObject->DecrementLockDelayCount();

    // transferObject->DumpUrbPacket("ProcessTransferIn");

    if (isLockDelay)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "LOCK DELAY : input %u samples", transferredSamplesInThisIrp);
    }

    // Determine if the input is stable
    if (streamObject->CheckInputStability(transferObject->GetIndex(), transferObject->GetNumberOfPacketsInThisIrp(), transferObject->GetStartFrameInThisIrp(), transferredBytesInThisIrp, invalidPacket))
    {
        streamObject->SetInputStreaming();
    }

    transferObject->UpdatePositionsIn(transferredSamplesInThisIrp);

    transferObject->CompensateNonFeedbackOutput(transferredSamplesInThisIrp);

    transferObject->FreeUrb();

    if (NT_SUCCESS(status))
    {
        streamObject->WakeupMixingEngineThread();
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

NONPAGED_CODE_SEG
static _Use_decl_annotations_
NTSTATUS ProcessTransferOut(
    PDEVICE_CONTEXT /* deviceContext */,
    StreamObject *   streamObject,
    TransferObject * transferObject
)
{
    NTSTATUS status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    USBD_STATUS usbdStatus = transferObject->GetUSBDStatus();
    if (!USBD_SUCCESS(usbdStatus))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "out frame %u : urb failed with status %08x", transferObject->GetStartFrame(), usbdStatus);
    }

    ULONG transferredBytesInThisIrp = 0;

    status = transferObject->UpdateTransferredBytesInThisIrp(transferredBytesInThisIrp, nullptr);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "out frame %u : transfer bytes in this irp = %d", transferObject->GetStartFrame(), transferredBytesInThisIrp);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "Output irp at index %d failed (%!STATUS!), but will be reused.", transferObject->GetIndex(), status);
        status = STATUS_SUCCESS;
    }

    if (NT_SUCCESS(status))
    {
        if (transferObject->GetLockDelayCount() == 0)
        {
            // Determine whether the input is stable. Update the
            // number of completed packets recorded in the
            // streamObject.
            streamObject->UpdateCompletedPacket(FALSE, transferObject->GetIndex(), transferObject->GetNumberOfPacketsInThisIrp());
        }
        streamObject->SetOutputStable();
    }

    // transferObject->DumpUrbPacket("ProcessTransferOut");

    transferObject->FreeUrb();

    if (NT_SUCCESS(status))
    {
        streamObject->WakeupMixingEngineThread();
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

NONPAGED_CODE_SEG
static _Use_decl_annotations_
NTSTATUS ProcessTransferFeedback(
    PDEVICE_CONTEXT /* deviceContext */,
    StreamObject *   streamObject,
    TransferObject * transferObject
)
{
    NTSTATUS status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    USBD_STATUS usbdStatus = transferObject->GetUSBDStatus();
    if (!USBD_SUCCESS(usbdStatus))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "feedback frame %u : urb failed with status %08x", transferObject->GetStartFrame(), usbdStatus);
    }

    ULONG transferredBytesInThisIrp = 0;
    ULONG feedbackSum = 0;
    ULONG validFeedback = 0;

    status = transferObject->UpdateTransferredBytesInThisIrp(transferredBytesInThisIrp, nullptr);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "Feedback irp at index %d failed (%!STATUS!), but will be reused.", transferObject->GetIndex(), status);
        status = STATUS_SUCCESS;
    }

    if (NT_SUCCESS(status))
    {
        feedbackSum = transferObject->GetFeedbackSum(validFeedback);

        ULONG lastFeedbackSize = streamObject->UpdatePositionsFeedback(feedbackSum, validFeedback);

        transferObject->DecrementLockDelayCount();

        transferObject->CompensateNonFeedbackOutput(lastFeedbackSize);
    }

    transferObject->FreeUrb();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

PAGED_CODE_SEG
static _Use_decl_annotations_
NTSTATUS StopIsoStream(
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    InterlockedExchange(&deviceContext->StartCounterIsoStream, 0);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_MULTICLIENT, " - start counter asio %ld, start counter acx audio %ld, start counter iso stream %ld", deviceContext->StartCounterAsio, deviceContext->StartCounterWdmAudio, deviceContext->StartCounterIsoStream);
    // cancel irp
    if (deviceContext->StreamObject != nullptr)
    {
        status = deviceContext->StreamObject->CancelRequestAll();

        AbortPipes(IsoDirection::In, deviceContext->Device);
        AbortPipes(IsoDirection::Feedback, deviceContext->Device);

        deviceContext->StreamObject->TerminateMixingEngineThread();
        deviceContext->StreamObject->Cleanup();
        delete deviceContext->StreamObject;
        deviceContext->StreamObject = nullptr;

        SelectAlternateInterface(IsoDirection::Out, deviceContext, deviceContext->AudioProperty.OutputInterfaceNumber, 0);

        SelectAlternateInterface(IsoDirection::In, deviceContext, deviceContext->AudioProperty.InputInterfaceNumber, 0);

        WdfDeviceResumeIdle(deviceContext->Device);
    }

    if (deviceContext->ErrorStatistics != nullptr)
    {
        deviceContext->ErrorStatistics->Report();
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

PAGED_CODE_SEG
static _Use_decl_annotations_
NTSTATUS NotifyDataFormatChange(
    WDFDEVICE     device,
    ACXCIRCUIT    circuit,
    ACXPIN        pin,
    ACXDATAFORMAT originalDataFormat
)
{
    NTSTATUS      status = STATUS_SUCCESS;
    ACXDATAFORMAT desiredDataFormat = nullptr;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    CODEC_PIN_CONTEXT * pinContext = GetCodecPinContext(pin);
    ASSERT(pinContext != nullptr);

    status = SplitAcxDataFormatByDeviceChannels(device, circuit, pinContext->NumOfChannelsPerDevice, desiredDataFormat, originalDataFormat);
    RETURN_NTSTATUS_IF_FAILED(status);

    ACXDATAFORMATLIST dataFormatList = AcxPinGetRawDataFormatList(pin);
    status = AcxDataFormatListAssignDefaultDataFormat(dataFormatList, desiredDataFormat);
    RETURN_NTSTATUS_IF_FAILED(status);

    status = AcxPinNotifyDataFormatChange(pin);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

PAGED_CODE_SEG
static _Use_decl_annotations_
NTSTATUS NotifyAllPinsDataFormatChange(
    bool            isInput,
    PDEVICE_CONTEXT deviceContext,
    ACXDATAFORMAT   dataFormatBeforeChange,
    ACXDATAFORMAT   dataFormatAfterChange
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    if (isInput)
    {
        if ((deviceContext->Capture != nullptr) && (dataFormatBeforeChange != nullptr) && (dataFormatAfterChange != nullptr) && !AcxDataFormatIsEqual(dataFormatBeforeChange, dataFormatAfterChange))
        {
            for (ULONG captureDeviceIndex = 0; captureDeviceIndex < deviceContext->NumOfInputDevices; captureDeviceIndex++)
            {
                ACXPIN pin = AcxCircuitGetPinById(deviceContext->Capture, captureDeviceIndex * CodecCapturePinCount + CodecCaptureHostPin);
                if (pin != nullptr)
                {
                    status = NotifyDataFormatChange(deviceContext->Device, deviceContext->Capture, pin, dataFormatAfterChange);
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - capture pin %u, AcxPinNotifyDataFormatChange %!STATUS!", captureDeviceIndex * 2, status);
                    IF_FAILED_JUMP(status, Exit);
                }
            }
        }
    }
    else
    {
        if ((deviceContext->Render != nullptr) && (dataFormatBeforeChange != nullptr) && (dataFormatAfterChange != nullptr) && !AcxDataFormatIsEqual(dataFormatBeforeChange, dataFormatAfterChange))
        {
            for (ULONG renderDeviceIndex = 0; renderDeviceIndex < deviceContext->NumOfOutputDevices; renderDeviceIndex++)
            {
                ACXPIN pin = AcxCircuitGetPinById(deviceContext->Render, renderDeviceIndex * CodecRenderPinCount + CodecRenderHostPin);
                if (pin != nullptr)
                {
                    status = NotifyDataFormatChange(deviceContext->Device, deviceContext->Render, pin, dataFormatAfterChange);
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - render pin %u, PinNotifyDataFormatChange %!STATUS!", renderDeviceIndex * 2, status);
                    IF_FAILED_JUMP(status, Exit);
                }
            }
        }
    }
Exit:

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
void USBAudioAcxDriverEvtFileCleanup(
    WDFOBJECT fileObject
)
/*++

Routine Description:

    Cleanup process for the File Object controlled by the ASIO Driver.

    When a host application using the ASIO Driver crashes, ASIO-related objects are destroyed and initialized.

Arguments:

    _In_ WDFOBJECT   FileObject

Return Value:

    void

--*/
{
    NTSTATUS        status = STATUS_UNSUCCESSFUL;
    PUNICODE_STRING fileName;
    PFILE_CONTEXT   fileContext;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    fileContext = GetFileContext(fileObject);
    fileName = WdfFileObjectGetFileName((WDFFILEOBJECT)fileObject);

    if (0 == fileName->Length)
    {
        status = STATUS_SUCCESS;
    }
    else
    {
        ANSI_STRING ansiString;
        RtlZeroMemory(&ansiString, sizeof(ANSI_STRING));
        status = RtlUnicodeStringToAnsiString(&ansiString, fileName, TRUE);
        if (NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - file name = %s", ansiString.Buffer);
            RtlFreeAnsiString(&ansiString);
        }
        status = STATUS_SUCCESS;
    }

    if ((fileContext != nullptr) && (fileContext->DeviceContext != nullptr) && (WdfFileObjectWdmGetFileObject((WDFFILEOBJECT)fileObject) != nullptr))
    {
        PDEVICE_CONTEXT deviceContext = fileContext->DeviceContext;

        WdfWaitLockAcquire(deviceContext->StreamWaitLock, nullptr);
        if ((WDFFILEOBJECT)fileObject == deviceContext->AsioOwner)
        {
            StopIsoStream(deviceContext);

            if (deviceContext->AsioBufferObject != nullptr)
            {
                status = deviceContext->AsioBufferObject->UnsetBuffer();
                delete deviceContext->AsioBufferObject;
                deviceContext->AsioBufferObject = nullptr;
            }
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "clear asio owner");
            deviceContext->AsioOwner = nullptr;
        }
        WdfWaitLockRelease(deviceContext->StreamWaitLock);
    }

    // WdfRequestComplete(request, status);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
}

PAGED_CODE_SEG
static _Use_decl_annotations_
void ReportInternalParameters(
    PDEVICE_CONTEXT deviceContext
)
{
    PAGED_CODE();

    UAC_AUDIO_PROPERTY & audioProp = deviceContext->AudioProperty;

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - Vendor ID:%04x, Product ID:%04x, DeviceRelease:%04x", audioProp.VendorId, audioProp.ProductId, audioProp.DeviceRelease);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - ProductName                  %ws", audioProp.ProductName);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - PacketsPerSec                %d", audioProp.PacketsPerSec);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - SampleRate                   %d", audioProp.SampleRate);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - SamplesPerPacket             %d", audioProp.SamplesPerPacket);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - SupportedSampleRate        0x%x", audioProp.SupportedSampleRate);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - SampleType                   %d", toInt(audioProp.SampleType));
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - InputInterfaceNumber         %d", audioProp.InputInterfaceNumber);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - InputAlternateSetting        %d", audioProp.InputAlternateSetting);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - InputEndpointNumber        0x%x", audioProp.InputEndpointNumber);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - OutputInterfaceNumber        %d", audioProp.OutputInterfaceNumber);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - OutputAlternateSetting       %d", audioProp.OutputAlternateSetting);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - OutputEndpointNumber       0x%x", audioProp.OutputEndpointNumber);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - InputBytesPerBlock           %d", audioProp.InputBytesPerBlock);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - InputMaxSamplesPerPacket     %d", audioProp.InputMaxSamplesPerPacket);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - InputLatencyOffset           %d", audioProp.InputLatencyOffset);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - InputFormatType              %d", audioProp.InputFormatType);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - InputFormat                  %d", audioProp.InputFormat);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - InputBytesPerSample          %d", audioProp.InputBytesPerSample);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - InputValidBitsPerSample      %d", audioProp.InputValidBitsPerSample);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - OutputBytesPerBlock          %d", audioProp.OutputBytesPerBlock);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - OutputMaxSamplesPerPacket    %d", audioProp.OutputMaxSamplesPerPacket);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - OutputLatencyOffset          %d", audioProp.OutputLatencyOffset);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - OutputFormatType             %d", audioProp.OutputFormatType);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - OutputFormat                 %d", audioProp.OutputFormat);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - OutputBytesPerSample         %d", audioProp.OutputBytesPerSample);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - OutputValidBitsPerSample     %d", audioProp.OutputValidBitsPerSample);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - AudioControlInterfaceNumber  %d", audioProp.AudioControlInterfaceNumber);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - InputMeasuredSampleRate      %d", audioProp.InputMeasuredSampleRate);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - OutputMeasuredSampleRate     %d", audioProp.OutputMeasuredSampleRate);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - ClockSources                 %d", audioProp.ClockSources);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - InputDriverBuffer            %d", audioProp.InputDriverBuffer);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - OutputDriverBuffer           %d", audioProp.OutputDriverBuffer);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - SupportedSampleFormat        %u", audioProp.SupportedSampleFormats);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - CurrentSampleFormat          %u", toULong(audioProp.CurrentSampleFormat));
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - InputUsbChannels			    %d", deviceContext->InputUsbChannels);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - OutputUsbChannels            %d", deviceContext->OutputUsbChannels);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - FeedbackInterfaceNumber      %d", deviceContext->FeedbackProperty.FeedbackInterfaceNumber);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - FeedbackAlternateSetting     %d", deviceContext->FeedbackProperty.FeedbackAlternateSetting);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - FeedbackEndpointNumber     0x%x", deviceContext->FeedbackProperty.FeedbackEndpointNumber);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - FeedbackInterval             %d", deviceContext->FeedbackProperty.FeedbackInterval);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - IsDeviceHighSpeed            %!bool!", deviceContext->IsDeviceHighSpeed);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - IsDeviceSuperSpeed           %!bool!", deviceContext->IsDeviceSuperSpeed);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - NumberOfConfiguredInterfaces %d", deviceContext->NumberOfConfiguredInterfaces);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - DeviceName                   %ws", deviceContext->DeviceName);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - SerialNumber                 %ws", deviceContext->SerialNumber);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - FramesPerMs                  %d", deviceContext->FramesPerMs);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - ClassicFramesPerIrp          %d", deviceContext->ClassicFramesPerIrp);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - IsDeviceAdaptive             %!bool!", deviceContext->IsDeviceAdaptive);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - IsDeviceSynchronous          %!bool!", deviceContext->IsDeviceSynchronous);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - DeviceClass                  %d", deviceContext->DeviceClass);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - DeviceProtocol               %d", deviceContext->DeviceProtocol);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - InputUsbChannels             %d", deviceContext->InputUsbChannels);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - OutputUsbChannels            %d", deviceContext->OutputUsbChannels);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - InputChannelNames            %d", deviceContext->InputChannelNames);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - OutputChannelNames           %d", deviceContext->OutputChannelNames);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - StartCounterAsio             %d", deviceContext->StartCounterAsio);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - StartCounterWdmAudio         %d", deviceContext->StartCounterWdmAudio);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - StartCounterIsoStream        %d", deviceContext->StartCounterIsoStream);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - LastActivationStatus         %!STATUS!", deviceContext->LastActivationStatus);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - InputIsoPacketSize           %d", deviceContext->InputIsoPacketSize);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - OutputIsoPacketSize          %d", deviceContext->OutputIsoPacketSize);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - InputLockDelay               %d", deviceContext->InputLockDelay);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - OutputLockDelay              %d", deviceContext->OutputLockDelay);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - SuperSpeedCompatible         %d", deviceContext->SuperSpeedCompatible);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - DesiredSampleFormat          %u", toULong(deviceContext->DesiredSampleFormat));
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - ClockSelectorId              %d", deviceContext->ClockSelectorId);
}
