// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================
// ASIO is a trademark and software of Steinberg Media Technologies GmbH

/*++

Module Name:

    MixingEngineThread.cpp

Abstract:

    Implement a class to handle the mixing engine thread processing.

Environment:

    Kernel-mode Driver Framework

--*/

#include "Driver.h"
#include "Device.h"
#include "Public.h"
#include "Common.h"
#include "MixingEngineThread.h"

#ifndef __INTELLISENSE__
#include "MixingEngineThread.tmh"
#endif

_Use_decl_annotations_
PAGED_CODE_SEG
MixingEngineThread *
MixingEngineThread::CreateMixingEngineThread(
    PDEVICE_CONTEXT deviceContext,
    StreamObject *  streamObject,
    ULONG           newTimerResolution
)
{
    PAGED_CODE();

    return new (POOL_FLAG_NON_PAGED, DRIVER_TAG) MixingEngineThread(deviceContext, streamObject, newTimerResolution);
}

_Use_decl_annotations_
PAGED_CODE_SEG
MixingEngineThread::MixingEngineThread(
    PDEVICE_CONTEXT deviceContext,
    StreamObject *  streamObject,
    ULONG           newTimerResolution
)
    : WorkerThread(deviceContext), m_streamObject(streamObject), m_newTimerResolution(newTimerResolution)
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
MixingEngineThread::~MixingEngineThread()
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    Terminate();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
MixingEngineThread::CreateThread(WORKER_THREAD_FUNCTION mixingEngineThreadFunction, KPRIORITY priority, LONG wakeUpIntervalUs, ULONG classicFramesPerIrp)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    m_wakeUpIntervalUs = wakeUpIntervalUs;
    m_classicFramesPerIrp = classicFramesPerIrp;

    status = WorkerThread::CreateThread(mixingEngineThreadFunction, priority);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
void MixingEngineThread::ThreadMain()
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! initialize instance.");

    ULONG defaultTimerResolution = ExSetTimerResolution(0, FALSE);
    m_currentTimerResolution = defaultTimerResolution;

    auto threadMainScope = wil::scope_exit([&]() {
        if (m_currentTimerResolution != defaultTimerResolution)
        {
            ExSetTimerResolution(defaultTimerResolution, TRUE);
            m_currentTimerResolution = defaultTimerResolution;
        }
    });

    if (m_newTimerResolution < defaultTimerResolution)
    {
        m_currentTimerResolution = m_newTimerResolution;
        ExSetTimerResolution(m_currentTimerResolution, TRUE);
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "Timer resolution was changed, old %d, new %d", defaultTimerResolution, m_currentTimerResolution);
    }
    else
    {
        m_currentTimerResolution = defaultTimerResolution;
    }

    PEX_TIMER exTimer = ExAllocateTimer(nullptr, nullptr, EX_TIMER_HIGH_RESOLUTION);
    m_waitEvents[toInt(MixingEngineWaitEventsNumber::TimerEvent)] = exTimer;
    m_waitEvents[toInt(MixingEngineWaitEventsNumber::OutputReadyEvent)] = nullptr;
    m_waitEventsCount = toInt(MixingEngineWaitEventsNumber::NumOfWaitEventsWithoutOutputReady);

    KeSetEvent(&m_threadReadyEvent, EVENT_INCREMENT, FALSE);

    status = KeWaitForMultipleObjects(ARRAYSIZE(m_startEvents), m_startEvents, WaitAny, Executive, KernelMode, FALSE, nullptr, nullptr);
    if (!NT_SUCCESS(status) || (status == STATUS_WAIT_0))
    {
        goto ThreadMain_Exit;
    }

    LONGLONG duetime = 0ll - (LONGLONG)m_classicFramesPerIrp * 10000LL;
    LONGLONG period = (LONGLONG)m_wakeUpIntervalUs * 10LL;

    EXT_SET_PARAMETERS setParameters;

    ExInitializeSetTimerParameters(&setParameters);
    setParameters.NoWakeTolerance = 10LL * 10LL;

    ExSetTimer(exTimer, duetime, period, &setParameters);

    // ======================================================================
    ASSERT(m_workerThreadFunction != nullptr);
    m_workerThreadFunction(m_deviceContext, this);

    EXT_DELETE_PARAMETERS deleteParameters;

    ExInitializeDeleteTimerParameters(&deleteParameters);
    deleteParameters.DeleteCallback = nullptr;
    deleteParameters.DeleteContext = nullptr;

    ExDeleteTimer(exTimer, FALSE, FALSE, &deleteParameters);

ThreadMain_Exit:

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! exit instance.");
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG MixingEngineThread::GetCurrentTimerResolution()
{
    PAGED_CODE();

    return m_currentTimerResolution;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS MixingEngineThread::Wait()
{
    LARGE_INTEGER waitTimeout;
    waitTimeout.QuadPart = -100 * 1000 * 10; // 100ms

    PAGED_CODE();

    NTSTATUS wakeUpReason = STATUS_SUCCESS;
    wakeUpReason = KeWaitForMultipleObjects(
        m_waitEventsCount,
        m_waitEvents,
        WaitAny,
        Executive,
        KernelMode,
        FALSE,
        &waitTimeout,
        m_waitBlock
    );

    return wakeUpReason;
}

_Use_decl_annotations_
PAGED_CODE_SEG
StreamObject *
MixingEngineThread::GetStreamObject() const noexcept
{
    PAGED_CODE();

    return m_streamObject;
}

_Use_decl_annotations_
PAGED_CODE_SEG
void MixingEngineThread::SetOutputReadyEvent(
    PKEVENT outputReadyEvent
)
{
    PAGED_CODE();

    if (outputReadyEvent != nullptr)
    {
        m_waitEvents[toInt(MixingEngineWaitEventsNumber::OutputReadyEvent)] = outputReadyEvent;
        m_waitEventsCount = toInt(MixingEngineWaitEventsNumber::NumOfWaitEvents);
    }
    else
    {
        m_waitEvents[toInt(MixingEngineWaitEventsNumber::OutputReadyEvent)] = nullptr;
        m_waitEventsCount = toInt(MixingEngineWaitEventsNumber::NumOfWaitEventsWithoutOutputReady);
    }
}
