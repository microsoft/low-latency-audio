// Copyright (c) Microsoft Corporation.
// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

/*++

Module Name:

    StreamEngine.h

Abstract:

    This file contains the USB Audio device definitions.

Environment:

    Kernel-mode Driver Framework

--*/

#ifndef _STREAMENGINE_H_
#define _STREAMENGINE_H_

#include <windef.h>
#include "Public.h"
#include "Private.h"
#include "device.h"

#define MAX_PACKET_COUNT  2

#define DEFAULT_FREQUENCY 220

EXTERN_C_START

VOID EvtStreamDestroy(
    _In_ WDFOBJECT Object
);

NTSTATUS
EvtStreamGetHwLatency(
    _In_ ACXSTREAM Stream,
    _Out_ ULONG *  FifoSize,
    _Out_ ULONG *  Delay
);

NTSTATUS
EvtStreamAllocateRtPackets(
    _In_ ACXSTREAM        Stream,
    _In_ ULONG            PacketCount,
    _In_ ULONG            PacketSize,
    _Out_ PACX_RTPACKET * Packets
);

VOID EvtStreamFreeRtPackets(
    _In_ ACXSTREAM     Stream,
    _In_ PACX_RTPACKET Packets,
    _In_ ULONG         PacketCount
);

NTSTATUS
EvtStreamPrepareHardware(
    _In_ ACXSTREAM Stream
);

NTSTATUS
EvtStreamReleaseHardware(
    _In_ ACXSTREAM Stream
);

NTSTATUS
EvtStreamRun(
    _In_ ACXSTREAM Stream
);

NTSTATUS
EvtStreamPause(
    _In_ ACXSTREAM Stream
);

NTSTATUS
EvtStreamAssignDrmContentId(
    _In_ ACXSTREAM     Stream,
    _In_ ULONG         DrmContentId,
    _In_ PACXDRMRIGHTS DrmRights
);

NTSTATUS
EvtStreamGetCurrentPacket(
    _In_ ACXSTREAM Stream,
    _Out_ PULONG   CurrentPacket
);

NTSTATUS
EvtStreamGetPresentationPosition(
    _In_ ACXSTREAM   Stream,
    _Out_ PULONGLONG PositionInBlocks,
    _Out_ PULONGLONG QPCPosition
);

EXTERN_C_END

class CStreamEngine
{
  public:
    virtual __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    AllocateRtPackets(
        _In_ ULONG            PacketCount,
        _In_ ULONG            PacketSize,
        _Out_ PACX_RTPACKET * Packets
    );

    virtual __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    VOID
    FreeRtPackets(
        _Frees_ptr_ _Pre_readable_size_(PacketCount * sizeof(ACX_RTPACKET)) PACX_RTPACKET Packets,
        _In_ ULONG                                                                        PacketCount
    );

    virtual __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    PrepareHardware();

    virtual __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    ReleaseHardware();

    virtual __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    Run();

    virtual __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    Pause();

    virtual __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    GetPresentationPosition(
        _Out_ PULONGLONG PositionInBlocks,
        _Out_ PULONGLONG QPCPosition
    );

    virtual __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    GetCurrentPacket(
        _Out_ PULONG CurrentPacket
    );

    virtual __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    GetHWLatency(
        _Out_ ULONG * FifoSize,
        _Out_ ULONG * Delay
    );

    virtual __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    AssignDrmContentId(
        _In_ ULONG         DrmContentId,
        _In_ PACXDRMRIGHTS DrmRights
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    PDEVICE_CONTEXT
    GetDeviceContext();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    CStreamEngine(
        _In_ BOOLEAN         Input,
        _In_ PDEVICE_CONTEXT DeviceContext,
        _In_ ACXSTREAM       Stream,
        _In_ ACXDATAFORMAT   StreamFormat,
        _In_ ULONG           DeviceIndex,
        _In_ ULONG           Channel,
        _In_ ULONG           NumOfChannelsPerDevice,
        _In_ BOOL            Offload
    );

    virtual __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ~CStreamEngine();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    ULONGLONG
    GetCurrentTime(
        _Out_opt_ PULONGLONG QPCPosition
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    NONPAGED_CODE_SEG
    ACXSTREAM
    GetACXStream();

    __drv_maxIRQL(PASSIVE_LEVEL)
    NONPAGED_CODE_SEG
    ACXDATAFORMAT
    GetACXDataFormat();

  protected:
    BOOLEAN          m_input;
    PDEVICE_CONTEXT  m_deviceContext;
    PVOID            m_packets[MAX_PACKET_COUNT]{};
    PVOID            m_packetTopAddresses[MAX_PACKET_COUNT]{};
    ULONG            m_packetCount;
    ULONG            m_packetSize;
    ULONG            m_firstPacketOffset;
    ACX_STREAM_STATE m_currentState;
    ACXSTREAM        m_stream;
    ACXDATAFORMAT    m_streamFormat;
    ULONG            m_deviceIndex;
    ULONG            m_channel;
    ULONG            m_numOfChannelsPerDevice;
    BOOL             m_offload;

    virtual __drv_maxIRQL(DISPATCH_LEVEL)
    ULONG
    GetBytesPerSecond();
};

class CRenderStreamEngine : public CStreamEngine
{
  public:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    CRenderStreamEngine(
        _In_ PDEVICE_CONTEXT DeviceContext,
        _In_ ACXSTREAM       Stream,
        _In_ ACXDATAFORMAT   StreamFormat,
        _In_ ULONG           DeviceIndex,
        _In_ ULONG           Channel,
        _In_ ULONG           NumOfChannelsPerDevice,
        _In_ BOOL            Offload
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ~CRenderStreamEngine();

    virtual __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    PrepareHardware();

    virtual __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    ReleaseHardware();

    virtual __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    AssignDrmContentId(
        _In_ ULONG         DrmContentId,
        _In_ PACXDRMRIGHTS DrmRights
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    SetRenderPacket(
        _In_ ULONG Packet,
        _In_ ULONG Flags,
        _In_ ULONG EosPacketLength
    );

  protected:
};

class CCaptureStreamEngine : public CStreamEngine
{
  public:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    CCaptureStreamEngine(
        _In_ PDEVICE_CONTEXT DeviceContext,
        _In_ ACXSTREAM       Stream,
        _In_ ACXDATAFORMAT   StreamFormat,
        _In_ ULONG           DeviceIndex,
        _In_ ULONG           Channel,
        _In_ ULONG           NumOfChannelsPerDevice
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ~CCaptureStreamEngine();

    virtual __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    PrepareHardware();

    virtual __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    ReleaseHardware();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    GetCapturePacket(
        _Out_ ULONG *     LastCapturePacket,
        _Out_ ULONGLONG * QPCPacketStart,
        _Out_ BOOLEAN *   MoreData
    );

  protected:
};

// Define circuit/stream pin context.
//
typedef struct _STREAM_TIMER_CONTEXT
{
    CStreamEngine * StreamEngine;
} STREAM_TIMER_CONTEXT, *PSTREAM_TIMER_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(STREAM_TIMER_CONTEXT, GetStreamTimerContext)

#endif // #ifndef _STREAMENGINE_H_
