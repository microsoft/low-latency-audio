// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

/*++

Module Name:

    WorkerThread.h

Abstract:

    Define a class to handle the worker thread processing.

Environment:

    Kernel-mode Driver Framework

--*/

#ifndef _WORKERTHREAD_H_
#define _WORKERTHREAD_H_

#include <acx.h>

class WorkerThread;

typedef void (*WORKER_THREAD_FUNCTION)(_In_ PDEVICE_CONTEXT DeviceContext, _In_ WorkerThread * ThisThread);

enum class WaitEventsNumber
{
    KillEvent,
    WakeUpEvent,
    NumOfWaitEvents = 2,
    NumOfStartEvents = 2,
    NumOfThreadEvents = 2
};

constexpr int toInt(WaitEventsNumber eventsNumber)
{
    return static_cast<int>(eventsNumber);
}

class WorkerThread
{
  public:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    WorkerThread(
        _In_ PDEVICE_CONTEXT deviceContext
    );

    virtual __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ~WorkerThread();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    CreateThread(
        _In_ WORKER_THREAD_FUNCTION workerThreadFunction,
        _In_ KPRIORITY              priority
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual void Terminate();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    virtual void WakeUp();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS Wait();

    static __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    WorkerThread * CreateWorkerThread(
        _In_ PDEVICE_CONTEXT deviceContext
    );

  protected:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    static KSTART_ROUTINE ThreadRoutine;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual void ThreadMain();

    const PDEVICE_CONTEXT m_deviceContext;
    KEVENT                m_threadReadyEvent{0};
    KEVENT                m_threadStartEvent{0};
    KEVENT                m_threadKillEvent{0};
    KEVENT                m_threadWakeUpEvent{0};
    PKTHREAD              m_thread{nullptr};

    PVOID m_startEvents[toInt(WaitEventsNumber::NumOfStartEvents)] = {
        (PVOID)&m_threadKillEvent,
        (PVOID)&m_threadStartEvent
    };
    PVOID m_threadEvents[toInt(WaitEventsNumber::NumOfThreadEvents)] = {
        (PVOID)&m_threadReadyEvent,
        (PVOID) nullptr // m_thread
    };
    WORKER_THREAD_FUNCTION m_workerThreadFunction{nullptr};

  protected:
    PVOID m_waitEvents[toInt(WaitEventsNumber::NumOfWaitEvents)] = {
        (PVOID)&m_threadKillEvent,
        (PVOID)&m_threadWakeUpEvent,
    };
    ULONG m_waitEventsCount{toInt(WaitEventsNumber::NumOfWaitEvents)};
};

#endif
