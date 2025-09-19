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

typedef void (*MIXING_ENGINE_THREAD_FUNCTION)(_In_ PDEVICE_CONTEXT DeviceContext);

enum class WaitEventsNumber
{
    KillEvent,
    WakeUpEvent,
    TimerEvent,
    OutputReadyEvent,
    NumOfWaitEvents = 4,
    NumOfWaitEventsWithoutOutputReady = 3,
    NumOfStartEvents = 2,
    NumOfThreadEvents = 2
};

constexpr int toInt(WaitEventsNumber eventsNumber)
{
    return static_cast<int>(eventsNumber);
}

class MixingEngineThread
{
  public:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    MixingEngineThread(
        _In_ PDEVICE_CONTEXT deviceContext,
        _In_ ULONG           newTimerResolution
    );

    virtual __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ~MixingEngineThread();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    CreateThread(
        _In_ MIXING_ENGINE_THREAD_FUNCTION mixingEngineThreadFunction,
        _In_ KPRIORITY                     priority,
        _In_ LONG                          wakeUpIntervalUs
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void Terminate();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void WakeUp();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void ClearWakeUpCount();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void IncrementWakeUpCount();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ULONG GetCurrentTimerResolution();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS Wait();

    static __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    MixingEngineThread * CreateMixingEngineThread(
        _In_ PDEVICE_CONTEXT deviceContext,
        _In_ ULONG           newTimerResolution
    );

  protected:
    const int WakeupIntervalUsDefault = 10 * 1000;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS AllocateBufferProperties();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void FreeBufferProperties();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    static KSTART_ROUTINE ThreadRoutine;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void ThreadMain();

    const PDEVICE_CONTEXT m_deviceContext;
    const ULONG           m_newTimerResolution;
    ULONG                 m_currentTimerResolution{0};
    KEVENT                m_threadReadyEvent{0};
    KEVENT                m_threadStartEvent{0};
    KEVENT                m_threadKillEvent{0};
    KEVENT                m_threadWakeUpEvent{0};
    PKTHREAD              m_thread{nullptr};
    LONG                  m_wakeUpIntervalUs{0};

    PVOID m_startEvents[toInt(WaitEventsNumber::NumOfStartEvents)] = {
        (PVOID)&m_threadKillEvent,
        (PVOID)&m_threadStartEvent
    };
    PVOID m_waitEvents[toInt(WaitEventsNumber::NumOfWaitEvents)] = {
        (PVOID)&m_threadKillEvent,
        (PVOID)&m_threadWakeUpEvent,
        nullptr,
        nullptr
    };
    ULONG       m_waitEnvetsCount{0};
    KWAIT_BLOCK m_waitBlock[toInt(WaitEventsNumber::NumOfWaitEvents)]{};
    PVOID       m_threadEvents[toInt(WaitEventsNumber::NumOfThreadEvents)] = {
        (PVOID)&m_threadReadyEvent,
        (PVOID) nullptr // m_thread
    };
    MIXING_ENGINE_THREAD_FUNCTION m_mixingEngineThreadFunction{nullptr};
};

#endif
