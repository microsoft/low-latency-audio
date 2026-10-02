// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================
// ASIO is a trademark and software of Steinberg Media Technologies GmbH

/*++

Module Name:

    WorkerThread.cpp

Abstract:

    Implement a class to handle the worker thread processing.

Environment:

    Kernel-mode Driver Framework

--*/

#include "Driver.h"
#include "Device.h"
#include "Public.h"
#include "Common.h"
#include "WorkerThread.h"

#ifndef __INTELLISENSE__
#include "WorkerThread.tmh"
#endif

_Use_decl_annotations_
PAGED_CODE_SEG
WorkerThread *
WorkerThread::CreateWorkerThread(
    PDEVICE_CONTEXT deviceContext
)
{
    PAGED_CODE();

    return new (POOL_FLAG_NON_PAGED, DRIVER_TAG) WorkerThread(deviceContext);
}

_Use_decl_annotations_
PAGED_CODE_SEG
WorkerThread::WorkerThread(
    PDEVICE_CONTEXT deviceContext
)
    : m_deviceContext(deviceContext)
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
WorkerThread::~WorkerThread()
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    Terminate();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
WorkerThread::CreateThread(
    WORKER_THREAD_FUNCTION workerThreadFunction,
    KPRIORITY              priority
)
{
    NTSTATUS status = STATUS_SUCCESS;
    HANDLE   thread = nullptr;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    m_workerThreadFunction = workerThreadFunction;

    status = PsCreateSystemThread(&thread, THREAD_ALL_ACCESS, nullptr, nullptr, nullptr, ThreadRoutine, this);
    IF_FAILED_JUMP(status, CreateThread_Exit);

    status = ObReferenceObjectByHandle(thread, THREAD_ALL_ACCESS, nullptr, KernelMode, (PVOID *)&m_thread, nullptr);
    ZwClose(thread);
    thread = nullptr;
    IF_FAILED_JUMP(status, CreateThread_Exit);

    KeSetPriorityThread(m_thread, priority); // TBD

    m_threadEvents[0] = (PVOID)&m_threadReadyEvent;
    m_threadEvents[1] = (PVOID)m_thread;

    status = KeWaitForMultipleObjects(ARRAYSIZE(m_threadEvents), m_threadEvents, WaitAny, Executive, KernelMode, FALSE, nullptr, nullptr);
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
void WorkerThread::Terminate()
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
void WorkerThread::WakeUp()
{
    KeSetEvent(&(m_threadWakeUpEvent), IO_SOUND_INCREMENT, FALSE);
}

_Use_decl_annotations_
PAGED_CODE_SEG
void WorkerThread::ThreadRoutine(
    PVOID startContext
)
{
    WorkerThread * workerThread = (WorkerThread *)startContext;

    PAGED_CODE();

    ASSERT(workerThread != nullptr);

    workerThread->ThreadMain();

    PsTerminateSystemThread(STATUS_SUCCESS);
}

_Use_decl_annotations_
PAGED_CODE_SEG
void WorkerThread::ThreadMain()
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! initialize instance.");

    KeSetEvent(&m_threadReadyEvent, EVENT_INCREMENT, FALSE);

    status = KeWaitForMultipleObjects(ARRAYSIZE(m_startEvents), m_startEvents, WaitAny, Executive, KernelMode, FALSE, nullptr, nullptr);
    if (!NT_SUCCESS(status) || (status == STATUS_WAIT_0))
    {
        goto ThreadMain_Exit;
    }

    // ======================================================================
    ASSERT(m_workerThreadFunction != nullptr);
    m_workerThreadFunction(m_deviceContext, this);

ThreadMain_Exit:

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! exit instance.");
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS WorkerThread::Wait()
{
    PAGED_CODE();

    NTSTATUS wakeUpReason = STATUS_SUCCESS;
    wakeUpReason = KeWaitForMultipleObjects(
        m_waitEventsCount,
        m_waitEvents,
        WaitAny,
        Executive,
        KernelMode,
        false,
        nullptr,
        nullptr
    );

    return wakeUpReason;
}
