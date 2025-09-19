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
    ULONG           newTimerResolution
)
{
    PAGED_CODE();

    return new (POOL_FLAG_NON_PAGED, DRIVER_TAG) MixingEngineThread(deviceContext, newTimerResolution);
}

_Use_decl_annotations_
PAGED_CODE_SEG
MixingEngineThread::MixingEngineThread(
    PDEVICE_CONTEXT deviceContext,
    ULONG           newTimerResolution
)
    : m_deviceContext(deviceContext), m_newTimerResolution(newTimerResolution)
{
    // NTSTATUS              status = STATUS_SUCCESS;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    KeInitializeEvent(&m_threadStartEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&m_threadReadyEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&m_threadKillEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&m_threadWakeUpEvent, SynchronizationEvent, FALSE);

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
MixingEngineThread::CreateThread(MIXING_ENGINE_THREAD_FUNCTION mixingEngineThreadFunction, KPRIORITY priority, LONG wakeUpIntervalUs)
{
    NTSTATUS status = STATUS_SUCCESS;
    HANDLE   thread = nullptr;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    m_mixingEngineThreadFunction = mixingEngineThreadFunction;
    m_wakeUpIntervalUs = wakeUpIntervalUs;

    status = PsCreateSystemThread(&thread, THREAD_ALL_ACCESS, nullptr, nullptr, nullptr, ThreadRoutine, this);
    IF_FAILED_JUMP(status, CreateThread_Exit);

    status = ObReferenceObjectByHandle(thread, THREAD_ALL_ACCESS, nullptr, KernelMode, (PVOID *)&m_thread, nullptr);
    ZwClose(thread);
    thread = nullptr;
    IF_FAILED_JUMP(status, CreateThread_Exit);

    KeSetPriorityThread(m_thread, priority); // TBD

    m_threadEvents[0] = (PVOID)&m_threadReadyEvent;
    m_threadEvents[1] = (PVOID)m_thread;

    status = KeWaitForMultipleObjects(sizeof(m_threadEvents) / sizeof(m_threadEvents[0]), m_threadEvents, WaitAny, Executive, KernelMode, FALSE, nullptr, nullptr);
    if (status == STATUS_WAIT_0)
    {
        status = STATUS_SUCCESS;
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "Thread was terminated before ready.");
    }
    KeSetEvent(&m_threadStartEvent, EVENT_INCREMENT, FALSE);

CreateThread_Exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
void MixingEngineThread::Terminate()
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    if (m_thread != nullptr)
    {
        KeSetEvent(&m_threadKillEvent, EVENT_INCREMENT, FALSE);
        KeWaitForSingleObject(m_thread, Executive, KernelMode, FALSE, nullptr);
        ObDereferenceObject(m_thread);
        m_thread = nullptr;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void MixingEngineThread::WakeUp()
{
    KeSetEvent(&(m_threadWakeUpEvent), IO_SOUND_INCREMENT, FALSE);
}

_Use_decl_annotations_
PAGED_CODE_SEG
void MixingEngineThread::ThreadRoutine(
    PVOID startContext
)
{
    MixingEngineThread * mixingEngineThread = (MixingEngineThread *)startContext;

    PAGED_CODE();

    ASSERT(mixingEngineThread != nullptr);

    mixingEngineThread->ThreadMain();

    PsTerminateSystemThread(STATUS_SUCCESS);
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
    m_waitEvents[toInt(WaitEventsNumber::TimerEvent)] = exTimer;
    m_waitEnvetsCount = toInt(WaitEventsNumber::NumOfWaitEventsWithoutOutputReady);

#if 0
	if ((deviceExtension->AsioBufferObject != nullptr) && (deviceExtension->AsioBufferObject->OutputReadyEvent != nullptr))
	{
		m_waitEvents[toInt(WaitEventsNumber::OutputReadyEvent)] = deviceExtension->AsioBufferObject->OutputReadyEvent;
		m_waitEnvetsCount = toInt(WaitEventsNumber::NumOfWaitEventsWithoutOutputReady);
	}
#endif
    KeSetEvent(&m_threadReadyEvent, EVENT_INCREMENT, FALSE);

    status = KeWaitForMultipleObjects(sizeof(m_startEvents) / sizeof(m_startEvents[0]), m_startEvents, WaitAny, Executive, KernelMode, FALSE, nullptr, nullptr);
    if (!NT_SUCCESS(status) || (status == STATUS_WAIT_0))
    {
        goto ThreadMain_Exit;
    }

    LARGE_INTEGER maxDueTime;
    maxDueTime.QuadPart = 0ll - (LONGLONG)m_deviceContext->ClassicFramesPerIrp * 10000LL;

    LARGE_INTEGER duetime;
    duetime.QuadPart = maxDueTime.QuadPart;

    EXT_SET_PARAMETERS setParameters;

    ExInitializeSetTimerParameters(&setParameters);
    setParameters.NoWakeTolerance = 10LL * 10LL;

    ExSetTimer(exTimer, duetime.QuadPart, 100LL * 10LL, &setParameters);

    // ======================================================================
    ASSERT(m_mixingEngineThreadFunction != nullptr);
    m_mixingEngineThreadFunction(m_deviceContext);

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
        m_waitEnvetsCount,
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
