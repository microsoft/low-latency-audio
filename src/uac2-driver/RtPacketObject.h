// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

/*++

Module Name:

    RtPacketObject.h

Abstract:

    Define a class to handle RtPacket processing.

Environment:

    Kernel-mode Driver Framework

--*/

#ifndef _RTPACKETOBJECT_H_
#define _RTPACKETOBJECT_H_

#include <acx.h>

class ContiguousMemory;
class TransferObject;

class RtPacketObject
{
  public:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    RtPacketObject(
        _In_ PDEVICE_CONTEXT deviceContext
    );

    virtual __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ~RtPacketObject();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    SetDataFormat(
        _In_ bool          isInput,
        _In_ ACXDATAFORMAT DataFormat
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    NONPAGED_CODE_SEG
    void SetIsoPacketInfo(
        _In_ IsoDirection direction,
        _In_ ULONG        isoPacketSize,
        _In_ ULONG        numIsoPackets
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    NONPAGED_CODE_SEG
    void Reset(
        _In_ bool  isInput,
        _In_ ULONG deviceIndex
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    NONPAGED_CODE_SEG
    void Reset(
        _In_ bool isInput
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void FeedOutputWriteBytes(
        _In_ ULONG ulByteCount
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    SetRtPackets(
        _In_ bool                                                                   isInput,
        _In_ ULONG                                                                  deviceIndex,
        _Inout_updates_(rtPacketsCount) _Inout_updates_bytes_(rtPacketSize) PVOID * rtPackets,
        _In_ ULONG                                                                  rtPacketsCount,
        _In_ ULONG                                                                  rtPacketSize,
        _In_ ULONG                                                                  channel,
        _In_ ULONG                                                                  numOfChannelsPerDevice
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void UnsetRtPackets(
        _In_ bool  Input,
        _In_ ULONG deviceIndex
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    CopyFromRtPacketToOutputData(
        _In_ ULONG                           deviceIndex,
        _Inout_updates_bytes_(length) PUCHAR buffer,
        _In_ ULONG                           length,
        _In_ ULONG                           totalProcessedBytesSoFar,
        _In_ TransferObject *                transferObject,
        _In_ ULONG                           usbBytesPerSample,
        _In_ ULONG                           usbValidBitsPerSample,
        _In_ ULONG                           usbChannels
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    CopyToRtPacketFromInputData(
        _In_ ULONG                      deviceIndex,
        _In_reads_bytes_(length) PUCHAR buffer,
        _In_ ULONG                      length,
        _In_ ULONG                      totalProcessedBytesSoFar,
        _In_ TransferObject *           transferObject,
        _In_ ULONG                      usbBytesPerSample,
        _In_ ULONG                      usbValidBitsPerSample,
        _In_ ULONG                      usbChannels
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    GetCurrentPacket(
        _In_ bool    isInput,
        _In_ ULONG   deviceIndex,
        _Out_ PULONG currentPacket
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    ResetCurrentPacket(
        _In_ bool  isInput,
        _In_ ULONG deviceIndex
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    GetCapturePacket(
        _In_ ULONG       deviceIndex,
        _Out_ PULONG     lastCapturePacket,
        _Out_ PULONGLONG qpcPacketStart
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    GetPresentationPosition(
        _In_ bool        isInput,
        _In_ ULONG       deviceIndex,
        _Out_ PULONGLONG positionInBlocks,
        _Out_ PULONGLONG qpcPosition
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    AssignDevices(
        _In_ ULONG numOfInputDevices,
        _In_ ULONG numOfOutputDevices
    );

    static __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    RtPacketObject * Create(
        _In_ PDEVICE_CONTEXT deviceContext
    );

  private:
    typedef struct _RT_PACKET_INFO
    {
        WDFSPINLOCK PositionSpinLock{nullptr};
        ULONG       IsoPacketSize{0};
        ULONG       NumIsoPackets{0};
        PVOID *     RtPackets{nullptr}; // This is retained regardless of Run/Stop.
        ULONG       RtPacketsCount{0};  // This is retained regardless of Run/Stop.
        ULONG       RtPacketSize{0};    // This is retained regardless of Run/Stop.
        ULONGLONG   RtPacketPosition{0ULL};
        ULONGLONG   RtPacketEstimatedPosition{0ULL};
        ULONG       RtPacketCurrentPacket{0};
        ULONGLONG   LastPacketStartQpcPosition{0ULL};
        ULONG       usbChannel{0}; // stereo 2nd strem will be 2
        ULONG       channels{0};   // Number of channels in Acx Audio
    } RT_PACKET_INFO;

    const PDEVICE_CONTEXT m_deviceContext;
    RT_PACKET_INFO *      m_inputRtPacketInfo{nullptr};
    RT_PACKET_INFO *      m_outputRtPacketInfo{nullptr};
    ULONG                 m_numOfInputDevices{0};
    ULONG                 m_numOfOutputDevices{0};
    WDFMEMORY             m_inputRtPacketInfoMemory{nullptr};
    WDFMEMORY             m_outputRtPacketInfoMemory{nullptr};

    PWAVEFORMATEX m_inputWaveFormat{nullptr};  // The origin of WAVEFORMATEX used by Acx Audio
    PWAVEFORMATEX m_outputWaveFormat{nullptr}; // The origin of WAVEFORMATEX used by Acx Audio
    ULONG         m_inputBytesPerSample{0};    // The number of bytes per sample per channel in Acx Audio. 3 for samples packed in 24bit.
    ULONG         m_outputBytesPerSample{0};   // The number of bytes per sample per channel in Acx Audio. 3 for samples packed in 24bit.
    ULONG         m_inputPaddingBytes{0};
    ULONG         m_outputPaddingBytes{0};
    DWORD         m_inputAvgBytesPerSec{0};
    DWORD         m_outputAvgBytesPerSec{0};
};

#endif
