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
#include "AudioIsochronousEngine.h"
#include "AsioBufferObject.h"
#include "StreamEngine.h"
#include "ErrorStatistics.h"
#include "CircuitHelper.h"
#include "InterruptDataMessage.h"

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
SelectConfiguration(
    _In_ PDEVICE_CONTEXT deviceContext
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
static NTSTATUS SetInterruptPipeInformation(
    _In_ PDEVICE_CONTEXT deviceContext
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

PAGED_CODE_SEG
_Use_decl_annotations_
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
    pnpPowerCallbacks.EvtDeviceSurpriseRemoval = USBAudioAcxDriverEvtDeviceSurpriseRemoval;
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

    deviceContext->ExcludeD3Cold = WdfFalse;
    deviceContext->IsDeviceRemoteWakeable = false;
    deviceContext->IsDeviceHighSpeed = false;
    deviceContext->IsDeviceSuperSpeed = false;
    deviceContext->IsIdleStopSucceeded = FALSE;

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

    deviceContext->IsPrepareHardwareSucceeded = false;

    status = ReadAndSelectDescriptors(device);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "ReadAndSelectDescriptors failed %!STATUS!", status);
        return status;
    }

    deviceContext->SupportedControl = g_SupportedControlList[0];
    for (int i = 1; i < ARRAYSIZE(g_SupportedControlList); ++i)
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

    if (deviceContext->VendorId == 0)
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
        RETURN_NTSTATUS_IF_FAILED(status);

        deviceContext->NumberOfAudioIsochronousEngines = deviceContext->UsbAudioConfiguration->GetNumOfStreamInterfaceGroup();

        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = device;

        RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(AudioIsochronousEngine *) * deviceContext->NumberOfAudioIsochronousEngines, &deviceContext->AudioIsochronousEnginesMemory, nullptr));
        deviceContext->AudioIsochronousEngines = (AudioIsochronousEngine **)WdfMemoryGetBuffer(deviceContext->AudioIsochronousEnginesMemory, nullptr);

        RtlZeroMemory(deviceContext->AudioIsochronousEngines, sizeof(AudioIsochronousEngine *) * deviceContext->NumberOfAudioIsochronousEngines);

        ULONG index = 0;
        for (auto usbAudioStreamInterfaceGroup : *deviceContext->UsbAudioConfiguration)
        {
            if (index < deviceContext->NumberOfAudioIsochronousEngines)
            {
                DECLARE_UNICODE_STRING_SIZE(circuitName, CIRCUITNAMELENGTH);

                deviceContext->AudioIsochronousEngines[index] = AudioIsochronousEngine::Create(deviceContext, usbAudioStreamInterfaceGroup);
                RETURN_NTSTATUS_IF_TRUE_ACTION(deviceContext->AudioIsochronousEngines[index] == nullptr, status = STATUS_INSUFFICIENT_RESOURCES, status);
                RETURN_NTSTATUS_IF_FAILED(deviceContext->AudioIsochronousEngines[index]->Initialize());

                RETURN_NTSTATUS_IF_FAILED(RtlUnicodeStringPrintf(&circuitName, RENDERCIRCUITNAME, index));
                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "render device name = %wZ, DeviceName = %ws", &circuitName, deviceContext->DeviceName);
                RETURN_NTSTATUS_IF_FAILED(deviceContext->AudioIsochronousEngines[index]->AddStaticRender(device, &CODEC_RENDER_COMPONENT_GUID, &circuitName));

                //
                // The driver uses this DDI to associate a circuit to a device. After
                // this call the circuit is not visible until the device goes in D0.
                // For a real driver there should be a check here to make sure the
                // circuit has not been added already (there could be a situation where
                // prepareHardware is called multiple times and releaseHardware is only
                // called once).
                //
                RETURN_NTSTATUS_IF_FAILED(deviceContext->AudioIsochronousEngines[index]->AddRenderCircuit(device));

                RETURN_NTSTATUS_IF_FAILED(RtlUnicodeStringPrintf(&circuitName, CAPTURECIRCUITNAME, index));
                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "capture device name = %wZ, DeviceName = %ws", &circuitName, deviceContext->DeviceName);
                RETURN_NTSTATUS_IF_FAILED(deviceContext->AudioIsochronousEngines[index]->AddStaticCapture(device, &CODEC_CAPTURE_COMPONENT_GUID, &MIC_CUSTOM_NAME, &circuitName));

                //
                // The driver uses this DDI to associate a circuit to a device. After
                // this call the circuit is not visible until the device goes in D0.
                // For a real driver there should be a check here to make sure the
                // circuit has not been added already (there could be a situation where
                // prepareHardware is called multiple times and releaseHardware is only
                // called once).
                //
                RETURN_NTSTATUS_IF_FAILED(deviceContext->AudioIsochronousEngines[index]->AddCaptureCircuit(device));

                deviceContext->AudioIsochronousEngines[index]->ReportInternalParameters();
            }
            index++;
        }
    }
    ReportInternalParameters(deviceContext);

    if (deviceContext->InterruptMessageProperty.IsValid)
    {
        RETURN_NTSTATUS_IF_FAILED(SetInterruptPipeInformation(deviceContext));

        RETURN_NTSTATUS_IF_FAILED(USBAudioAcxDriverStartInterruptDataReception(deviceContext));
    }

    if (NT_SUCCESS(status))
    {
        deviceContext->IsPrepareHardwareSucceeded = true;
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
    NTSTATUS        status = STATUS_SUCCESS;
    PDEVICE_CONTEXT deviceContext;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    deviceContext = GetDeviceContext(device);
    NT_ASSERT(deviceContext != nullptr);

    USBAudioAcxDriverStopInterruptDataReception(deviceContext);

    if ((deviceContext->AudioIsochronousEnginesMemory != nullptr) && (deviceContext->AudioIsochronousEngines != nullptr))
    {
        for (ULONG index = 0; index < deviceContext->NumberOfAudioIsochronousEngines; index++)
        {
            if (deviceContext->AudioIsochronousEngines[index] != nullptr)
            {
                //
                // The driver uses this DDI to delete a circuit from the current device.
                //
                deviceContext->AudioIsochronousEngines[index]->RemoveRenderCircuit(device);
                deviceContext->AudioIsochronousEngines[index]->RemoveCaptureCircuit(device);
                deviceContext->AudioIsochronousEngines[index]->CleanupBeforeDestroy();
                deviceContext->AudioIsochronousEngines[index]->Release();
                deviceContext->AudioIsochronousEngines[index] = nullptr;
            }
        }
        WdfObjectDelete(deviceContext->AudioIsochronousEnginesMemory);
        deviceContext->AudioIsochronousEnginesMemory = nullptr;
        deviceContext->AudioIsochronousEngines = nullptr;
    }

    if (deviceContext->UsbAudioConfiguration != nullptr)
    {
        delete deviceContext->UsbAudioConfiguration;
        deviceContext->UsbAudioConfiguration = nullptr;
    }

    if (deviceContext->ErrorStatistics != nullptr)
    {
        delete deviceContext->ErrorStatistics;
        deviceContext->ErrorStatistics = nullptr;
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

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
VOID USBAudioAcxDriverEvtDeviceSurpriseRemoval(
    WDFDEVICE device
)
{
    PDEVICE_CONTEXT deviceContext;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    deviceContext = GetDeviceContext(device);
    NT_ASSERT(deviceContext != nullptr);

    if (deviceContext->AudioIsochronousEngines != nullptr)
    {
        for (ULONG index = 0; index < deviceContext->NumberOfAudioIsochronousEngines; index++)
        {
            if (deviceContext->AudioIsochronousEngines[index] != nullptr)
            {
                deviceContext->AudioIsochronousEngines[index]->SetTerminateStream();
            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

NONPAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
USBAudioAcxDriverEvtDeviceD0Entry(
    WDFDEVICE              device,
    WDF_POWER_DEVICE_STATE previousState
)
{
    PDEVICE_CONTEXT deviceContext;

    // PASSIVE_LEVEL, but you should not make this callback function pageable.
    // PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, FLAG_POWER, "%!FUNC! Entry, previousState = %u", previousState);

    deviceContext = GetDeviceContext(device);
    ASSERT(deviceContext != nullptr);

    if (deviceContext->AudioIsochronousEngines != nullptr)
    {
        for (ULONG index = 0; index < deviceContext->NumberOfAudioIsochronousEngines; index++)
        {
            if (deviceContext->AudioIsochronousEngines[index] != nullptr)
            {
                deviceContext->AudioIsochronousEngines[index]->D0Entry();
            }
        }
    }

    if (deviceContext->InterruptMessageProperty.IsValid && deviceContext->InterruptInterfaceAndPipe.Pipe != nullptr)
    {
        NTSTATUS status = WdfIoTargetStart(WdfUsbTargetPipeGetIoTarget(deviceContext->InterruptInterfaceAndPipe.Pipe));
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "WdfIoTargetStart %!STATUS!", status);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, FLAG_POWER, "%!FUNC! Exit");

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
    TraceEvents(TRACE_LEVEL_INFORMATION, FLAG_POWER, "%!FUNC! Entry, targetState = %u", targetState);

    powerAction = WdfDeviceGetSystemPowerAction(device);

    deviceContext = GetDeviceContext(device);
    ASSERT(deviceContext != nullptr);

    if (deviceContext->AudioIsochronousEngines != nullptr)
    {
        for (ULONG index = 0; index < deviceContext->NumberOfAudioIsochronousEngines; index++)
        {
            if (deviceContext->AudioIsochronousEngines[index] != nullptr)
            {
                deviceContext->AudioIsochronousEngines[index]->D0Exit();
            }
        }
    }

    if (deviceContext->InterruptMessageProperty.IsValid && deviceContext->InterruptInterfaceAndPipe.Pipe != nullptr)
    {
        WdfIoTargetStop(WdfUsbTargetPipeGetIoTarget(deviceContext->InterruptInterfaceAndPipe.Pipe), WdfIoTargetCancelSentIo);
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "WdfIoTargetStop");
    }

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

    TraceEvents(TRACE_LEVEL_INFORMATION, FLAG_POWER, "%!FUNC! Exit %!STATUS!", status);

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

    TraceEvents(TRACE_LEVEL_INFORMATION, FLAG_POWER, "%!FUNC! Entry");
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

    if (deviceContext->UsbAudioConfiguration->HasInterruptDataMessageInterfaces() && deviceContext->InterruptMessageProperty.IsValid)
    {
        //
        // To receive interrupt data messages from the device, configure the device so that its power state always remains at D0.
        //
        idleSettings.Enabled = WdfFalse;
        idleSettings.UserControlOfIdleSettings = IdleDoNotAllowUserControl;
        TraceEvents(TRACE_LEVEL_VERBOSE, FLAG_POWER, " - WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS::Enable WdfFalse");
        TraceEvents(TRACE_LEVEL_VERBOSE, FLAG_POWER, " - IdleDoNotAllowUserControl");
    }
    else
    {
        idleSettings.Enabled = WdfTrue;
        TraceEvents(TRACE_LEVEL_VERBOSE, FLAG_POWER, " - WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS::Enable WdfTrue");
    }

    RETURN_NTSTATUS_IF_FAILED(WdfDeviceAssignS0IdleSettings(device, &idleSettings));

    TraceEvents(TRACE_LEVEL_INFORMATION, FLAG_POWER, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
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

NONPAGED_CODE_SEG
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

    attributes.ParentObject = device;

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
NTSTATUS SetInterruptPipeInformation(
    PDEVICE_CONTEXT deviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    if (deviceContext->InterruptMessageProperty.IsValid && deviceContext->InterruptInterfaceAndPipe.Pipe == nullptr)
    {
        WDFUSBINTERFACE usbInterface = nullptr;

        UCHAR numInterfaces = WdfUsbTargetDeviceGetNumInterfaces(deviceContext->UsbDevice);
        for (UCHAR interfaceIndex = 0; interfaceIndex < numInterfaces; interfaceIndex++)
        {
            if (WdfUsbInterfaceGetInterfaceNumber(deviceContext->Pairs[interfaceIndex].UsbInterface) == deviceContext->InterruptMessageProperty.InterfaceNumber)
            {
                usbInterface = deviceContext->Pairs[interfaceIndex].UsbInterface;
                break;
            }
        }
        if (usbInterface != nullptr)
        {
            UCHAR numberConfiguredPipes = WdfUsbInterfaceGetNumConfiguredPipes(usbInterface);

            for (UCHAR pipeIndex = 0; pipeIndex < numberConfiguredPipes; pipeIndex++)
            {
                WDFUSBPIPE               pipe;
                WDF_USB_PIPE_INFORMATION pipeInfo;

                WDF_USB_PIPE_INFORMATION_INIT(&pipeInfo);
                pipe = WdfUsbInterfaceGetConfiguredPipe(usbInterface, pipeIndex, &pipeInfo);
                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, " - [%u] %p", pipeIndex, pipe);
                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "WDF_USB_PIPE_INFORMATION::Size                %u", pipeInfo.Size);
                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "WDF_USB_PIPE_INFORMATION::MaximumPacketSize   %u", pipeInfo.MaximumPacketSize);
                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "WDF_USB_PIPE_INFORMATION::EndpointAddress     0x%x", pipeInfo.EndpointAddress);
                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "WDF_USB_PIPE_INFORMATION::Interval            %u", pipeInfo.Interval);
                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "WDF_USB_PIPE_INFORMATION::SettingIndex        %u", pipeInfo.SettingIndex);
                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "WDF_USB_PIPE_INFORMATION::MaximumTransferSize 0x%x", pipeInfo.MaximumTransferSize);
                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPTTRANSFER, "WDF_USB_PIPE_INFORMATION::PipeType %s", (pipeInfo.PipeType == WdfUsbPipeTypeInvalid) ? "WdfUsbPipeTypeInvalid" : (pipeInfo.PipeType == WdfUsbPipeTypeControl)   ? "WdfUsbPipeTypeControl"
                                                                                                                                                                                            : (pipeInfo.PipeType == WdfUsbPipeTypeIsochronous) ? "WdfUsbPipeTypeIsochronous"
                                                                                                                                                                                            : (pipeInfo.PipeType == WdfUsbPipeTypeBulk)        ? "WdfUsbPipeTypeBulk"
                                                                                                                                                                                            : (pipeInfo.PipeType == WdfUsbPipeTypeInterrupt)   ? "WdfUsbPipeTypeInterrupt"
                                                                                                                                                                                                                                               : "unknown");
                if (pipe != nullptr)
                {
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_INTERRUPTTRANSFER, " - [%u], EndpointAddress 0x%x Interrupt EndpointNumber 0x%x", pipeIndex, pipeInfo.EndpointAddress, deviceContext->InterruptMessageProperty.EndpointNumber);
                    if (pipeInfo.EndpointAddress == deviceContext->InterruptMessageProperty.EndpointNumber)
                    {
                        WdfUsbTargetPipeSetNoMaximumPacketSizeCheck(pipe);
                        deviceContext->InterruptInterfaceAndPipe.Pipe = pipe;
                        deviceContext->InterruptInterfaceAndPipe.PipeInfo = pipeInfo;
                        deviceContext->InterruptInterfaceAndPipe.MaximumTransferSize = 0;
                        // PPIPE_CONTEXT pipeContext = GetPipeContext(pipe);
                        // pipeContext->SelectedInterfaceAndPipe = &(deviceContext->InterruptInterfaceAndPipe);
                        break;
                    }
                }
            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

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

    CODEC_CIRCUIT_CONTEXT * circuitContext = GetCircuitContext((ACXCIRCUIT)object);
    ASSERT(circuitContext != nullptr);

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

    if (circuitContext->AudioIsochronousEngine != nullptr)
    {
        status = circuitContext->AudioIsochronousEngine->GetAudioProperty(*audioProperty);
    }

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

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    CODEC_CIRCUIT_CONTEXT * circuitContext = GetCircuitContext((ACXCIRCUIT)object);
    ASSERT(circuitContext != nullptr);

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

    if (circuitContext->AudioIsochronousEngine != nullptr)
    {
        ULONG                         minValueSize = 0;
        PUAC_GET_CHANNEL_INFO_CONTEXT channelInfo = static_cast<PUAC_GET_CHANNEL_INFO_CONTEXT>(params.Parameters.Property.Value);

        status = circuitContext->AudioIsochronousEngine->GetChannelInfo(channelInfo, params.Parameters.Property.ValueCb, minValueSize);
        outDataCb = minValueSize;
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

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    CODEC_CIRCUIT_CONTEXT * circuitContext = GetCircuitContext((ACXCIRCUIT)object);
    ASSERT(circuitContext != nullptr);

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

    if (circuitContext->AudioIsochronousEngine != nullptr)
    {
        ULONG                       minValueSize = 0;
        PUAC_GET_CLOCK_INFO_CONTEXT clockInfo = (PUAC_GET_CLOCK_INFO_CONTEXT)(params.Parameters.Property.Value);

        status = circuitContext->AudioIsochronousEngine->GetClockInfo(clockInfo, params.Parameters.Property.ValueCb, minValueSize);
        outDataCb = minValueSize;
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

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    CODEC_CIRCUIT_CONTEXT * circuitContext = GetCircuitContext((ACXCIRCUIT)object);
    ASSERT(circuitContext != nullptr);

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

    if (circuitContext->AudioIsochronousEngine != nullptr)
    {
        ULONG                         minValueSize = 0;
        PUAC_SET_CLOCK_SOURCE_CONTEXT clockSource = (PUAC_SET_CLOCK_SOURCE_CONTEXT)params.Parameters.Property.Value;

        status = circuitContext->AudioIsochronousEngine->SetClockSource(clockSource, params.Parameters.Property.ValueCb, minValueSize);
        outDataCb = minValueSize;
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

    CODEC_CIRCUIT_CONTEXT * circuitContext = GetCircuitContext((ACXCIRCUIT)object);
    ASSERT(circuitContext != nullptr);

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

    if (circuitContext->AudioIsochronousEngine != nullptr)
    {
        UACSampleFormat sampleFormat = (UACSampleFormat)(*(PULONG)params.Parameters.Property.Value);

        status = circuitContext->AudioIsochronousEngine->SetSampleFormat(sampleFormat);
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

    CODEC_CIRCUIT_CONTEXT * circuitContext = GetCircuitContext((ACXCIRCUIT)object);
    ASSERT(circuitContext != nullptr);

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

    if (circuitContext->AudioIsochronousEngine != nullptr)
    {
        ULONG desiredSampleRate = *((ULONG *)params.Parameters.Property.Value);

        status = circuitContext->AudioIsochronousEngine->ChangeSampleRate(desiredSampleRate);
    }

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

    CODEC_CIRCUIT_CONTEXT * circuitContext = GetCircuitContext((ACXCIRCUIT)object);
    ASSERT(circuitContext != nullptr);

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

    if (circuitContext->AudioIsochronousEngine != nullptr)
    {
        status = circuitContext->AudioIsochronousEngine->GetAsioOwnership(WdfRequestGetFileObject(request));
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

    CODEC_CIRCUIT_CONTEXT * circuitContext = GetCircuitContext((ACXCIRCUIT)object);
    ASSERT(circuitContext != nullptr);

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

    if (circuitContext->AudioIsochronousEngine != nullptr)
    {
        status = circuitContext->AudioIsochronousEngine->StartAsioStream();
    }
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

    CODEC_CIRCUIT_CONTEXT * circuitContext = GetCircuitContext((ACXCIRCUIT)object);
    ASSERT(circuitContext != nullptr);

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

    if (circuitContext->AudioIsochronousEngine != nullptr)
    {
        status = circuitContext->AudioIsochronousEngine->StopAsioStream();
    }

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

    CODEC_CIRCUIT_CONTEXT * circuitContext = GetCircuitContext((ACXCIRCUIT)object);
    ASSERT(circuitContext != nullptr);

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

    IF_TRUE_ACTION_JUMP(((params.Parameters.Property.Control == nullptr) ||
                         (params.Parameters.Property.ControlCb < sizeof(UAC_ASIO_PLAY_BUFFER_HEADER)) ||
                         (params.Parameters.Property.Value == nullptr) ||
                         (params.Parameters.Property.ValueCb < sizeof(UAC_ASIO_REC_BUFFER_HEADER))),
                        ASSERT(FALSE);
                        outDataCb = 0; status = STATUS_INVALID_PARAMETER;,
                                                                         Exit);

    PIRP irp = WdfRequestWdmGetIrp(request);

    IF_TRUE_ACTION_JUMP(irp == nullptr, ASSERT(FALSE); outDataCb = 0; status = STATUS_INVALID_PARAMETER;, Exit);

    PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(irp);
    PBYTE              inBuffer = (PBYTE)irpStack->Parameters.DeviceIoControl.Type3InputBuffer;
    PBYTE              outBuffer = (PBYTE)irp->UserBuffer;
    ULONG              inBufferLength = irpStack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG              outBufferLength = irpStack->Parameters.DeviceIoControl.OutputBufferLength;

    if (circuitContext->AudioIsochronousEngine != nullptr)
    {
        outDataCb = params.Parameters.Property.ValueCb;

        status = circuitContext->AudioIsochronousEngine->SetAsioBuffer(
            static_cast<ULONG>(outBufferLength),
            (PBYTE)outBuffer,
            0,
            static_cast<ULONG>(inBufferLength),
            (PBYTE)inBuffer,
            sizeof(KSPROPERTY)
        );
    }

Exit:

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

    CODEC_CIRCUIT_CONTEXT * circuitContext = GetCircuitContext((ACXCIRCUIT)object);
    ASSERT(circuitContext != nullptr);

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

    if (circuitContext->AudioIsochronousEngine != nullptr)
    {
        status = circuitContext->AudioIsochronousEngine->UnsetAsioBuffer();
    }

Exit:

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

    CODEC_CIRCUIT_CONTEXT * circuitContext = GetCircuitContext((ACXCIRCUIT)object);
    ASSERT(circuitContext != nullptr);

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

    if (circuitContext->AudioIsochronousEngine != nullptr)
    {
        status = circuitContext->AudioIsochronousEngine->ReleaseAsioOwnership(WdfRequestGetFileObject(request));
    }

Exit:

    WdfRequestCompleteWithInformation(request, status, outDataCb);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
}

PAGED_CODE_SEG
_Use_decl_annotations_
VOID EvtUSBAudioAcxDriverGetBufferPeriod(
    WDFOBJECT  object,
    WDFREQUEST request
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    NTSTATUS  status = STATUS_NOT_SUPPORTED;
    ULONG_PTR outDataCb = 0;

    ACX_REQUEST_PARAMETERS params{};
    ACX_REQUEST_PARAMETERS_INIT(&params);
    AcxRequestGetParameters(request, &params);

    ASSERT(params.Type == AcxRequestTypeProperty);
    ASSERT(params.Parameters.Property.Verb == AcxPropertyVerbGet);
    ASSERT(params.Parameters.Property.Control == nullptr);
    ASSERT(params.Parameters.Property.ControlCb == 0);
    ASSERT(params.Parameters.Property.Value != nullptr);
    ASSERT(params.Parameters.Property.ValueCb == sizeof(ULONG));

    CODEC_CIRCUIT_CONTEXT * circuitContext = GetCircuitContext((ACXCIRCUIT)object);
    ASSERT(circuitContext != nullptr);

    IF_TRUE_ACTION_JUMP(
        (
            (params.Parameters.Property.Control != nullptr) ||
            (params.Parameters.Property.ControlCb != 0) ||
            (params.Parameters.Property.Value == nullptr) ||
            (params.Parameters.Property.ValueCb < sizeof(ULONG))
        ),
        ASSERT(FALSE);
        outDataCb = 0;
        status = STATUS_INVALID_PARAMETER;,
                                          Exit
    );

    if (circuitContext->AudioIsochronousEngine != nullptr)
    {
        ULONG * bufferPeriod = static_cast<ULONG *>(params.Parameters.Property.Value);
        status = circuitContext->AudioIsochronousEngine->GetBufferPeriod(*bufferPeriod);
        outDataCb = sizeof(ULONG);
    }

Exit:

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    WdfRequestCompleteWithInformation(request, status, outDataCb);
}

PAGED_CODE_SEG
_Use_decl_annotations_
VOID EvtUSBAudioAcxDriverSetBufferPeriod(
    WDFOBJECT  object,
    WDFREQUEST request
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    NTSTATUS  status = STATUS_NOT_SUPPORTED;
    ULONG_PTR outDataCb = 0;

    ACX_REQUEST_PARAMETERS params{};
    ACX_REQUEST_PARAMETERS_INIT(&params);
    AcxRequestGetParameters(request, &params);

    ASSERT(params.Type == AcxRequestTypeProperty);
    ASSERT(params.Parameters.Property.Verb == AcxPropertyVerbGet);
    ASSERT(params.Parameters.Property.Control == nullptr);
    ASSERT(params.Parameters.Property.ControlCb == 0);
    ASSERT(params.Parameters.Property.Value != nullptr);
    ASSERT(params.Parameters.Property.ValueCb == sizeof(ULONG));

    CODEC_CIRCUIT_CONTEXT * circuitContext = GetCircuitContext((ACXCIRCUIT)object);
    ASSERT(circuitContext != nullptr);

    IF_TRUE_ACTION_JUMP(
        (
            (params.Parameters.Property.Control != nullptr) ||
            (params.Parameters.Property.ControlCb != 0) ||
            (params.Parameters.Property.Value == nullptr) ||
            (params.Parameters.Property.ValueCb < sizeof(ULONG))
        ),
        ASSERT(FALSE);
        outDataCb = 0;
        status = STATUS_INVALID_PARAMETER;,
                                          Exit
    );

    if (circuitContext->AudioIsochronousEngine != nullptr)
    {
        ULONG * bufferPeriod = static_cast<ULONG *>(params.Parameters.Property.Value);

        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "BufferPeriod = %u", *bufferPeriod);

        status = circuitContext->AudioIsochronousEngine->SetBufferPeriod(*bufferPeriod);
    }

Exit:

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    WdfRequestCompleteWithInformation(request, status, outDataCb);
}

PAGED_CODE_SEG
_Use_decl_annotations_
VOID EvtUSBAudioAcxDriverGetInputLatency(
    WDFOBJECT  object,
    WDFREQUEST request
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    NTSTATUS  status = STATUS_NOT_SUPPORTED;
    ULONG_PTR outDataCb = 0;

    ACX_REQUEST_PARAMETERS params{};
    ACX_REQUEST_PARAMETERS_INIT(&params);
    AcxRequestGetParameters(request, &params);

    ASSERT(params.Type == AcxRequestTypeProperty);
    ASSERT(params.Parameters.Property.Verb == AcxPropertyVerbGet);
    ASSERT(params.Parameters.Property.Control == nullptr);
    ASSERT(params.Parameters.Property.ControlCb == 0);
    ASSERT(params.Parameters.Property.Value != nullptr);
    ASSERT(params.Parameters.Property.ValueCb == sizeof(LONG));

    CODEC_CIRCUIT_CONTEXT * circuitContext = GetCircuitContext((ACXCIRCUIT)object);
    ASSERT(circuitContext != nullptr);

    IF_TRUE_ACTION_JUMP(
        (
            (params.Parameters.Property.Control != nullptr) ||
            (params.Parameters.Property.ControlCb != 0) ||
            (params.Parameters.Property.Value == nullptr) ||
            (params.Parameters.Property.ValueCb < sizeof(LONG))
        ),
        ASSERT(FALSE);
        outDataCb = 0;
        status = STATUS_INVALID_PARAMETER;,
                                          Exit
    );

    if (circuitContext->AudioIsochronousEngine != nullptr)
    {
        LONG * inputLatency = static_cast<LONG *>(params.Parameters.Property.Value);

        status = circuitContext->AudioIsochronousEngine->GetInputLatency(*inputLatency);
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "InputLatency = %d", *inputLatency);

        outDataCb = sizeof(LONG);
    }

Exit:

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    WdfRequestCompleteWithInformation(request, status, outDataCb);
}

PAGED_CODE_SEG
_Use_decl_annotations_
VOID EvtUSBAudioAcxDriverGetOutputLatency(
    WDFOBJECT  object,
    WDFREQUEST request
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    NTSTATUS  status = STATUS_NOT_SUPPORTED;
    ULONG_PTR outDataCb = 0;

    ACX_REQUEST_PARAMETERS params{};
    ACX_REQUEST_PARAMETERS_INIT(&params);
    AcxRequestGetParameters(request, &params);

    ASSERT(params.Type == AcxRequestTypeProperty);
    ASSERT(params.Parameters.Property.Verb == AcxPropertyVerbGet);
    ASSERT(params.Parameters.Property.Control == nullptr);
    ASSERT(params.Parameters.Property.ControlCb == 0);
    ASSERT(params.Parameters.Property.Value != nullptr);
    ASSERT(params.Parameters.Property.ValueCb == sizeof(LONG));

    CODEC_CIRCUIT_CONTEXT * circuitContext = GetCircuitContext((ACXCIRCUIT)object);
    ASSERT(circuitContext != nullptr);

    IF_TRUE_ACTION_JUMP(
        (
            (params.Parameters.Property.Control != nullptr) ||
            (params.Parameters.Property.ControlCb != 0) ||
            (params.Parameters.Property.Value == nullptr) ||
            (params.Parameters.Property.ValueCb < sizeof(LONG))
        ),
        ASSERT(FALSE);
        outDataCb = 0;
        status = STATUS_INVALID_PARAMETER;,
                                          Exit
    );

    if (circuitContext->AudioIsochronousEngine != nullptr)
    {
        LONG * outputLatency = static_cast<LONG *>(params.Parameters.Property.Value);

        status = circuitContext->AudioIsochronousEngine->GetOutputLatency(*outputLatency);
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "OutputLatency = %d", *outputLatency);

        outDataCb = sizeof(LONG);
    }

Exit:

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    WdfRequestCompleteWithInformation(request, status, outDataCb);
}

PAGED_CODE_SEG
_Use_decl_annotations_
VOID EvtUSBAudioAcxDriverSetAsioDevice(
    WDFOBJECT  object,
    WDFREQUEST request
)
{
    NTSTATUS status = STATUS_NOT_SUPPORTED;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    ACX_REQUEST_PARAMETERS params{};
    ACX_REQUEST_PARAMETERS_INIT(&params);
    AcxRequestGetParameters(request, &params);

    ASSERT(params.Type == AcxRequestTypeProperty);
    ASSERT(params.Parameters.Property.Verb == AcxPropertyVerbGet);
    ASSERT(params.Parameters.Property.Control == nullptr);
    ASSERT(params.Parameters.Property.ControlCb == 0);
    ASSERT(params.Parameters.Property.Value != nullptr);
    ASSERT(0 < params.Parameters.Property.ValueCb);

    CODEC_CIRCUIT_CONTEXT * circuitContext = GetCircuitContext((ACXCIRCUIT)object);
    ASSERT(circuitContext != nullptr);

    IF_TRUE_ACTION_JUMP(
        (
            (params.Parameters.Property.Control != nullptr) ||
            (params.Parameters.Property.ControlCb != 0) ||
            (params.Parameters.Property.Value == nullptr) ||
            (params.Parameters.Property.ValueCb == 0)
        ),
        ASSERT(FALSE);
        status = STATUS_INVALID_PARAMETER;,
                                          Exit
    );

    if (circuitContext->AudioIsochronousEngine != nullptr)
    {
        WDFSTRING             asioDeviceString = nullptr;
        UNICODE_STRING        asioDevice{};
        WDFMEMORY             unicodeMemory = nullptr;
        PWCHAR                unicodeStrings = nullptr;
        size_t                unicodeStringsBytes = params.Parameters.Property.ValueCb + sizeof(WCHAR);
        WDF_OBJECT_ATTRIBUTES attributes{};

        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = request;

        //
        // The ASIO device selection string passed in is not null-terminated,
        // so ensure it is null-terminated before treating it as a UNICODE_STRING.
        //
        status = WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, unicodeStringsBytes, &unicodeMemory, (PVOID *)&unicodeStrings);
        IF_FAILED_JUMP(status, Exit);

        RtlZeroMemory(unicodeStrings, unicodeStringsBytes);
        RtlCopyMemory(unicodeStrings, params.Parameters.Property.Value, params.Parameters.Property.ValueCb);
        RtlInitUnicodeString(&asioDevice, unicodeStrings);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - asio device %wZ", &asioDevice);

        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = request;

        status = WdfStringCreate(&asioDevice, &attributes, &asioDeviceString);
        if (NT_SUCCESS(status))
        {
            status = circuitContext->AudioIsochronousEngine->SetAsioDevice(asioDeviceString);
        }
    }
Exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    WdfRequestCompleteWithInformation(request, status, 0);
}

PAGED_CODE_SEG
_Use_decl_annotations_
VOID EvtUSBAudioAcxDriverGetAsioDevice(
    WDFOBJECT  object,
    WDFREQUEST request
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    ULONG_PTR outDataCb = 0;

    ACX_REQUEST_PARAMETERS params{};
    ACX_REQUEST_PARAMETERS_INIT(&params);
    AcxRequestGetParameters(request, &params);

    ASSERT(params.Type == AcxRequestTypeProperty);
    ASSERT(params.Parameters.Property.Verb == AcxPropertyVerbGet);
    ASSERT(params.Parameters.Property.Control == nullptr);
    ASSERT(params.Parameters.Property.ControlCb == 0);
    ASSERT(params.Parameters.Property.Value != nullptr);
    ASSERT(0 < params.Parameters.Property.ValueCb);

    CODEC_CIRCUIT_CONTEXT * circuitContext = GetCircuitContext((ACXCIRCUIT)object);
    ASSERT(circuitContext != nullptr);

    NTSTATUS status = STATUS_NOT_SUPPORTED;

    IF_TRUE_ACTION_JUMP(
        (
            (params.Parameters.Property.Control != nullptr) ||
            (params.Parameters.Property.ControlCb != 0) ||
            (params.Parameters.Property.Value == nullptr) ||
            (params.Parameters.Property.ValueCb == 0)
        ),
        ASSERT(FALSE);
        status = STATUS_INVALID_PARAMETER;,
                                          Exit
    );

    if (circuitContext->AudioIsochronousEngine != nullptr)
    {
        WDFSTRING             asioDeviceString = nullptr;
        UNICODE_STRING        asioDevice{};
        WDF_OBJECT_ATTRIBUTES attributes{};

        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = request;

        status = WdfStringCreate(nullptr, &attributes, &asioDeviceString);
        IF_FAILED_JUMP(status, Exit);

        status = circuitContext->AudioIsochronousEngine->GetAsioDevice(asioDeviceString);
        if (NT_SUCCESS(status) || (status == STATUS_OBJECT_NAME_NOT_FOUND))
        {
            if (NT_SUCCESS(status))
            {
                WdfStringGetUnicodeString(asioDeviceString, &asioDevice);
            }
            else
            {
                //
                // If the "AsioDevice" value has not been created yet (first call), return an empty string and treat it as a normal completion.
                //
                RtlInitUnicodeString(&asioDevice, L"");
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - asio device value has not been created yes (first call).");
                status = STATUS_SUCCESS;
            }
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - asio device %wZ", &asioDevice);

            if (params.Parameters.Property.ValueCb >= asioDevice.MaximumLength)
            {
                RtlZeroMemory(params.Parameters.Property.Value, params.Parameters.Property.ValueCb);
                RtlCopyMemory(params.Parameters.Property.Value, asioDevice.Buffer, asioDevice.MaximumLength);
            }
            else
            {
                status = STATUS_BUFFER_TOO_SMALL;
            }
            outDataCb = asioDevice.MaximumLength;
        }
    }
Exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS! outDataCb = %llu", status, outDataCb);

    WdfRequestCompleteWithInformation(request, status, outDataCb);
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
    PISOCHRONOUS_REQUEST_CONTEXT requestContext = (PISOCHRONOUS_REQUEST_CONTEXT)context;
    AudioIsochronousEngine *     audioTransferEngine = requestContext->AudioIsochronousEngine;
    StreamObject *               streamObject = requestContext->StreamObject;
    TransferObject *             transferObject = requestContext->TransferObject;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry, %p", requestContext);

    ASSERT(audioTransferEngine);
    ASSERT(streamObject);
    ASSERT(transferObject);

    audioTransferEngine->IsoRequestCompletionRoutine(completionParams, streamObject, transferObject);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return;
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

        if ((deviceContext->AudioIsochronousEngines != nullptr) && (deviceContext->AudioIsochronousEngines[0] != nullptr))
        {
            status = deviceContext->AudioIsochronousEngines[0]->FileCleanup((WDFFILEOBJECT)fileObject);
        }
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

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - AudioControlInterfaceNumber  %d", deviceContext->AudioControlInterfaceNumber);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - Interrupt IsValid            %!bool!", deviceContext->InterruptMessageProperty.IsValid);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - Interrupt InterfaceNumber    %d", deviceContext->InterruptMessageProperty.InterfaceNumber);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - Interrupt EndpointNumber     0x%d", deviceContext->InterruptMessageProperty.EndpointNumber);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - Interrupt Interval           %d", deviceContext->InterruptMessageProperty.Interval);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - IsDeviceHighSpeed            %!bool!", deviceContext->IsDeviceHighSpeed);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - IsDeviceSuperSpeed           %!bool!", deviceContext->IsDeviceSuperSpeed);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - NumberOfConfiguredInterfaces %d", deviceContext->NumberOfConfiguredInterfaces);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - DeviceName                   %ws", deviceContext->DeviceName);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - SerialNumber                 %ws", deviceContext->SerialNumber);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - FramesPerMs                  %d", deviceContext->FramesPerMs);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - DeviceClass                  %d", deviceContext->DeviceClass);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - DeviceProtocol               %d", deviceContext->DeviceProtocol);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - SuperSpeedCompatible         %d", deviceContext->SuperSpeedCompatible);
}
