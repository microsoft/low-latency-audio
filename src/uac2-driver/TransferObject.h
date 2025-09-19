// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

/*++

Module Name:

    TransferObject.h

Abstract:

    Define a class to manage a single USB transfer.

Environment:

    Kernel-mode Driver Framework

--*/

#ifndef _TRANSFEROBJECT_H_
#define _TRANSFEROBJECT_H_

class RtPacketObject;

class TransferObject
{
  public:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    TransferObject(
        _In_ PDEVICE_CONTEXT deviceContext,
        _In_ StreamObject *  streamObject,
        _In_ LONG            index,
        _In_ IsoDirection    direction
    );

    virtual __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ~TransferObject();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    AttachDataBuffer(
        _In_ PUCHAR dataBuffer,
        _In_ ULONG  numIsoPackets,
        _In_ ULONG  isoPacketSize,
        _In_ ULONG  maxXferSize
    );

    __drv_maxIRQL(DIAPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void Free();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    Reset();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    NTSTATUS
    SetUrbIsochronousParametersInput(
        _In_ ULONG                          startFrame,
        _In_ WDFUSBPIPE                     pipe,
        _In_ bool                           asap,
        _In_ EVT_WDF_OBJECT_CONTEXT_CLEANUP requestContextCleanup
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    NTSTATUS
    SetUrbIsochronousParametersOutput(
        _In_ ULONG                          startFrame,
        _In_ WDFUSBPIPE                     pipe,
        _In_ bool                           asap,
        _In_ EVT_WDF_OBJECT_CONTEXT_CLEANUP requestContextCleanup
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    NTSTATUS
    SetUrbIsochronousParametersFeedback(
        _In_ ULONG                          startFrame,
        _In_ WDFUSBPIPE                     pipe,
        _In_ bool                           asap,
        _In_ EVT_WDF_OBJECT_CONTEXT_CLEANUP requestContextCleanup
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    NTSTATUS
    FreeRequest();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    NTSTATUS
    FreeUrb();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void TransferObject::DumpUrbPacket(
        _In_ LPCSTR Label
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    NTSTATUS
    SendIsochronousRequest(
        _In_ IsoDirection                       direction,
        _In_ PFN_WDF_REQUEST_COMPLETION_ROUTINE completionRoutine
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    NONPAGED_CODE_SEG
    NTSTATUS
    CancelRequest();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void CompleteRequest(
        _In_ ULONGLONG completedTimeUs,
        _In_ ULONGLONG qpcPosition,
        _In_ ULONGLONG periodUs,
        _In_ ULONGLONG periodQPCPosition
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    USBD_STATUS
    GetUSBDStatus();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    bool
    IsRequested();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    ULONG
    GetStartFrame();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    NTSTATUS
    UpdateTransferredBytesInThisIrp(
        _Out_ ULONG &       transferredBytesInThisIrp,
        _Inout_opt_ ULONG * invalidPacket
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void RecordIsoPacketLength();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    ULONG GetFeedbackSum(
        _Out_ ULONG & validFeedback
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void UpdatePositionsIn(
        _In_ ULONG transferredSamplesInThisIrp
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void CompensateNonFeedbackOutput(
        _In_ ULONG transferredSamplesInThisIrp
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void SetPresendSamples(
        _In_ ULONG presendSamples
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void SetFeedbackSamples(
        _In_ ULONG feedbackSamples
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    ULONG GetFeedbackSamples();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    IsoDirection
    GetDirection();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    LONG
    GetIndex();

    __drv_maxIRQL(PASSIVE_LEVEL)
    NONPAGED_CODE_SEG
    ULONG
    GetNumPackets();

    __drv_maxIRQL(PASSIVE_LEVEL)
    NONPAGED_CODE_SEG
    PUCHAR
    GetDataBuffer();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ULONG GetTransferredBytesInThisIrp();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    PUCHAR GetIsoPacketBuffer(
        _In_ ULONG isoPacket
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    PUCHAR GetRecordedIsoPacketBuffer(
        _In_ ULONG isoPacket
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ULONG GetIsoPacketOffset(
        _In_ ULONG isoPacket
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ULONG GetIsoPacketLength(
        _In_ ULONG isoPacket
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ULONG GetRecordedIsoPacketLength(
        _In_ ULONG isoPacket
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ULONG GetTotalProcessedBytesSoFar(
        _In_ ULONG isoPacket
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    ULONG GetNumberOfPacketsInThisIrp();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    ULONG GetStartFrameInThisIrp();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ULONGLONG GetQPCPosition();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ULONGLONG GetPeriodQPCPosition();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ULONGLONG CalculateEstimatedQPCPosition(
        _In_ ULONG bytesCopiedUpToBoundary
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void SetLockDelayCount(
        _In_ ULONG lockDelayCount
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    ULONG GetLockDelayCount();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    bool DecrementLockDelayCount();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void DebugReport();

    static __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    TransferObject * Create(
        _In_ PDEVICE_CONTEXT deviceContext,
        _In_ StreamObject *  streamObject,
        _In_ LONG            index,
        _In_ IsoDirection    direction
    );

  private:
    const PDEVICE_CONTEXT m_deviceContext;
    StreamObject *        m_streamObject{nullptr};
    const LONG            m_index;
    const IsoDirection    m_direction;
    bool                  m_isCompleted{true};
    PURB                  m_urb{nullptr};
    WDFMEMORY             m_urbMemory{nullptr};
    WDFREQUEST            m_request{nullptr};
    bool                  m_isRequested{false};
    PMDL                  m_dataBufferMdl{nullptr};
    PUCHAR                m_dataBuffer{nullptr};
    ULONG                 m_numIsoPackets{0}; // Number of IsoPackets in the URB
    ULONG                 m_isoPacketSize{0}; // Interval per Offset of IsoPacket within the URB, for input
    ULONG                 m_maxXferSize{0};   // Buffer size used for transfer in the URB
    ULONG                 m_feedbackSamples{0};
    ULONG                 m_feedbackRemainder{0};
    ULONG                 m_presendSamples{0};
    ULONG                 m_totalBytesProcessed{0};
    ULONG                 m_transferredBytesInThisIrp{0};
    ULONG                 m_errorPacketCount{0};
    LONG                  m_asyncPacketsCount{0};
    LONG                  m_syncPacketsCount{0};
    ULONG                 m_lockDelayCount{0};
    PUCHAR                m_isoPacketBuffer[UAC_MAX_CLASSIC_FRAMES_PER_IRP * UAC_MAX_FRAMES_PER_MS]{0};
    ULONG                 m_isoPacketLength[UAC_MAX_CLASSIC_FRAMES_PER_IRP * UAC_MAX_FRAMES_PER_MS]{0};
    ULONG                 m_totalProcessedBytesSoFar[UAC_MAX_CLASSIC_FRAMES_PER_IRP * UAC_MAX_FRAMES_PER_MS]{0};
    WDFSPINLOCK           m_spinLock{nullptr};
    KEVENT                m_requestCompletedEvent{0};
    ULONGLONG             m_completedTimeUs{0ULL};   // Time when the URB was processed (microseconds)
    ULONGLONG             m_qpcPosition{0ULL};       // Time when the URB was processed (query performance counter value)
    ULONGLONG             m_periodUs{0ULL};          // Interval between the time the previous URB was processed and the time this URB was processed; 0 for the first URB (microseconds)
    ULONGLONG             m_periodQPCPosition{0ULL}; // Interval between the time the previous URB was processed and the time this URB was processed; 0 for the first URB (query performance counter value)
};

#endif
