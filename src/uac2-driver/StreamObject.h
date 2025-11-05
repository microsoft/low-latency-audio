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

    StreamObject.h

Abstract:

    Define a class to manage USB streaming.

Environment:

    Kernel-mode Driver Framework

--*/

#ifndef _STREAMOBJECT_H_
#define _STREAMOBJECT_H_

#include "MixingEngineThread.h"

enum class StreamStatuses
{
    NotStable = 0,
    InputStable = 1 << 0,                                                                     // 0x01
    OutputStable = 1 << 1,                                                                    // 0x02
    InputStreaming = 1 << 2,                                                                  // 0x04
    OutputStreaming = 1 << 3,                                                                 // 0x03
    IoStable = (static_cast<ULONG>(InputStable) | static_cast<ULONG>(OutputStable)),          // 0x03
    IoStreaming = (static_cast<ULONG>(InputStreaming) | static_cast<ULONG>(OutputStreaming)), // 0x0c
    IoSteady = (static_cast<ULONG>(IoStable) | static_cast<ULONG>(IoStreaming))               // 0x0f
};

constexpr int toInt(StreamStatuses statuses)
{
    return static_cast<int>(statuses);
}

enum class PacketLoopReason
{
    ContinueLoop = 0,               // Continue looping
    ExitLoopListCycleCompleted,     // I've completed one lap of the list.
    ExitLoopAsioNotifyTimeExceeded, // The estimated time for ASIO notification has already passed.
    ExitLoopPacketEstimateReached,  // Processing position reaches current position prediction
    ExitLoopNoMoreAsioBuffers,      // No more ASIO buffers to process
    ExitLoopAtAsioBoundary,         // Exit loop if ASIO buffer boundary is reached
    ExitLoopAfterSafetyOffset,      // Exit the loop after processing a safety offset
    ExitLoopAtInSync,               // Exit the loop when synchronized with IN
    ExitLoopToPreventOutOverlap,    // Prevents OUT processing from going around once the buffer and reaching the currently processed position
};

typedef struct BUFFER_PROPERTY_
{
    ULONG Irp;
    ULONG Packet;
    ULONG PacketId;
    // PUCHAR Header;
    PUCHAR Buffer;
    // bool   Completed;
    ULONG            Offset; // Usually 0, but when an ASIO buffer boundary is reached, the number of bytes up to the boundary is filled in.
    ULONG            Length;
    ULONG            TotalProcessedBytesSoFar;
    TransferObject * TransferObject;
} BUFFER_PROPERTY, *PBUFFER_PROPERTY;

typedef struct UAC_STREAM_STATISTICS_
{
    ULONG         Time;
    ULONG         BusTime;
    LARGE_INTEGER PerformanceCounter;
    ULONG         InputEstimatedPacket;
    ULONG         InputStartPacket;
    ULONG         InputEndPacket;
    ULONG         InputObtainedPackets;
    ULONG         InputFilledPackets;
    ULONG         OutputStartPacket;
    ULONG         OutputEndPacket;
    ULONG         OutputFilledPackets;
    ULONG         Notify;
    LONGLONG      InputAsioBytes;
    LONGLONG      OutputAsioBytes;
#ifdef MULTI_BUFFER_THREAD
    ULONG ThreadIndex;
#endif
    LONGLONG OutReadyPos;
    LONGLONG DueTime;
    NTSTATUS WakeupReason;
    ULONG    SpinCount;
    //	UCHAR EvaluatedPacketSize[UAC_MAX_CLASSIC_FRAMES_PER_IRP * UAC_MAX_IRP_NUMBER * 8];
    //	UCHAR ReportedPacketSize[UAC_MAX_CLASSIC_FRAMES_PER_IRP * UAC_MAX_IRP_NUMBER * 8];
    ULONG    InputElapsedTimeAfterDpc;
    ULONG    OutputElapsedTimeAfterDpc;
    ULONG    AsioNotifyCount;
    LONG     ClientProcessingTime;
    LONG     SafetyOffset;
    ULONG    FeedbackSamples;
    LONG     IoSamplesDiff;
    LONG     IfSamplesDiff;
    ULONG    DpcCompleteStatus;
    ULONG    MeasuredSampleRate;
    ULONG    InputLoopExitReason;
    ULONG    OutputLoopExitReason;
    ULONG    IoStable;
    LONGLONG AsioWritePosition;
    LONGLONG AsioReadPosition;
    LONG     OutputReady;
    LONG     ReadyBuffers;
    LONG     CallbackRemain;
    LONG     AsioProcessStart;
    LONG     AsioProcessComplete;
    ULONG    LastSyncPacketId;
    ULONG    LastTransferPacketId;
    ULONG    WdmOutPosition;
} UAC_STREAM_STATISTICS, *PUAC_STREAM_STATISTICS;

class StreamObject
{
  public:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    StreamObject(
        _In_ PDEVICE_CONTEXT      deviceContext,
        _In_ const StreamStatuses ioStable,
        _In_ const StreamStatuses ioStreaming,
        _In_ const StreamStatuses ioSteady
    );

    virtual __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ~StreamObject();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void ResetNextMeasureFrames(
        _In_ LONG measureFrames
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    NONPAGED_CODE_SEG
    void SetStartIsoFrame(
        _In_ ULONG currentFrame,
        _In_ LONG  outputFrameDelay
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    NONPAGED_CODE_SEG
    void SetIsoFrameDelay(
        _In_ ULONG firstPacketLatency
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void SetTransferObject(
        _In_ LONG             index,
        _In_ IsoDirection     direction,
        _In_ TransferObject * transferObject
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    TransferObject * GetTransferObject(
        _In_ LONG         index,
        _In_ IsoDirection direction
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    ULONG
    GetStartFrame(
        _In_ IsoDirection direction,
        _In_ ULONG        numPackets
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    CancelRequestAll();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    Cleanup();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void ResetIsoRequestCompletionTime();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void CompleteRequest(
        _In_ const IsoDirection direction,
        _In_ const ULONGLONG    currentTimeUs,
        _In_ const ULONGLONG    qpcPosition,
        _Out_ ULONGLONG &       periodUs,
        _Out_ ULONGLONG &       periodQPC
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    _Success_(return == TRUE)
    bool CalculateSampleRate(
        _In_ const bool        isInput,
        _In_ const ULONG       bytesPerBlock,
        _In_ const ULONG       packetsPerSec,
        _In_ const ULONG       length,
        _Out_ volatile ULONG & measuredSampleRate
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    ULONG
    CalculateTransferSizeAndSetURB(
        _In_ const LONG  index,
        _In_ PURB        urb,
        _In_ const ULONG startFrame,
        _In_ const ULONG numPackets,
        _In_ const ULONG lockDelayCount,
        _In_ LONG *      asyncPacketsCount,
        _In_ LONG *      syncPacketsCount
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS CreateMixingEngineThread(
        _In_ KPRIORITY priority,
        _In_ LONG      wakeUpIntervalUs
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void
    TerminateMixingEngineThread();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void
    WakeupMixingEngineThread();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void
    SaveStartPCUs();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void UpdateCompletedPacket(
        _In_ bool  isInput,
        _In_ ULONG index,
        _In_ ULONG numberOfPackets
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    bool CheckInputStability(
        _In_ ULONG index,
        _In_ ULONG numberOfPacketsInThisIrp,
        _In_ ULONG startFrameInThisIrp,
        _In_ ULONG transferredBytesInThisIrp,
        _In_ ULONG invalidPacket
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void UpdatePositionsIn(
        _In_ ULONG length
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    ULONG UpdatePositionsFeedback(
        _In_ ULONG feedbackSum,
        _In_ ULONG validFeedback
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void SetInputStable();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void SetOutputStable();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void SetInputStreaming();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void SetOutputStreaming(
        _In_ ULONG index,
        _In_ ULONG lockDelayCount
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    bool IsIoSteady();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void SetFeedbackStale(
        _In_ const ULONG startFrame,
        _In_ const ULONG feedbackValue
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    bool IsFeedbackStable();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void AddCompensateSamples(
        _In_ LONG nonFeedbackSamples
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void SetTerminateStream();

    static __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    StreamObject * Create(
        _In_ PDEVICE_CONTEXT      deviceContext,
        _In_ const StreamStatuses ioStable,
        _In_ const StreamStatuses ioStreaming,
        _In_ const StreamStatuses ioSteady
    );

  private:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS Wait();

    __drv_maxIRQL(PASSIVE_LEVEL)
    NONPAGED_CODE_SEG
    StreamStatuses GetStreamStatuses(
        _Out_ bool & isProcessIo
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    NONPAGED_CODE_SEG
    StreamStatuses GetStreamStatuses();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void ClearWakeUpCount();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void IncrementWakeUpCount();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    bool IsFirstWakeUp();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void
    SaveWakeUpTimePCUs(
        _In_ ULONGLONG currentTimePCUs
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ULONGLONG
    GetWakeUpDiffPCUs();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ULONG
    EstimateUSBBusTime(
        _In_ ULONG usbBusTimeCurrent,
        _In_ ULONG wakeupDiffPCUs
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void UpdateElapsedTimeUs(
        _In_ ULONG wakeUpDiffPCUs
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    bool IsOverrideIgnoreEstimation();

    __drv_maxIRQL(PASSIVE_LEVEL)
    NONPAGED_CODE_SEG
    void GetCompletedPacket(
        _Out_ LONGLONG & inCompletedPacket,
        _Out_ LONGLONG & outCompletedPacket
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void DeterminePacket(
        _In_ LONGLONG inCompletedPacket,
        _In_ ULONG    usbBusTimeDiff,
        _In_ ULONG    packetsPerIrp
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    bool
    CreateCompletedInputPacketList(
        _Out_writes_bytes_(packetsPerIrp * numIrp * sizeof(BUFFER_PROPERTY)) PBUFFER_PROPERTY inputBuffers,
        _In_ PBUFFER_PROPERTY                                                                 inputReminder,
        _In_ const ULONG                                                                      packetsPerIrp,
        _In_ const ULONG                                                                      numIrp
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    bool
    CreateCompletedOutputPacketList(
        _Out_writes_bytes_(packetsPerIrp * numIrp * sizeof(BUFFER_PROPERTY)) PBUFFER_PROPERTY outputBuffers,
        _In_ PBUFFER_PROPERTY                                                                 outputReminder,
        _In_ const ULONG                                                                      outputBuffersCount,
        _In_ const ULONG                                                                      packetsPerIrp,
        _In_ const ULONG                                                                      numIrp
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    bool
    IsInputPacketAtEstimatedPosition(
        _In_ ULONG inOffset
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    bool
    IsOutputPacketOverlapWithEstimatePosition(
        _In_ ULONG inOffset
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    bool
    IsOutputPacketAtEstimatedPosition();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void IncrementInputProcessedPacket();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void IncrementOutputProcessedPacket();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    bool
    IsTerminateStream();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    ULONG CalculateDropoutThresholdTime();

    static __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void ReportPacketLoopReason(
        _In_ LPCSTR           label,
        _In_ PacketLoopReason packetLoopReason
    );

    static __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void ClearOutputBuffer(
        _In_ UACSampleFormat                               currentSampleFormat,
        _Out_writes_bytes_(bytesPerBlock * samples) PUCHAR outBuffer,
        _In_ ULONG                                         outChannels,
        _In_ ULONG                                         bytesPerBlock,
        _In_ ULONG                                         samples
    );

    static __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void MixingEngineThreadFunction(
        _In_ PDEVICE_CONTEXT deviceContext
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void MixingEngineThreadMain(
        _In_ PDEVICE_CONTEXT deviceContext
    );

    const PDEVICE_CONTEXT m_deviceContext;

    TransferObject *     m_inputTransferObject[UAC_MAX_IRP_NUMBER]{};
    TransferObject *     m_outputTransferObject[UAC_MAX_IRP_NUMBER]{};
    TransferObject *     m_transferObjectFeedback[UAC_MAX_IRP_NUMBER]{};
    MixingEngineThread * m_mixingEngineThread{nullptr};

    LONG   m_pendingIrps{0};
    KEVENT m_noPendingIrpEvent{0};

    volatile ULONG m_streamStatus{toInt(StreamStatuses::NotStable)};
    bool           m_feedbackStable{false};

    LONG m_recoverActive{0};
    LONG m_requirePortReset{0};
    LONG m_donePortReset{0};

    bool m_IsTerminateStream{false};

    LONGLONG m_inputWritePosition{0LL};
    LONGLONG m_inputSyncPosition{0LL};
    LONGLONG m_inputPrevWritePosition{0LL};
    LONGLONG m_inputCompletedPosition{0LL};
    ULONG    m_inputNextIsoFrame{0};
    LONG     m_inputIsoFrameDelay{0};

    LONGLONG m_outputReadPosition{0LL};
    LONGLONG m_outputSyncPosition{0LL};
    ULONG    m_outputNextIsoFrame{0};
    LONG     m_outputIsoFrameDelay{0};
    LONG     m_outputRemainder{0};

    LONGLONG m_feedbackPosition{0LL};
    ULONG    m_feedbackRemainder{0};
    ULONG    m_lastFeedbackSize{0};
    ULONG    m_feedbackNextIsoFrame{0};
    ULONG    m_feedbackIsoFrameDelay{0};

    ULONG m_startIsoFrame{0};

    WDFSPINLOCK m_positionSpinLock{nullptr};

    LONG m_inputBytesLastOneSec{0};
    LONG m_inputProcessedFrames{0};
    LONG m_inputNextMeasureFrames{0};

    LONG m_outputBytesLastOneSec{0};
    LONG m_outputProcessedFrames{0};
    LONG m_outputNextMeasureFrames{0};

    LONG m_outputRequireZeroFill{0};

    LONG m_inputValidPackets{0};
    LONG m_outputValidPackets{0};

    // m_xxCompletedPacket is protected by SpinLock because it is operated within the DPC.

    WDFSPINLOCK m_packetSpinLock{nullptr};

    LONGLONG m_inputCompletedPacket{0LL};
    LONGLONG m_inputSyncPacket{0LL};
    LONGLONG m_inputEstimatedPacket{0LL};
    LONGLONG m_inputProcessedPacket{0LL};

    LONGLONG m_outputCompletedPacket{0LL};
    LONGLONG m_outputSyncPacket{0LL};
    LONGLONG m_outputProcessedPacket{0LL};

    LONGLONG m_asioReadyPosition{0LL};
    LONGLONG m_threadWakeUpCount{0LL};
    ULONG    m_bufferProcessed{0};

    LONGLONG m_outputAsioBufferedPosition{0LL};
    LONGLONG m_inputAsioBufferedPosition{0LL};

    typedef struct _ISO_REQUEST_COMPLETION_TIME
    {
        ULONGLONG LastTimeUs{0ULL};
        ULONGLONG LastPeriodUs{0ULL};
        ULONGLONG LastQPCPosition{0ULL};
        ULONGLONG LastPeriodQPCPosition{0ULL};
    } ISO_REQUEST_COMPLETION_TIME;

    ISO_REQUEST_COMPLETION_TIME m_inputIsoRequestCompletionTime;
    ISO_REQUEST_COMPLETION_TIME m_outputIsoRequestCompletionTime;
    ISO_REQUEST_COMPLETION_TIME m_feedbackIsoRequestCompletionTime;

    ULONG m_dopMarkerToggle{0};

    ULONGLONG m_startPCUs{0ULL};
    ULONGLONG m_elapsedPCUs{0ULL};
    ULONGLONG m_wakeUpDiffPCUs{0ULL};
    ULONGLONG m_lastWakePCUs{0ULL};

    ULONG m_usbBusTimeEstimated{0};
    ULONG m_usbBusTimePrev{0};

    ULONG m_syncElapsedTimeUs{0};
    ULONG m_asioElapsedTimeUs{0};

    ULONG m_dpcCompleteStatus{0};
    ULONG m_outCalculatedFactor{0};

    LONG m_compensateSamples{0};

    ULONG m_inputLastProcessedIrpIndex{0};
    ULONG m_outputLastProcessedIrpIndex{0};
    ULONG m_inputNextIrpIndex{0};
    ULONG m_outputNextIrpIndex{0};

    BUFFER_PROPERTY m_inputBuffers[UAC_MAX_IRP_NUMBER * UAC_MAX_CLASSIC_FRAMES_PER_IRP * 8]{};
    BUFFER_PROPERTY m_outputBuffers[UAC_MAX_IRP_NUMBER * UAC_MAX_CLASSIC_FRAMES_PER_IRP * 8]{};

    const StreamStatuses c_ioStable;
    const StreamStatuses c_ioStreaming;
    const StreamStatuses c_ioSteady;
};

#endif
