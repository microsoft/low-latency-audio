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

    StreamEngine.cpp - this module controls stream logic for USB Audio device

Abstract:

   This file contains the USB Audio device entry points and callbacks.

Environment:

    Kernel-mode Driver Framework

--*/

#include "Driver.h"
#include "Device.h"
#include "USBAudio.h"
#include "StreamEngine.h"
#include "Common.h"
#ifndef __INTELLISENSE__
#include "StreamEngine.tmh"
#endif

//
//  Global variables
//

//
//  Local function prototypes
//
EXTERN_C_START
EXTERN_C_END

#if !defined(STATIC_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
#define STATIC_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT \
    DEFINE_WAVEFORMATEX_GUID(WAVE_FORMAT_IEEE_FLOAT)
DEFINE_GUIDSTRUCT("00000003-0000-0010-8000-00aa00389b71", KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
#define KSDATAFORMAT_SUBTYPE_IEEE_FLOAT DEFINE_GUIDNAMED(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
#endif

_Use_decl_annotations_
PAGED_CODE_SEG
CStreamEngine::CStreamEngine(
    BOOLEAN         Input,
    PDEVICE_CONTEXT DeviceContext,
    ACXSTREAM       Stream,
    ACXDATAFORMAT   StreamFormat,
    ULONG           DeviceIndex,
    ULONG           Channel,
    ULONG           NumOfChannelsPerDevice,
    BOOL            Offload
)
    : m_input(Input),
      m_deviceContext(DeviceContext),
      m_packetCount(0),
      m_packetSize(0),
      m_firstPacketOffset(0),
      m_currentState(AcxStreamStateStop),
      m_stream(Stream),
      m_streamFormat(StreamFormat),
      m_deviceIndex(DeviceIndex),
      m_channel(Channel),
      m_numOfChannelsPerDevice(NumOfChannelsPerDevice),
      m_offload(Offload)
{
    PAGED_CODE();
    GUID ksDataFormatSubType = AcxDataFormatGetSubFormat(StreamFormat);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry, %p, isInput = %!bool!, Stream = %p, StreamFormat = %p", this, Input, Stream, StreamFormat);
    RtlZeroMemory(m_packets, sizeof(m_packets));

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, "this = %p, m_streamFormat = %p", this, m_streamFormat);
    if (IsEqualGUIDAligned(ksDataFormatSubType, KSDATAFORMAT_SUBTYPE_PCM))
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - KSDATAFORMAT_SUBTYPE_PCM");
    }
    else if (IsEqualGUIDAligned(ksDataFormatSubType, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
    {
        // NS_USBAudio0200::IEEE_FLOAT;
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - KSDATAFORMAT_SUBTYPE_IEEE_FLOAT");
    }
    else if (IsEqualGUIDAligned(ksDataFormatSubType, KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_DIGITAL))
    {
        // NS_USBAudio0200::NS_USBAudio0200::IEC61937_AC_3;
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_DIGITAL");
    }
    else if (IsEqualGUIDAligned(ksDataFormatSubType, KSDATAFORMAT_SUBTYPE_IEC61937_AAC))
    {
        // NS_USBAudio0200::IEC61937_MPEG_2_AAC_ADTS;
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - KSDATAFORMAT_SUBTYPE_IEC61937_AAC");
    }
    else if (IsEqualGUIDAligned(ksDataFormatSubType, KSDATAFORMAT_SUBTYPE_IEC61937_DTS))
    {
        // NS_USBAudio0200::IEC61937_DTS_I;
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - KSDATAFORMAT_SUBTYPE_IEC61937_DTS");
    }
    // else if (IsEqualGUIDAligned(ksDataFormatSubType, ))
    // {
    //     // NS_USBAudio0200::IEC61937_DTS_II;
    //     TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - KSDATAFORMAT_SUBTYPE_ ");
    // }
    // else if (IsEqualGUIDAligned(ksDataFormatSubType, ))
    // {
    //     // NS_USBAudio0200::FORMAT_TYPE_III;
    //     TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - KSDATAFORMAT_SUBTYPE_ ");
    // }
    else if (IsEqualGUIDAligned(ksDataFormatSubType, KSDATAFORMAT_SUBTYPE_IEC61937_WMA_PRO))
    {
        // NS_USBAudio0200::TYPE_III_WMA;
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - KSDATAFORMAT_SUBTYPE_IEC61937_WMA_PRO");
    }
    else
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - KSDATAFORMAT_SUBTYPE unknown");
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - %u, %llu, %u, %u, %u, %u, %u, %u, %u", AcxDataFormatGetChannelsCount(m_streamFormat), AcxDataFormatGetChannelMask(m_streamFormat), AcxDataFormatGetSampleSize(m_streamFormat), AcxDataFormatGetBitsPerSample(m_streamFormat), AcxDataFormatGetValidBitsPerSample(m_streamFormat), AcxDataFormatGetSamplesPerBlock(m_streamFormat), AcxDataFormatGetBlockAlign(m_streamFormat), AcxDataFormatGetSampleRate(m_streamFormat), AcxDataFormatGetAverageBytesPerSec(m_streamFormat));

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
CStreamEngine::~CStreamEngine()
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Entry, %p", this);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CIRCUIT, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
CStreamEngine::AllocateRtPackets(
    ULONG           PacketCount,
    ULONG           PacketSize,
    PACX_RTPACKET * Packets
)
{
    NTSTATUS      status = STATUS_SUCCESS;
    PACX_RTPACKET packets = nullptr;
    PVOID         packetBuffer = nullptr;
    ULONG         i;
    ULONG         packetAllocSizeInPages = 0;
    ULONG         packetAllocSizeInBytes = 0;
    ULONG         firstPacketOffset = 0;
    size_t        packetsSize = 0;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry, PacketCount = %d, PacketSize = %d", PacketCount, PacketSize);

    if (PacketCount > MAX_PACKET_COUNT)
    {
        ASSERT(FALSE);
        status = STATUS_INVALID_PARAMETER;
        goto exit;
    }

    status = RtlSizeTMult(PacketCount, sizeof(ACX_RTPACKET), &packetsSize);
    if (!NT_SUCCESS(status))
    {
        ASSERT(FALSE);
        goto exit;
    }

    packets = (PACX_RTPACKET) new (POOL_FLAG_NON_PAGED, DRIVER_TAG) ACX_RTPACKET[PacketCount];
    if (!packets)
    {
        status = STATUS_NO_MEMORY;
        ASSERT(FALSE);
        goto exit;
    }

    //
    // We need to allocate page-aligned buffers, to ensure no kernel memory leaks
    // to user space. Round up the packet size to page aligned, then calculate
    // the first packet's buffer offset so packet 0 ends on a page boundary and
    // packet 1 begins on a page boundary.
    //
    status = RtlULongAdd(PacketSize, PAGE_SIZE - 1, &packetAllocSizeInPages);
    if (!NT_SUCCESS(status))
    {
        ASSERT(FALSE);
        goto exit;
    }
    packetAllocSizeInPages = packetAllocSizeInPages / PAGE_SIZE;
    packetAllocSizeInBytes = PAGE_SIZE * packetAllocSizeInPages;
    firstPacketOffset = packetAllocSizeInBytes - PacketSize;

    for (i = 0; i < PacketCount; ++i)
    {
        PMDL pMdl = nullptr;

        ACX_RTPACKET_INIT(&packets[i]);

        packetBuffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, packetAllocSizeInBytes, DRIVER_TAG);
        if (packetBuffer == nullptr)
        {
            status = STATUS_NO_MEMORY;
            goto exit;
        }

        pMdl = IoAllocateMdl(packetBuffer, packetAllocSizeInBytes, FALSE, TRUE, nullptr);
        if (pMdl == nullptr)
        {
            status = STATUS_NO_MEMORY;
            goto exit;
        }

        MmBuildMdlForNonPagedPool(pMdl);

        WDF_MEMORY_DESCRIPTOR_INIT_MDL(&(packets[i].RtPacketBuffer), pMdl, packetAllocSizeInBytes);

        packets[i].RtPacketSize = PacketSize;
        if (i == 0)
        {
            packets[i].RtPacketOffset = firstPacketOffset;
            m_packetTopAddresses[i] = ((UCHAR *)packetBuffer) + firstPacketOffset;
        }
        else
        {
            packets[i].RtPacketOffset = 0;
            m_packetTopAddresses[i] = packetBuffer;
        }
        m_packets[i] = packetBuffer;

        packetBuffer = nullptr;
    }

    status = USBAudioAcxDriverStreamSetRtPackets(m_input, m_deviceIndex, m_deviceContext, m_packetTopAddresses, PacketCount, PacketSize, m_channel, m_numOfChannelsPerDevice);
    IF_FAILED_JUMP(status, exit);

    *Packets = packets;
    packets = nullptr;
    m_packetCount = PacketCount;
    m_packetSize = PacketSize;
    m_firstPacketOffset = firstPacketOffset;

exit:
    if (packetBuffer)
    {
        ExFreePoolWithTag(packetBuffer, DRIVER_TAG);
    }
    if (packets)
    {
        FreeRtPackets(packets, PacketCount);
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
VOID CStreamEngine::FreeRtPackets(
    PACX_RTPACKET Packets,
    ULONG         PacketCount
)
{
    ULONG i;
    PVOID buffer;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    USBAudioAcxDriverStreamUnsetRtPackets(m_input, m_deviceIndex, m_deviceContext);

    for (i = 0; i < PacketCount; ++i)
    {
        if (Packets[i].RtPacketBuffer.u.MdlType.Mdl)
        {
            buffer = MmGetMdlVirtualAddress(Packets[i].RtPacketBuffer.u.MdlType.Mdl);
            IoFreeMdl(Packets[i].RtPacketBuffer.u.MdlType.Mdl);
            ExFreePool(buffer);
        }
    }

    delete[] Packets;
    Packets = nullptr;
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
CStreamEngine::PrepareHardware()
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    // WDF_TIMER_CONFIG      timerConfig;
    // WDF_OBJECT_ATTRIBUTES timerAttributes;
    // PSTREAM_TIMER_CONTEXT timerContext;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    //
    // If already in this state, do nothing.
    //
    if (m_currentState == AcxStreamStatePause)
    {
        // Nothing to do.
        status = STATUS_SUCCESS;
        goto exit;
    }

    if (m_currentState != AcxStreamStateStop)
    {
        // Error out.
        status = STATUS_INVALID_STATE_TRANSITION;
        goto exit;
    }

    //
    // Stop to Pause.
    //

    m_currentState = AcxStreamStatePause;
    status = STATUS_SUCCESS;

exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
CStreamEngine::ReleaseHardware()
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    //
    // If already in this state, do nothing.
    //
    if (m_currentState == AcxStreamStateStop)
    {
        // Nothing to do.
        goto exit;
    }

    //
    // Just assert we are in the correct state.
    // On the way down we always want to succeed.
    //
    ASSERT(m_currentState == AcxStreamStatePause);

    KeFlushQueuedDpcs();

    USBAudioAcxDriverStreamResetCurrentPacket(m_input, m_deviceIndex, m_deviceContext);

    m_currentState = AcxStreamStateStop;

exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
CStreamEngine::Pause()
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    if (m_currentState == AcxStreamStatePause)
    {
        // Nothing to do.
        status = STATUS_SUCCESS;
        goto exit;
    }

    if (m_currentState != AcxStreamStateRun)
    {
        // Error out.
        status = STATUS_INVALID_STATE_TRANSITION;
        goto exit;
    }

    m_currentState = AcxStreamStatePause;

    status = USBAudioAcxDriverStreamPause(m_input, m_deviceIndex, m_deviceContext);

exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
CStreamEngine::Run()
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    if (m_currentState == AcxStreamStateRun)
    {
        // Nothing to do.
        status = STATUS_SUCCESS;
        goto exit;
    }

    if (m_currentState != AcxStreamStatePause)
    {
        status = STATUS_INVALID_STATE_TRANSITION;
        goto exit;
    }

    //
    // When a device is connected or the driver calls
    // AcxPinNotifyDataFormatChange, Windows invokes EvtStreamPrepareHardware
    // and EvtStreamReleaseHardware to check the corresponding DataFormat.
    //
    // If the sample rate of the device is changed within
    // EvtStreamPrepareHardware, it may result in frequent changes in a short
    // period, leading to unexpected behavior or issues.
    //
    // Similarly, when the sample rate is changed via ASIO and notified
    // through AcxPinNotifyDataFormatChange, the same problem can occur,
    // causing the sample rate set by ASIO to be unintentionally altered.
    //
    // To address this issue, we have modified the implementation so that the
    // sample rate is no longer changed in EvtStreamPrepareHardware, but
    // instead in EvtStreamRun.
    //

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, "this = %p, m_streamFormat = %p", this, m_streamFormat);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, " - %u, %llu, %u, %u, %u, %u, %u, %u, %u", AcxDataFormatGetChannelsCount(m_streamFormat), AcxDataFormatGetChannelMask(m_streamFormat), AcxDataFormatGetSampleSize(m_streamFormat), AcxDataFormatGetBitsPerSample(m_streamFormat), AcxDataFormatGetValidBitsPerSample(m_streamFormat), AcxDataFormatGetSamplesPerBlock(m_streamFormat), AcxDataFormatGetBlockAlign(m_streamFormat), AcxDataFormatGetSampleRate(m_streamFormat), AcxDataFormatGetAverageBytesPerSec(m_streamFormat));

    status = USBAudioAcxDriverStreamSetDataFormat(m_input, m_deviceIndex, m_deviceContext, m_streamFormat);
    IF_FAILED_JUMP(status, exit);

    status = USBAudioAcxDriverStreamRun(m_input, m_deviceIndex, m_deviceContext);
    IF_FAILED_JUMP(status, exit);

    m_currentState = AcxStreamStateRun;
    status = STATUS_SUCCESS;

exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
CStreamEngine::GetPresentationPosition(
    PULONGLONG PositionInBlocks,
    PULONGLONG QPCPosition
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    status = USBAudioAcxDriverStreamGetPresentationPosition(m_input, m_deviceIndex, m_deviceContext, PositionInBlocks, QPCPosition);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
CStreamEngine::AssignDrmContentId(
    ULONG /* DrmContentId */,
    PACXDRMRIGHTS /* DrmRights */
)
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    //
    // At this point the driver should enforce the new DrmRights.
    //
    // HDMI render: if DigitalOutputDisable or CopyProtect is true, enable HDCP.
    //
    // From MSDN:
    //
    // This sample doesn't forward protected content, but if your driver uses
    // lower layer drivers or a different stack to properly work, please see the
    // following info from MSDN:
    //
    // "Before allowing protected content to flow through a data path, the system
    // verifies that the data path is secure. To do so, the system authenticates
    // each module in the data path beginning at the upstream end of the data path
    // and moving downstream. As each module is authenticated, that module gives
    // the system information about the next module in the data path so that it
    // can also be authenticated. To be successfully authenticated, a module's
    // binary file must be signed as DRM-compliant.
    //
    // Two adjacent modules in the data path can communicate with each other in
    // one of several ways. If the upstream module calls the downstream module
    // through IoCallDriver, the downstream module is part of a WDM driver. In
    // this case, the upstream module calls the AcxDrmForwardContentToDeviceObject
    // function to provide the system with the device object representing the
    // downstream module. (If the two modules communicate through the downstream
    // module's content handlers, the upstream module calls AcxDrmAddContentHandlers
    // instead.)
    //
    // For more information, see MSDN's DRM Functions and Interfaces.
    //
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
CStreamEngine::GetHWLatency(
    ULONG * FifoSize,
    ULONG * Delay
)
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    *FifoSize = 128;
    *Delay = 0;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
ULONG
CStreamEngine::GetBytesPerSecond()
{
    ULONG bytesPerSecond;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    bytesPerSecond = AcxDataFormatGetAverageBytesPerSec(m_streamFormat);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");

    return bytesPerSecond;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
CStreamEngine::GetCurrentPacket(
    PULONG CurrentPacket
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    // currentPacket  = (ULONG)InterlockedCompareExchange((LONG *)&m_CurrentPacket, -1, -1);

    // *CurrentPacket = currentPacket;
    status = USBAudioAcxDriverStreamGetCurrentPacket(m_input, m_deviceIndex, m_deviceContext, CurrentPacket);

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DRIVER, " - *CurrentPacket = %d", *CurrentPacket);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
CRenderStreamEngine::CRenderStreamEngine(
    PDEVICE_CONTEXT DeviceContext,
    ACXSTREAM       Stream,
    ACXDATAFORMAT   StreamFormat,
    ULONG           DeviceIndex,
    ULONG           Channel,
    ULONG           NumOfChannelsPerDevice,
    BOOL            Offload
)
    : CStreamEngine(FALSE, DeviceContext, Stream, StreamFormat, DeviceIndex, Channel, NumOfChannelsPerDevice, Offload)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
CRenderStreamEngine::~CRenderStreamEngine()
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
CRenderStreamEngine::PrepareHardware()
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    auto prepareHardwareScope = wil::scope_exit([&]() {
        if (!NT_SUCCESS(status))
        {
            USBAudioAcxDriverStreamReleaseHardware(false, m_deviceIndex, GetDeviceContext());
        }
    });

    status = CStreamEngine::PrepareHardware();
    RETURN_NTSTATUS_IF_FAILED(status);

    status = USBAudioAcxDriverStreamPrepareHardware(false, m_deviceIndex, GetDeviceContext(), this);
    RETURN_NTSTATUS_IF_FAILED(status);

    //
    // For the reason why sample rate changes are not performed here,
    // please refer to the comments in CStreamEngine::Run().
    //
    // status = USBAudioAcxDriverStreamSetDataFormat(false, GetDeviceContext(), m_streamFormat);
    // RETURN_NTSTATUS_IF_FAILED(status);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
CRenderStreamEngine::ReleaseHardware()
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    // m_SaveData.WaitAllWorkItems();
    // m_SaveData.Cleanup();

    USBAudioAcxDriverStreamReleaseHardware(false, m_deviceIndex, GetDeviceContext());

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");

    return CStreamEngine::ReleaseHardware();
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
CRenderStreamEngine::AssignDrmContentId(
    ULONG /* DrmContentId */,
    PACXDRMRIGHTS /* DrmRights */
)
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    //
    // At this point the driver should enforce the new DrmRights.
    // The sample driver handles DrmRights per stream basis, and
    // stops writing the stream to disk, if CopyProtect = TRUE.
    //
    // HDMI render: if DigitalOutputDisable or CopyProtect is true, enable HDCP.
    // Loopback: if CopyProtect is true, disable loopback stream.
    //

    //
    // Sample writes each stream separately to disk. If the rights for this
    // stream indicates that the stream is CopyProtected, stop writing to disk.
    //
    // m_SaveData.Disable(DrmRights->CopyProtect);

    //
    // From MSDN:
    //
    // This sample doesn't forward protected content, but if your driver uses
    // lower layer drivers or a different stack to properly work, please see the
    // following info from MSDN:
    //
    // "Before allowing protected content to flow through a data path, the system
    // verifies that the data path is secure. To do so, the system authenticates
    // each module in the data path beginning at the upstream end of the data path
    // and moving downstream. As each module is authenticated, that module gives
    // the system information about the next module in the data path so that it
    // can also be authenticated. To be successfully authenticated, a module's
    // binary file must be signed as DRM-compliant.
    //
    // Two adjacent modules in the data path can communicate with each other in
    // one of several ways. If the upstream module calls the downstream module
    // through IoCallDriver, the downstream module is part of a WDM driver. In
    // this case, the upstream module calls the AcxDrmForwardContentToDeviceObject
    // function to provide the system with the device object representing the
    // downstream module. (If the two modules communicate through the downstream
    // module's content handlers, the upstream module calls AcxDrmAddContentHandlers
    // instead.)
    //
    // For more information, see MSDN's DRM Functions and Interfaces.
    //

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
CRenderStreamEngine::SetRenderPacket(
    ULONG Packet,
    ULONG Flags,
    ULONG EosPacketLength
)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG    currentPacket;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry, Packet = %u, Flags = 0x%x, EosPacketLength = %u", Packet, Flags, EosPacketLength);

    status = USBAudioAcxDriverStreamGetCurrentPacket(m_input, m_deviceIndex, m_deviceContext, &currentPacket);
    if (!NT_SUCCESS(status))
    {
        goto SetRenderPacket_Exit;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DRIVER, " - currentPacket = %u", currentPacket);

    if (Packet <= currentPacket)
    {
        // ASSERT(FALSE);
        status = STATUS_DATA_LATE_ERROR;
    }
    else if (Packet > currentPacket + 1)
    {
        // ASSERT(FALSE);
        status = STATUS_DATA_OVERRUN;
    }

SetRenderPacket_Exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
CCaptureStreamEngine::CCaptureStreamEngine(
    PDEVICE_CONTEXT DeviceContext,
    ACXSTREAM       Stream,
    ACXDATAFORMAT   StreamFormat,
    ULONG           DeviceIndex,
    ULONG           Channel,
    ULONG           NumOfChannelsPerDevice
)
    : CStreamEngine(TRUE, DeviceContext, Stream, StreamFormat, DeviceIndex, Channel, NumOfChannelsPerDevice, FALSE)
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    // m_CurrentPacketStart.QuadPart = 0;
    // m_LastPacketStart.QuadPart    = 0;
    USBAudioAcxDriverStreamResetCurrentPacket(TRUE, m_deviceIndex, m_deviceContext);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
CCaptureStreamEngine::~CCaptureStreamEngine()
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
CCaptureStreamEngine::PrepareHardware()
{
    NTSTATUS              status = STATUS_SUCCESS;
    PWAVEFORMATEXTENSIBLE pwfext = nullptr;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    auto prepareHardwareScope = wil::scope_exit([&]() {
        if (!NT_SUCCESS(status))
        {
            USBAudioAcxDriverStreamReleaseHardware(true, m_deviceIndex, GetDeviceContext());
        }
    });

    status = CStreamEngine::PrepareHardware();
    RETURN_NTSTATUS_IF_FAILED(status);

    status = USBAudioAcxDriverStreamPrepareHardware(true, m_deviceIndex, GetDeviceContext(), this);
    RETURN_NTSTATUS_IF_FAILED(status);

    pwfext = (PWAVEFORMATEXTENSIBLE)AcxDataFormatGetWaveFormatExtensible(m_streamFormat);
    if (pwfext == nullptr)
    {
        // Cannot initialize reader or generator with a format that's not understood
        status = STATUS_NO_MATCH;
        RETURN_NTSTATUS_IF_FAILED(status);
    }

    //
    // For the reason why sample rate changes are not performed here,
    // please refer to the comments in CStreamEngine::Run().
    //
    // status = USBAudioAcxDriverStreamSetDataFormat(true, GetDeviceContext(), m_streamFormat);
    // RETURN_NTSTATUS_IF_FAILED(status);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
CCaptureStreamEngine::ReleaseHardware()
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    USBAudioAcxDriverStreamReleaseHardware(true, m_deviceIndex, GetDeviceContext());

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");

    return CStreamEngine::ReleaseHardware();
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
CCaptureStreamEngine::GetCapturePacket(
    ULONG *     LastCapturePacket,
    ULONGLONG * QPCPacketStart,
    BOOLEAN *   MoreData
)
{
    NTSTATUS status = STATUS_SUCCESS;
    // ULONG    currentPacket;
    // LONGLONG qpcPacketStart;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    status = USBAudioAcxDriverStreamGetCapturePacket(m_deviceContext, m_deviceIndex, LastCapturePacket, QPCPacketStart); // TBD. How to handle ModeData

    // currentPacket      = (ULONG)InterlockedCompareExchange((LONG *)&m_CurrentPacket, -1, -1);
    // qpcPacketStart     = InterlockedCompareExchange64(&m_LastPacketStart.QuadPart, -1, -1);

    // *LastCapturePacket = currentPacket - 1;
    // *QPCPacketStart    = (ULONGLONG)qpcPacketStart;
    *MoreData = FALSE;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PDEVICE_CONTEXT
CStreamEngine::GetDeviceContext()
{
    PAGED_CODE();
    return m_deviceContext;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
ULONGLONG
CStreamEngine::GetCurrentTime(
    PULONGLONG QPCPosition
)
{
    ULONGLONG currentTime = USBAudioAcxDriverStreamGetCurrentTime(GetDeviceContext(), QPCPosition);

    return currentTime;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
ACXSTREAM
CStreamEngine::GetACXStream()
{
    return m_stream;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
ACXDATAFORMAT
CStreamEngine::GetACXDataFormat()
{
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CIRCUIT, "this = %p, m_streamFormat = %p", this, m_streamFormat);

    return m_streamFormat;
}

NONPAGED_CODE_SEG
VOID EvtStreamDestroy(
    _In_ WDFOBJECT Object
)
{
    // PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    PSTREAMENGINE_CONTEXT context;
    CStreamEngine *       streamEngine = nullptr;

    context = GetStreamEngineContext((ACXSTREAM)Object);

    streamEngine = (CStreamEngine *)context->StreamEngine;
    context->StreamEngine = nullptr;
    delete streamEngine;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
}

PAGED_CODE_SEG
NTSTATUS
EvtStreamGetHwLatency(
    _In_ ACXSTREAM Stream,
    _Out_ ULONG *  FifoSize,
    _Out_ ULONG *  Delay
)
{
    PSTREAMENGINE_CONTEXT context;
    CStreamEngine *       streamEngine = nullptr;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    context = GetStreamEngineContext(Stream);

    streamEngine = (CStreamEngine *)context->StreamEngine;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");

    return streamEngine->GetHWLatency(FifoSize, Delay);
}

PAGED_CODE_SEG
NTSTATUS
EvtStreamAllocateRtPackets(
    _In_ ACXSTREAM        Stream,
    _In_ ULONG            PacketCount,
    _In_ ULONG            PacketSize,
    _Out_ PACX_RTPACKET * Packets
)
{
    PSTREAMENGINE_CONTEXT context;
    CStreamEngine *       streamEngine = nullptr;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    context = GetStreamEngineContext(Stream);

    streamEngine = (CStreamEngine *)context->StreamEngine;

    NTSTATUS status = streamEngine->AllocateRtPackets(PacketCount, PacketSize, Packets);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
VOID EvtStreamFreeRtPackets(
    _In_ ACXSTREAM     Stream,
    _In_ PACX_RTPACKET Packets,
    _In_ ULONG         PacketCount
)
{
    PSTREAMENGINE_CONTEXT context;
    CStreamEngine *       streamEngine = nullptr;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    context = GetStreamEngineContext(Stream);

    streamEngine = (CStreamEngine *)context->StreamEngine;

    streamEngine->FreeRtPackets(Packets, PacketCount);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
}

PAGED_CODE_SEG
NTSTATUS
EvtStreamPrepareHardware(
    _In_ ACXSTREAM Stream
)
{
    PSTREAMENGINE_CONTEXT context;
    CStreamEngine *       streamEngine = nullptr;
    PDEVICE_CONTEXT       deviceContext = nullptr;
    NTSTATUS              status = STATUS_SUCCESS;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    context = GetStreamEngineContext(Stream);

    streamEngine = (CStreamEngine *)context->StreamEngine;

    deviceContext = streamEngine->GetDeviceContext();

    status = streamEngine->PrepareHardware();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
NTSTATUS
EvtStreamReleaseHardware(
    _In_ ACXSTREAM Stream
)
{
    PSTREAMENGINE_CONTEXT context;
    CStreamEngine *       streamEngine = nullptr;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    context = GetStreamEngineContext(Stream);

    {
    }

    streamEngine = (CStreamEngine *)context->StreamEngine;

    NTSTATUS status = streamEngine->ReleaseHardware();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
NTSTATUS
EvtStreamRun(
    _In_ ACXSTREAM Stream
)
{
    PSTREAMENGINE_CONTEXT context;
    CStreamEngine *       streamEngine = nullptr;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    context = GetStreamEngineContext(Stream);

    streamEngine = (CStreamEngine *)context->StreamEngine;

    NTSTATUS status = streamEngine->Run();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
NTSTATUS
EvtStreamPause(
    _In_ ACXSTREAM Stream
)
{
    PSTREAMENGINE_CONTEXT context;
    CStreamEngine *       streamEngine = nullptr;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    context = GetStreamEngineContext(Stream);

    streamEngine = (CStreamEngine *)context->StreamEngine;

    NTSTATUS status = streamEngine->Pause();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
NTSTATUS
EvtStreamAssignDrmContentId(
    _In_ ACXSTREAM     Stream,
    _In_ ULONG         DrmContentId,
    _In_ PACXDRMRIGHTS DrmRights
)
{
    PSTREAMENGINE_CONTEXT context;
    CStreamEngine *       streamEngine = nullptr;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    context = GetStreamEngineContext(Stream);

    streamEngine = (CStreamEngine *)context->StreamEngine;

    NTSTATUS status = streamEngine->AssignDrmContentId(DrmContentId, DrmRights);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
NTSTATUS
EvtStreamGetCurrentPacket(
    _In_ ACXSTREAM Stream,
    _Out_ PULONG   CurrentPacket
)
{
    PSTREAMENGINE_CONTEXT context;
    CStreamEngine *       streamEngine = nullptr;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    context = GetStreamEngineContext(Stream);

    streamEngine = static_cast<CStreamEngine *>(context->StreamEngine);

    NTSTATUS status = streamEngine->GetCurrentPacket(CurrentPacket);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

PAGED_CODE_SEG
NTSTATUS
EvtStreamGetPresentationPosition(
    _In_ ACXSTREAM   Stream,
    _Out_ PULONGLONG PositionInBlocks,
    _Out_ PULONGLONG QPCPosition
)
{
    PSTREAMENGINE_CONTEXT context;
    CStreamEngine *       streamEngine = nullptr;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    context = GetStreamEngineContext(Stream);

    streamEngine = static_cast<CStreamEngine *>(context->StreamEngine);

    NTSTATUS status = streamEngine->GetPresentationPosition(PositionInBlocks, QPCPosition);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

// ======================================================================

// ======================================================================
