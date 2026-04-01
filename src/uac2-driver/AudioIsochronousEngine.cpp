// Copyright (c) Yamaha Corporation.
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================
// ASIO is a trademark and software of Steinberg Media Technologies GmbH

/*++

Module Name:

    AudioIsochronousEngine.cpp

Abstract:

    Implement a class to manage USB streaming.

Environment:

    Kernel-mode Driver Framework

--*/

#include "Driver.h"
#include "Device.h"
#include "Public.h"
#include "Common.h"
#include "AudioIsochronousEngine.h"
#include "USBAudio.h"
#include "USBAudioConfiguration.h"
#include "ContiguousMemory.h"
#include "TransferObject.h"
#include "StreamObject.h"
#include "RtPacketObject.h"
#include "AudioIsochronousEngine.h"
#include "AsioBufferObject.h"
#include "StreamEngine.h"
#include "CircuitHelper.h"
#include "ErrorStatistics.h"

#ifndef __INTELLISENSE__
#include "AudioIsochronousEngine.tmh"
#endif

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

// INTERNAL_PARAMETERS Registry Value Name
static const WCHAR c_FirstPacketLatencyName[] = L"FirstPacketLatency";
static const WCHAR c_ClassicFramesPerIrpName[] = L"ClassicFramesPerIrp";
static const WCHAR c_MaxIrpNumberName[] = L"MaxIrpNumber";
static const WCHAR c_PreSendFramesName[] = L"PreSendFrames";
static const WCHAR c_OutputFrameDelayName[] = L"OutputFrameDelay";
static const WCHAR c_DelayedOutputBufferSwitchName[] = L"DelayedOutputBufferSwitch";
static const WCHAR c_InputBufferOperationOffsetName[] = L"InputBufferOperationOffset";
static const WCHAR c_InputHubOffsetName[] = L"InputHubOffset";
static const WCHAR c_OutputBufferOperationOffsetName[] = L"OutputBufferOperationOffset";
static const WCHAR c_OutputHubOffsetName[] = L"OutputHubOffset";
static const WCHAR c_BufferThreadPriorityName[] = L"BufferThreadPriority";
static const WCHAR c_ClassicFramesPerIrp2Name[] = L"ClassicFramesPerIrp2";
static const WCHAR c_SuggestedBufferPeriodName[] = L"SuggestedBufferPeriod";
static const WCHAR c_AsioDeviceName[] = L"AsioDevice";
static const WCHAR c_SampleRateName[] = L"SampleRate";

_Use_decl_annotations_
PAGED_CODE_SEG
AudioIsochronousEngine * AudioIsochronousEngine::Create(
    PDEVICE_CONTEXT                deviceContext,
    USBAudioStreamInterfaceGroup * usbAudioStreamInterfaceGroup
)
{
    PAGED_CODE();

    return new (POOL_FLAG_NON_PAGED, DRIVER_TAG) AudioIsochronousEngine(deviceContext, usbAudioStreamInterfaceGroup);
}

_Use_decl_annotations_
PAGED_CODE_SEG
AudioIsochronousEngine::AudioIsochronousEngine(
    PDEVICE_CONTEXT                deviceContext,
    USBAudioStreamInterfaceGroup * usbAudioStreamInterfaceGroup
)
    : m_deviceContext(deviceContext), m_usbAudioStreamInterfaceGroup(usbAudioStreamInterfaceGroup)
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    m_audioStreamPropertySet.AudioProperty.VendorId = m_deviceContext->VendorId;
    m_audioStreamPropertySet.AudioProperty.ProductId = m_deviceContext->ProductId;
    m_audioStreamPropertySet.AudioProperty.DeviceRelease = m_deviceContext->DeviceRelease;
    m_audioStreamPropertySet.DesiredSampleFormat = UACSampleFormat::UAC_SAMPLE_FORMAT_PCM;
    m_audioStreamPropertySet.AudioProperty.CurrentSampleFormat = UACSampleFormat::UAC_SAMPLE_FORMAT_PCM;
    m_audioStreamPropertySet.AudioProperty.ProductName[0] = NULL;

    if (m_deviceContext->UsbAudioConfiguration->IsMultipleClockSources())
    {
        DECLARE_UNICODE_STRING_SIZE(productName, UAC_MAX_PRODUCT_NAME_LENGTH);
        NTSTATUS status = RtlUnicodeStringPrintf(&productName, L"%ws %d", m_deviceContext->ProductName, m_usbAudioStreamInterfaceGroup->GetGroupIndex());
        if (NT_SUCCESS(status))
        {
            RtlStringCbCopyW(m_audioStreamPropertySet.AudioProperty.ProductName, UAC_MAX_PRODUCT_NAME_LENGTH * sizeof(WCHAR), productName.Buffer);
        }
    }
    if (m_audioStreamPropertySet.AudioProperty.ProductName[0] == NULL)
    {
        RtlStringCbCopyW(m_audioStreamPropertySet.AudioProperty.ProductName, UAC_MAX_PRODUCT_NAME_LENGTH * sizeof(WCHAR), m_deviceContext->ProductName);
    }

    if (m_deviceContext->IsDeviceHighSpeed || m_deviceContext->IsDeviceSuperSpeed)
    {
        // USB 2.0 or USB 3.0
        m_audioStreamPropertySet.ClassicFramesPerIrp = m_audioStreamPropertySet.InternalParameters.ClassicFramesPerIrp2;
    }
    else
    {
        // USB 1.1
        m_audioStreamPropertySet.ClassicFramesPerIrp = m_audioStreamPropertySet.InternalParameters.ClassicFramesPerIrp;
    }
    if (m_audioStreamPropertySet.ClassicFramesPerIrp == 0)
    {
        m_audioStreamPropertySet.ClassicFramesPerIrp = 1;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
AudioIsochronousEngine::~AudioIsochronousEngine()
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    if (m_deviceContext->IsPrepareHardwareSucceeded)
    {
        SaveInternalParametersToDeviceRegistry();

        SaveSampleRateToRegistry(m_deviceContext->Device, m_audioStreamPropertySet.AudioProperty.SampleRate);
    }

    if (m_contiguousMemory != nullptr)
    {
        delete m_contiguousMemory;
        m_contiguousMemory = nullptr;
    }

    if (m_rtPacketObject != nullptr)
    {
        delete m_rtPacketObject;
        m_rtPacketObject = nullptr;
    }

    if (m_streamObject != nullptr)
    {
        delete m_streamObject;
        m_streamObject = nullptr;
    }

    if (m_asioBufferObject != nullptr)
    {
        delete m_asioBufferObject;
        m_asioBufferObject = nullptr;
    }

    if (m_captureStreamEngineMemory != nullptr)
    {
        WdfObjectDelete(m_captureStreamEngineMemory);
        m_captureStreamEngineMemory = nullptr;
    }

    if (m_renderStreamEngineMemory != nullptr)
    {
        WdfObjectDelete(m_renderStreamEngineMemory);
        m_renderStreamEngineMemory = nullptr;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
AudioIsochronousEngine::Initialize()
{
    NTSTATUS              status = STATUS_SUCCESS;
    WDF_OBJECT_ATTRIBUTES attributes;

    PAGED_CODE();

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = m_deviceContext->Device;

    status = WdfWaitLockCreate(&attributes, &m_streamWaitLock);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "WdfWaitLockCreate failed %!STATUS!", status);
        return status;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = m_deviceContext->Device;

    status = WdfWaitLockCreate(&attributes, &m_streamEngineWaitLock);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "WdfWaitLockCreate failed %!STATUS!", status);
        return status;
    }

    //
    // AsioWaitLock protects the AsioBufferObject within a narrower scope compared to StreamWaitLock.
    // Locking order: StreamWaitLock is acquired first, then AsioWaitLock.
    // This design ensures proper synchronization and avoids deadlocks.
    //
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = m_deviceContext->Device;

    status = WdfWaitLockCreate(&attributes, &m_asioWaitLock);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "WdfWaitLockCreate failed %!STATUS!", status);
        return status;
    }

    status = LoadInternalParametersFromDeviceRegistry();
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "LoadInternalParametersFromDeviceRegistry failed %!STATUS!", status);
        return status;
    }

    status = m_usbAudioStreamInterfaceGroup->QueryRangeAttributeAll(m_audioStreamPropertySet);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "QueryRangeAttributeAll failed %!STATUS!", status);
        return status;
    }

    m_contiguousMemory = ContiguousMemory::Create();
    RETURN_NTSTATUS_IF_TRUE(m_contiguousMemory == nullptr, STATUS_INSUFFICIENT_RESOURCES);

    m_rtPacketObject = RtPacketObject::Create(m_deviceContext, this);
    RETURN_NTSTATUS_IF_TRUE(m_rtPacketObject == nullptr, STATUS_INSUFFICIENT_RESOURCES);

    // The default is PCM, but for devices that do not support PCM, the format closest to PCM will be selected.
    ULONG desiredFormatType = NS_USBAudio0200::FORMAT_TYPE_I;
    ULONG desiredFormat = NS_USBAudio0200::PCM;
    for (ULONG sampleFormat = 0; sampleFormat < toULong(UACSampleFormat::UAC_SAMPLE_FORMAT_LAST_ENTRY); sampleFormat++)
    {
        if ((m_audioStreamPropertySet.AudioProperty.SupportedSampleFormats & (1 << sampleFormat)))
        {
            RETURN_NTSTATUS_IF_FAILED(USBAudioDataFormat::ConvertFormatToSampleFormat((UACSampleFormat)sampleFormat, desiredFormatType, desiredFormat));
            break;
        }
    }

    m_audioStreamPropertySet.AudioProperty.IsEnableASIO = m_deviceContext->UsbAudioConfiguration->IsEnableASIO() ? TRUE : FALSE;

    ULONG desiredSampleRate = UAC_DEFAULT_SAMPLE_RATE;

    LoadSampleRateFromRegistry(m_deviceContext->Device, desiredSampleRate);
    RETURN_NTSTATUS_IF_FAILED(m_usbAudioStreamInterfaceGroup->GetNearestSupportedSampleRate(m_audioStreamPropertySet, desiredSampleRate));

    ULONG inputBytesPerSample = 0;
    ULONG inputValidBitsPerSample = 0;
    ULONG outputBytesPerSample = 0;
    ULONG outputValidBitsPerSample = 0;

    if (m_usbAudioStreamInterfaceGroup->HasInputIsochronousInterface())
    {
        RETURN_NTSTATUS_IF_FAILED(m_usbAudioStreamInterfaceGroup->GetMaxSupportedValidBitsPerSample(true, desiredFormatType, desiredFormat, inputBytesPerSample, inputValidBitsPerSample));
    }
    if (m_usbAudioStreamInterfaceGroup->HasOutputIsochronousInterface())
    {
        RETURN_NTSTATUS_IF_FAILED(m_usbAudioStreamInterfaceGroup->GetMaxSupportedValidBitsPerSample(false, desiredFormatType, desiredFormat, outputBytesPerSample, outputValidBitsPerSample));
    }

    RETURN_NTSTATUS_IF_FAILED(ActivateAudioInterface(desiredSampleRate, desiredFormatType, desiredFormat, inputBytesPerSample, inputValidBitsPerSample, outputBytesPerSample, outputValidBitsPerSample, true));

    if (m_usbAudioStreamInterfaceGroup->HasOutputIsochronousInterface())
    {
        RETURN_NTSTATUS_IF_FAILED(SelectAlternateInterface(IsoDirection::Out, m_audioStreamPropertySet.OutputProperty.InterfaceNumber, 0));
    }
    if (m_usbAudioStreamInterfaceGroup->HasInputIsochronousInterface())
    {
        RETURN_NTSTATUS_IF_FAILED(SelectAlternateInterface(IsoDirection::In, m_audioStreamPropertySet.InputProperty.InterfaceNumber, 0));
    }

    ULONG numOfInputDevices = 0, numOfOutputDevices = 0;
    RETURN_NTSTATUS_IF_FAILED(m_usbAudioStreamInterfaceGroup->GetStreamDevices(true, m_audioStreamPropertySet, numOfInputDevices));
    RETURN_NTSTATUS_IF_FAILED(m_usbAudioStreamInterfaceGroup->GetStreamDevices(false, m_audioStreamPropertySet, numOfOutputDevices));

    RETURN_NTSTATUS_IF_FAILED(m_rtPacketObject->AssignDevices(numOfInputDevices, numOfOutputDevices));

    if (numOfInputDevices != 0)
    {
        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = m_deviceContext->Device;

        RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(CStreamEngine *) * numOfInputDevices, &m_captureStreamEngineMemory, (PVOID *)&m_captureStreamEngine));
        RtlZeroMemory(m_captureStreamEngine, sizeof(CStreamEngine *) * numOfInputDevices);
    }
    m_numOfInputDevices = numOfInputDevices;

    if (numOfOutputDevices != 0)
    {
        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = m_deviceContext->Device;

        RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(CStreamEngine *) * numOfOutputDevices, &m_renderStreamEngineMemory, (PVOID *)&m_renderStreamEngine));
        RtlZeroMemory(m_renderStreamEngine, sizeof(CStreamEngine *) * numOfOutputDevices);
    }
    m_numOfOutputDevices = numOfOutputDevices;

    //
    // To prevent the DMA buffer from becoming a double buffer on a PC
    // with 4GB or more of memory, contiguous memory is allocated in
    // an area less than 4GB.
    //
    RETURN_NTSTATUS_IF_FAILED(m_contiguousMemory->Allocate(m_usbAudioStreamInterfaceGroup, m_deviceContext->SupportedControl.MaxBurstOverride, UAC_MAX_CLASSIC_FRAMES_PER_IRP, m_deviceContext->FramesPerMs));

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
AudioIsochronousEngine::ActivateAudioInterface(
    ULONG desiredSampleRate,
    ULONG desiredFormatType,
    ULONG desiredFormat,
    ULONG desiredBytesPerSampleIn,
    ULONG desiredValidBitsPerSampleIn,
    ULONG desiredBytesPerSampleOut,
    ULONG desiredValidBitsPerSampleOut,
    bool  forceSetSampleRate /* = false */
)
{
    NTSTATUS                      status = STATUS_SUCCESS;
    PUSB_CONFIGURATION_DESCRIPTOR configDescriptor = m_deviceContext->UsbConfigurationDescriptor;
    ULONG                         previousSampleRate = m_audioStreamPropertySet.AudioProperty.SampleRate;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry, %u, %u, %u, %u, %u, %u, %u, %!bool!", desiredSampleRate, desiredFormatType, desiredFormat, desiredBytesPerSampleIn, desiredValidBitsPerSampleIn, desiredBytesPerSampleOut, desiredValidBitsPerSampleOut, forceSetSampleRate);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "<PID %04x>", m_audioStreamPropertySet.AudioProperty.ProductId);

    _IRQL_limited_to_(PASSIVE_LEVEL);

    auto activateAudioInterfaceScope = wil::scope_exit([&]() {
        m_lastActivationStatus = status;
    });

    m_lastActivationStatus = STATUS_UNSUCCESSFUL;
    if (configDescriptor == nullptr)
    {
        status = STATUS_DEVICE_NOT_READY;
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! failed %!STATUS!", status);
        return status;
    }

    {
        status = m_usbAudioStreamInterfaceGroup->ActivateAudioInterface(m_audioStreamPropertySet, desiredSampleRate, desiredFormatType, desiredFormat, desiredBytesPerSampleIn, desiredValidBitsPerSampleIn, desiredBytesPerSampleOut, desiredValidBitsPerSampleOut, forceSetSampleRate);
        RETURN_NTSTATUS_IF_FAILED_MSG(status, "ActivateAudioInterface failed");

        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "<PID %04x>", m_audioStreamPropertySet.AudioProperty.ProductId);

        RtlZeroMemory(&m_usbLatency, sizeof(UAC_USB_LATENCY));
        CalculateUsbLatency(&m_usbLatency);

        m_audioStreamPropertySet.AudioProperty.InputLatencyOffset = m_usbLatency.InputLatency;
        m_audioStreamPropertySet.AudioProperty.OutputLatencyOffset = m_usbLatency.OutputLatency;

        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "<PID %04x>", m_audioStreamPropertySet.AudioProperty.ProductId);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - Re-calculated Latency Offset In %d samples, Out %d samples", m_audioStreamPropertySet.AudioProperty.InputLatencyOffset, m_audioStreamPropertySet.AudioProperty.OutputLatencyOffset);

        // For some USB devices, switching the sample rate before SetInterface
        // can cause a STATUS_UNSUCCESSFUL error and a Code 10 error when
        // selecting the alternate interface.
        status = SetPipeInformation();
    }
    RETURN_NTSTATUS_IF_FAILED_MSG(status, "SetPipeInformation failed");

    BuildChannelMap();

    if (m_usbAudioStreamInterfaceGroup->HasInputIsochronousInterface() || m_usbAudioStreamInterfaceGroup->HasOutputIsochronousInterface())
    {
        bool notify = false;
        if (previousSampleRate != m_audioStreamPropertySet.AudioProperty.SampleRate)
        {
            WdfWaitLockAcquire(m_asioWaitLock, nullptr);
            if (m_asioBufferObject != nullptr)
            {
                m_asioBufferObject->UpdateCurrentSampleRate();
                notify |= m_asioBufferObject->SetRecDeviceStatus(DeviceStatuses::SampleRateChanged);
            }
            WdfWaitLockRelease(m_asioWaitLock);
        }

        //
        // If clock source changes are implemented, include a check for state changes here.
        // When a change occurs, set `notify` to true.
        //

        if (notify)
        {
            WdfWaitLockAcquire(m_asioWaitLock, nullptr);
            if (m_asioBufferObject != nullptr)
            {
                m_asioBufferObject->SetRecDeviceStatus(DeviceStatuses::ResetRequired);
                m_asioBufferObject->SendNotificationToAsio();
            }
            WdfWaitLockRelease(m_asioWaitLock);
        }
        status = STATUS_SUCCESS;
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! ActivateAudioInterface() failed. InputBytesPerBlock %u, OutputBytesPerBlock %u", m_audioStreamPropertySet.InputProperty.BytesPerBlock, m_audioStreamPropertySet.OutputProperty.BytesPerBlock);
        status = STATUS_UNSUCCESSFUL;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
AudioIsochronousEngine::CalculateUsbLatency(
    PUAC_USB_LATENCY usbLatency
)
{
    const ULONG classicFramesPerIrp1 = m_audioStreamPropertySet.InternalParameters.ClassicFramesPerIrp;
    const ULONG classicFramesPerIrp2 = m_audioStreamPropertySet.InternalParameters.ClassicFramesPerIrp2;
    const ULONG classicFramesPerIrp = m_deviceContext->FramesPerMs > 1 ? classicFramesPerIrp2 : classicFramesPerIrp1;
    const ULONG inBufferOperationOffset = m_audioStreamPropertySet.InternalParameters.InputBufferOperationOffset;
    const ULONG inHubOffset = m_audioStreamPropertySet.InternalParameters.InputHubOffset;
    const ULONG outBufferOperationOffset = m_audioStreamPropertySet.InternalParameters.OutputBufferOperationOffset;
    const ULONG outHubOffset = m_audioStreamPropertySet.InternalParameters.OutputHubOffset;
    const ULONG sampleRate = m_audioStreamPropertySet.AudioProperty.SampleRate;
    const bool  hub = m_deviceContext->HubCount > 1;
    const ULONG inRawOffset = (inBufferOperationOffset & 0x0fffffffUL);
    ULONG       inHardwareMs = 0;
    ULONG       inHubMs = 0;
    const ULONG outRawOffset = (outBufferOperationOffset & 0x0fffffffUL);
    ULONG       outHardwareMs = 0;
    ULONG       outHubMs = 0;

    PAGED_CODE();

    switch ((inBufferOperationOffset & 0x30000000UL) >> 28)
    {
    case 0x00:
        inHubMs = hub ? inHubOffset : 0;
        break;
    case 0x01:
        inHardwareMs = m_deviceContext->LatencyOffsetList->InputBufferOperationOffset;
        inHubMs = hub ? m_deviceContext->LatencyOffsetList->InputHubOffset : 0;
        break;
    case 0x02:
        break;
    case 0x03:
        inHubMs = hub ? m_deviceContext->LatencyOffsetList->InputHubOffset : 0;
        break;
    default:
        break;
    }

    if ((inBufferOperationOffset & 0x40000000UL) != 0)
    {
        usbLatency->InputOffsetFrame = (inHardwareMs + inHubMs) * m_deviceContext->FramesPerMs + (inRawOffset * m_deviceContext->FramesPerMs / 8);
        usbLatency->InputOffsetMs = usbLatency->InputOffsetFrame / m_deviceContext->FramesPerMs;
    }
    else
    {
        usbLatency->InputOffsetMs = inHardwareMs + inHubMs + inRawOffset;
        usbLatency->InputOffsetFrame = usbLatency->InputOffsetMs * m_deviceContext->FramesPerMs;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "In  Offset : %ums, %uframes", usbLatency->InputOffsetMs, usbLatency->InputOffsetFrame);

    switch ((outBufferOperationOffset & 0x30000000UL) >> 28)
    {
    case 0x00:
        outHubMs = hub ? outHubOffset : 0;
        break;
    case 0x01:
        outHardwareMs = m_deviceContext->LatencyOffsetList->OutputBufferOperationOffset;
        outHubMs = hub ? m_deviceContext->LatencyOffsetList->OutputHubOffset : 0;
        break;
    case 0x02:
        break;
    case 0x03:
        outHubMs = hub ? m_deviceContext->LatencyOffsetList->OutputHubOffset : 0;
        break;
    default:
        break;
    }

    if ((outBufferOperationOffset & 0x40000000UL) != 0)
    {
        usbLatency->OutputOffsetFrame = (outHardwareMs + outHubMs) * m_deviceContext->FramesPerMs + (outRawOffset * m_deviceContext->FramesPerMs / 8);
        usbLatency->OutputOffsetMs = usbLatency->OutputOffsetFrame / m_deviceContext->FramesPerMs;
        if (outHardwareMs != 0)
        {
            usbLatency->OutputMinOffsetFrame = (outHubMs + 1) * m_deviceContext->FramesPerMs + (outRawOffset * 8 / m_deviceContext->FramesPerMs);
        }
        else
        {
            usbLatency->OutputMinOffsetFrame = 1;
        }
    }
    else
    {
        usbLatency->OutputOffsetMs = outHardwareMs + outHubMs + outRawOffset;
        usbLatency->OutputOffsetFrame = usbLatency->OutputOffsetMs * m_deviceContext->FramesPerMs;
        if (outHardwareMs != 0)
        {
            usbLatency->OutputMinOffsetFrame = (outHubMs + outRawOffset + 1) * m_deviceContext->FramesPerMs;
        }
        else
        {
            usbLatency->OutputMinOffsetFrame = 1;
        }
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "Out Offset : %ums, %uframes, %uframes minimum", usbLatency->OutputOffsetMs, usbLatency->OutputOffsetFrame, usbLatency->OutputMinOffsetFrame);

    usbLatency->InputDriverBuffer = (ULONG)((double)(sampleRate * (classicFramesPerIrp * m_deviceContext->FramesPerMs + usbLatency->InputOffsetFrame)) / (double)(m_deviceContext->FramesPerMs * 1000));
    usbLatency->OutputDriverBuffer = (ULONG)((double)(sampleRate * usbLatency->OutputOffsetFrame /* - usbLatency->InputOffsetFrame */) / (double)(m_deviceContext->FramesPerMs * 1000));

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

_Use_decl_annotations_
PAGED_CODE_SEG
void AudioIsochronousEngine::BuildChannelMap()
{
    PAGED_CODE();

    m_audioStreamPropertySet.AudioProperty.InputAsioChannels = m_audioStreamPropertySet.InputProperty.UsbChannels;
    m_audioStreamPropertySet.AudioProperty.OutputAsioChannels = m_audioStreamPropertySet.OutputProperty.UsbChannels;

    for (ULONG asioInChannel = 0; asioInChannel < m_audioStreamPropertySet.AudioProperty.InputAsioChannels; asioInChannel++)
    {
        WDFMEMORY memory = nullptr;
        PWSTR     channelName = nullptr;
        NTSTATUS  status = m_deviceContext->UsbAudioConfiguration->GetChannelName(m_audioStreamPropertySet.InputProperty.ChannelNames, asioInChannel, memory, channelName);

        if (NT_SUCCESS(status))
        {
            RtlStringCchCopyW(m_inputAsioChannelName[asioInChannel], UAC_MAX_CHANNEL_NAME_LENGTH, channelName);
            WdfObjectDelete(memory);
            memory = nullptr;
            channelName = nullptr;
        }
        else
        {
            RtlStringCchCopyW(m_inputAsioChannelName[asioInChannel], UAC_MAX_CHANNEL_NAME_LENGTH, m_audioStreamPropertySet.AudioProperty.ProductName);
        }
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - in asio channel name [%d] %ws", asioInChannel, m_inputAsioChannelName[asioInChannel]);
    }

    for (ULONG asioOutChannel = 0; asioOutChannel < m_audioStreamPropertySet.AudioProperty.OutputAsioChannels; asioOutChannel++)
    {
        WDFMEMORY memory = nullptr;
        PWSTR     channelName = nullptr;
        NTSTATUS  status = m_deviceContext->UsbAudioConfiguration->GetChannelName(m_audioStreamPropertySet.OutputProperty.ChannelNames, asioOutChannel, memory, channelName);

        if (NT_SUCCESS(status))
        {
            RtlStringCchCopyW(m_outputAsioChannelName[asioOutChannel], UAC_MAX_CHANNEL_NAME_LENGTH, channelName);
            WdfObjectDelete(memory);
            memory = nullptr;
            channelName = nullptr;
        }
        else
        {
            RtlStringCchCopyW(m_outputAsioChannelName[asioOutChannel], UAC_MAX_CHANNEL_NAME_LENGTH, m_audioStreamPropertySet.AudioProperty.ProductName);
        }
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - out asio channel name [%d] %ws", asioOutChannel, m_outputAsioChannelName[asioOutChannel]);
    }
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
AudioIsochronousEngine::SelectAlternateInterface(
    IsoDirection direction,
    UCHAR        interfaceNumber,
    UCHAR        alternateSetting
)
{
    NTSTATUS                      status = STATUS_SUCCESS;
    SELECTED_INTERFACE_AND_PIPE & selectedInterfaceAndPipe = (direction == IsoDirection::In) ? m_inputInterfaceAndPipe : (direction == IsoDirection::Out) ? m_outputInterfaceAndPipe
                                                                                                                                                          : m_feedbackInterfaceAndPipe;
    ASSERT(direction != IsoDirection::Feedback);

    _IRQL_limited_to_(PASSIVE_LEVEL);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry, interfaceNumber = %u, alternateSetting = %u", interfaceNumber, alternateSetting);

    if (m_deviceContext->SupportedControl.AvoidToSetSameAlternate && (selectedInterfaceAndPipe.SelectedAlternateSetting == alternateSetting))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "Skipping (already selected) Interface %u, Alternate %u.", interfaceNumber, alternateSetting);
        return STATUS_SUCCESS;
    }

    //
    // Get the interface descriptor for the specified interface number
    // and alternate setting.
    //
    PUSB_CONFIGURATION_DESCRIPTOR configDesc = m_deviceContext->UsbConfigurationDescriptor;
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

    if (WdfUsbTargetDeviceGetNumInterfaces(m_deviceContext->UsbDevice) > 0)
    {
        status = RetrieveDeviceInformation(m_deviceContext->Device);
        RETURN_NTSTATUS_IF_FAILED_MSG(status, "RetrieveDeviceInformation failed");
    }
    WDFUSBINTERFACE usbInterface = nullptr;

    UCHAR numInterfaces = WdfUsbTargetDeviceGetNumInterfaces(m_deviceContext->UsbDevice);
    for (UCHAR interfaceIndex = 0; interfaceIndex < numInterfaces; interfaceIndex++)
    {
        if (WdfUsbInterfaceGetInterfaceNumber(m_deviceContext->Pairs[interfaceIndex].UsbInterface) == interfaceNumber)
        {
            usbInterface = m_deviceContext->Pairs[interfaceIndex].UsbInterface;
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
    // initialize SELECTED_INTERFACE_AND_PIPE.
    // Therefore, SELECTED_INTERFACE_AND_PIPE should not
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
                selectedInterfaceAndPipe.MaximumTransferSize = m_audioStreamPropertySet.InputProperty.IsoPacketSize * UAC_MAX_CLASSIC_FRAMES_PER_IRP * m_deviceContext->FramesPerMs;
                break;
            case IsoDirection::Out:
                selectedInterfaceAndPipe.MaximumTransferSize = m_audioStreamPropertySet.OutputProperty.IsoPacketSize * UAC_MAX_CLASSIC_FRAMES_PER_IRP * m_deviceContext->FramesPerMs;
                break;
            case IsoDirection::Feedback:
                selectedInterfaceAndPipe.MaximumTransferSize = m_audioStreamPropertySet.OutputProperty.IsoPacketSize * UAC_MAX_CLASSIC_FRAMES_PER_IRP * m_deviceContext->FramesPerMs;
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
_Use_decl_annotations_
NTSTATUS
AudioIsochronousEngine::SetPipeInformation()
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    // m_deviceContext->PipeInformationIn       = nullptr;
    // m_deviceContext->PipeInformationOut      = nullptr;
    // m_deviceContext->PipeInformationFeedback = nullptr;
    bool failed = false;

    if (m_audioStreamPropertySet.OutputProperty.InterfaceNumber != 0)
    {
        status = SelectAlternateInterface(IsoDirection::Out, m_audioStreamPropertySet.OutputProperty.InterfaceNumber, m_audioStreamPropertySet.OutputProperty.AlternateSetting);

        if (NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - OutputInterfaceAndPipe.NumberConfiguredPipes %u", m_outputInterfaceAndPipe.NumberConfiguredPipes);
            for (UCHAR pipeIndex = 0; pipeIndex < m_outputInterfaceAndPipe.NumberConfiguredPipes; pipeIndex++)
            {
                WDFUSBPIPE               pipe;
                WDF_USB_PIPE_INFORMATION pipeInfo;

                pipe = WdfUsbInterfaceGetConfiguredPipe(m_outputInterfaceAndPipe.UsbInterface, pipeIndex, nullptr);
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] %p", pipeIndex, pipe);
                if (pipe != nullptr)
                {
                    WDF_USB_PIPE_INFORMATION_INIT(&pipeInfo);
                    WdfUsbTargetPipeGetInformation(pipe, &pipeInfo);
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u], EndpointAddress 0x%x OutputEndpointNumber 0x%x", pipeIndex, pipeInfo.EndpointAddress, m_audioStreamPropertySet.OutputProperty.EndpointNumber);
                    if (pipeInfo.EndpointAddress == m_audioStreamPropertySet.OutputProperty.EndpointNumber)
                    {
                        m_outputInterfaceAndPipe.Pipe = pipe;
                        m_outputInterfaceAndPipe.PipeInfo = pipeInfo;
                        PPIPE_CONTEXT pipeContext = GetPipeContext(pipe);
                        pipeContext->SelectedInterfaceAndPipe = &(m_outputInterfaceAndPipe);
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - set OutputInterfaceAndPipe.Pipe");
                    }
                    else if (pipeInfo.EndpointAddress == m_audioStreamPropertySet.FeedbackProperty.FeedbackEndpointNumber)
                    {
                        m_feedbackInterfaceAndPipe.InterfaceDescriptor = m_outputInterfaceAndPipe.InterfaceDescriptor;
                        m_feedbackInterfaceAndPipe.UsbInterface = m_outputInterfaceAndPipe.UsbInterface;
                        m_feedbackInterfaceAndPipe.SelectedAlternateSetting = m_outputInterfaceAndPipe.SelectedAlternateSetting;
                        m_feedbackInterfaceAndPipe.NumberConfiguredPipes = m_outputInterfaceAndPipe.NumberConfiguredPipes;
                        m_feedbackInterfaceAndPipe.MaximumTransferSize = m_outputInterfaceAndPipe.MaximumTransferSize;
                        m_feedbackInterfaceAndPipe.Pipe = pipe;
                        m_feedbackInterfaceAndPipe.PipeInfo = pipeInfo;
                        PPIPE_CONTEXT pipeContext = GetPipeContext(pipe);
                        pipeContext->SelectedInterfaceAndPipe = &(m_feedbackInterfaceAndPipe);
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - set FeedbackInterfaceAndPipe.Pipe");
                    }
                }
            }
        }
        else
        {
            failed = true;
        }
    }

    if (m_audioStreamPropertySet.InputProperty.InterfaceNumber != 0)
    {
        status = SelectAlternateInterface(IsoDirection::In, m_audioStreamPropertySet.InputProperty.InterfaceNumber, m_audioStreamPropertySet.InputProperty.AlternateSetting);

        if (NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - InputInterfaceAndPipe.NumberConfiguredPipes %u", m_inputInterfaceAndPipe.NumberConfiguredPipes);
            for (UCHAR pipeIndex = 0; pipeIndex < m_inputInterfaceAndPipe.NumberConfiguredPipes; pipeIndex++)
            {
                WDFUSBPIPE               pipe;
                WDF_USB_PIPE_INFORMATION pipeInfo;

                WDF_USB_PIPE_INFORMATION_INIT(&pipeInfo);
                pipe = WdfUsbInterfaceGetConfiguredPipe(m_inputInterfaceAndPipe.UsbInterface, pipeIndex, &pipeInfo);
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] %p", pipeIndex, pipe);
                if (pipe != nullptr)
                {
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u], EndpointAddress 0x%x InputEndpointNumber 0x%x", pipeIndex, pipeInfo.EndpointAddress, m_audioStreamPropertySet.InputProperty.EndpointNumber);
                    if (pipeInfo.EndpointAddress == m_audioStreamPropertySet.InputProperty.EndpointNumber)
                    {
                        m_inputInterfaceAndPipe.Pipe = pipe;
                        m_inputInterfaceAndPipe.PipeInfo = pipeInfo;
                        PPIPE_CONTEXT pipeContext = GetPipeContext(pipe);
                        pipeContext->SelectedInterfaceAndPipe = &(m_inputInterfaceAndPipe);
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - set InputInterfaceAndPipe.Pipe");
                    }
                }
            }
        }
        else
        {
            failed = true;
        }
    }

    if (failed)
    {
        m_deviceContext->ErrorStatistics->SetBandWidthError();
        status = STATUS_UNSUCCESSFUL;
    }
    else
    {
        m_deviceContext->ErrorStatistics->ClearBandWidthError();
        status = STATUS_SUCCESS;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::StartIsoStream()
{
    NTSTATUS status = STATUS_SUCCESS;
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    auto startIsoStreamScope = wil::scope_exit([&]() {
        if (!NT_SUCCESS(status) && (m_streamObject != nullptr) && (status != STATUS_DEVICE_BUSY))
        {
            delete m_streamObject;
            m_streamObject = nullptr;
        }
        else
        {
            InterlockedIncrement(&m_startCounterIsoStream);
            WdfWaitLockAcquire(m_asioWaitLock, nullptr);
            if (m_asioBufferObject != nullptr)
            {
                m_asioBufferObject->SetReady();
            }
            WdfWaitLockRelease(m_asioWaitLock);
        }

        if (m_streamObject != nullptr)
        {
            NTSTATUS statusTemp = WdfDeviceStopIdle(m_deviceContext->Device, FALSE);
            if (NT_SUCCESS(statusTemp))
            {
                InterlockedExchange(&m_deviceContext->IsIdleStopSucceeded, TRUE);
            }
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "WdfDeviceStopIdle %!STATUS!", statusTemp);
        }
    });

    RETURN_NTSTATUS_IF_TRUE_ACTION(m_streamObject != nullptr, status = STATUS_DEVICE_BUSY, status);
    InterlockedExchange(&m_startCounterIsoStream, 0);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_MULTICLIENT, " - start counter asio %ld, start counter acx audio %ld, start counter iso stream %ld", m_startCounterAsio, m_startCounterWdmAudio, m_startCounterIsoStream);
    status = SetPipeInformation();
    RETURN_NTSTATUS_IF_FAILED_MSG(status, "SetPipeInformation failed");

    SELECTED_INTERFACE_AND_PIPE * interfaceAndPipe[] = {
        &m_inputInterfaceAndPipe,
        &m_outputInterfaceAndPipe,
        &m_feedbackInterfaceAndPipe,
    };

    for (ULONG index = 0; index < ARRAYSIZE(interfaceAndPipe); index++)
    {
        if (interfaceAndPipe[index]->MaximumTransferSize != 0)
        {
            if (m_deviceContext->IsDeviceSuperSpeed && m_deviceContext->SuperSpeedCompatible)
            {
                status = InitializePipeContextForSuperSpeedDevice(interfaceAndPipe[index]->UsbInterface, interfaceAndPipe[index]->SelectedAlternateSetting, interfaceAndPipe[index]->Pipe);
            }
            else if (m_deviceContext->IsDeviceHighSpeed)
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

            if (interfaceAndPipe[index] == &m_inputInterfaceAndPipe)
            {
                if (WdfUsbTargetPipeIsInEndpoint(interfaceAndPipe[index]->Pipe) == FALSE)
                {
                    status = STATUS_INVALID_PARAMETER;
                    TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "Invalid pipe - not an input pipe");
                    break;
                }
            }
            else if (interfaceAndPipe[index] == &m_outputInterfaceAndPipe)
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

    if ((m_outputInterfaceAndPipe.Pipe != nullptr) && (m_inputInterfaceAndPipe.Pipe == nullptr))
    { // output only
        m_streamObject = StreamObject::Create(m_deviceContext, this, StreamStatuses::OutputStable, StreamStatuses::OutputStreaming, (StreamStatuses)(toInt(StreamStatuses::OutputStable) | toInt(StreamStatuses::OutputStreaming)));
    }
    else if ((m_outputInterfaceAndPipe.Pipe == nullptr) && (m_inputInterfaceAndPipe.Pipe != nullptr))
    { // input only
        m_streamObject = StreamObject::Create(m_deviceContext, this, StreamStatuses::InputStable, StreamStatuses::InputStreaming, (StreamStatuses)(toInt(StreamStatuses::InputStable) | toInt(StreamStatuses::InputStreaming)));
    }
    else
    {
        m_streamObject = StreamObject::Create(m_deviceContext, this, StreamStatuses::IoStable, StreamStatuses::IoStreaming, StreamStatuses::IoSteady);
    }

    RETURN_NTSTATUS_IF_TRUE_ACTION(m_streamObject == nullptr, status = STATUS_INSUFFICIENT_RESOURCES, status);

    m_streamObject->ResetNextMeasureFrames(m_audioStreamPropertySet.InputProperty.PacketsPerSec, m_audioStreamPropertySet.OutputProperty.PacketsPerSec);

    // Before measurement, initialize with the nominal sample rate.
    m_audioStreamPropertySet.InputProperty.MeasuredSampleRate = m_audioStreamPropertySet.AudioProperty.SampleRate;
    m_audioStreamPropertySet.OutputProperty.MeasuredSampleRate = m_audioStreamPropertySet.AudioProperty.SampleRate;

    status = m_streamObject->CreateMixingEngineThread(HIGH_PRIORITY, 100);
    RETURN_NTSTATUS_IF_FAILED(status);

    if (m_rtPacketObject != nullptr)
    {
        m_rtPacketObject->ResetInternal(true);
        m_rtPacketObject->ResetInternal(false);
    }

    m_streamObject->SetStartIsoFrame(GetCurrentFrame(m_deviceContext), m_audioStreamPropertySet.InternalParameters.OutputFrameDelay);
    m_streamObject->SetIsoFrameDelay(m_audioStreamPropertySet.InternalParameters.FirstPacketLatency);
    m_streamObject->ResetIsoRequestCompletionTime();
    m_streamObject->SaveStartPCUs();

    for (ULONG i = 0; i < m_audioStreamPropertySet.InternalParameters.MaxIrpNumber; i++)
    {
        if (m_feedbackInterfaceAndPipe.Pipe != nullptr)
        {
            status = StartTransfer(m_streamObject, i, IsoDirection::Feedback);
            RETURN_NTSTATUS_IF_FAILED(status);
        }
        if (m_inputInterfaceAndPipe.Pipe != nullptr)
        {
            status = StartTransfer(m_streamObject, i, IsoDirection::In);
            RETURN_NTSTATUS_IF_FAILED(status);
        }
        if (m_outputInterfaceAndPipe.Pipe != nullptr)
        {
            status = StartTransfer(m_streamObject, i, IsoDirection::Out);
        }
        RETURN_NTSTATUS_IF_FAILED(status);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::StartTransfer(
    StreamObject * streamObject,
    ULONG          index,
    IsoDirection   direction
)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG    maxXferSize = 0;
    ULONG    isoPacketSize = 0;
    ULONG    numIsoPackets = 0;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    IF_TRUE_ACTION_JUMP(streamObject == nullptr, status = STATUS_INVALID_PARAMETER, StartTransfer_Exit);
    IF_TRUE_ACTION_JUMP(m_contiguousMemory == nullptr, status = STATUS_INVALID_PARAMETER, StartTransfer_Exit);
    IF_TRUE_ACTION_JUMP(!m_contiguousMemory->IsValid(index, direction), status = STATUS_INVALID_PARAMETER, StartTransfer_Exit);

    switch (direction)
    {
    case IsoDirection::In:
        maxXferSize = m_inputInterfaceAndPipe.MaximumTransferSize;
        isoPacketSize = m_inputInterfaceAndPipe.PipeInfo.MaximumPacketSize * m_deviceContext->SupportedControl.MaxBurstOverride;
        numIsoPackets = m_audioStreamPropertySet.ClassicFramesPerIrp * m_deviceContext->FramesPerMs;
        numIsoPackets >>= (m_inputInterfaceAndPipe.PipeInfo.Interval - 1);
        if (numIsoPackets > 128)
        { // Ensure the number of packets is within the WDK limit.
            numIsoPackets = 128;
            maxXferSize = isoPacketSize * numIsoPackets;
        }
        break;
    case IsoDirection::Out:
        maxXferSize = m_outputInterfaceAndPipe.MaximumTransferSize;
        // isoPacketSize is not used.
        isoPacketSize = m_outputInterfaceAndPipe.PipeInfo.MaximumPacketSize * m_deviceContext->SupportedControl.MaxBurstOverride;
        numIsoPackets = m_audioStreamPropertySet.ClassicFramesPerIrp * m_deviceContext->FramesPerMs;
        numIsoPackets >>= (m_outputInterfaceAndPipe.PipeInfo.Interval - 1);
        break;
    case IsoDirection::Feedback:
        maxXferSize = m_feedbackInterfaceAndPipe.MaximumTransferSize;
        isoPacketSize = m_feedbackInterfaceAndPipe.PipeInfo.MaximumPacketSize;
        numIsoPackets = m_audioStreamPropertySet.ClassicFramesPerIrp * m_deviceContext->FramesPerMs;
        numIsoPackets >>= (m_audioStreamPropertySet.FeedbackProperty.FeedbackInterval - 1);
        break;
    default:
        ASSERT(false);
        break;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "num packets = %u, Classic frames per irp = %u, frames per ms = %u", numIsoPackets, m_audioStreamPropertySet.ClassicFramesPerIrp, m_deviceContext->FramesPerMs);

    TransferObject * transferObject = streamObject->GetTransferObject(index, direction);
    if (transferObject == nullptr)
    {
        transferObject = TransferObject::Create(m_deviceContext, this, streamObject, index, direction);
        IF_TRUE_ACTION_JUMP(transferObject == nullptr, status = STATUS_INSUFFICIENT_RESOURCES, StartTransfer_Exit);

        transferObject->AttachDataBuffer(m_contiguousMemory->GetDataBuffer(index, direction), numIsoPackets, isoPacketSize, maxXferSize);

        streamObject->SetTransferObject(index, direction, transferObject);
    }

    transferObject->Reset();

    ULONG lockDelayCount = 0;
    if (!m_deviceContext->SupportedControl.SkipInitialSamples)
    {
        lockDelayCount = 0;
    }
    else
    {
        switch (direction)
        {
        case IsoDirection::In:
            if (m_audioStreamPropertySet.InputProperty.LockDelay != 0)
            {
                lockDelayCount = (m_audioStreamPropertySet.InputProperty.LockDelay + m_audioStreamPropertySet.InternalParameters.MaxIrpNumber - 1) / m_audioStreamPropertySet.InternalParameters.MaxIrpNumber;
            }
            else
            {
                lockDelayCount = UAC_DEFAULT_LOCK_DELAY;
            }
            break;
        case IsoDirection::Out:
        case IsoDirection::Feedback:
            if (m_audioStreamPropertySet.OutputProperty.LockDelay != 0)
            {
                lockDelayCount = (m_audioStreamPropertySet.OutputProperty.LockDelay + m_audioStreamPropertySet.InternalParameters.MaxIrpNumber - 1) / m_audioStreamPropertySet.InternalParameters.MaxIrpNumber;
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
        status = InitializeIsoUrbIn(streamObject, transferObject, numIsoPackets);
        RETURN_NTSTATUS_IF_FAILED_MSG(status, "InitializeIsoUrbIn failed");
        break;
    case IsoDirection::Out:
        status = InitializeIsoUrbOut(streamObject, transferObject, numIsoPackets);
        RETURN_NTSTATUS_IF_FAILED_MSG(status, "InitializeIsoUrbOut failed");

        //
        // Advance half a screen as the initial transfer position. If
        // playback starts late, reconsider this position.
        //
        m_rtPacketObject->FeedOutputWriteBytes(numIsoPackets * isoPacketSize / 2);
        break;
    case IsoDirection::Feedback:
        status = InitializeIsoUrbFeedback(streamObject, transferObject, numIsoPackets);
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
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::InitializeIsoUrbIn(
    StreamObject *   streamObject,
    TransferObject * transferObject,
    ULONG            numPackets
)
{
    NTSTATUS status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    ULONG startFrame = streamObject->GetStartFrame(IsoDirection::In, numPackets);

    bool asap = false;

    if (m_deviceContext->UsbAudioConfiguration->GetClockEntityCountForTerminal() > 1)
    {
        // TBD
        // For devices that handle I/O processing independently, consider whether `asap` should be set to true from the first run.
        // This is because devices like Creative Sound Blaster G3 require `asap` to be true initially; otherwise,
        // even with sufficient StartFrame margin in the URB, USBD_STATUS_ISO_NOT_ACCESSED_LATE may still occur.
        //
        asap = true;
    }

    if (streamObject->IsIoSteady())
    {
        asap = true;
    }
    status = transferObject->SetUrbIsochronousParametersInput(startFrame, m_inputInterfaceAndPipe.Pipe, asap, USBAudioAcxDriverEvtIsoRequestContextCleanup);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

NONPAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::InitializeIsoUrbOut(
    StreamObject *   streamObject,
    TransferObject * transferObject,
    ULONG            numPackets
)
{
    NTSTATUS status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    ULONG startFrame = streamObject->GetStartFrame(IsoDirection::Out, numPackets);

    bool asap = false;

    if (m_deviceContext->UsbAudioConfiguration->GetClockEntityCountForTerminal() > 1)
    {
        // TBD
        // For devices that handle I/O processing independently, consider whether `asap` should be set to true from the first run.
        // This is because devices like Creative Sound Blaster G3 require `asap` to be true initially; otherwise,
        // even with sufficient StartFrame margin in the URB, USBD_STATUS_ISO_NOT_ACCESSED_LATE may still occur.
        //
        asap = true;
    }

    if (streamObject->IsIoSteady())
    {
        asap = true;
    }

    status = transferObject->SetUrbIsochronousParametersOutput(startFrame, m_outputInterfaceAndPipe.Pipe, asap, USBAudioAcxDriverEvtIsoRequestContextCleanup);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

NONPAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::InitializeIsoUrbFeedback(
    StreamObject *   streamObject,
    TransferObject * transferObject,
    ULONG            numPackets
)
{
    NTSTATUS status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    ULONG startFrame = streamObject->GetStartFrame(IsoDirection::Feedback, numPackets);

    bool asap = false;

    if (m_deviceContext->UsbAudioConfiguration->GetClockEntityCountForTerminal() > 1)
    {
        // TBD
        // For devices that handle I/O processing independently, consider whether `asap` should be set to true from the first run.
        // This is because devices like Creative Sound Blaster G3 require `asap` to be true initially; otherwise,
        // even with sufficient StartFrame margin in the URB, USBD_STATUS_ISO_NOT_ACCESSED_LATE may still occur.
        //
        asap = true;
    }

    if (streamObject->IsIoSteady())
    {
        asap = true;
    }

    status = transferObject->SetUrbIsochronousParametersFeedback(startFrame, m_feedbackInterfaceAndPipe.Pipe, asap, USBAudioAcxDriverEvtIsoRequestContextCleanup);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

NONPAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::ProcessTransferIn(
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
    ULONG transferredSamplesInThisIrp = transferredBytesInThisIrp / m_audioStreamPropertySet.InputProperty.BytesPerBlock;
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
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::ProcessTransferOut(
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
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::ProcessTransferFeedback(
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

        ULONG lastFeedbackSize = streamObject->UpdatePositionsFeedback(transferObject, feedbackSum, validFeedback);

        transferObject->DecrementLockDelayCount();

        transferObject->CompensateNonFeedbackOutput(lastFeedbackSize);
    }

    transferObject->FreeUrb();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
AudioIsochronousEngine::StopIsoStream()
{
    NTSTATUS status = STATUS_SUCCESS;
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    InterlockedExchange(&m_startCounterIsoStream, 0);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_MULTICLIENT, " - start counter asio %ld, start counter acx audio %ld, start counter iso stream %ld", m_startCounterAsio, m_startCounterWdmAudio, m_startCounterIsoStream);
    // cancel irp
    if (m_streamObject != nullptr)
    {
        status = m_streamObject->CancelRequestAll();

        AbortPipes(IsoDirection::In);
        AbortPipes(IsoDirection::Feedback);

        m_streamObject->TerminateMixingEngineThread();
        m_streamObject->Cleanup();
        delete m_streamObject;
        m_streamObject = nullptr;
        if (m_audioStreamPropertySet.OutputProperty.InterfaceNumber != 0)
        {
            SelectAlternateInterface(IsoDirection::Out, m_audioStreamPropertySet.OutputProperty.InterfaceNumber, 0);
        }
        if (m_audioStreamPropertySet.InputProperty.InterfaceNumber != 0)
        {
            SelectAlternateInterface(IsoDirection::In, m_audioStreamPropertySet.InputProperty.InterfaceNumber, 0);
        }
        if (InterlockedCompareExchange(&m_deviceContext->IsIdleStopSucceeded, FALSE, TRUE) == TRUE)
        {
            WdfDeviceResumeIdle(m_deviceContext->Device);
        }
    }
    if (m_contiguousMemory != nullptr)
    {
        m_contiguousMemory->Clear();
    }

    if (m_deviceContext->ErrorStatistics != nullptr)
    {
        m_deviceContext->ErrorStatistics->Report();
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

NONPAGED_CODE_SEG
_Use_decl_annotations_
void AudioIsochronousEngine::SetTerminateStream()
{
    if (m_streamObject != nullptr)
    {
        m_streamObject->SetTerminateStream();
    }
}

NONPAGED_CODE_SEG
_Use_decl_annotations_
void AudioIsochronousEngine::SetAccessible(
    bool accessible
)
{
    m_audioStreamPropertySet.AudioProperty.IsAccessible = accessible ? TRUE : FALSE;
}

NONPAGED_CODE_SEG
_Use_decl_annotations_
VOID AudioIsochronousEngine::IsoRequestCompletionRoutine(
    PWDF_REQUEST_COMPLETION_PARAMS completionParams,
    StreamObject *                 streamObject,
    TransferObject *               transferObject
)
{
    NTSTATUS    status = STATUS_SUCCESS;
    USBD_STATUS usbdStatus = STATUS_SUCCESS;
    ULONGLONG   currentTimeUs = 0ULL;
    ULONGLONG   qpcPosition = 0ULL;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    ASSERT(transferObject);
    ASSERT(streamObject);

    currentTimeUs = USBAudioAcxDriverStreamGetCurrentTimeUs(m_deviceContext, &qpcPosition);

    status = completionParams->IoStatus.Status;
    if (!NT_SUCCESS(status) && (status != STATUS_CANCELLED))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "frame %u : %s completion failed with status %!STATUS!", transferObject->GetStartFrame(), GetDirectionString(transferObject->GetDirection()), status);
    }

    usbdStatus = transferObject->GetUSBDStatus();
    if (!USBD_SUCCESS(usbdStatus) && (usbdStatus != USBD_STATUS_CANCELED))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "frame %u : %s urb failed with status %08x", transferObject->GetStartFrame(), GetDirectionString(transferObject->GetDirection()), usbdStatus);
        m_deviceContext->ErrorStatistics->LogErrorOccurrence(ErrorStatus::UrbFailed, usbdStatus);
        if (status != STATUS_NO_SUCH_DEVICE) // STATUS_NO_SUCH_DEVICE: surprise remove
        {
#if false
			// TBD Add a recovery process
#else
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "irp at index %d failed (%!STATUS!), but will be reused.", transferObject->GetIndex(), status);
#endif
            usbdStatus = USBD_STATUS_SUCCESS;
            status = STATUS_SUCCESS;
        }
    }

    if (transferObject != nullptr)
    {
        ULONGLONG periodUs = 0ULL;
        ULONGLONG periodQPC = 0ULL;
        streamObject->CompleteRequest(transferObject->GetDirection(), currentTimeUs, qpcPosition, periodUs, periodQPC);
        transferObject->CompleteRequest(currentTimeUs, qpcPosition, periodUs, periodQPC);
    }

    if (NT_SUCCESS(status) && USBD_SUCCESS(usbdStatus) && (m_startCounterIsoStream != 0))
    {
        //		WdfWaitLockAcquire(m_deviceContext->StreamWaitLock, nullptr);
        if ((streamObject != nullptr) && !streamObject->IsTerminateStream())
        {
            switch (transferObject->GetDirection())
            {
            case IsoDirection::In: {
                status = ProcessTransferIn(streamObject, transferObject);
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
                status = InitializeIsoUrbIn(streamObject, transferObject, transferObject->GetNumPackets());
                if (!NT_SUCCESS(status))
                {
                    TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "InitializeIsoUrbIn failed %!STATUS!", status);

                    goto USBAudioAcxDriverEvtIsoRequestCompletionRoutine_Exit;
                }
            }
            break;
            case IsoDirection::Out: {
                status = ProcessTransferOut(streamObject, transferObject);
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
                status = InitializeIsoUrbOut(streamObject, transferObject, transferObject->GetNumPackets());
                if (!NT_SUCCESS(status))
                {
                    TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "InitializeIsoUrbOut failed %!STATUS!", status);

                    goto USBAudioAcxDriverEvtIsoRequestCompletionRoutine_Exit;
                }
            }
            break;
            case IsoDirection::Feedback: {
                status = ProcessTransferFeedback(streamObject, transferObject);
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

                status = InitializeIsoUrbFeedback(streamObject, transferObject, transferObject->GetNumPackets());
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
        }
        else
        {
            status = transferObject->FreeRequest();
            if (!NT_SUCCESS(status))
            {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "FreeRequest failed %!STATUS!", status);

                goto USBAudioAcxDriverEvtIsoRequestCompletionRoutine_Exit;
            }
        }
        //		WdfWaitLockRelease(m_deviceContext->StreamWaitLock);
    }

USBAudioAcxDriverEvtIsoRequestCompletionRoutine_Exit:

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
AudioIsochronousEngine::AbortPipes(
    IsoDirection direction
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
    ULONG    count;
    NTSTATUS status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    //
    // initialize variables
    //

    SELECTED_INTERFACE_AND_PIPE & selectedInterfaceAndPipe = (direction == IsoDirection::In) ? m_inputInterfaceAndPipe : (direction == IsoDirection::Out) ? m_outputInterfaceAndPipe
                                                                                                                                                          : m_feedbackInterfaceAndPipe;

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
_Use_decl_annotations_
NTSTATUS
AudioIsochronousEngine::InitializePipeContextForSuperSpeedDevice(
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

        status = InitializePipeContextForSuperSpeedIsochPipe(WdfUsbInterfaceGetInterfaceNumber(Interface), selectedAlternateSetting, pipe);
    }
    else if (WdfUsbPipeTypeBulk == pipeInfo.PipeType)
    {

        ASSERT(WdfUsbPipeTypeBulk != pipeInfo.PipeType);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
PUSB_ENDPOINT_DESCRIPTOR
AudioIsochronousEngine::GetEndpointDescriptorForEndpointAddress(
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

    pConfigurationDescriptor = m_deviceContext->UsbConfigurationDescriptor;

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
_Use_decl_annotations_
NTSTATUS
AudioIsochronousEngine::InitializePipeContextForSuperSpeedIsochPipe(
    UCHAR      interfaceNumber,
    UCHAR      selectedAlternateSetting,
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
_Use_decl_annotations_
NTSTATUS
AudioIsochronousEngine::InitializePipeContextForHighSpeedDevice(
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
_Use_decl_annotations_
NTSTATUS
AudioIsochronousEngine::InitializePipeContextForFullSpeedDevice(
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

_Use_decl_annotations_
NONPAGED_CODE_SEG
void AudioIsochronousEngine::SetMeasuredSampleRate(
    bool  isInput,
    ULONG measuredSampleRate
)
{
    if (isInput)
    {
        m_audioStreamPropertySet.InputProperty.MeasuredSampleRate = measuredSampleRate;
    }
    else
    {
        m_audioStreamPropertySet.OutputProperty.MeasuredSampleRate = measuredSampleRate;
    }
}

_Use_decl_annotations_
PAGED_CODE_SEG
void AudioIsochronousEngine::SetAsioBufferPeriod(
    ULONG bufferPeriod
)
{
    PAGED_CODE();

    m_audioStreamPropertySet.AudioProperty.AsioBufferPeriod = bufferPeriod;
}

_Use_decl_annotations_
PAGED_CODE_SEG
void AudioIsochronousEngine::SetAsioDriverVersion(
    ULONG asioDriverVersion
)
{
    PAGED_CODE();

    m_audioStreamPropertySet.AudioProperty.AsioDriverVersion = asioDriverVersion;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
AudioIsochronousEngine::GetStreamDevices(
    bool    isInput,
    ULONG & numOfDevices
)
{
    PAGED_CODE();

    return m_usbAudioStreamInterfaceGroup->GetStreamDevices(isInput, m_audioStreamPropertySet, numOfDevices);
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
AudioIsochronousEngine::GetStreamDevicesAdjusted(
    bool    isInput,
    ULONG & numOfDevices
)
{
    PAGED_CODE();

    return m_usbAudioStreamInterfaceGroup->GetStreamDevicesAdjusted(isInput, m_audioStreamPropertySet, numOfDevices);
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
AudioIsochronousEngine::GetStreamChannels(
    bool    isInput,
    UCHAR & numOfChannels
)
{
    PAGED_CODE();

    return m_usbAudioStreamInterfaceGroup->GetStreamChannels(isInput, m_audioStreamPropertySet, numOfChannels);
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
AudioIsochronousEngine::GetChannelName(
    bool        isInput,
    ULONG       channel,
    WDFMEMORY & memory,
    PWSTR &     channelName
)
{
    NTSTATUS status = STATUS_NOT_SUPPORTED;

    PAGED_CODE();

    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry, %!bool!, %u", isInput, channel);

    if (isInput)
    {
        if (m_audioStreamPropertySet.InputProperty.ChannelNames != USBAudioConfiguration::InvalidString)
        {
            status = m_deviceContext->UsbAudioConfiguration->GetChannelName(m_audioStreamPropertySet.InputProperty.ChannelNames, channel, memory, channelName);
        }
    }
    else
    {
        if (m_audioStreamPropertySet.OutputProperty.ChannelNames != USBAudioConfiguration::InvalidString)
        {
            status = m_deviceContext->UsbAudioConfiguration->GetChannelName(m_audioStreamPropertySet.OutputProperty.ChannelNames, channel, memory, channelName);
        }
    }

    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit %!STATUS!,", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
AudioIsochronousEngine::GetStereoChannelName(
    bool        isInput,
    ULONG       channel,
    WDFMEMORY & memory,
    PWSTR &     channelName
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PAGED_CODE();

    if (isInput)
    {
        status = m_deviceContext->UsbAudioConfiguration->GetStereoChannelName(m_audioStreamPropertySet.InputProperty.ChannelNames, channel, memory, channelName);
    }
    else
    {
        status = m_deviceContext->UsbAudioConfiguration->GetStereoChannelName(m_audioStreamPropertySet.OutputProperty.ChannelNames, channel, memory, channelName);
    }
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioDataFormatManager *
AudioIsochronousEngine::GetUSBAudioDataFormatManager(
    _In_ bool isInput
)
{
    PAGED_CODE();

    return m_usbAudioStreamInterfaceGroup->GetUSBAudioDataFormatManager(isInput);
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
AudioIsochronousEngine::GetCurrentDataFormat(
    bool            isInput,
    ACXDATAFORMAT & dataFormat
)
{
    UCHAR                               numOfChannels = 0;
    KSDATAFORMAT_WAVEFORMATEXTENSIBLE * ksDataFormatWaveFormatExtensible = nullptr;
    WDFMEMORY                           ksDataFormatWaveFormatExtensibleMemory = nullptr;

    PAGED_CODE();

    ASSERT(m_deviceContext->Device != nullptr);

    auto createInterfaceScope = wil::scope_exit([&]() {
        if (ksDataFormatWaveFormatExtensibleMemory != nullptr)
        {
            WdfObjectDelete(ksDataFormatWaveFormatExtensibleMemory);
            ksDataFormatWaveFormatExtensibleMemory = nullptr;
        }
        ksDataFormatWaveFormatExtensible = nullptr;
    });

    RtlZeroMemory(&dataFormat, sizeof(dataFormat));

    RETURN_NTSTATUS_IF_FAILED(GetStreamChannels(isInput, numOfChannels));

    if (isInput)
    {
        ASSERT(m_captureCircuit != nullptr);

        if (m_usbAudioStreamInterfaceGroup->HasInputIsochronousInterface())
        {
            RETURN_NTSTATUS_IF_FAILED(USBAudioDataFormat::BuildWaveFormatExtensible(
                m_deviceContext->UsbDevice,
                m_audioStreamPropertySet.AudioProperty.SampleRate,
                numOfChannels,
                (UCHAR)m_audioStreamPropertySet.InputProperty.BytesPerSample,
                (UCHAR)m_audioStreamPropertySet.InputProperty.ValidBitsPerSample,
                m_audioStreamPropertySet.InputProperty.FormatType,
                m_audioStreamPropertySet.InputProperty.Format,
                false,
                ksDataFormatWaveFormatExtensible,
                ksDataFormatWaveFormatExtensibleMemory
            ));
            ASSERT(ksDataFormatWaveFormatExtensible != nullptr);
            RETURN_NTSTATUS_IF_FAILED(AllocateFormat(ksDataFormatWaveFormatExtensible, m_captureCircuit, m_deviceContext->Device, &dataFormat));
        }
    }
    else
    {
        ASSERT(m_renderCircuit != nullptr);

        if (m_usbAudioStreamInterfaceGroup->HasOutputIsochronousInterface())
        {
            RETURN_NTSTATUS_IF_FAILED(USBAudioDataFormat::BuildWaveFormatExtensible(
                m_deviceContext->UsbDevice,
                m_audioStreamPropertySet.AudioProperty.SampleRate,
                numOfChannels,
                (UCHAR)m_audioStreamPropertySet.OutputProperty.BytesPerSample,
                (UCHAR)m_audioStreamPropertySet.OutputProperty.ValidBitsPerSample,
                m_audioStreamPropertySet.OutputProperty.FormatType,
                m_audioStreamPropertySet.OutputProperty.Format,
                false,
                ksDataFormatWaveFormatExtensible,
                ksDataFormatWaveFormatExtensibleMemory
            ));
            ASSERT(ksDataFormatWaveFormatExtensible != nullptr);
            RETURN_NTSTATUS_IF_FAILED(AllocateFormat(ksDataFormatWaveFormatExtensible, m_renderCircuit, m_deviceContext->Device, &dataFormat));
        }
    }

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
bool AudioIsochronousEngine::HasInputIsochronousInterface() const
{
    return m_usbAudioStreamInterfaceGroup->HasInputIsochronousInterface();
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
bool AudioIsochronousEngine::HasOutputIsochronousInterface() const
{
    return m_usbAudioStreamInterfaceGroup->HasOutputIsochronousInterface();
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
bool AudioIsochronousEngine::HasInputAndOutputIsochronousInterfaces() const
{
    return m_usbAudioStreamInterfaceGroup->HasInputAndOutputIsochronousInterfaces();
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
AudioIsochronousEngine::GetStreamChannelInfo(
    bool     isInput,
    UCHAR &  numOfChannels,
    USHORT & terminalType,
    UCHAR &  volumeUnitID,
    UCHAR &  muteUnitID
)
{
    PAGED_CODE();

    return m_usbAudioStreamInterfaceGroup->GetStreamChannelInfo(isInput, m_audioStreamPropertySet, numOfChannels, terminalType, volumeUnitID, muteUnitID);
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
AudioIsochronousEngine::GetStreamChannelInfoAdjusted(
    bool     isInput,
    UCHAR &  numOfChannels,
    USHORT & terminalType,
    UCHAR &  volumeUnitID,
    UCHAR &  muteUnitID
)
{
    PAGED_CODE();

    return m_usbAudioStreamInterfaceGroup->GetStreamChannelInfoAdjusted(isInput, m_audioStreamPropertySet, numOfChannels, terminalType, volumeUnitID, muteUnitID);
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
AudioIsochronousEngine::AddStaticRender(
    WDFDEVICE              device,
    const GUID *           componentGuid,
    const UNICODE_STRING * circuitName
)
{
    PAGED_CODE();

    return CodecR_AddStaticRender(device, componentGuid, circuitName, this);
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
AudioIsochronousEngine::AddRenderCircuit(
    WDFDEVICE device
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PAGED_CODE();

    if (m_renderCircuit != nullptr)
    {
        status = AcxDeviceAddCircuit(device, m_renderCircuit);
    }

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
AudioIsochronousEngine::RemoveRenderCircuit(
    WDFDEVICE device
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PAGED_CODE();

    if (m_renderCircuit != nullptr)
    {
        status = AcxDeviceRemoveCircuit(device, m_renderCircuit);
    }

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
AudioIsochronousEngine::SetRenderCircuit(
    ACXCIRCUIT renderCircuit
)
{
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    PAGED_CODE();

    if (m_renderCircuit == nullptr)
    {
        m_renderCircuit = renderCircuit;
    }

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
AudioIsochronousEngine::AddStaticCapture(
    WDFDEVICE              device,
    const GUID *           componentGuid,
    const GUID *           micCustomName,
    const UNICODE_STRING * circuitName
)
{
    PAGED_CODE();

    return CodecC_AddStaticCapture(device, componentGuid, micCustomName, circuitName, this);
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
AudioIsochronousEngine::AddCaptureCircuit(
    WDFDEVICE device
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PAGED_CODE();

    if (m_captureCircuit != nullptr)
    {
        status = AcxDeviceAddCircuit(device, m_captureCircuit);
    }

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
AudioIsochronousEngine::RemoveCaptureCircuit(
    WDFDEVICE device
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PAGED_CODE();

    if (m_captureCircuit != nullptr)
    {
        status = AcxDeviceRemoveCircuit(device, m_captureCircuit);
    }

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
AudioIsochronousEngine::SetCaptureCircuit(
    ACXCIRCUIT captureCircuit
)
{
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    PAGED_CODE();

    if (m_captureCircuit == nullptr)
    {
        m_captureCircuit = captureCircuit;
    }

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
AudioIsochronousEngine::VolumeChangeLevelNotification(
    UCHAR entityID
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PAGED_CODE();

    if (m_renderCircuit != nullptr)
    {
        status = CodecR_VolumeChangeLevelNotification(m_renderCircuit, entityID);
    }
    if (m_captureCircuit != nullptr)
    {
        status = CodecC_VolumeChangeLevelNotification(m_captureCircuit, entityID);
    }

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
AudioIsochronousEngine::MuteChangeStateNotification(
    UCHAR entityID
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PAGED_CODE();

    if (m_renderCircuit != nullptr)
    {
        status = CodecR_MuteChangeStateNotification(m_renderCircuit, entityID);
    }
    if (m_captureCircuit != nullptr)
    {
        status = CodecC_MuteChangeStateNotification(m_captureCircuit, entityID);
    }

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
AudioIsochronousEngine::ConnectorChangeStateNotification(
    UCHAR entityID
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PAGED_CODE();

    if (m_renderCircuit != nullptr)
    {
        status = CodecR_ConnectorChangeStateNotification(m_renderCircuit, entityID);
    }
    if (m_captureCircuit != nullptr)
    {
        status = CodecC_ConnectorChangeStateNotification(m_captureCircuit, entityID);
    }

    return status;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void AudioIsochronousEngine::D0Entry()
{
    SetAccessible(true);
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void AudioIsochronousEngine::D0Exit()
{
    SetAccessible(false);
    SetTerminateStream();

    AcquireStreamWaitLock();
    if ((m_startCounterAsio != 0) || (m_startCounterWdmAudio != 0))
    {
        AcquireAsioWaitLock();
        if (m_asioBufferObject != nullptr)
        {
            m_asioBufferObject->Clear();
        }
        ReleaseAsioWaitLock();

        if (m_contiguousMemory != nullptr)
        {
            m_contiguousMemory->Clear();
        }
        TraceEvents(TRACE_LEVEL_INFORMATION, FLAG_POWER, "Stop USB isochronous transfer.");

        StopIsoStream();

        InterlockedExchange(&m_startCounterAsio, 0);
        InterlockedExchange(&m_startCounterWdmAudio, 0);
    }
    ReleaseStreamWaitLock();
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
AudioIsochronousEngine::StreamPrepareHardware(
    bool            isInput,
    ULONG           deviceIndex,
    CStreamEngine * streamEngine
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    _IRQL_limited_to_(PASSIVE_LEVEL);

    auto prepareHardwareScope = wil::scope_exit([&]() {
        ReleaseStreamEngineWaitLock();
    });

    AcquireStreamEngineWaitLock();
    if (isInput)
    {
        RETURN_NTSTATUS_IF_TRUE(m_captureStreamEngine == nullptr, STATUS_UNSUCCESSFUL);
        RETURN_NTSTATUS_IF_TRUE(deviceIndex >= m_numOfInputDevices, STATUS_INVALID_PARAMETER);
        RETURN_NTSTATUS_IF_TRUE(m_captureStreamEngine[deviceIndex] != nullptr, STATUS_UNSUCCESSFUL);
        m_captureStreamEngine[deviceIndex] = streamEngine;
    }
    else
    {
        RETURN_NTSTATUS_IF_TRUE(m_renderStreamEngine == nullptr, STATUS_UNSUCCESSFUL);
        RETURN_NTSTATUS_IF_TRUE(deviceIndex >= m_numOfOutputDevices, STATUS_INVALID_PARAMETER);
        RETURN_NTSTATUS_IF_TRUE(m_renderStreamEngine[deviceIndex] != nullptr, STATUS_UNSUCCESSFUL);
        m_renderStreamEngine[deviceIndex] = streamEngine;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
AudioIsochronousEngine::StreamReleaseHardware(
    bool  isInput,
    ULONG deviceIndex
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    _IRQL_limited_to_(PASSIVE_LEVEL);

    auto releaseHardwareScope = wil::scope_exit([&]() {
        ReleaseStreamEngineWaitLock();
    });

    AcquireStreamEngineWaitLock();
    if (isInput)
    {
        RETURN_NTSTATUS_IF_TRUE(m_captureStreamEngine == nullptr, STATUS_UNSUCCESSFUL);
        RETURN_NTSTATUS_IF_TRUE(deviceIndex >= m_numOfInputDevices, STATUS_INVALID_PARAMETER);
        m_captureStreamEngine[deviceIndex] = nullptr;
    }
    else
    {
        RETURN_NTSTATUS_IF_TRUE(m_renderStreamEngine == nullptr, STATUS_UNSUCCESSFUL);
        RETURN_NTSTATUS_IF_TRUE(deviceIndex >= m_numOfOutputDevices, STATUS_INVALID_PARAMETER);
        m_renderStreamEngine[deviceIndex] = nullptr;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
AudioIsochronousEngine::StreamSetDataFormat(
    bool          isInput,
    ULONG         deviceIndex,
    ACXDATAFORMAT dataFormat
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry, %!bool!", isInput);

    _IRQL_limited_to_(PASSIVE_LEVEL);

    AcquireStreamWaitLock();

    if (m_rtPacketObject != nullptr)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - data format %u, %llu, %u, %u, %u, %u, %u, %u, %u", AcxDataFormatGetChannelsCount(dataFormat), AcxDataFormatGetChannelMask(dataFormat), AcxDataFormatGetSampleSize(dataFormat), AcxDataFormatGetBitsPerSample(dataFormat), AcxDataFormatGetValidBitsPerSample(dataFormat), AcxDataFormatGetSamplesPerBlock(dataFormat), AcxDataFormatGetBlockAlign(dataFormat), AcxDataFormatGetSampleRate(dataFormat), AcxDataFormatGetAverageBytesPerSec(dataFormat));

        // TraceAcxDataFormat(TRACE_LEVEL_VERBOSE, dataFormat);

        status = m_rtPacketObject->SetDataFormat(isInput, dataFormat);
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

        ACXDATAFORMAT inputDataFormatBeforeChange = nullptr;
        ACXDATAFORMAT outputDataFormatBeforeChange = nullptr;
        ACXDATAFORMAT inputDataFormatAfterChange = nullptr;
        ACXDATAFORMAT outputDataFormatAfterChange = nullptr;
        ULONG         formatType, format;
        bool          streamRunning = false;
        ULONG         desiredBytesPerSampleIn = m_audioStreamPropertySet.InputProperty.BytesPerSample;
        ULONG         desiredValidBitsPerSampleIn = m_audioStreamPropertySet.InputProperty.ValidBitsPerSample;
        ULONG         desiredBytesPerSampleOut = m_audioStreamPropertySet.OutputProperty.BytesPerSample;
        ULONG         desiredValidBitsPerSampleOut = m_audioStreamPropertySet.OutputProperty.ValidBitsPerSample;

        status = GetCurrentDataFormat(true, inputDataFormatBeforeChange);
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

        status = GetCurrentDataFormat(false, outputDataFormatBeforeChange);
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

        status = ConvertAudioDataFormat(dataFormat, formatType, format);
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

        if (isInput)
        {
            desiredBytesPerSampleIn = AcxDataFormatGetBitsPerSample(dataFormat) / 8;
            desiredValidBitsPerSampleIn = AcxDataFormatGetValidBitsPerSample(dataFormat);

            status = m_usbAudioStreamInterfaceGroup->GetNearestSupportedValidBitsPerSamples(isInput, formatType, format, desiredBytesPerSampleOut, desiredValidBitsPerSampleOut);
            IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);
        }
        else
        {
            desiredBytesPerSampleOut = AcxDataFormatGetBitsPerSample(dataFormat) / 8;
            desiredValidBitsPerSampleOut = AcxDataFormatGetValidBitsPerSample(dataFormat);

            status = m_usbAudioStreamInterfaceGroup->GetNearestSupportedValidBitsPerSamples(isInput, formatType, format, desiredBytesPerSampleIn, desiredValidBitsPerSampleIn);
            IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);
        }

        if (m_usbAudioStreamInterfaceGroup->HasInputIsochronousInterface() && m_usbAudioStreamInterfaceGroup->HasOutputIsochronousInterface())
        {
            IF_TRUE_JUMP((m_audioStreamPropertySet.AudioProperty.SampleRate == AcxDataFormatGetSampleRate(dataFormat)) && (m_audioStreamPropertySet.InputProperty.FormatType == formatType) && (m_audioStreamPropertySet.InputProperty.Format == format) && (m_audioStreamPropertySet.InputProperty.BytesPerSample == desiredBytesPerSampleIn) && (m_audioStreamPropertySet.InputProperty.ValidBitsPerSample == desiredValidBitsPerSampleIn) && (m_audioStreamPropertySet.OutputProperty.FormatType == formatType) && (m_audioStreamPropertySet.OutputProperty.Format == format) && (m_audioStreamPropertySet.OutputProperty.BytesPerSample == desiredBytesPerSampleOut) && (m_audioStreamPropertySet.OutputProperty.ValidBitsPerSample == desiredValidBitsPerSampleOut), Exit_BeforeWaitLockRelease);
        }
        else if (m_usbAudioStreamInterfaceGroup->HasInputIsochronousInterface())
        {
            IF_TRUE_JUMP((m_audioStreamPropertySet.AudioProperty.SampleRate == AcxDataFormatGetSampleRate(dataFormat)) && (m_audioStreamPropertySet.InputProperty.FormatType == formatType) && (m_audioStreamPropertySet.InputProperty.Format == format) && (m_audioStreamPropertySet.InputProperty.BytesPerSample == desiredBytesPerSampleIn) && (m_audioStreamPropertySet.InputProperty.ValidBitsPerSample == desiredValidBitsPerSampleIn), Exit_BeforeWaitLockRelease);
        }
        else if (m_usbAudioStreamInterfaceGroup->HasOutputIsochronousInterface())
        {
            IF_TRUE_JUMP((m_audioStreamPropertySet.AudioProperty.SampleRate == AcxDataFormatGetSampleRate(dataFormat)) && (m_audioStreamPropertySet.OutputProperty.FormatType == formatType) && (m_audioStreamPropertySet.OutputProperty.Format == format) && (m_audioStreamPropertySet.OutputProperty.BytesPerSample == desiredBytesPerSampleOut) && (m_audioStreamPropertySet.OutputProperty.ValidBitsPerSample == desiredValidBitsPerSampleOut), Exit_BeforeWaitLockRelease);
        }

        if (m_streamObject != nullptr)
        {
            AcquireAsioWaitLock();
            if (m_asioBufferObject == nullptr)
            {
                streamRunning = true;
            }
            ReleaseAsioWaitLock();
            if ((m_startCounterAsio != 0) || (m_startCounterWdmAudio != 0))
            {
                StopIsoStream();
            }
        }
        if (m_rtPacketObject != nullptr)
        {
            m_rtPacketObject->Pause();
        }
        status = ActivateAudioInterface(AcxDataFormatGetSampleRate(dataFormat), formatType, format, desiredBytesPerSampleIn, desiredValidBitsPerSampleIn, desiredBytesPerSampleOut, desiredValidBitsPerSampleOut);
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

        if (streamRunning && NT_SUCCESS(status))
        {
            if ((m_startCounterAsio != 0) || (m_startCounterWdmAudio != 0))
            {
                StartIsoStream();
            }
        }

        status = GetCurrentDataFormat(true, inputDataFormatAfterChange);
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

        status = GetCurrentDataFormat(false, outputDataFormatAfterChange);
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

        if ((m_renderCircuit != nullptr) && (outputDataFormatBeforeChange != nullptr) && (outputDataFormatAfterChange != nullptr) && !AcxDataFormatIsEqual(outputDataFormatBeforeChange, outputDataFormatAfterChange))
        {
            for (ULONG renderDeviceIndex = 0; renderDeviceIndex < m_numOfOutputDevices; renderDeviceIndex++)
            {
                if (isInput || (!isInput && (renderDeviceIndex != deviceIndex)))
                {
                    ACXPIN pin = AcxCircuitGetPinById(m_renderCircuit, renderDeviceIndex * CodecRenderPinCount + CodecRenderHostPin);
                    if (pin != nullptr)
                    {
                        status = NotifyDataFormatChange(m_deviceContext->Device, m_renderCircuit, pin, outputDataFormatAfterChange);
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - render pin %u, PinNotifyDataFormatChange %!STATUS!", renderDeviceIndex * 2, status);
                        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);
                    }
                }
            }
        }
        if ((m_captureCircuit != nullptr) && (inputDataFormatBeforeChange != nullptr) && (inputDataFormatAfterChange != nullptr) && !AcxDataFormatIsEqual(inputDataFormatBeforeChange, inputDataFormatAfterChange))
        {
            for (ULONG captureDeviceIndex = 0; captureDeviceIndex < m_numOfInputDevices; captureDeviceIndex++)
            {
                if (!isInput || (isInput && (captureDeviceIndex != deviceIndex)))
                {
                    ACXPIN pin = AcxCircuitGetPinById(m_captureCircuit, captureDeviceIndex * CodecCapturePinCount + CodecCaptureHostPin);
                    if (pin != nullptr)
                    {
                        status = NotifyDataFormatChange(m_deviceContext->Device, m_captureCircuit, pin, inputDataFormatAfterChange);
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - capture pin %u, AcxPinNotifyDataFormatChange %!STATUS!", captureDeviceIndex * 2, status);
                        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);
                    }
                }
            }
        }
    }
Exit_BeforeWaitLockRelease:
    ReleaseStreamWaitLock();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
AudioIsochronousEngine::StreamSetRtPackets(
    bool    isInput,
    ULONG   deviceIndex,
    PVOID * packets,
    ULONG   packetsCount,
    ULONG   packetSize,
    ULONG   channel,
    ULONG   numOfChannelsPerDevice
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry, %s, packetsCount = %d, packetSize = %d", isInput ? "Input" : "Output", packetsCount, packetSize);

    AcquireStreamWaitLock();

    if (m_rtPacketObject != nullptr)
    {
        status = m_rtPacketObject->SetRtPackets(isInput, deviceIndex, packets, packetsCount, packetSize, channel, numOfChannelsPerDevice);
    }

    ReleaseStreamWaitLock();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
void AudioIsochronousEngine::StreamUnsetRtPackets(
    bool  isInput,
    ULONG deviceIndex
)
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    AcquireStreamWaitLock();

    if (m_rtPacketObject != nullptr)
    {
        m_rtPacketObject->UnsetRtPackets(isInput, deviceIndex);
    }

    ReleaseStreamWaitLock();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
AudioIsochronousEngine::StreamRun(
    bool  isInput,
    ULONG deviceIndex
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    AcquireStreamWaitLock();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_MULTICLIENT, " - start counter asio %ld, start counter acx audio %ld, start counter iso stream %ld", m_startCounterAsio, m_startCounterWdmAudio, m_startCounterIsoStream);
    if ((m_startCounterAsio == 0) && (m_startCounterWdmAudio == 0))
    {
        status = StartIsoStream();
    }
    else
    {
        if (m_rtPacketObject != nullptr)
        {
            m_rtPacketObject->ResetInternal(isInput, deviceIndex);
        }
        status = STATUS_SUCCESS;
    }
    if (NT_SUCCESS(status))
    {
        if (m_rtPacketObject != nullptr)
        {
            m_rtPacketObject->Resume(isInput, deviceIndex);
        }
        InterlockedIncrement(&m_startCounterWdmAudio);
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_MULTICLIENT, " - start counter asio %ld, start counter acx audio %ld, start counter iso stream %ld", m_startCounterAsio, m_startCounterWdmAudio, m_startCounterIsoStream);
    }

    ReleaseStreamWaitLock();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
AudioIsochronousEngine::StreamPause(
    bool /* isInput */,
    ULONG /* deviceIndex */
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    _IRQL_limited_to_(PASSIVE_LEVEL);

    AcquireStreamWaitLock();
    // AbortPipes(IsoDirection::In);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_MULTICLIENT, " - start counter asio %ld, start counter acx audio %ld, start counter iso stream %ld", m_startCounterAsio, m_startCounterWdmAudio, m_startCounterIsoStream);
    if (m_startCounterWdmAudio)
    {
        InterlockedDecrement(&m_startCounterWdmAudio);
        if ((m_startCounterAsio == 0) && (m_startCounterWdmAudio == 0))
        {
            status = StopIsoStream();
        }
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_MULTICLIENT, " - start counter asio %ld, start counter acx audio %ld, start counter iso stream %ld", m_startCounterAsio, m_startCounterWdmAudio, m_startCounterIsoStream);
    }

    ReleaseStreamWaitLock();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
AudioIsochronousEngine::StreamGetCurrentPacket(
    bool   isInput,
    ULONG  deviceIndex,
    PULONG currentPacket
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    _IRQL_limited_to_(PASSIVE_LEVEL);

    IF_TRUE_ACTION_JUMP(m_rtPacketObject == nullptr, status = STATUS_INVALID_PARAMETER, StreamGetCurrentPacket_Exit);
    IF_TRUE_ACTION_JUMP(currentPacket == nullptr, status = STATUS_INVALID_PARAMETER, StreamGetCurrentPacket_Exit);

    status = m_rtPacketObject->GetCurrentPacket(isInput, deviceIndex, currentPacket);

StreamGetCurrentPacket_Exit:

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
AudioIsochronousEngine::StreamResetCurrentPacket(
    bool  isInput,
    ULONG deviceIndex
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    _IRQL_limited_to_(PASSIVE_LEVEL);

    IF_TRUE_ACTION_JUMP(m_rtPacketObject == nullptr, status = STATUS_INVALID_PARAMETER, StreamResetCurrentPacket_Exit);

    status = m_rtPacketObject->ResetCurrentPacket(isInput, deviceIndex);

StreamResetCurrentPacket_Exit:

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
AudioIsochronousEngine::StreamResetInternal(
    bool  isInput,
    ULONG deviceIndex
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    _IRQL_limited_to_(PASSIVE_LEVEL);

    IF_TRUE_ACTION_JUMP(m_rtPacketObject == nullptr, status = STATUS_INVALID_PARAMETER, StreamResetInternal_Exit);

    m_rtPacketObject->ResetInternal(isInput, deviceIndex);

StreamResetInternal_Exit:

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
AudioIsochronousEngine::StreamGetCapturePacket(
    ULONG      deviceIndex,
    PULONG     lastCapturePacket,
    PULONGLONG qpcPacketStart
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    _IRQL_limited_to_(PASSIVE_LEVEL);

    IF_TRUE_ACTION_JUMP(m_rtPacketObject == nullptr, status = STATUS_INVALID_PARAMETER, StreamGetCapturePacket_Exit);
    IF_TRUE_ACTION_JUMP(lastCapturePacket == nullptr, status = STATUS_INVALID_PARAMETER, StreamGetCapturePacket_Exit);
    IF_TRUE_ACTION_JUMP(qpcPacketStart == nullptr, status = STATUS_INVALID_PARAMETER, StreamGetCapturePacket_Exit);

    status = m_rtPacketObject->GetCapturePacket(deviceIndex, lastCapturePacket, qpcPacketStart);

StreamGetCapturePacket_Exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
AudioIsochronousEngine::StreamGetPresentationPosition(
    bool       isInput,
    ULONG      deviceIndex,
    PULONGLONG positionInBlocks,
    PULONGLONG qpcPosition
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    _IRQL_limited_to_(PASSIVE_LEVEL);

    IF_TRUE_ACTION_JUMP(m_rtPacketObject == nullptr, status = STATUS_INVALID_PARAMETER, StreamGetCapturePacket_Exit);
    IF_TRUE_ACTION_JUMP(positionInBlocks == nullptr, status = STATUS_INVALID_PARAMETER, StreamGetCapturePacket_Exit);
    IF_TRUE_ACTION_JUMP(qpcPosition == nullptr, status = STATUS_INVALID_PARAMETER, StreamGetCapturePacket_Exit);

    status = m_rtPacketObject->GetPresentationPosition(isInput, deviceIndex, positionInBlocks, qpcPosition);

StreamGetCapturePacket_Exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
bool AudioIsochronousEngine::HasAsioOwnership()
{
    bool hasAsioOwnership = false;

    PAGED_CODE();

    AcquireStreamWaitLock();

    hasAsioOwnership = (m_asioOwner != nullptr);

    ReleaseStreamWaitLock();

    return hasAsioOwnership;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
AudioIsochronousEngine::GetAudioProperty(
    UAC_AUDIO_PROPERTY & audioProperty
)
{
    PAGED_CODE();

    m_audioStreamPropertySet.AudioProperty.InputDriverBuffer = m_usbLatency.InputDriverBuffer;
    m_audioStreamPropertySet.AudioProperty.OutputDriverBuffer = m_usbLatency.OutputDriverBuffer;

    audioProperty = m_audioStreamPropertySet.AudioProperty;

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - InputAsioChannels  %d", audioProperty.InputAsioChannels);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - OutputAsioChannels %d", audioProperty.OutputAsioChannels);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - IsAccessible    %!bool!", audioProperty.IsAccessible);

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
AudioIsochronousEngine::GetChannelInfo(
    PUAC_GET_CHANNEL_INFO_CONTEXT channelInfo,
    ULONG                         contextSize,
    ULONG &                       minValueSize
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    ULONG numChannels = m_audioStreamPropertySet.AudioProperty.InputAsioChannels + m_audioStreamPropertySet.AudioProperty.OutputAsioChannels;
    minValueSize = offsetof(UAC_GET_CHANNEL_INFO_CONTEXT, Channel) + (sizeof(UAC_CHANNEL_INFO) * numChannels);

    if (contextSize == 0)
    {
        status = STATUS_BUFFER_OVERFLOW;
    }
    else if (contextSize < minValueSize)
    {
        minValueSize = 0;
        status = STATUS_BUFFER_TOO_SMALL;
    }
    else
    {
        channelInfo->NumChannels = numChannels;
        BOOL  input = m_usbAudioStreamInterfaceGroup->HasInputIsochronousInterface() ? TRUE : FALSE;
        ULONG asioCh = 0;
        for (ULONG i = 0; i < numChannels; ++i)
        {
            RtlStringCchCopyW(channelInfo->Channel[i].Name, UAC_MAX_CHANNEL_NAME_LENGTH, input ? m_inputAsioChannelName[asioCh] : m_outputAsioChannelName[asioCh]);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - channel info. channel name [%d] %ws", i, channelInfo->Channel[i].Name);
            channelInfo->Channel[i].Index = asioCh;
            channelInfo->Channel[i].IsInput = input;
            channelInfo->Channel[i].IsActive = 0;     // not used
            channelInfo->Channel[i].ChannelGroup = 0; // not used
            ++asioCh;
            if (input && asioCh >= m_audioStreamPropertySet.AudioProperty.InputAsioChannels)
            {
                input = FALSE;
                asioCh = 0;
            }
        }
        status = STATUS_SUCCESS;
    }
    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::GetClockInfo(
    _In_ PUAC_GET_CLOCK_INFO_CONTEXT clockInfo,
    _In_ ULONG                       contextSize,
    _Out_ ULONG &                    minValueSize
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    ULONG numClockSources = 1;
    minValueSize = offsetof(UAC_GET_CLOCK_INFO_CONTEXT, ClockSource) + (sizeof(UAC_CLOCK_INFO) * numClockSources);

    if (contextSize == 0)
    {
        status = STATUS_BUFFER_OVERFLOW;
    }
    else if (contextSize < minValueSize)
    {
        minValueSize = 0;
        status = STATUS_BUFFER_TOO_SMALL;
    }
    else
    {
        clockInfo->NumClockSource = numClockSources;
        for (ULONG i = 0; i < numClockSources; ++i)
        {
            clockInfo->ClockSource[i].Index = i;
            clockInfo->ClockSource[i].AssociatedChannel = 0; // not used
            clockInfo->ClockSource[i].AssociatedGroup = 0;   // not used
            clockInfo->ClockSource[i].IsCurrentSource = 1;
            clockInfo->ClockSource[i].IsLocked = 0;          // not used
            RtlStringCchCopyW(clockInfo->ClockSource[i].Name, UAC_MAX_CLOCK_SOURCE_NAME_LENGTH, L"Internal");
        }
        status = STATUS_SUCCESS;
    }
    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::SetClockSource(
    PUAC_SET_CLOCK_SOURCE_CONTEXT /* clockSource */,
    ULONG /* contextSize */,
    ULONG & minValueSize
)
{
    PAGED_CODE();

    minValueSize = 0;

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::SetSampleFormat(
    UACSampleFormat sampleFormat
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    if ((m_audioStreamPropertySet.AudioProperty.SupportedSampleFormats & (1 << toULong(sampleFormat))) == 0)
    {
        status = STATUS_INVALID_PARAMETER;
    }
    else if (sampleFormat == m_audioStreamPropertySet.AudioProperty.CurrentSampleFormat)
    {
        status = STATUS_SUCCESS;
    }
    else
    {
        ULONG formatType = 0;
        ULONG format = 0;

        AcquireStreamWaitLock();

        StopIsoStream();
        m_audioStreamPropertySet.DesiredSampleFormat = sampleFormat;
        status = USBAudioDataFormat::ConvertFormatToSampleFormat(sampleFormat, formatType, format);
        if (NT_SUCCESS(status))
        {
            status = ActivateAudioInterface(m_audioStreamPropertySet.AudioProperty.SampleRate, formatType, format, m_audioStreamPropertySet.InputProperty.BytesPerSample, m_audioStreamPropertySet.InputProperty.ValidBitsPerSample, m_audioStreamPropertySet.OutputProperty.BytesPerSample, m_audioStreamPropertySet.OutputProperty.ValidBitsPerSample);
        }
        ReleaseStreamWaitLock();
        status = STATUS_SUCCESS;
    }
    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::ChangeSampleRate(
    ULONG desiredSampleRate
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    bool streamRunning = false;
    AcquireStreamWaitLock();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_MULTICLIENT, " - start counter asio %ld, start counter acx audio %ld, start counter iso stream %ld", m_startCounterAsio, m_startCounterWdmAudio, m_startCounterIsoStream);
    if (m_streamObject != nullptr)
    {
        AcquireAsioWaitLock();
        if (m_asioBufferObject == nullptr)
        {
            streamRunning = true;
        }
        ReleaseAsioWaitLock();
        if ((m_startCounterAsio != 0) || (m_startCounterWdmAudio != 0))
        {
            StopIsoStream();
        }
    }
    ACXDATAFORMAT inputDataFormatBeforeChange = nullptr;
    ACXDATAFORMAT outputDataFormatBeforeChange = nullptr;
    ACXDATAFORMAT inputDataFormatAfterChange = nullptr;
    ACXDATAFORMAT outputDataFormatAfterChange = nullptr;

    if (m_usbAudioStreamInterfaceGroup->HasInputIsochronousInterface())
    {
        status = GetCurrentDataFormat(true, inputDataFormatBeforeChange);
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);
    }
    if (m_usbAudioStreamInterfaceGroup->HasOutputIsochronousInterface())
    {
        status = GetCurrentDataFormat(false, outputDataFormatBeforeChange);
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);
    }
    if (NT_SUCCESS(status))
    {
        ULONG desiredFormatType = NS_USBAudio0200::FORMAT_TYPE_I;
        ULONG desiredFormat = NS_USBAudio0200::PCM;

        USBAudioDataFormat::ConvertFormatToSampleFormat(m_audioStreamPropertySet.AudioProperty.CurrentSampleFormat, desiredFormatType, desiredFormat);

        status = ActivateAudioInterface(desiredSampleRate, desiredFormatType, desiredFormat, m_audioStreamPropertySet.InputProperty.BytesPerSample, m_audioStreamPropertySet.InputProperty.ValidBitsPerSample, m_audioStreamPropertySet.OutputProperty.BytesPerSample, m_audioStreamPropertySet.OutputProperty.ValidBitsPerSample);
        if (streamRunning && NT_SUCCESS(status))
        {
            if ((m_startCounterAsio != 0) || (m_startCounterWdmAudio != 0))
            {
                StartIsoStream();
            }
        }
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_MULTICLIENT, " - start counter asio %ld, start counter acx audio %ld, start counter iso stream %ld", m_startCounterAsio, m_startCounterWdmAudio, m_startCounterIsoStream);
    }

    IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

    if (m_usbAudioStreamInterfaceGroup->HasOutputIsochronousInterface() && (outputDataFormatBeforeChange != nullptr))
    {
        status = GetCurrentDataFormat(false, outputDataFormatAfterChange);
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

        status = NotifyAllPinsDataFormatChange(false, outputDataFormatBeforeChange, outputDataFormatAfterChange);
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);
    }
    if (m_usbAudioStreamInterfaceGroup->HasInputIsochronousInterface() && (inputDataFormatBeforeChange != nullptr))
    {
        status = GetCurrentDataFormat(true, inputDataFormatAfterChange);
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

        status = NotifyAllPinsDataFormatChange(true, inputDataFormatBeforeChange, inputDataFormatAfterChange);
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);
    }

Exit_BeforeWaitLockRelease:
    ReleaseStreamWaitLock();

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::GetAsioOwnership(
    WDFFILEOBJECT fileObject
)
{
    NTSTATUS      status = STATUS_SUCCESS;
    LARGE_INTEGER systemTime = {0};

    PAGED_CODE();

    KeQuerySystemTime(&systemTime);
    if (m_asioOwner != nullptr || systemTime.QuadPart < m_deviceContext->ResetEnableTime.QuadPart) //
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

        AcquireStreamWaitLock();

        if (m_usbAudioStreamInterfaceGroup->HasInputIsochronousInterface())
        {
            status = GetCurrentDataFormat(true, inputDataFormatBeforeChange);
            IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);
        }
        if (m_usbAudioStreamInterfaceGroup->HasOutputIsochronousInterface())
        {
            status = GetCurrentDataFormat(false, outputDataFormatBeforeChange);
            IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);
        }
        status = USBAudioDataFormat::ConvertFormatToSampleFormat(m_audioStreamPropertySet.AudioProperty.CurrentSampleFormat, desiredFormatType, desiredFormat);
        IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

        if (!m_deviceContext->UsbAudioConfiguration->IsEnableASIO())
        {
            status = STATUS_INVALID_DEVICE_REQUEST;
            IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);
        }

        if (m_audioStreamPropertySet.AudioProperty.SupportedSampleFormats & (1 << toULong(UACSampleFormat::UAC_SAMPLE_FORMAT_IEEE_FLOAT)))
        {
            m_audioStreamPropertySet.SampleFormatBackup = m_audioStreamPropertySet.AudioProperty.CurrentSampleFormat;
            desiredFormatType = NS_USBAudio0200::FORMAT_TYPE_I;
            desiredFormat = NS_USBAudio0200::IEEE_FLOAT;
        }
        else if (m_audioStreamPropertySet.AudioProperty.SupportedSampleFormats & (1 << toULong(UACSampleFormat::UAC_SAMPLE_FORMAT_PCM)))
        {
            m_audioStreamPropertySet.SampleFormatBackup = m_audioStreamPropertySet.AudioProperty.CurrentSampleFormat;
            desiredFormatType = NS_USBAudio0200::FORMAT_TYPE_I;
            desiredFormat = NS_USBAudio0200::PCM;
        }

        if (m_usbAudioStreamInterfaceGroup->HasInputIsochronousInterface())
        {
            status = m_usbAudioStreamInterfaceGroup->GetMaxSupportedValidBitsPerSample(true, desiredFormatType, desiredFormat, inputBytesPerSample, inputValidBitsPerSample);
            IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);
        }
        if (m_usbAudioStreamInterfaceGroup->HasOutputIsochronousInterface())
        {
            status = m_usbAudioStreamInterfaceGroup->GetMaxSupportedValidBitsPerSample(false, desiredFormatType, desiredFormat, outputBytesPerSample, outputValidBitsPerSample);
            IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);
        }
        //
        // When using ASIO, the maximum bit depth is used independently for input and output.
        //
        status = ActivateAudioInterface(m_audioStreamPropertySet.AudioProperty.SampleRate, desiredFormatType, desiredFormat, inputBytesPerSample, inputValidBitsPerSample, outputBytesPerSample, outputValidBitsPerSample);

        if (fileObject != nullptr)
        {
            m_asioOwner = fileObject;

            PFILE_CONTEXT fileContext = GetFileContext(fileObject);
            if (fileContext != nullptr)
            {
                fileContext->DeviceContext = m_deviceContext;
            }
            status = STATUS_SUCCESS;
        }
        else
        {
            status = STATUS_INVALID_DEVICE_REQUEST;
        }
        if (m_usbAudioStreamInterfaceGroup->HasOutputIsochronousInterface() && (outputDataFormatBeforeChange != nullptr))
        {
            status = GetCurrentDataFormat(false, outputDataFormatAfterChange);
            IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

            status = NotifyAllPinsDataFormatChange(false, outputDataFormatBeforeChange, outputDataFormatAfterChange);
            IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);
        }
        if (m_usbAudioStreamInterfaceGroup->HasInputIsochronousInterface() && (inputDataFormatBeforeChange != nullptr))
        {
            status = GetCurrentDataFormat(true, inputDataFormatAfterChange);
            IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);

            status = NotifyAllPinsDataFormatChange(true, inputDataFormatBeforeChange, inputDataFormatAfterChange);
            IF_FAILED_JUMP(status, Exit_BeforeWaitLockRelease);
        }
    Exit_BeforeWaitLockRelease:
        ReleaseStreamWaitLock();
    }
    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::StartAsioStream()
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    AcquireStreamWaitLock();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_MULTICLIENT, " - start counter asio %ld, start counter acx audio %ld, start counter iso stream %ld", m_startCounterAsio, m_startCounterWdmAudio, m_startCounterIsoStream);
    if (m_startCounterAsio == 0)
    {
        if (m_startCounterWdmAudio == 0)
        {
            status = StartIsoStream();
        }
        else
        {
            AcquireAsioWaitLock();
            if (m_asioBufferObject != nullptr)
            {
                m_asioBufferObject->SetReady();
                status = STATUS_SUCCESS;
            }
            else
            {
                status = STATUS_UNSUCCESSFUL;
            }
            ReleaseAsioWaitLock();
        }
        if (NT_SUCCESS(status))
        {
            InterlockedIncrement(&m_startCounterAsio);
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_MULTICLIENT, " - start counter asio %ld, start counter acx audio %ld, start counter iso stream %ld", m_startCounterAsio, m_startCounterWdmAudio, m_startCounterIsoStream);
        }
    }
    else
    {
        status = STATUS_SUCCESS;
    }
    ReleaseStreamWaitLock();
    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::StopAsioStream()
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    AcquireStreamWaitLock();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_MULTICLIENT, " - start counter asio %ld, start counter acx audio %ld, start counter iso stream %ld", m_startCounterAsio, m_startCounterWdmAudio, m_startCounterIsoStream);
    if (m_startCounterAsio)
    {
        InterlockedDecrement(&m_startCounterAsio);
        if ((m_startCounterAsio == 0) && (m_startCounterWdmAudio == 0))
        {
            status = StopIsoStream();
        }
        else
        {
            status = STATUS_SUCCESS;
        }
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_MULTICLIENT, " - start counter asio %ld, start counter acx audio %ld, start counter iso stream %ld", m_startCounterAsio, m_startCounterWdmAudio, m_startCounterIsoStream);
    }
    else
    {
        status = STATUS_SUCCESS;
    }

    ReleaseStreamWaitLock();

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::SetAsioBuffer(
    ULONG recBufferLength,
    PBYTE recBuffer,
    ULONG recBufferOffset,
    ULONG playBufferLength,
    PBYTE playBuffer,
    ULONG playBufferOffset
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    AcquireAsioWaitLock();

    IF_TRUE_ACTION_JUMP((m_asioBufferOwner != nullptr) || (m_asioBufferObject != nullptr),
                        status = STATUS_DEVICE_BUSY;
                        , Exit);

    m_asioBufferObject = AsioBufferObject::Create(m_deviceContext, this);
    IF_TRUE_ACTION_JUMP(m_asioBufferObject == nullptr, STATUS_INSUFFICIENT_RESOURCES, Exit);

    status = m_asioBufferObject->SetBuffer(recBufferLength, recBuffer, recBufferOffset, playBufferLength, playBuffer, playBufferOffset);
Exit:
    ReleaseAsioWaitLock();

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::UnsetAsioBuffer()
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    AcquireAsioWaitLock();
    if (m_asioBufferObject != nullptr)
    {
        status = m_asioBufferObject->UnsetBuffer();
        delete m_asioBufferObject;
        m_asioBufferObject = nullptr;
    }
    else
    {
        status = STATUS_SUCCESS;
    }
    ReleaseAsioWaitLock();

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::ReleaseAsioOwnership(
    WDFFILEOBJECT fileObject
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    AcquireStreamWaitLock();

    if (m_asioOwner != nullptr)
    {
        if (m_asioOwner == fileObject)
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "clear asio owner");
            m_asioOwner = nullptr;
        }
    }
    status = STATUS_SUCCESS;

    if ((m_audioStreamPropertySet.AudioProperty.SupportedSampleFormats & (1 << toULong(UACSampleFormat::UAC_SAMPLE_FORMAT_IEEE_FLOAT))) && (m_audioStreamPropertySet.SampleFormatBackup != m_audioStreamPropertySet.AudioProperty.CurrentSampleFormat))
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

        if (m_usbAudioStreamInterfaceGroup->HasInputIsochronousInterface())
        {
            status = GetCurrentDataFormat(true, inputDataFormatBeforeChange);
            IF_FAILED_JUMP(status, Exit);
        }
        if (m_usbAudioStreamInterfaceGroup->HasOutputIsochronousInterface())
        {
            status = GetCurrentDataFormat(false, outputDataFormatBeforeChange);
            IF_FAILED_JUMP(status, Exit);
        }
        status = USBAudioDataFormat::ConvertFormatToSampleFormat(m_audioStreamPropertySet.SampleFormatBackup, desiredFormatType, desiredFormat);
        IF_FAILED_JUMP(status, Exit);

        if (m_usbAudioStreamInterfaceGroup->HasInputIsochronousInterface())
        {
            status = m_usbAudioStreamInterfaceGroup->GetMaxSupportedValidBitsPerSample(true, desiredFormatType, desiredFormat, inputBytesPerSample, inputValidBitsPerSample);
            IF_FAILED_JUMP(status, Exit);
        }
        if (m_usbAudioStreamInterfaceGroup->HasOutputIsochronousInterface())
        {
            status = m_usbAudioStreamInterfaceGroup->GetMaxSupportedValidBitsPerSample(false, desiredFormatType, desiredFormat, outputBytesPerSample, outputValidBitsPerSample);
            IF_FAILED_JUMP(status, Exit);
        }
        status = ActivateAudioInterface(m_audioStreamPropertySet.AudioProperty.SampleRate, desiredFormatType, desiredFormat, inputBytesPerSample, inputValidBitsPerSample, outputBytesPerSample, outputValidBitsPerSample);
        IF_FAILED_JUMP(status, Exit);

        if (m_usbAudioStreamInterfaceGroup->HasOutputIsochronousInterface() && (outputDataFormatBeforeChange != nullptr))
        {
            status = GetCurrentDataFormat(false, outputDataFormatAfterChange);
            IF_FAILED_JUMP(status, Exit);

            status = NotifyAllPinsDataFormatChange(false, outputDataFormatBeforeChange, outputDataFormatAfterChange);
            IF_FAILED_JUMP(status, Exit);
        }
        if (m_usbAudioStreamInterfaceGroup->HasInputIsochronousInterface() && (inputDataFormatBeforeChange != nullptr))
        {
            status = GetCurrentDataFormat(true, inputDataFormatAfterChange);
            IF_FAILED_JUMP(status, Exit);

            status = NotifyAllPinsDataFormatChange(true, inputDataFormatBeforeChange, inputDataFormatAfterChange);
            IF_FAILED_JUMP(status, Exit);
        }
    }

Exit:
    ReleaseStreamWaitLock();

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::GetBufferPeriod(
    _Out_ ULONG & bufferPeriod
)
{
    PAGED_CODE();

    AcquireStreamWaitLock();

    bufferPeriod = m_audioStreamPropertySet.InternalParameters.SuggestedBufferPeriod;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "BufferPeriod = %u", bufferPeriod);

    ReleaseStreamWaitLock();

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::SetBufferPeriod(
    ULONG bufferPeriod
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    AcquireStreamWaitLock();

    if (bufferPeriod != m_audioStreamPropertySet.InternalParameters.SuggestedBufferPeriod)
    {

        if ((m_startCounterAsio != 0) || (m_startCounterWdmAudio != 0))
        {
            StopIsoStream();
        }

        status = UpdateFramePerIrp(bufferPeriod);
        ASSERT(NT_SUCCESS(status));

        status = UpdateBufferOperationOffset(bufferPeriod);
        ASSERT(NT_SUCCESS(status));

        m_audioStreamPropertySet.InternalParameters.SuggestedBufferPeriod = bufferPeriod;

        status = ActivateAudioInterface(
            m_audioStreamPropertySet.AudioProperty.SampleRate,
            NS_USBAudio0200::FORMAT_TYPE_I,
            NS_USBAudio0200::PCM,
            m_audioStreamPropertySet.InputProperty.BytesPerSample,
            m_audioStreamPropertySet.InputProperty.ValidBitsPerSample,
            m_audioStreamPropertySet.OutputProperty.BytesPerSample,
            m_audioStreamPropertySet.OutputProperty.ValidBitsPerSample
        );

        if (NT_SUCCESS(status))
        {
            if ((m_startCounterAsio != 0) || (m_startCounterWdmAudio != 0))
            {
                StartIsoStream();
            }
            else
            {
                ASSERT(NT_SUCCESS(status));
            }
        }
    }
    else
    {
        // Nothing is done because there is no change in flag.
        status = STATUS_SUCCESS;
    }

    ReleaseStreamWaitLock();

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::GetInputLatency(
    LONG & inputLatency
)
{
    PAGED_CODE();

    AcquireStreamWaitLock();

    inputLatency = m_audioStreamPropertySet.InternalParameters.SuggestedBufferPeriod + m_audioStreamPropertySet.AudioProperty.InputLatencyOffset;

    ReleaseStreamWaitLock();

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::GetOutputLatency(
    LONG & outputLatency
)
{
    PAGED_CODE();

    AcquireStreamWaitLock();

    outputLatency = m_audioStreamPropertySet.InternalParameters.SuggestedBufferPeriod + m_audioStreamPropertySet.AudioProperty.OutputLatencyOffset;

    ReleaseStreamWaitLock();

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::SetAsioDevice(
    const WDFSTRING asioDeviceString
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    AcquireStreamWaitLock();

    status = SaveAsioDeviceToRegistry(asioDeviceString);

    ReleaseStreamWaitLock();

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::GetAsioDevice(
    WDFSTRING & asioDeviceString
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    AcquireStreamWaitLock();

    status = LoadAsioDeviceFromRegistry(asioDeviceString);

    ReleaseStreamWaitLock();

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
AudioIsochronousEngine::NotifyAllPinsDataFormatChange(
    bool          isInput,
    ACXDATAFORMAT dataFormatBeforeChange,
    ACXDATAFORMAT dataFormatAfterChange
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    if (isInput)
    {
        if ((m_captureCircuit != nullptr) && (dataFormatBeforeChange != nullptr) && (dataFormatAfterChange != nullptr) && !AcxDataFormatIsEqual(dataFormatBeforeChange, dataFormatAfterChange))
        {
            for (ULONG captureDeviceIndex = 0; captureDeviceIndex < m_numOfInputDevices; captureDeviceIndex++)
            {
                ACXPIN pin = AcxCircuitGetPinById(m_captureCircuit, captureDeviceIndex * CodecCapturePinCount + CodecCaptureHostPin);
                if (pin != nullptr)
                {
                    status = NotifyDataFormatChange(m_deviceContext->Device, m_captureCircuit, pin, dataFormatAfterChange);
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - capture pin %u, AcxPinNotifyDataFormatChange %!STATUS!", captureDeviceIndex * 2, status);
                    IF_FAILED_JUMP(status, Exit);
                }
            }
        }
    }
    else
    {
        if ((m_renderCircuit != nullptr) && (dataFormatBeforeChange != nullptr) && (dataFormatAfterChange != nullptr) && !AcxDataFormatIsEqual(dataFormatBeforeChange, dataFormatAfterChange))
        {
            for (ULONG renderDeviceIndex = 0; renderDeviceIndex < m_numOfOutputDevices; renderDeviceIndex++)
            {
                ACXPIN pin = AcxCircuitGetPinById(m_renderCircuit, renderDeviceIndex * CodecRenderPinCount + CodecRenderHostPin);
                if (pin != nullptr)
                {
                    status = NotifyDataFormatChange(m_deviceContext->Device, m_renderCircuit, pin, dataFormatAfterChange);
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
NTSTATUS AudioIsochronousEngine::FileCleanup(
    WDFFILEOBJECT fileObject
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    AcquireStreamWaitLock();
    if (fileObject == m_asioOwner)
    {
        StopIsoStream();

        AcquireAsioWaitLock();
        if (m_asioBufferObject != nullptr)
        {
            status = m_asioBufferObject->UnsetBuffer();
            delete m_asioBufferObject;
            m_asioBufferObject = nullptr;
        }
        ReleaseAsioWaitLock();

        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "clear asio owner");
        m_asioOwner = nullptr;
    }
    ReleaseStreamWaitLock();

    return status;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void AudioIsochronousEngine::AcquireAsioWaitLock()
{
    WdfWaitLockAcquire(m_asioWaitLock, nullptr);
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void AudioIsochronousEngine::ReleaseAsioWaitLock()
{
    WdfWaitLockRelease(m_asioWaitLock);
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void AudioIsochronousEngine::AcquireStreamWaitLock()
{
    WdfWaitLockAcquire(m_streamWaitLock, nullptr);
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void AudioIsochronousEngine::ReleaseStreamWaitLock()
{
    WdfWaitLockRelease(m_streamWaitLock);
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void AudioIsochronousEngine::AcquireStreamEngineWaitLock()
{
    WdfWaitLockAcquire(m_streamEngineWaitLock, nullptr);
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void AudioIsochronousEngine::ReleaseStreamEngineWaitLock()
{
    WdfWaitLockRelease(m_streamEngineWaitLock);
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
const AUDIO_STREAM_PROPERTY_SET &
AudioIsochronousEngine::GetAudioStreamPropertySet()
{
    return m_audioStreamPropertySet;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
const SELECTED_INTERFACE_AND_PIPE &
AudioIsochronousEngine::GetInputInterfaceAndPipe()
{
    return m_inputInterfaceAndPipe;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
const SELECTED_INTERFACE_AND_PIPE &
AudioIsochronousEngine::GetOutputInterfaceAndPipe()
{
    return m_outputInterfaceAndPipe;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
const SELECTED_INTERFACE_AND_PIPE &
AudioIsochronousEngine::GetFeedbackInterfaceAndPipe()
{
    return m_feedbackInterfaceAndPipe;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
const UAC_USB_LATENCY &
AudioIsochronousEngine::GetUsbLatency()
{
    return m_usbLatency;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
ContiguousMemory *
AudioIsochronousEngine::GetContiguousMemory() const noexcept
{
    return m_contiguousMemory;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
AsioBufferObject *
AudioIsochronousEngine::GetAsioBufferObject() const noexcept
{
    return m_asioBufferObject;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
RtPacketObject * AudioIsochronousEngine::GetRtPacketObject() const noexcept
{
    return m_rtPacketObject;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
StreamObject * AudioIsochronousEngine::GetStreamObject() const noexcept
{
    return m_streamObject;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
CStreamEngine *
AudioIsochronousEngine::GetCaptureStreamEngine(
    ULONG deviceIndex
) const noexcept
{
    if (deviceIndex < m_numOfInputDevices)
    {
        return m_captureStreamEngine[deviceIndex];
    }
    return nullptr;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
CStreamEngine *
AudioIsochronousEngine::GetRenderStreamEngine(
    ULONG deviceIndex
) const noexcept
{
    if (deviceIndex < m_numOfOutputDevices)
    {
        return m_renderStreamEngine[deviceIndex];
    }
    return nullptr;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
const ULONG AudioIsochronousEngine::GetNumOfInputDevices()
{
    return m_numOfInputDevices;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
const ULONG AudioIsochronousEngine::GetNumOfOutputDevices()
{
    return m_numOfOutputDevices;
}

PAGED_CODE_SEG
_Use_decl_annotations_
bool AudioIsochronousEngine::IsValidInternalParameters(
    const INTERNAL_PARAMETERS & internalParameters
)
{
    bool isValid = false;

    PAGED_CODE();

    if ((internalParameters.FirstPacketLatency > USBD_ISO_START_FRAME_RANGE) ||
        /* (internalParameters.ClassicFramesPerIrp < UAC_MIN_CLASSIC_FRAMES_PER_IRP) || */
        (internalParameters.ClassicFramesPerIrp > UAC_MAX_CLASSIC_FRAMES_PER_IRP) ||
        (internalParameters.MaxIrpNumber < UAC_MIN_MAX_IRP_NUMBER) ||
        (internalParameters.MaxIrpNumber > UAC_MAX_IRP_NUMBER) ||
        (internalParameters.PreSendFrames > UAC_MAX_PRE_SEND_FRAMES) ||
        (internalParameters.OutputFrameDelay < UAC_MIN_OUTPUT_FRAME_DELAY) ||
        (internalParameters.OutputFrameDelay > UAC_MAX_OUTPUT_FRAME_DELAY) ||
        //(internalParameters.BufferOperationThread > UAC_MAX_BUFFER_OPERATION_THREAD) ||
        ((internalParameters.InputBufferOperationOffset & 0xfffffff) > UAC_MAX_CLASSIC_FRAMES_PER_IRP * UAC_MAX_IRP_NUMBER * 8) ||
        (internalParameters.InputHubOffset > UAC_MAX_CLASSIC_FRAMES_PER_IRP * UAC_MAX_IRP_NUMBER * 8) ||
        ((internalParameters.OutputBufferOperationOffset & 0xfffffff) > UAC_MAX_CLASSIC_FRAMES_PER_IRP * UAC_MAX_IRP_NUMBER * 8) ||
        (internalParameters.OutputHubOffset > UAC_MAX_CLASSIC_FRAMES_PER_IRP * UAC_MAX_IRP_NUMBER * 8) ||
        (internalParameters.BufferThreadPriority > HIGH_PRIORITY))
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
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::UpdateFramePerIrp(
    ULONG bufferPeriod
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC!");

    int bufferSizeIndex = 0;
    for (; bufferSizeIndex < (ARRAYSIZE(g_DriverSettingsTable) - 1); ++bufferSizeIndex)
    {
        if (g_DriverSettingsTable[bufferSizeIndex].PeriodFrames == bufferPeriod)
        {
            break;
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "ClassicFramesPerIrp  %u -> %u", m_audioStreamPropertySet.InternalParameters.ClassicFramesPerIrp, g_DriverSettingsTable[bufferSizeIndex].Parameter.ClassicFramesPerIrp);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "ClassicFramesPerIrp2 %u -> %u", m_audioStreamPropertySet.InternalParameters.ClassicFramesPerIrp2, g_DriverSettingsTable[bufferSizeIndex].Parameter.ClassicFramesPerIrp2);

    m_audioStreamPropertySet.InternalParameters.ClassicFramesPerIrp = g_DriverSettingsTable[bufferSizeIndex].Parameter.ClassicFramesPerIrp;
    m_audioStreamPropertySet.InternalParameters.ClassicFramesPerIrp2 = g_DriverSettingsTable[bufferSizeIndex].Parameter.ClassicFramesPerIrp2;

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::UpdateBufferOperationOffset(
    ULONG bufferPeriod
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC!");

    int bufferSizeIndex = 0;

    for (; bufferSizeIndex < (ARRAYSIZE(g_DriverSettingsTable) - 1); ++bufferSizeIndex)
    {
        if (g_DriverSettingsTable[bufferSizeIndex].PeriodFrames == bufferPeriod)
        {
            break;
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "OutputBufferOperationOffset  0x%08x -> 0x%08x", m_audioStreamPropertySet.InternalParameters.OutputBufferOperationOffset, g_DriverSettingsTable[bufferSizeIndex].Parameter.OutputBufferOperationOffset);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "InputBufferOperationOffset   0x%08x -> 0x%08x", m_audioStreamPropertySet.InternalParameters.InputBufferOperationOffset, g_DriverSettingsTable[bufferSizeIndex].Parameter.InputBufferOperationOffset);

    m_audioStreamPropertySet.InternalParameters.OutputBufferOperationOffset = g_DriverSettingsTable[bufferSizeIndex].Parameter.OutputBufferOperationOffset;
    m_audioStreamPropertySet.InternalParameters.InputBufferOperationOffset = g_DriverSettingsTable[bufferSizeIndex].Parameter.InputBufferOperationOffset;

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::LoadInternalParametersFromDeviceRegistry()
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC!");

    {
        WDFKEY registryKey = nullptr;
        WDFKEY registrySubKey = nullptr;

        auto exitProcess = wil::scope_exit(
            [&]() {
                if (registrySubKey != nullptr)
                {
                    WdfRegistryClose(registrySubKey);
                    registrySubKey = nullptr;
                }

                if (registryKey != nullptr)
                {
                    WdfRegistryClose(registryKey);
                    registryKey = nullptr;
                }
            }
        );

        RETURN_NTSTATUS_IF_FAILED(WdfDeviceOpenRegistryKey(m_deviceContext->Device, PLUGPLAY_REGKEY_DEVICE, KEY_READ | KEY_WRITE, WDF_NO_OBJECT_ATTRIBUTES, &registryKey));

        RETURN_NTSTATUS_IF_FAILED(OpenSubRegistryKey(registryKey, registrySubKey));

        struct NameAndDataAddress
        {
            const WCHAR * name;
            DWORD *       dataAddless;
        } internalParametersNameAndDataAddressTable[] = {
            {c_FirstPacketLatencyName, reinterpret_cast<DWORD *>(&m_audioStreamPropertySet.InternalParameters.FirstPacketLatency)},
            {c_ClassicFramesPerIrpName, reinterpret_cast<DWORD *>(&m_audioStreamPropertySet.InternalParameters.ClassicFramesPerIrp)},
            {c_MaxIrpNumberName, reinterpret_cast<DWORD *>(&m_audioStreamPropertySet.InternalParameters.MaxIrpNumber)},
            {c_PreSendFramesName, reinterpret_cast<DWORD *>(&m_audioStreamPropertySet.InternalParameters.PreSendFrames)},
            {c_OutputFrameDelayName, reinterpret_cast<DWORD *>(&m_audioStreamPropertySet.InternalParameters.OutputFrameDelay)},
            {c_DelayedOutputBufferSwitchName, reinterpret_cast<DWORD *>(&m_audioStreamPropertySet.InternalParameters.DelayedOutputBufferSwitch)},
            {c_InputBufferOperationOffsetName, reinterpret_cast<DWORD *>(&m_audioStreamPropertySet.InternalParameters.InputBufferOperationOffset)},
            {c_InputHubOffsetName, reinterpret_cast<DWORD *>(&m_audioStreamPropertySet.InternalParameters.InputHubOffset)},
            {c_OutputBufferOperationOffsetName, reinterpret_cast<DWORD *>(&m_audioStreamPropertySet.InternalParameters.OutputBufferOperationOffset)},
            {c_OutputHubOffsetName, reinterpret_cast<DWORD *>(&m_audioStreamPropertySet.InternalParameters.OutputHubOffset)},
            {c_BufferThreadPriorityName, reinterpret_cast<DWORD *>(&m_audioStreamPropertySet.InternalParameters.BufferThreadPriority)},
            {c_ClassicFramesPerIrp2Name, reinterpret_cast<DWORD *>(&m_audioStreamPropertySet.InternalParameters.ClassicFramesPerIrp2)},
            {c_SuggestedBufferPeriodName, reinterpret_cast<DWORD *>(&m_audioStreamPropertySet.InternalParameters.SuggestedBufferPeriod)},
        };

        const ULONG internalParametersNameAndDataAddressTableSize = SIZEOF_ARRAY(internalParametersNameAndDataAddressTable);

        for (ULONG index = 0; index < internalParametersNameAndDataAddressTableSize; ++index)
        {
            UNICODE_STRING valueName;
            ULONG          resultLength = 0;
            DWORD          value;

            RtlInitUnicodeString(&valueName, internalParametersNameAndDataAddressTable[index].name);

            NTSTATUS status = WdfRegistryQueryValue(
                registrySubKey, // Key
                &valueName,     // ValueName
                sizeof(value),  // ValueLength
                &value,         // Value
                &resultLength,  // ValueLengthQueried
                nullptr         // ValueType
            );

            if (!NT_SUCCESS(status))
            {
                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "index = %u, name = %ls, status = %!STATUS!", index, internalParametersNameAndDataAddressTable[index].name, status);
                break;
            }

            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "index = %u, name = %ls, status = %!STATUS!, value(ULONG) = %u, value(LONG) = %d", index, internalParametersNameAndDataAddressTable[index].name, status, value, value);

            *internalParametersNameAndDataAddressTable[index].dataAddless = value;
        }
    }

    // Check InternalParameters
    bool isValid = IsValidInternalParameters(m_audioStreamPropertySet.InternalParameters);

    if (!isValid)
    {
        m_audioStreamPropertySet.InternalParameters.FirstPacketLatency = UAC_DEFAULT_FIRST_PACKET_LATENCY;
        m_audioStreamPropertySet.InternalParameters.ClassicFramesPerIrp = UAC_DEFAULT_CLASSIC_FRAMES_PER_IRP;
        m_audioStreamPropertySet.InternalParameters.MaxIrpNumber = UAC_DEFAULT_MAX_IRP_NUMBER;
        m_audioStreamPropertySet.InternalParameters.PreSendFrames = UAC_DEFAULT_PRE_SEND_FRAMES;
        m_audioStreamPropertySet.InternalParameters.OutputFrameDelay = UAC_DEFAULT_OUTPUT_FRAME_DELAY;
        m_audioStreamPropertySet.InternalParameters.DelayedOutputBufferSwitch = UAC_DEFAULT_DELAYED_OUTPUT_BUFFER_SWITCH;
        m_audioStreamPropertySet.InternalParameters.InputBufferOperationOffset = UAC_DEFAULT_IN_BUFFER_OPERATION_OFFSET;
        m_audioStreamPropertySet.InternalParameters.InputHubOffset = UAC_DEFAULT_IN_HUB_OFFSET;
        m_audioStreamPropertySet.InternalParameters.OutputBufferOperationOffset = UAC_DEFAULT_OUT_BUFFER_OPERATION_OFFSET;
        m_audioStreamPropertySet.InternalParameters.OutputHubOffset = UAC_DEFAULT_OUT_HUB_OFFSET;
        m_audioStreamPropertySet.InternalParameters.BufferThreadPriority = UAC_DEFAULT_BUFFER_THREAD_PRIORITY;
        m_audioStreamPropertySet.InternalParameters.ClassicFramesPerIrp2 = UAC_DEFAULT_CLASSIC_FRAMES_PER_IRP;
        m_audioStreamPropertySet.InternalParameters.SuggestedBufferPeriod = UAC_DEFAULT_SUGGESTED_BUFFER_PERIOD;

        RETURN_NTSTATUS_IF_FAILED(SaveInternalParametersToDeviceRegistry());
    }

    RETURN_NTSTATUS_IF_FAILED(UpdateFramePerIrp(m_audioStreamPropertySet.InternalParameters.SuggestedBufferPeriod));

    RETURN_NTSTATUS_IF_FAILED(UpdateBufferOperationOffset(m_audioStreamPropertySet.InternalParameters.SuggestedBufferPeriod));

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::SaveInternalParametersToDeviceRegistry()
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC!");

    WDFKEY registryKey = nullptr;
    WDFKEY registrySubKey = nullptr;

    auto exitProcess = wil::scope_exit(
        [&]() {
            if (registrySubKey != nullptr)
            {
                WdfRegistryClose(registrySubKey);
                registrySubKey = nullptr;
            }

            if (registryKey != nullptr)
            {
                WdfRegistryClose(registryKey);
                registryKey = nullptr;
            }
        }
    );

    RETURN_NTSTATUS_IF_FAILED(WdfDeviceOpenRegistryKey(m_deviceContext->Device, PLUGPLAY_REGKEY_DEVICE, KEY_READ | KEY_WRITE, WDF_NO_OBJECT_ATTRIBUTES, &registryKey));

    RETURN_NTSTATUS_IF_FAILED(OpenSubRegistryKey(registryKey, registrySubKey));

    const struct NameAndDataAddress
    {
        const WCHAR * name;
        DWORD *       dataAddless;
    } internalParametersNameAndDataAddressTable[] = {
        {c_FirstPacketLatencyName, reinterpret_cast<DWORD *>(&m_audioStreamPropertySet.InternalParameters.FirstPacketLatency)},
        {c_ClassicFramesPerIrpName, reinterpret_cast<DWORD *>(&m_audioStreamPropertySet.InternalParameters.ClassicFramesPerIrp)},
        {c_MaxIrpNumberName, reinterpret_cast<DWORD *>(&m_audioStreamPropertySet.InternalParameters.MaxIrpNumber)},
        {c_PreSendFramesName, reinterpret_cast<DWORD *>(&m_audioStreamPropertySet.InternalParameters.PreSendFrames)},
        {c_OutputFrameDelayName, reinterpret_cast<DWORD *>(&m_audioStreamPropertySet.InternalParameters.OutputFrameDelay)},
        {c_DelayedOutputBufferSwitchName, reinterpret_cast<DWORD *>(&m_audioStreamPropertySet.InternalParameters.DelayedOutputBufferSwitch)},
        {c_InputBufferOperationOffsetName, reinterpret_cast<DWORD *>(&m_audioStreamPropertySet.InternalParameters.InputBufferOperationOffset)},
        {c_InputHubOffsetName, reinterpret_cast<DWORD *>(&m_audioStreamPropertySet.InternalParameters.InputHubOffset)},
        {c_OutputBufferOperationOffsetName, reinterpret_cast<DWORD *>(&m_audioStreamPropertySet.InternalParameters.OutputBufferOperationOffset)},
        {c_OutputHubOffsetName, reinterpret_cast<DWORD *>(&m_audioStreamPropertySet.InternalParameters.OutputHubOffset)},
        {c_BufferThreadPriorityName, reinterpret_cast<DWORD *>(&m_audioStreamPropertySet.InternalParameters.BufferThreadPriority)},
        {c_ClassicFramesPerIrp2Name, reinterpret_cast<DWORD *>(&m_audioStreamPropertySet.InternalParameters.ClassicFramesPerIrp2)},
        {c_SuggestedBufferPeriodName, reinterpret_cast<DWORD *>(&m_audioStreamPropertySet.InternalParameters.SuggestedBufferPeriod)},
    };

    const ULONG internalParametersNameAndDataAddressTableSize = SIZEOF_ARRAY(internalParametersNameAndDataAddressTable);

    for (ULONG index = 0; index < internalParametersNameAndDataAddressTableSize; ++index)
    {
        UNICODE_STRING valueName;

        RtlInitUnicodeString(&valueName, internalParametersNameAndDataAddressTable[index].name);

        DWORD value = *internalParametersNameAndDataAddressTable[index].dataAddless;

        RETURN_NTSTATUS_IF_FAILED(WdfRegistryAssignValue(registrySubKey, &valueName, REG_DWORD, sizeof(value), &value));

        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "index = %u, name = %ls, value(ULONG) = %u, value(LONG) = %d", index, internalParametersNameAndDataAddressTable[index].name, value, value);
    }

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::SaveAsioDeviceToRegistry(
    const WDFSTRING asioDeviceString
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    NTSTATUS status = STATUS_SUCCESS;
    WDFKEY   registryKey = nullptr;

    auto exitProcess = wil::scope_exit(
        [&]() {
            if (registryKey != nullptr)
            {
                WdfRegistryClose(registryKey);
                registryKey = nullptr;
            }

            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
        }
    );

    RETURN_NTSTATUS_IF_FAILED(WdfRegistryOpenKey(nullptr, &g_RegistryPath, KEY_READ | KEY_WRITE, WDF_NO_OBJECT_ATTRIBUTES, &registryKey));

    UNICODE_STRING valueName;
    RtlInitUnicodeString(&valueName, c_AsioDeviceName);

    RETURN_NTSTATUS_IF_FAILED(WdfRegistryAssignString(registryKey, &valueName, asioDeviceString));

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::LoadAsioDeviceFromRegistry(
    WDFSTRING & asioDeviceString
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    NTSTATUS status = STATUS_SUCCESS;
    WDFKEY   registryKey = nullptr;

    auto exitProcess = wil::scope_exit(
        [&]() {
            if (registryKey != nullptr)
            {
                WdfRegistryClose(registryKey);
                registryKey = nullptr;
            }

            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
        }
    );

    RETURN_NTSTATUS_IF_FAILED(WdfRegistryOpenKey(nullptr, &g_RegistryPath, KEY_READ | KEY_WRITE, WDF_NO_OBJECT_ATTRIBUTES, &registryKey));

    UNICODE_STRING valueName;
    RtlInitUnicodeString(&valueName, c_AsioDeviceName);

    return WdfRegistryQueryString(registryKey, &valueName, asioDeviceString);
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::SaveSampleRateToRegistry(
    WDFDEVICE device,
    ULONG     sampleRate
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    WDFKEY registryKey = nullptr;
    WDFKEY registrySubKey = nullptr;

    auto exitProcess = wil::scope_exit(
        [&]() {
            if (registrySubKey != nullptr)
            {
                WdfRegistryClose(registrySubKey);
                registrySubKey = nullptr;
            }

            if (registryKey != nullptr)
            {
                WdfRegistryClose(registryKey);
                registryKey = nullptr;
            }

            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
        }
    );

    RETURN_NTSTATUS_IF_TRUE(device == nullptr, STATUS_INVALID_PARAMETER);

    RETURN_NTSTATUS_IF_FAILED(WdfDeviceOpenRegistryKey(device, PLUGPLAY_REGKEY_DEVICE, KEY_READ | KEY_WRITE, WDF_NO_OBJECT_ATTRIBUTES, &registryKey));

    RETURN_NTSTATUS_IF_FAILED(OpenSubRegistryKey(registryKey, registrySubKey));

    UNICODE_STRING valueName;
    RtlInitUnicodeString(&valueName, c_SampleRateName);

    return WdfRegistryAssignValue(registrySubKey, &valueName, REG_DWORD, sizeof(sampleRate), &sampleRate);
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::LoadSampleRateFromRegistry(
    WDFDEVICE device,
    ULONG &   sampleRate
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    WDFKEY registryKey = nullptr;
    WDFKEY registrySubKey = nullptr;

    sampleRate = UAC_DEFAULT_SAMPLE_RATE;

    auto exitProcess = wil::scope_exit(
        [&]() {
            if (registrySubKey != nullptr)
            {
                WdfRegistryClose(registrySubKey);
                registrySubKey = nullptr;
            }

            if (registryKey != nullptr)
            {
                WdfRegistryClose(registryKey);
                registryKey = nullptr;
            }

            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
        }
    );

    RETURN_NTSTATUS_IF_TRUE(device == nullptr, STATUS_INVALID_PARAMETER);

    RETURN_NTSTATUS_IF_FAILED(WdfDeviceOpenRegistryKey(device, PLUGPLAY_REGKEY_DEVICE, KEY_READ | KEY_WRITE, WDF_NO_OBJECT_ATTRIBUTES, &registryKey));

    RETURN_NTSTATUS_IF_FAILED(OpenSubRegistryKey(registryKey, registrySubKey));

    UNICODE_STRING valueName;
    RtlInitUnicodeString(&valueName, c_SampleRateName);

    ULONG value = 0;
    ULONG resultLength = 0;

    NTSTATUS status = WdfRegistryQueryValue(registrySubKey, &valueName, sizeof(ULONG), &value, &resultLength, nullptr);
    if (NT_SUCCESS(status))
    {
        sampleRate = value;
    }
    else if (status == STATUS_OBJECT_NAME_NOT_FOUND)
    {
        status = STATUS_SUCCESS;
        sampleRate = UAC_DEFAULT_SAMPLE_RATE;
    }

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS AudioIsochronousEngine::MakeRegistoryIndexKey(
    ULONG            index,
    UNICODE_STRING * keyName,
    WCHAR *          stringBuffer
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    RtlInitEmptyUnicodeString(keyName, stringBuffer, sizeof(WCHAR) * 4);
    if (index < 0x1000)
    {
        status = RtlUnicodeStringPrintf(keyName, L"%03x", index);
    }
    else
    {
        status = STATUS_INVALID_PARAMETER;
    }
    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS
AudioIsochronousEngine::OpenSubRegistryKey(
    WDFKEY   registryKey,
    WDFKEY & subRegistryKey
)
{
    UNICODE_STRING subKeyName;
    WCHAR          buffer[4];

    PAGED_CODE();

    RETURN_NTSTATUS_IF_FAILED(MakeRegistoryIndexKey(m_usbAudioStreamInterfaceGroup->GetGroupIndex(), &subKeyName, buffer));

    RETURN_NTSTATUS_IF_FAILED(WdfRegistryCreateKey(registryKey, &subKeyName, KEY_READ | KEY_WRITE, REG_OPTION_NON_VOLATILE, nullptr, WDF_NO_OBJECT_ATTRIBUTES, &subRegistryKey));

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG
_Use_decl_annotations_
void AudioIsochronousEngine::ReportInternalParameters(
)
{
    PAGED_CODE();

    ULONG groupIndex = m_usbAudioStreamInterfaceGroup->GetGroupIndex();

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] Vendor ID:%04x, Product ID:%04x, DeviceRelease:%04x", groupIndex, m_audioStreamPropertySet.AudioProperty.VendorId, m_audioStreamPropertySet.AudioProperty.ProductId, m_audioStreamPropertySet.AudioProperty.DeviceRelease);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] ProductName                  %ws", groupIndex, m_audioStreamPropertySet.AudioProperty.ProductName);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] SampleRate                   %d", groupIndex, m_audioStreamPropertySet.AudioProperty.SampleRate);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] SupportedSampleRate          0x%x", groupIndex, m_audioStreamPropertySet.AudioProperty.SupportedSampleRate);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] SampleType                   %d", groupIndex, toInt(m_audioStreamPropertySet.AudioProperty.SampleType));
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] InputInterfaceNumber         %d", groupIndex, m_audioStreamPropertySet.InputProperty.InterfaceNumber);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] InputAlternateSetting        %d", groupIndex, m_audioStreamPropertySet.InputProperty.AlternateSetting);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] InputEndpointNumber          0x%x", groupIndex, m_audioStreamPropertySet.InputProperty.EndpointNumber);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] OutputInterfaceNumber        %d", groupIndex, m_audioStreamPropertySet.OutputProperty.InterfaceNumber);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] OutputAlternateSetting       %d", groupIndex, m_audioStreamPropertySet.OutputProperty.AlternateSetting);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] OutputEndpointNumber         0x%x", groupIndex, m_audioStreamPropertySet.OutputProperty.EndpointNumber);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] InputBytesPerBlock           %d", groupIndex, m_audioStreamPropertySet.InputProperty.BytesPerBlock);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] InputMaxSamplesPerPacket     %d", groupIndex, m_audioStreamPropertySet.InputProperty.MaxSamplesPerPacket);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] InputLatencyOffset           %d", groupIndex, m_audioStreamPropertySet.AudioProperty.InputLatencyOffset);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] InputFormatType              %d", groupIndex, m_audioStreamPropertySet.InputProperty.FormatType);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] InputFormat                  %d", groupIndex, m_audioStreamPropertySet.InputProperty.Format);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] InputBytesPerSample          %d", groupIndex, m_audioStreamPropertySet.InputProperty.BytesPerSample);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] InputValidBitsPerSample      %d", groupIndex, m_audioStreamPropertySet.InputProperty.ValidBitsPerSample);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] InputPacketsPerSec           %d", groupIndex, m_audioStreamPropertySet.InputProperty.PacketsPerSec);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] InputSamplesPerPacket        %d", groupIndex, m_audioStreamPropertySet.InputProperty.SamplesPerPacket);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] OutputBytesPerBlock          %d", groupIndex, m_audioStreamPropertySet.OutputProperty.BytesPerBlock);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] OutputMaxSamplesPerPacket    %d", groupIndex, m_audioStreamPropertySet.OutputProperty.MaxSamplesPerPacket);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] OutputLatencyOffset          %d", groupIndex, m_audioStreamPropertySet.AudioProperty.OutputLatencyOffset);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] OutputFormatType             %d", groupIndex, m_audioStreamPropertySet.OutputProperty.FormatType);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] OutputFormat                 %d", groupIndex, m_audioStreamPropertySet.OutputProperty.Format);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] OutputBytesPerSample         %d", groupIndex, m_audioStreamPropertySet.OutputProperty.BytesPerSample);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] OutputValidBitsPerSample     %d", groupIndex, m_audioStreamPropertySet.OutputProperty.ValidBitsPerSample);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] OutputPacketsPerSec          %d", groupIndex, m_audioStreamPropertySet.OutputProperty.PacketsPerSec);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] OutputSamplesPerPacket       %d", groupIndex, m_audioStreamPropertySet.OutputProperty.SamplesPerPacket);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] InputMeasuredSampleRate      %d", groupIndex, m_audioStreamPropertySet.InputProperty.MeasuredSampleRate);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] OutputMeasuredSampleRate     %d", groupIndex, m_audioStreamPropertySet.OutputProperty.MeasuredSampleRate);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] ClockSources                 %d", groupIndex, m_audioStreamPropertySet.AudioProperty.ClockSources);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] InputDriverBuffer            %d", groupIndex, m_audioStreamPropertySet.AudioProperty.InputDriverBuffer);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] OutputDriverBuffer           %d", groupIndex, m_audioStreamPropertySet.AudioProperty.OutputDriverBuffer);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] SupportedSampleFormat        %u", groupIndex, m_audioStreamPropertySet.AudioProperty.SupportedSampleFormats);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] CurrentSampleFormat          %u", groupIndex, toULong(m_audioStreamPropertySet.AudioProperty.CurrentSampleFormat));
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] InputUsbChannels             %d", groupIndex, m_audioStreamPropertySet.InputProperty.UsbChannels);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] OutputUsbChannels            %d", groupIndex, m_audioStreamPropertySet.OutputProperty.UsbChannels);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] FeedbackInterfaceNumber      %d", groupIndex, m_audioStreamPropertySet.FeedbackProperty.FeedbackInterfaceNumber);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] FeedbackAlternateSetting     %d", groupIndex, m_audioStreamPropertySet.FeedbackProperty.FeedbackAlternateSetting);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] FeedbackEndpointNumber       0x%x", groupIndex, m_audioStreamPropertySet.FeedbackProperty.FeedbackEndpointNumber);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] FeedbackInterval             %d", groupIndex, m_audioStreamPropertySet.FeedbackProperty.FeedbackInterval);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] ClassicFramesPerIrp          %d", groupIndex, m_audioStreamPropertySet.ClassicFramesPerIrp);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] IsDeviceAdaptive             %!bool!", groupIndex, m_audioStreamPropertySet.IsDeviceAdaptive);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] IsDeviceSynchronous          %!bool!", groupIndex, m_audioStreamPropertySet.IsDeviceSynchronous);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] InputUsbChannels             %d", groupIndex, m_audioStreamPropertySet.InputProperty.UsbChannels);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] OutputUsbChannels            %d", groupIndex, m_audioStreamPropertySet.OutputProperty.UsbChannels);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] InputChannelNames            %d", groupIndex, m_audioStreamPropertySet.InputProperty.ChannelNames);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] OutputChannelNames           %d", groupIndex, m_audioStreamPropertySet.OutputProperty.ChannelNames);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] StartCounterAsio             %d", groupIndex, m_startCounterAsio);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] StartCounterWdmAudio         %d", groupIndex, m_startCounterWdmAudio);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] StartCounterIsoStream        %d", groupIndex, m_startCounterIsoStream);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] InputIsoPacketSize           %d", groupIndex, m_audioStreamPropertySet.InputProperty.IsoPacketSize);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] OutputIsoPacketSize          %d", groupIndex, m_audioStreamPropertySet.OutputProperty.IsoPacketSize);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] InputLockDelay               %d", groupIndex, m_audioStreamPropertySet.InputProperty.LockDelay);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] OutputLockDelay              %d", groupIndex, m_audioStreamPropertySet.OutputProperty.LockDelay);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] DesiredSampleFormat          %u", groupIndex, toULong(m_audioStreamPropertySet.DesiredSampleFormat));
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - [%u] LastActivationStatus         %!STATUS!", groupIndex, m_lastActivationStatus);
}
