// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

/*++

Module Name:

    MixingEngineThread.h

Abstract:

    Define a class to handle the mixing engine thread processing.

Environment:

    Kernel-mode Driver Framework

--*/

#ifndef _MIXINGENGINETHREAD_H_
#define _MIXINGENGINETHREAD_H_

#include <acx.h>
#include "WorkerThread.h"

class StreamObject;

enum class MixingEngineWaitEventsNumber
{
    KillEvent,
    WakeUpEvent,
    TimerEvent,
    OutputReadyEvent,
    NumOfWaitEvents = 4,
    NumOfWaitEventsWithoutOutputReady = 3
};

constexpr int toInt(MixingEngineWaitEventsNumber eventsNumber)
{
    return static_cast<int>(eventsNumber);
}

class MixingEngineThread : public WorkerThread
{
  public:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    MixingEngineThread(
        _In_ PDEVICE_CONTEXT deviceContext,
        _In_ StreamObject *  streamObject,
        _In_ ULONG           newTimerResolution
    );

    virtual __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ~MixingEngineThread();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    CreateThread(
        _In_ WORKER_THREAD_FUNCTION mixingEngineThreadFunction,
        _In_ KPRIORITY              priority,
        _In_ LONG                   wakeUpIntervalUs,
        _In_ ULONG                  classicFramesPerIrp
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ULONG GetCurrentTimerResolution();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS Wait();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    StreamObject * GetStreamObject() const noexcept;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void SetOutputReadyEvent(
        _In_ PKEVENT outputReadyEvent
    );

    static __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    MixingEngineThread * CreateMixingEngineThread(
        _In_ PDEVICE_CONTEXT deviceContext,
        _In_ StreamObject *  streamObject,
        _In_ ULONG           newTimerResolution
    );

  protected:
    const int WakeupIntervalUsDefault = 10 * 1000;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual void ThreadMain();

    StreamObject * m_streamObject{nullptr};
    const ULONG    m_newTimerResolution;
    ULONG          m_currentTimerResolution{0};
    LONG           m_wakeUpIntervalUs{0};
    KWAIT_BLOCK    m_waitBlock[toInt(MixingEngineWaitEventsNumber::NumOfWaitEvents)]{};
    PVOID          m_waitEvents[toInt(MixingEngineWaitEventsNumber::NumOfWaitEvents)] = {
        (PVOID)&m_threadKillEvent,
        (PVOID)&m_threadWakeUpEvent,
        nullptr,
        nullptr
    };
    ULONG m_waitEventsCount{0};
    ULONG m_classicFramesPerIrp{1};
};

#endif
