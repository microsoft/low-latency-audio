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

    StreamObject.cpp

Abstract:

    Implement a class to manage USB streaming.

Environment:

    Kernel-mode Driver Framework

--*/

#include "Driver.h"
#include "Device.h"
#include "Public.h"
#include "Common.h"
#include "USBAudio.h"
#include "USBAudioConfiguration.h"
#include "StreamObject.h"
#include "StreamEngine.h"
#include "ErrorStatistics.h"
#include "TransferObject.h"
#include "RtPacketObject.h"
#include "AsioBufferObject.h"
#include "AudioIsochronousEngine.h"

#ifndef __INTELLISENSE__
#include "StreamObject.tmh"
#endif

_Use_decl_annotations_
PAGED_CODE_SEG
StreamObject * StreamObject::Create(
    PDEVICE_CONTEXT          deviceContext,
    AudioIsochronousEngine * audioIsochronousEngine,
    const StreamStatuses     ioStable,
    const StreamStatuses     ioStreaming,
    const StreamStatuses     ioSteady
)
{
    PAGED_CODE();

    return new (POOL_FLAG_NON_PAGED, DRIVER_TAG) StreamObject(deviceContext, audioIsochronousEngine, ioStable, ioStreaming, ioSteady);
}

_Use_decl_annotations_
PAGED_CODE_SEG
StreamObject::StreamObject(
    PDEVICE_CONTEXT          deviceContext,
    AudioIsochronousEngine * audioIsochronousEngine,
    const StreamStatuses     ioStable,
    const StreamStatuses     ioStreaming,
    const StreamStatuses     ioSteady
)
    : m_deviceContext(deviceContext), m_audioIsochronousEngine(audioIsochronousEngine), c_ioStable(ioStable), c_ioStreaming(ioStreaming), c_ioSteady(ioSteady)
{
    WDF_OBJECT_ATTRIBUTES attributes;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = deviceContext->Device;
    WdfSpinLockCreate(&attributes, &m_positionSpinLock);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = deviceContext->Device;
    WdfSpinLockCreate(&attributes, &m_packetSpinLock);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
StreamObject::~StreamObject()
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    TerminateMixingEngineThread();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
void StreamObject::ResetNextMeasureFrames(
    LONG inputMeasureFrames,
    LONG outputMeasureFrames
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    m_inputProcessedFrames = 0;
    m_inputBytesLastOneSec = 0;
    m_outputProcessedFrames = 0;
    m_outputBytesLastOneSec = 0;

    m_inputNextMeasureFrames = inputMeasureFrames;
    m_outputNextMeasureFrames = outputMeasureFrames;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void StreamObject::SetStartIsoFrame(
    ULONG currentFrame,
    LONG  outputFrameDelay
)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    WdfSpinLockAcquire(m_positionSpinLock);
    m_inputNextIsoFrame = m_outputNextIsoFrame = m_feedbackNextIsoFrame = m_startIsoFrame = currentFrame;

    if (outputFrameDelay >= 0)
    {
        m_outputNextIsoFrame += outputFrameDelay;
    }
    else
    {
        m_inputNextIsoFrame += (ULONG)(0 - outputFrameDelay);
    }

    WdfSpinLockRelease(m_positionSpinLock);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void StreamObject::SetIsoFrameDelay(
    ULONG firstPacketLatency
)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    WdfSpinLockAcquire(m_positionSpinLock);
    m_inputIsoFrameDelay =
        m_outputIsoFrameDelay =
            m_feedbackIsoFrameDelay = firstPacketLatency;

    WdfSpinLockRelease(m_positionSpinLock);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
void StreamObject::SetTransferObject(
    LONG             index,
    IsoDirection     direction,
    TransferObject * transferObject
)
{
    class TransferObject ** transferObjectArray = nullptr;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    ASSERT(index < UAC_MAX_IRP_NUMBER);

    switch (direction)
    {
    case IsoDirection::In:
        transferObjectArray = &(m_inputTransferObject[index]);
        break;
    case IsoDirection::Out:
        transferObjectArray = &(m_outputTransferObject[index]);
        break;
    case IsoDirection::Feedback:
        transferObjectArray = &(m_transferObjectFeedback[index]);
        break;
    default:
        ASSERT((direction == IsoDirection::In) || (direction == IsoDirection::Out) || (direction == IsoDirection::Feedback));
        break;
    }

    if (transferObject != nullptr)
    {
        ASSERT(transferObjectArray != nullptr);
        if (transferObjectArray != nullptr)
        {
            *transferObjectArray = transferObject;
        }
    }
    else
    {
        ASSERT(transferObjectArray != nullptr);
        if (transferObjectArray != nullptr)
        {
            *transferObjectArray = nullptr;
        }
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
TransferObject *
StreamObject::GetTransferObject(
    LONG         index,
    IsoDirection direction
)
{
    TransferObject * transferObject = nullptr;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    ASSERT(index < UAC_MAX_IRP_NUMBER);

    switch (direction)
    {
    case IsoDirection::In:
        transferObject = m_inputTransferObject[index];
        break;
    case IsoDirection::Out:
        transferObject = m_outputTransferObject[index];
        break;
    case IsoDirection::Feedback:
        transferObject = m_transferObjectFeedback[index];
        break;
    default:
        ASSERT((direction == IsoDirection::In) || (direction == IsoDirection::Out) || (direction == IsoDirection::Feedback));
        break;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");

    return transferObject;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
ULONG
StreamObject::GetStartFrame(
    IsoDirection direction,
    ULONG        numPackets
)
{
    ULONG startFrame = 0;
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    WdfSpinLockAcquire(m_positionSpinLock);

    switch (direction)
    {
    case IsoDirection::In: {
        startFrame = m_inputNextIsoFrame + m_inputIsoFrameDelay;
        // m_inputNextIsoFrame = startFrame + m_audioIsochronousEngine->GetAudioStreamPropertySet().ClassicFramesPerIrp;
        m_inputNextIsoFrame = startFrame + ((numPackets << (m_audioIsochronousEngine->GetInputInterfaceAndPipe().PipeInfo.Interval - 1)) / m_deviceContext->FramesPerMs);
        m_inputIsoFrameDelay = 0;
    }
    break;
    case IsoDirection::Out: {
        startFrame = m_outputNextIsoFrame + m_outputIsoFrameDelay;
        // m_outputNextIsoFrame = startFrame + m_audioIsochronousEngine->GetAudioStreamPropertySet().ClassicFramesPerIrp;
        m_outputNextIsoFrame = startFrame + ((numPackets << (m_audioIsochronousEngine->GetOutputInterfaceAndPipe().PipeInfo.Interval - 1)) / m_deviceContext->FramesPerMs);
        m_outputIsoFrameDelay = 0;
    }
    break;
    case IsoDirection::Feedback: {
        startFrame = m_feedbackNextIsoFrame + m_feedbackIsoFrameDelay;
        // m_feedbackNextIsoFrame = startFrame + m_audioIsochronousEngine->GetAudioStreamPropertySet().ClassicFramesPerIrp;
        m_feedbackNextIsoFrame = startFrame + ((numPackets << (m_audioIsochronousEngine->GetAudioStreamPropertySet().FeedbackProperty.FeedbackInterval - 1)) / m_deviceContext->FramesPerMs);
        m_feedbackIsoFrameDelay = 0;
    }
    break;
    default:
        ASSERT((direction == IsoDirection::In) || (direction == IsoDirection::Out));
        break;
    }

    WdfSpinLockRelease(m_positionSpinLock);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");

    return startFrame;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
StreamObject::CancelRequestAll()
{
    NTSTATUS status = STATUS_SUCCESS;
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    for (ULONG index = 0; index < UAC_MAX_IRP_NUMBER; index++)
    {
        if (m_inputTransferObject[index] != nullptr)
        {
            m_inputTransferObject[index]->CancelRequest();
        }
        if (m_outputTransferObject[index] != nullptr)
        {
            m_outputTransferObject[index]->CancelRequest();
        }
        if (m_transferObjectFeedback[index] != nullptr)
        {
            m_transferObjectFeedback[index]->CancelRequest();
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
StreamObject::Cleanup()
{
    NTSTATUS status = STATUS_SUCCESS;
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    for (ULONG index = 0; index < UAC_MAX_IRP_NUMBER; index++)
    {
        if (m_inputTransferObject[index] != nullptr)
        {
            m_inputTransferObject[index]->CancelRequest();
            delete m_inputTransferObject[index];
            m_inputTransferObject[index] = nullptr;
        }
        if (m_outputTransferObject[index] != nullptr)
        {
            m_outputTransferObject[index]->CancelRequest();
            delete m_outputTransferObject[index];
            m_outputTransferObject[index] = nullptr;
        }

        if (m_transferObjectFeedback[index] != nullptr)
        {
            m_transferObjectFeedback[index]->CancelRequest();
            delete m_transferObjectFeedback[index];
            m_transferObjectFeedback[index] = nullptr;
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
void StreamObject::ResetIsoRequestCompletionTime()
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    m_inputIsoRequestCompletionTime.LastTimeUs = 0ULL;
    m_inputIsoRequestCompletionTime.LastPeriodUs = 0ULL;
    m_outputIsoRequestCompletionTime.LastTimeUs = 0ULL;
    m_outputIsoRequestCompletionTime.LastPeriodUs = 0ULL;
    m_feedbackIsoRequestCompletionTime.LastTimeUs = 0ULL;
    m_feedbackIsoRequestCompletionTime.LastPeriodUs = 0ULL;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

// The first time a Complete Request is made, 0 is returned as the interval time.
_Use_decl_annotations_
NONPAGED_CODE_SEG
void StreamObject::CompleteRequest(
    const IsoDirection direction,
    const ULONGLONG    currentTimeUs,
    const ULONGLONG    qpcPosition,
    ULONGLONG &        periodUs,
    ULONGLONG &        periodQPCPosition
)
{
    ISO_REQUEST_COMPLETION_TIME & isoRequestCompletionTime = (direction == IsoDirection::In) ? m_inputIsoRequestCompletionTime : (direction == IsoDirection::Out) ? m_outputIsoRequestCompletionTime
                                                                                                                                                                  : m_feedbackIsoRequestCompletionTime;
    ULONGLONG                     lastTimeUs = isoRequestCompletionTime.LastTimeUs;
    ULONGLONG                     timeDiffUs = (LONGLONG)currentTimeUs - lastTimeUs;
    ULONG                         thresholdUs = CalculateDropoutThresholdTime();

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - currentTimeUs, qpcPosition, %s, %llu, %llu,", GetDirectionString(direction), currentTimeUs, qpcPosition);

    if ((lastTimeUs != 0) && (timeDiffUs > thresholdUs))
    {
        if ((m_audioIsochronousEngine->GetAsioBufferObject() != nullptr) && m_audioIsochronousEngine->GetAsioBufferObject()->IsRecHeaderRegistered())
        {
            m_audioIsochronousEngine->GetAsioBufferObject()->SetRecDeviceStatus(DeviceStatuses::OverloadDetected);
        }
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "process transfer %s: dropout detected. Elapsed time after previous DPC: %llu us, threshold %uus.", GetDirectionString(direction), timeDiffUs, thresholdUs);
        m_deviceContext->ErrorStatistics->LogErrorOccurrence(ErrorStatus::DropoutDetectedElapsedTime, (ULONG)(timeDiffUs - thresholdUs));
    }
#ifdef BUFFER_THREAD_STATISTICS
    if (streamObject->NumInDpcStats < YUA_DPC_STATISTICS_SIZE)
    {
        streamObject->InDpcStats[streamObject->NumInDpcStats].PerformanceCounterIn.QuadPart = currentPc.QuadPart * 1000000ULL / performanceFreq.QuadPart;
    }
#endif

    if (isoRequestCompletionTime.LastTimeUs == 0ULL)
    {
        isoRequestCompletionTime.LastPeriodUs = 0ULL;
        isoRequestCompletionTime.LastPeriodQPCPosition = 0ULL;
    }
    else
    {
        isoRequestCompletionTime.LastPeriodUs = currentTimeUs - isoRequestCompletionTime.LastTimeUs;
        isoRequestCompletionTime.LastPeriodQPCPosition = qpcPosition - isoRequestCompletionTime.LastQPCPosition;
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - isoRequestCompletionTime.LastPeriodUs          = currentTimeUs - isoRequestCompletionTime.LastTimeUs,      %llu, %llu, %llu", isoRequestCompletionTime.LastPeriodUs, currentTimeUs, isoRequestCompletionTime.LastTimeUs);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - isoRequestCompletionTime.LastPeriodQPCPosition = qpcPosition   - isoRequestCompletionTime.LastQPCPosition, %llu, %llu, %llu", isoRequestCompletionTime.LastPeriodQPCPosition, qpcPosition, isoRequestCompletionTime.LastQPCPosition);

    isoRequestCompletionTime.LastTimeUs = currentTimeUs;
    isoRequestCompletionTime.LastQPCPosition = qpcPosition;

    periodUs = isoRequestCompletionTime.LastPeriodUs;
    periodQPCPosition = isoRequestCompletionTime.LastPeriodQPCPosition;

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - isoRequestCompletionTime.LastTimeUs, isoRequestCompletionTime.LastQPCPosition, %llu, %llu", isoRequestCompletionTime.LastTimeUs, isoRequestCompletionTime.LastQPCPosition);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - qpcPosition, periodUs, periodQPCPosition, %llu, %llu, %llu", qpcPosition, periodUs, periodQPCPosition);
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
bool StreamObject::CalculateSampleRate(
    const bool       input,
    const ULONG      bytesPerBlock,
    const ULONG      packetsPerSec,
    const ULONG      length,
    volatile ULONG & measuredSampleRate
)
{
    volatile LONG * processedFrames = 0;
    volatile LONG * bytesLastOneSec = nullptr;
    LONG *          nextMeasureFrames = nullptr;
    bool            updated = false;

    if (input)
    {
        processedFrames = &m_inputProcessedFrames;
        bytesLastOneSec = &m_inputBytesLastOneSec;
        nextMeasureFrames = &m_inputNextMeasureFrames;
    }
    else
    {
        processedFrames = &m_outputProcessedFrames;
        bytesLastOneSec = &m_outputBytesLastOneSec;
        nextMeasureFrames = &m_outputNextMeasureFrames;
    }

    InterlockedIncrement(processedFrames);
    InterlockedExchangeAdd(bytesLastOneSec, (LONG)length);

    ASSERT(bytesPerBlock != 0);
    if (*processedFrames >= *nextMeasureFrames)
    {
        updated = true;
        InterlockedExchangeAdd(nextMeasureFrames, packetsPerSec);
        LONG bytesOneSec = InterlockedExchange(bytesLastOneSec, 0);
        measuredSampleRate = bytesOneSec / bytesPerBlock;
    }
    return updated;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
ULONG
StreamObject::CalculateTransferSizeAndSetURB(
    const LONG  index,
    PURB        urb,
    const ULONG startFrame,
    const ULONG numPackets,
    const ULONG lockDelayCount,
    LONG *      asyncPacketsCount,
    LONG *      syncPacketsCount
)
{
    LONGLONG inPosition = m_inputWritePosition;
    LONGLONG readPosition = m_outputReadPosition;
    ULONG    transferSamples = 0;
    ULONG    transferSize = 0;
    LONGLONG requiredSamples = 0;

    // Use only the contents of the previous IRP
    ULONG irpIndex = (index == 0) ? m_audioIsochronousEngine->GetAudioStreamPropertySet().InternalParameters.MaxIrpNumber - 1 : index - 1;

    requiredSamples = 0;

    if (m_transferObjectFeedback[irpIndex] != nullptr)
    {
        requiredSamples = m_transferObjectFeedback[irpIndex]->GetFeedbackSamples();
        if (requiredSamples != 0)
        {
            m_transferObjectFeedback[irpIndex]->SetFeedbackSamples(0);
        }
    }
    else if (m_inputTransferObject[irpIndex] != nullptr)
    {
        requiredSamples = m_inputTransferObject[irpIndex]->GetFeedbackSamples();
        if (requiredSamples != 0)
        {
            m_inputTransferObject[irpIndex]->SetFeedbackSamples(0);
        }
    }
    m_inputPrevWritePosition = inPosition;

    if ((m_audioIsochronousEngine->GetAudioStreamPropertySet().IsDeviceSynchronous) || ((m_streamStatus & toInt(c_ioStable)) != (ULONG)toInt(c_ioStable) && !m_feedbackStable) || (lockDelayCount != 0) || (requiredSamples < numPackets))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "preparing output packets by calculation (independent to input)...");
        transferSamples = 0;

        LONG  remainder = m_audioIsochronousEngine->GetAudioStreamPropertySet().AudioProperty.SampleRate % m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.PacketsPerSec;
        ULONG rounded = m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.SamplesPerPacket * m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.PacketsPerSec;
        if (((m_streamStatus & toInt(c_ioStable)) == (ULONG)toInt(c_ioStable)) && (m_audioIsochronousEngine->GetAudioStreamPropertySet().InputProperty.MeasuredSampleRate != 0) && (m_deviceContext->UsbAudioConfiguration->GetClockEntityCountForTerminal() == 1))
        {
            remainder = ((LONG)m_audioIsochronousEngine->GetAudioStreamPropertySet().InputProperty.MeasuredSampleRate - (LONG)rounded) % (LONG)m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.PacketsPerSec;
        }
        for (ULONG i = 0; i < numPackets; ++i)
        {
            ULONG samples = m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.SamplesPerPacket;
            m_outputRemainder += remainder;
            if (m_outputRemainder - (LONG)m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.PacketsPerSec >= 0)
            {
                ++samples;
                m_outputRemainder -= m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.PacketsPerSec;
                // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "Frame %u Packet %u: adjusting sample +1, %u samples, measured %u Hz, remainder %d, sum %d.",startFrame,i,samples,m_audioIsochronousEngine->GetAudioStreamPropertySet().AudioProperty.InMeasuredSampleRate,remainder,m_outputRemainder);
                if (m_compensateSamples < 0)
                {
                    ++m_compensateSamples;
                    --samples;
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "Frame %u Packet %u: compensating sample -1, %u samples.", startFrame, i, samples);
                }
            }
            else if (m_outputRemainder + (LONG)m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.PacketsPerSec <= 0)
            {
                --samples;
                m_outputRemainder += m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.PacketsPerSec;
                // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "Frame %u Packet %u: adjusting sample -1, %u samples, measured %u Hz, remainder %d, sum %d.",startFrame,i,samples,m_audioIsochronousEngine->GetAudioStreamPropertySet().AudioProperty.InMeasuredSampleRate,remainder,m_outputRemainder);
            }
            else
            {
                if (m_compensateSamples > 0)
                {
                    --m_compensateSamples;
                    ++samples;
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "Frame %u Packet %u: compensating sample +1, %u samples.", startFrame, i, samples);
                }
            }

            ULONG packetSize = samples * m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.BytesPerBlock;
            if (transferSize + packetSize > m_audioIsochronousEngine->GetOutputInterfaceAndPipe().MaximumTransferSize)
            {
                packetSize = 0;
            }
            urb->UrbIsochronousTransfer.IsoPacket[i].Offset = transferSize;
            urb->UrbIsochronousTransfer.IsoPacket[i].Length = packetSize;
            transferSize += packetSize;
            InterlockedIncrement(asyncPacketsCount);
        }
        if (m_audioIsochronousEngine->GetAudioStreamPropertySet().IsDeviceSynchronous && (m_streamStatus & toInt(c_ioStable)) == (ULONG)toInt(c_ioStable))
        {
            transferSamples = transferSize / m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.BytesPerBlock;
            m_outputSyncPosition += transferSize;
        }
        if (!m_audioIsochronousEngine->GetAudioStreamPropertySet().IsDeviceSynchronous && ((m_streamStatus & toInt(c_ioStable)) == (ULONG)toInt(c_ioStable) || m_feedbackStable))
        {
            // In cases where the OUT DPC comes back before the IN DPC,
            // the number of samples is calculated based on the theoretical value and sent.
            transferSamples = transferSize / m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.BytesPerBlock;

            if (m_feedbackStable && (m_transferObjectFeedback[index] != nullptr))
            {
                m_transferObjectFeedback[index]->SetPresendSamples(transferSamples);
            }
            else if ((m_streamStatus & toInt(c_ioStable)) == (ULONG)toInt(c_ioStable) && (lockDelayCount == 0) && (m_inputTransferObject[index] != nullptr))
            {
                m_inputTransferObject[index]->SetPresendSamples(transferSamples);
            }

            m_outputSyncPosition += transferSize;
        }
    }
    else
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "preparing output packets by feedback (input or feedback endpoint)...");

        ULONG remainSamples = static_cast<ULONG>(requiredSamples);
        if (m_compensateSamples != 0)
        {
            remainSamples = (ULONG)((LONG)remainSamples + m_compensateSamples);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "compensating %d samples ", m_compensateSamples);
            m_compensateSamples = 0;
        }

        ULONG limitSamplesPerPacket = min((m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.MaxSamplesPerPacket), (m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.SamplesPerPacket + 1));
        if (remainSamples > limitSamplesPerPacket * numPackets)
        {
            m_compensateSamples = remainSamples - (limitSamplesPerPacket * numPackets);
            // Packet size is limited so that packets larger than MaximumPacketSize are not sent.
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "transfer size (%d) exceeds limit (%d).", remainSamples, limitSamplesPerPacket * numPackets);
            remainSamples = limitSamplesPerPacket * numPackets;
        }
        transferSamples = remainSamples;
        ULONG framesPerPacket = remainSamples / numPackets;
        ULONG remainder = remainSamples % numPackets;
        ULONG remainderSum = numPackets - 1;
        for (ULONG i = 0; i < numPackets; ++i)
        {
            ULONG samples = framesPerPacket;
            remainderSum += remainder;
            if (remainderSum >= numPackets)
            {
                ++samples;
                remainderSum -= numPackets;
            }

            ULONG packetSize = samples * m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.BytesPerBlock;
            if ((samples < m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.SamplesPerPacket - 1) || (samples > m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.SamplesPerPacket + 1))
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "Abnormal output packet size, %d samples, frame %u, iso packet %u.", samples, startFrame, i);
            }
            if (transferSize + packetSize > m_audioIsochronousEngine->GetOutputInterfaceAndPipe().MaximumTransferSize)
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "Transfer size exceeds limit, size %u bytes, limit %u bytes, frame %u, iso packet %u.", transferSize + packetSize, m_audioIsochronousEngine->GetOutputInterfaceAndPipe().MaximumTransferSize, startFrame, i);
                packetSize = 0;
            }
            urb->UrbIsochronousTransfer.IsoPacket[i].Offset = transferSize;
            urb->UrbIsochronousTransfer.IsoPacket[i].Length = packetSize;
            transferSize += packetSize;
            LONG packetsCount = InterlockedIncrement(syncPacketsCount);
            if (packetsCount == 1)
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "output state changed from async to sync, frame %d.", startFrame);
            }
        }
        m_outputSyncPosition += transferSize;
    }
    if (m_audioIsochronousEngine->HasInputIsochronousInterface())
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "initialized OUT URB. in sample %I64d, out sample %I64d, startFrame %d, %u bytes", inPosition / m_audioIsochronousEngine->GetAudioStreamPropertySet().InputProperty.BytesPerBlock, readPosition / m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.BytesPerBlock, startFrame, transferSize);
    }
    else
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "initialized OUT URB. out sample %I64d, startFrame %d, %u bytes", readPosition / m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.BytesPerBlock, startFrame, transferSize);
    }
    m_outputReadPosition += transferSize;

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "transferSize = %u, read position = %llu", transferSize, m_outputReadPosition);
    return transferSize;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
StreamObject::CreateMixingEngineThread(
    KPRIORITY priority,
    LONG      wakeUpIntervalUs
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    ASSERT(m_deviceContext != nullptr);
    if (m_mixingEngineThread == nullptr)
    {
        m_mixingEngineThread = MixingEngineThread::CreateMixingEngineThread(m_deviceContext, this, 1000);
        IF_TRUE_ACTION_JUMP(m_mixingEngineThread == nullptr, status = STATUS_INSUFFICIENT_RESOURCES, CreateMixingEngineThread_Exit);

        status = m_mixingEngineThread->CreateThread(MixingEngineThreadFunction, priority, wakeUpIntervalUs, m_audioIsochronousEngine->GetAudioStreamPropertySet().ClassicFramesPerIrp);
    }

CreateMixingEngineThread_Exit:

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
void StreamObject::TerminateMixingEngineThread()
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    if (m_mixingEngineThread != nullptr)
    {
        m_mixingEngineThread->Terminate();
        delete m_mixingEngineThread;
        m_mixingEngineThread = nullptr;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void StreamObject::WakeupMixingEngineThread()
{
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");
    if (m_mixingEngineThread != nullptr)
    {
        m_mixingEngineThread->WakeUp();
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS StreamObject::Wait()
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");
    if (m_mixingEngineThread != nullptr)
    {
        status = m_mixingEngineThread->Wait();
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit %!STATUS!", status);
    return status;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
StreamStatuses StreamObject::GetStreamStatuses(bool & isProcessIo)
{
    StreamStatuses status;

    WdfSpinLockAcquire(m_positionSpinLock);

    status = static_cast<StreamStatuses>(m_streamStatus);
    if (m_audioIsochronousEngine->HasInputAndOutputIsochronousInterfaces())
    {
        isProcessIo = m_inputLastProcessedIrpIndex == m_outputLastProcessedIrpIndex;
    }
    else
    {
        isProcessIo = true;
    }

    WdfSpinLockRelease(m_positionSpinLock);

    return status;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
StreamStatuses StreamObject::GetStreamStatuses()
{
    StreamStatuses status;

    WdfSpinLockAcquire(m_positionSpinLock);

    status = static_cast<StreamStatuses>(m_streamStatus);

    WdfSpinLockRelease(m_positionSpinLock);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
void StreamObject::ClearWakeUpCount()
{
    PAGED_CODE();

    m_threadWakeUpCount = 0LL;
}

_Use_decl_annotations_
PAGED_CODE_SEG
void StreamObject::IncrementWakeUpCount()
{
    PAGED_CODE();

    m_threadWakeUpCount++;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool StreamObject::IsFirstWakeUp()
{
    PAGED_CODE();

    return (m_threadWakeUpCount <= 1);
}

_Use_decl_annotations_
PAGED_CODE_SEG
void StreamObject::SaveStartPCUs()
{
    ULONGLONG currentTimePC = 0ULL;

    PAGED_CODE();

    m_startPCUs = USBAudioAcxDriverStreamGetCurrentTimeUs(m_deviceContext, &currentTimePC);
    m_elapsedPCUs = 0ULL;
    m_wakeUpDiffPCUs = 0ULL;
    m_lastWakePCUs = 0ULL;
    m_syncElapsedTimeUs = 0;
    m_asioElapsedTimeUs = 0;
}

_Use_decl_annotations_
PAGED_CODE_SEG
void StreamObject::SaveWakeUpTimePCUs(ULONGLONG currentTimePCUs)
{
    PAGED_CODE();

    m_elapsedPCUs = currentTimePCUs - m_startPCUs;
    m_wakeUpDiffPCUs = currentTimePCUs - m_lastWakePCUs;
    m_lastWakePCUs = currentTimePCUs;
    // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - current time %llu us, elapsed %llu us, wakeup diff %llu us, last wakeup %llu us", currentTimePCUs, m_elapsedPCUs, m_wakeUpDiffPCUs, m_lastWakePCUs);
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONGLONG
StreamObject::GetWakeUpDiffPCUs()
{
    PAGED_CODE();

    return m_wakeUpDiffPCUs;
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG
StreamObject::EstimateUSBBusTime(
    ULONG usbBusTimeCurrent,
    ULONG wakeupDiffPCUs
)
{
    ULONG usbBusTimeDiff = 0;

    PAGED_CODE();

    if (IsFirstWakeUp())
    {
        // First loop
        usbBusTimeDiff = 0;
        m_usbBusTimeEstimated = 0;
        m_usbBusTimePrev = usbBusTimeCurrent;
    }
    else if (m_usbBusTimeEstimated != 0)
    {
        // If an estimated value was used last time, measure the difference between the estimated value and the current value.
        if ((usbBusTimeCurrent < m_usbBusTimeEstimated) || ((usbBusTimeCurrent - m_usbBusTimeEstimated) > m_audioIsochronousEngine->GetAudioStreamPropertySet().ClassicFramesPerIrp))
        {
            // If the guessed value is wrong, erase it as a fixed value.
            usbBusTimeDiff = 0;
        }
        else
        {
            usbBusTimeDiff = usbBusTimeCurrent - m_usbBusTimeEstimated;
        }
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "USB bus time recovered, current %x prev %x, assuming Tdiff %u", usbBusTimeCurrent, m_usbBusTimePrev, usbBusTimeDiff);
        m_usbBusTimePrev = usbBusTimeCurrent;
        m_usbBusTimeEstimated = 0;
    }
    else if ((usbBusTimeCurrent < m_usbBusTimePrev) || ((usbBusTimeCurrent - m_usbBusTimePrev) > m_audioIsochronousEngine->GetAudioStreamPropertySet().ClassicFramesPerIrp))
    {
        // When an abnormal value is detected in BusTime,the elapsed time is estimated from Performance Counter.
        usbBusTimeDiff = (wakeupDiffPCUs + 500) / 1000;
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "USB bus time error, current %x prev %x, assuming Tdiff %u", usbBusTimeCurrent, m_usbBusTimePrev, usbBusTimeDiff);
        m_deviceContext->ErrorStatistics->LogErrorOccurrence(ErrorStatus::IllegalBusTime, 0);
        m_usbBusTimeEstimated = m_usbBusTimePrev + usbBusTimeDiff;
    }
    else
    {
        usbBusTimeDiff = usbBusTimeCurrent - m_usbBusTimePrev;
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "USB bus time is normal, current %x prev %x, assuming Tdiff %u", usbBusTimeCurrent, m_usbBusTimePrev, usbBusTimeDiff);
        m_usbBusTimePrev = usbBusTimeCurrent;
    }

    return usbBusTimeDiff;
}

_Use_decl_annotations_
PAGED_CODE_SEG
void StreamObject::UpdateElapsedTimeUs(ULONG wakeUpDiffPCUs)
{
    PAGED_CODE();

    m_syncElapsedTimeUs += wakeUpDiffPCUs;
    m_asioElapsedTimeUs += wakeUpDiffPCUs;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool StreamObject::IsOverrideIgnoreEstimation()
{
    bool overrideIgnoreEstimation = false;

    PAGED_CODE();

    ASSERT(m_mixingEngineThread != nullptr);

    // if(m_audioIsochronousEngine->GetAsioBufferObject() != nullptr && (m_mixingEngineThread->GetCurrentTimerResolution * 2) > (m_audioIsochronousEngine->GetAsioBufferObject().BufferPeriod * 10000000 / m_audioIsochronousEngine->GetAudioStreamPropertySet().AudioProperty.SampleRate)){
    // 	overrideIgnoreEstimation = true;
    // }

    return overrideIgnoreEstimation;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void StreamObject::GetCompletedPacket(
    LONGLONG & inCompletedPacket,
    LONGLONG & outCompletedPacket
)
{
    WdfSpinLockAcquire(m_packetSpinLock);
    inCompletedPacket = m_inputCompletedPacket;
    outCompletedPacket = m_outputCompletedPacket;
    WdfSpinLockRelease(m_packetSpinLock);
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void StreamObject::GetCompletedPacket(
    bool       isInput,
    LONGLONG & completedPacket
)
{
    WdfSpinLockAcquire(m_packetSpinLock);
    if (isInput)
    {
        completedPacket = m_inputCompletedPacket;
    }
    else
    {
        completedPacket = m_outputCompletedPacket;
    }
    WdfSpinLockRelease(m_packetSpinLock);
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void StreamObject::UpdateCompletedPacket(
    bool  isInput,
    ULONG index,
    ULONG numberOfPackets
)
{
    ULONG currentPacketNumber = 0;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry, %s, %u, %u", isInput ? "Input" : "Output", numberOfPackets, index);

    WdfSpinLockAcquire(m_packetSpinLock);
    if (isInput)
    {
        currentPacketNumber = (ULONG)((m_inputCompletedPacket / numberOfPackets) % m_audioIsochronousEngine->GetAudioStreamPropertySet().InternalParameters.MaxIrpNumber);
        m_inputCompletedPacket += numberOfPackets;
    }
    else
    {
        currentPacketNumber = (ULONG)((m_outputCompletedPacket / numberOfPackets) % m_audioIsochronousEngine->GetAudioStreamPropertySet().InternalParameters.MaxIrpNumber);
        m_outputCompletedPacket += numberOfPackets;
        if (!m_audioIsochronousEngine->HasInputIsochronousInterface())
        {
            m_inputCompletedPacket = m_outputCompletedPacket;
        }
    }
    WdfSpinLockRelease(m_packetSpinLock);

    if (currentPacketNumber != index)
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "IN current packet number %u does not match transfer index %u", currentPacketNumber, index);
        InterlockedIncrement(&m_requirePortReset);
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
void StreamObject::DeterminePacket(
    const LONGLONG inCompletedPacket,
    const ULONG    usbBusTimeDiff,
    const ULONG    packetsPerIrp,
    const ULONG    packetsPerMs
)
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry, %llu, %u, %u, %u", inCompletedPacket, usbBusTimeDiff, packetsPerIrp, packetsPerMs);

    if (IsOverrideIgnoreEstimation())
    {
        // Ignore callback time calculations entirely and process all INs as soon as they are recognized
        m_inputSyncPacket = m_inputEstimatedPacket = inCompletedPacket;
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " -  In sync packet %llu, estimated packet %llu, completed packet %llu", m_inputSyncPacket, m_inputEstimatedPacket, inCompletedPacket);
    }
    else if (m_inputSyncPacket == inCompletedPacket)
    {
        // If the number of INs completed has not changed since the previous loop,
        // predict the position of the packet to be currently processed according to the USB bus time elapsed since the previous loop.
        LONG packetRoom = (LONG)(m_inputSyncPacket - m_inputEstimatedPacket);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " -  packetRoom %u, In sync packet %llu,  estimated packet %llu", packetRoom, m_inputSyncPacket, m_inputEstimatedPacket);
        if (packetRoom > 0)
        {
            ULONG packetsPerUsbBusTimeDiff = usbBusTimeDiff * packetsPerMs;
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " -  packetRoom %u, usb bus time diff %u, frames per ms %u, packets per usb bus time diff %u", packetRoom, usbBusTimeDiff, m_deviceContext->FramesPerMs, packetsPerUsbBusTimeDiff);
            if (packetRoom > (LONG)packetsPerUsbBusTimeDiff)
            {
                m_inputEstimatedPacket += packetsPerUsbBusTimeDiff;
            }
            else
            {
                m_inputEstimatedPacket += packetRoom;
            }
        }
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " -  In sync packet %llu, estimated packet %llu, completed packet %llu", m_inputSyncPacket, m_inputEstimatedPacket, inCompletedPacket);
    }
    else
    {
        // If an IN is found for the first time in this loop
        m_inputSyncPacket = inCompletedPacket;
        m_inputEstimatedPacket = m_inputSyncPacket - packetsPerIrp;
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " -  In sync packet %llu, estimated packet %llu, completed packet %llu", m_inputSyncPacket, m_inputEstimatedPacket, inCompletedPacket);
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool StreamObject::CreateCompletedInputPacketList(
    PBUFFER_PROPERTY inputBuffers,
    PBUFFER_PROPERTY inputRemainder,
    const ULONG      packetsPerIrp,
    const ULONG      numIrp
)
{
    bool  inProcessRemainder = false;
    ULONG evaluatedPacketsCount = 0;
    ULONG obtainedBuffersCount = 0;

    PAGED_CODE();
    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    // >>comment-001<<
    for (ULONG i = 0; i < (packetsPerIrp * numIrp); ++i)
    {
        // Count packets that have completed isochronous IN processing and create a list

        // If inputRemainder->Buffer is not nullptr, it is assumed that there is valid remainder in inputRemainder and it is assigned to InBuffer[0].
        if ((inputRemainder->Buffer != nullptr) && (i == 0))
        {
            inputBuffers[i] = *inputRemainder;
            inProcessRemainder = true;
            // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - in process remainder true, transfer object %p", inputBuffers[i].TransferObject);
        }
        else
        {
            ULONG            irp = (ULONG)(((m_inputProcessedPacket + evaluatedPacketsCount) / packetsPerIrp) % numIrp);
            ULONG            packet = (ULONG)((m_inputProcessedPacket + evaluatedPacketsCount) % packetsPerIrp);
            TransferObject * transferObject = m_inputTransferObject[irp];
            // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - irp %u, transfer object %p", irp, transferObject);
            if (transferObject != nullptr)
            {
                inputBuffers[i].Buffer = transferObject->GetRecordedIsoPacketBuffer(packet);
                inputBuffers[i].Length = transferObject->GetRecordedIsoPacketLength(packet);
                inputBuffers[i].TotalProcessedBytesSoFar = transferObject->GetTotalProcessedBytesSoFar(packet);
                inputBuffers[i].Offset = 0;
                inputBuffers[i].Irp = irp;
                inputBuffers[i].Packet = packet;
                inputBuffers[i].TransferObject = transferObject;
                if (inputBuffers[i].Length != 0)
                {
                    obtainedBuffersCount++;
                }
            }

            ++evaluatedPacketsCount;
        }
    }

    // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - evaluatedPacketsCount, obtainedBuffersCount, %u, %u", evaluatedPacketsCount, obtainedBuffersCount);
    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit, %u %u", evaluatedPacketsCount, obtainedBuffersCount);

    return inProcessRemainder;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool StreamObject::CreateCompletedOutputPacketList(
    PBUFFER_PROPERTY outputBuffers,
    PBUFFER_PROPERTY outputRemainder,
    const ULONG      outputBuffersCount,
    const ULONG      packetsPerIrp,
    const ULONG      numIrp
)
{
    bool outProcessRemainder = false;

    PAGED_CODE();
    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    if (outputBuffersCount < (packetsPerIrp * numIrp))
    {
        outputBuffers[outputBuffersCount] = *outputRemainder;

        if (outputBuffers[outputBuffersCount].Buffer != nullptr)
        {
            outProcessRemainder = true;
        }
        else
        {
            ULONG            irp = (ULONG)((m_outputProcessedPacket / packetsPerIrp) % numIrp);
            ULONG            packet = (ULONG)(m_outputProcessedPacket % packetsPerIrp);
            TransferObject * transferObject = m_outputTransferObject[irp];
            // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - irp %u, transfer object %p, index %d", irp, transferObject, (transferObject != nullptr)? transferObject->GetIndex(): -1);
            if (transferObject != nullptr)
            {
                outputBuffers[outputBuffersCount].Buffer = transferObject->GetRecordedIsoPacketBuffer(packet);
                outputBuffers[outputBuffersCount].Length = transferObject->GetRecordedIsoPacketLength(packet);
                outputBuffers[outputBuffersCount].TotalProcessedBytesSoFar = transferObject->GetTotalProcessedBytesSoFar(packet);
                outputBuffers[outputBuffersCount].Offset = 0;
                outputBuffers[outputBuffersCount].Irp = irp;
                outputBuffers[outputBuffersCount].Packet = packet;
                outputBuffers[outputBuffersCount].TransferObject = transferObject;
                // if (transferObject->MarkForAsio[packet] == 0 && handleAsioBuffer)
                // {
                // 	transferObject->MarkForAsio[packet] = 1;
                // }
            }
        }
    }
    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");

    return outProcessRemainder;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool StreamObject::IsInputPacketAtEstimatedPosition(
    _In_ ULONG inOffsetFrame
)
{
    PAGED_CODE();

    // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - In processed packet %llu, offset %u, estimated packet %llu", m_inputProcessedPacket, inOffsetFrame, m_inputEstimatedPacket);

    return (m_inputProcessedPacket + inOffsetFrame) >= m_inputEstimatedPacket;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool StreamObject::IsOutputPacketOverlapWithEstimatePosition(
    _In_ const ULONG outLimit,
    _In_ const ULONG inputInterval,
    _In_ const ULONG outputInterval
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - Out processed packet %llu, estimated packet %llu, out limit %u", m_outputProcessedPacket, m_inputEstimatedPacket, outLimit);

    if (inputInterval == outputInterval)
    {
        // Use Input m_inputEstimatedPacket
        return m_outputProcessedPacket >= (m_inputEstimatedPacket + outLimit);
    }
    else if (inputInterval > outputInterval)
    {
        return m_outputProcessedPacket >= ((m_inputEstimatedPacket << (inputInterval - outputInterval)) + outLimit);
    }
    else // (inputInterval < outputInterval)
    {
        return (m_outputProcessedPacket << (outputInterval - inputInterval)) >= (m_inputEstimatedPacket + (outLimit << (outputInterval - inputInterval)));
    }
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool StreamObject::IsOutputPacketAtEstimatedPosition(
    _In_ const ULONG inputInterval,
    _In_ const ULONG outputInterval
)
{
    bool result = false;
    PAGED_CODE();

    if (inputInterval == 0)
    {
        // When input is disabled, m_inputEstimatedPacket is also generated by the output side, so the interval is not used.
        result = (m_outputProcessedPacket >= m_inputEstimatedPacket);
    }
    else
    {
        if (inputInterval == outputInterval)
        {
            result = (m_outputProcessedPacket >= m_inputEstimatedPacket);
        }
        else if (inputInterval > outputInterval)
        {
            result = (m_outputProcessedPacket >= (m_inputEstimatedPacket << (inputInterval - outputInterval)));
        }
        else // (inputInterval < outputInterval)
        {
            result = (m_outputProcessedPacket << (outputInterval - inputInterval)) >= m_inputEstimatedPacket;
        }
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - Out processed packet %llu, estimated packet %llu, sync packet %llu, result %!bool!", m_outputProcessedPacket, m_inputEstimatedPacket, m_inputSyncPacket, result);

    return result;
}

_Use_decl_annotations_
PAGED_CODE_SEG
void StreamObject::IncrementInputProcessedPacket(
)
{
    PAGED_CODE();

    m_inputProcessedPacket++;
}

_Use_decl_annotations_
PAGED_CODE_SEG
void StreamObject::IncrementOutputProcessedPacket()
{
    PAGED_CODE();

    m_outputProcessedPacket++;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
bool StreamObject::CheckInputStability(
    ULONG index,
    ULONG numberOfPacketsInThisIrp,
    ULONG startFrameInThisIrp,
    ULONG transferredBytesInThisIrp,
    ULONG invalidPacket
)
{
    ULONG transferredSamplesInThisIrp = transferredBytesInThisIrp / m_audioIsochronousEngine->GetAudioStreamPropertySet().InputProperty.BytesPerBlock;

    WdfSpinLockAcquire(m_positionSpinLock);
    m_inputLastProcessedIrpIndex = index;
    m_inputCompletedPosition += transferredSamplesInThisIrp;

    if (!(m_streamStatus & toInt(StreamStatuses::InputStable)))
    {
        if (invalidPacket == 0)
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "now input may be stable, frame %d, transferred bytes %d.", startFrameInThisIrp, transferredBytesInThisIrp);
            InterlockedOr(reinterpret_cast<volatile LONG *>(&m_streamStatus), toInt(StreamStatuses::InputStable));
            if ((m_streamStatus & toInt(StreamStatuses::OutputStable)) != 0)
            {
                WdfSpinLockRelease(m_positionSpinLock);
                return false;
            }
        }
        else
        {
            WdfSpinLockRelease(m_positionSpinLock);
            return false;
        }
    }
    else
    {
        // If an abnormally small number of packets arrive after stabilization, it will raise an error and reset.
        // abnormally small number = 1 sample or less per packet.
        if (((m_deviceContext->DeviceClass == USB_AUDIO_CLASS) || (m_deviceContext->DeviceProtocol == NS_USBAudio0200::AF_VERSION_02_00)) &&
            (transferredSamplesInThisIrp < numberOfPacketsInThisIrp))
        {
            WdfSpinLockRelease(m_positionSpinLock);
            // return STATUS_UNSUCCESSFUL;
            return false;
        }
    }
    WdfSpinLockRelease(m_positionSpinLock);

    return true;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void StreamObject::UpdatePositionsIn(ULONG length)
{
    WdfSpinLockAcquire(m_positionSpinLock);
    m_inputWritePosition += length;
    m_inputSyncPosition += length;
    ++m_inputValidPackets;
    WdfSpinLockRelease(m_positionSpinLock);
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
ULONG StreamObject::UpdatePositionsFeedback(
    TransferObject * transferObject,
    ULONG            feedbackSum,
    ULONG            validFeedback
)
{
    feedbackSum <<= (m_audioIsochronousEngine->GetAudioStreamPropertySet().FeedbackProperty.FeedbackInterval - 1);
    feedbackSum += m_feedbackRemainder;
    m_feedbackRemainder = 0;

    if (validFeedback != 0)
    {
        if ((m_deviceContext->IsDeviceSuperSpeed && m_deviceContext->SuperSpeedCompatible) || (m_deviceContext->IsDeviceHighSpeed))
        {
            // For high-speed endpoints, the value is treated as a fixed-point number in 16.16 format.
            m_lastFeedbackSize = feedbackSum / 0x10000;
            m_feedbackRemainder = feedbackSum % 0x10000;
        }
        else
        {
            // For full-speed endpoints, the value is treated as a fixed-point number in 10.14 format.
            m_lastFeedbackSize = feedbackSum / 0x4000;
            m_feedbackRemainder = feedbackSum % 0x4000;
        }

        {
            m_feedbackPosition += m_lastFeedbackSize;
            transferObject->SetFeedbackSamples(m_lastFeedbackSize);
        }
    }

    return m_lastFeedbackSize;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void StreamObject::SetInputStable()
{
    if (!(m_streamStatus & toInt(StreamStatuses::InputStable)))
    {
        InterlockedOr(reinterpret_cast<volatile LONG *>(&m_streamStatus), toInt(StreamStatuses::InputStable));
    }
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void StreamObject::SetOutputStable()
{
    if (!(m_streamStatus & toInt(StreamStatuses::OutputStable)))
    {
        InterlockedOr(reinterpret_cast<volatile LONG *>(&m_streamStatus), toInt(StreamStatuses::OutputStable));
    }
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void StreamObject::SetInputStreaming()
{
    if (!(m_streamStatus & toInt(StreamStatuses::InputStreaming)))
    {
        InterlockedOr(reinterpret_cast<volatile LONG *>(&m_streamStatus), toInt(StreamStatuses::InputStreaming));
    }
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void StreamObject::SetOutputStreaming(
    ULONG Index,
    ULONG LockDelayCount
)
{
    if (LockDelayCount == 0)
    {
        if (!(m_streamStatus & toInt(StreamStatuses::OutputStreaming)))
        {
            InterlockedOr(reinterpret_cast<volatile LONG *>(&m_streamStatus), toInt(StreamStatuses::OutputStreaming));
        }
    }
    WdfSpinLockAcquire(m_positionSpinLock);
    m_outputLastProcessedIrpIndex = Index;
    WdfSpinLockRelease(m_positionSpinLock);
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
bool StreamObject::IsIoSteady()
{
    bool isIoSteady = false;

    WdfSpinLockAcquire(m_positionSpinLock);

    isIoSteady = (m_streamStatus == (ULONG)toInt(c_ioSteady));

    WdfSpinLockRelease(m_positionSpinLock);

    return isIoSteady;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void StreamObject::SetFeedbackStale(
    const ULONG startFrame,
    const ULONG feedbackValue
)
{
    if (!m_feedbackStable)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "now feedback may be stable, frame %u, value %08x.", startFrame, feedbackValue);
        m_feedbackStable = true;
    }
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
bool StreamObject::IsFeedbackStable()
{
    return m_feedbackStable;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void StreamObject::AddCompensateSamples(
    LONG nonFeedbackSamples
)
{
    m_compensateSamples += nonFeedbackSamples;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
bool StreamObject::IsTerminateStream()
{
    return InterlockedCompareExchange(&m_IsTerminateStream, 0, 0) ? true : false;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void StreamObject::SetTerminateStream()
{
    InterlockedExchange(&m_IsTerminateStream, (ULONG) true);
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
ULONG StreamObject::CalculateDropoutThresholdTime()
{
    // 2 * 1000 microseconds, margin of 500 microseconds.
    return (ULONG)((m_audioIsochronousEngine->GetAudioStreamPropertySet().ClassicFramesPerIrp * 2 * 1000) - 500);
}

_Use_decl_annotations_
PAGED_CODE_SEG
void StreamObject::ReportPacketLoopReason(
    LPCSTR           label,
    PacketLoopReason packetLoopReason
)
{
    PAGED_CODE();

    switch (packetLoopReason)
    {
    case PacketLoopReason::ContinueLoop:
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%s: ContinueLoop", label);
        break;
    case PacketLoopReason::ExitLoopListCycleCompleted:
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%s: ExitLoopListCycleCompleted", label);
        break;
    case PacketLoopReason::ExitLoopAsioNotifyTimeExceeded:
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%s: ExitLoopAsioNotifyTimeExceeded", label);
        break;
    case PacketLoopReason::ExitLoopPacketEstimateReached:
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%s: ExitLoopPacketEstimateReached", label);
        break;
    case PacketLoopReason::ExitLoopNoMoreAsioBuffers:
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%s: ExitLoopNoMoreAsioBuffers", label);
        break;
    case PacketLoopReason::ExitLoopAtAsioBoundary:
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%s: ExitLoopAtAsioBoundary", label);
        break;
    case PacketLoopReason::ExitLoopAfterSafetyOffset:
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%s: ExitLoopAfterSafetyOffset", label);
        break;
    case PacketLoopReason::ExitLoopAtInSync:
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%s: ExitLoopAtInSync", label);
        break;
    case PacketLoopReason::ExitLoopToPreventOutOverlap:
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%s: ExitLoopToPreventOutOverlap", label);
        break;
    default:
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%s: packetLoopReason is illegal.", label);
        break;
    }
}

_Use_decl_annotations_
PAGED_CODE_SEG
void StreamObject::ClearOutputBuffer(
    UACSampleFormat currentSampleFormat,
    PUCHAR          outBuffer,
    ULONG /* outChannels */,
    ULONG bytesPerBlock,
    ULONG samples
)
{
    PAGED_CODE();
    switch (currentSampleFormat)
    {
    case UACSampleFormat::UAC_SAMPLE_FORMAT_PCM:
    case UACSampleFormat::UAC_SAMPLE_FORMAT_IEEE_FLOAT:
        RtlZeroMemory(outBuffer, samples * bytesPerBlock);
        break;
    default:
        // TBD
        // Clear with zeros according to the audio format.
        RtlZeroMemory(outBuffer, samples * bytesPerBlock);
        break;
    }
}

_Use_decl_annotations_
PAGED_CODE_SEG
void StreamObject::MixingEngineThreadFunction(
    PDEVICE_CONTEXT deviceContext,
    WorkerThread *  thisThread
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    MixingEngineThread * mixingEngineThread = (MixingEngineThread *)thisThread;
    StreamObject *       streamObject = mixingEngineThread->GetStreamObject();

    ASSERT(streamObject != nullptr);
    ASSERT(streamObject->m_deviceContext != nullptr);
    ASSERT(streamObject->m_deviceContext == deviceContext);

    streamObject->MixingEngineThreadMain(deviceContext);
    // streamObject->MixingEngineThreadMainWithoutASIO(deviceContext);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
void StreamObject::MixingEngineThreadMain(
    PDEVICE_CONTEXT deviceContext
)
{
    ULONGLONG       currentTimePCUs = 0ULL;
    ULONGLONG       currentTimePC = 0ULL;
    const ULONG     inputInterval = m_audioIsochronousEngine->GetInputInterfaceAndPipe().PipeInfo.Interval;
    const ULONG     outputInterval = m_audioIsochronousEngine->GetOutputInterfaceAndPipe().PipeInfo.Interval;
    const ULONG     inputPacketsPerMs = m_deviceContext->FramesPerMs >> (inputInterval - 1);
    const ULONG     outputPacketsPerMs = m_deviceContext->FramesPerMs >> (outputInterval - 1);
    const ULONG     inputPacketsPerIrp = m_audioIsochronousEngine->GetAudioStreamPropertySet().ClassicFramesPerIrp * inputPacketsPerMs;
    const ULONG     outputPacketsPerIrp = m_audioIsochronousEngine->GetAudioStreamPropertySet().ClassicFramesPerIrp * outputPacketsPerMs;
    const ULONG     numIrp = m_audioIsochronousEngine->GetAudioStreamPropertySet().InternalParameters.MaxIrpNumber;
    bool            safetyOffsetApplied = false;
    BUFFER_PROPERTY inRemainder{};
    BUFFER_PROPERTY outRemainder{};
    ULONGLONG       lastAsioNotifyPCUs = 0ULL;
    ULONGLONG       lastInProcessedPCUs = 0ULL;
    LONG            curAsioMeasuredPeriodUs = 0;
    LONG            prevAsioMeasuredPeriodUs = 0;
    bool            outputReadyInThisPeriod = false;
    bool            outputReadyInPrevPeriod = true;
    LONG            curClientProcessingTimeUs = 0;
    LONG            prevClientProcessingTimeUs = 0;
    LARGE_INTEGER   timerExpired = {0};
    LONGLONG        inBuffersTotalCount = 0ULL;
    LONGLONG        outBuffersTotalCount = 0ULL;
    LONGLONG        asioNotifyCount = 0LL;
    const bool      hasInputIsochronousInterface = m_audioIsochronousEngine->HasInputIsochronousInterface();
    const bool      hasOutputIsochronousInterface = m_audioIsochronousEngine->HasOutputIsochronousInterface();

    PAGED_CODE();

    for (;;)
    {
        NTSTATUS wakeupReason = STATUS_SUCCESS;
        bool     isProcessIo = false;
        wakeupReason = Wait();

        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "MaxingEngineThreadMain() WakeUp reason = %d", static_cast<int>(wakeupReason));

        // If the wakeup result is an error, exit.
        if (!NT_SUCCESS(wakeupReason) || (wakeupReason == STATUS_WAIT_0) || IsTerminateStream())
        {
            break;
        }

        // Get the current status of stream.
        StreamStatuses streamStatus = GetStreamStatuses(isProcessIo);

        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "GetStreamStatuses() %s, %d", isProcessIo ? "true" : "false", static_cast<int>(streamStatus));

        // Updated valid wake-up count.
        // Since timerExpired and ThreadWakeup are initialized and updated at the same time, they will be made common.
        IncrementWakeUpCount();

        // Gets highly accurate current time based on KeQueryPerformanceCounter.
        currentTimePCUs = USBAudioAcxDriverStreamGetCurrentTimeUs(deviceContext, &currentTimePC);

        SaveWakeUpTimePCUs(currentTimePCUs);

        ULONG pcDiffUs = static_cast<ULONG>(GetWakeUpDiffPCUs());

        LONG inElapsedTimeAfterDpc = 0;

        if (hasInputIsochronousInterface)
        {
            inElapsedTimeAfterDpc = (LONG)((LONGLONG)currentTimePCUs - m_inputIsoRequestCompletionTime.LastTimeUs);
        }
        else
        {
            inElapsedTimeAfterDpc = (LONG)((LONGLONG)currentTimePCUs - m_outputIsoRequestCompletionTime.LastTimeUs);
        }

        m_audioIsochronousEngine->AcquireAsioWaitLock();
        if ((m_audioIsochronousEngine->GetAsioBufferObject() != nullptr) && m_audioIsochronousEngine->GetAsioBufferObject()->IsRecBufferReady() && m_audioIsochronousEngine->GetAsioBufferObject()->IsRecHeaderRegistered() && (asioNotifyCount > 1))
        {
            ULONG thresholdUs = CalculateDropoutThresholdTime();
            if (inElapsedTimeAfterDpc > (LONG)thresholdUs)
            {
#ifdef BUFFER_THREAD_STATISTICS
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%03u.%02u: mixing engine thread: dropout detected. Long elapsed time after IN DPC, cur %dus, threshold %uus, stats %u.", (LONG)(m_elapsedPCUs / 60000000), (LONG)(m_elapsedPCUs / 1000000 % 60), inElapsedTimeAfterDpc, thresholdUs, StreamObject->NumStats);
#else
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%03u.%02u: mixing engine thread: dropout detected. Long elapsed time after IN DPC, cur %dus, threshold %uus.", (LONG)(m_elapsedPCUs / 60000000), (LONG)(m_elapsedPCUs / 1000000 % 60), inElapsedTimeAfterDpc, thresholdUs);
#endif
                m_audioIsochronousEngine->GetAsioBufferObject()->SetRecDeviceStatus(DeviceStatuses::OverloadDetected);
                m_deviceContext->ErrorStatistics->LogErrorOccurrence(ErrorStatus::DropoutDetectedInDPC, (ULONG)(inElapsedTimeAfterDpc - thresholdUs));
            }
        }
        m_audioIsochronousEngine->ReleaseAsioWaitLock();
        // Use WdfUsbTargetDeviceRetrieveCurrentFrameNumber() instead of USB_BUS_INTERFACE_USBDI_V1::QueryBusTime().
        // Use USB bus time for control
        ULONG usbBusTimeCurrent = GetCurrentFrame(deviceContext);

        // Guess USB bus time so that you can respond even if the obtained USB bus time is an abnormal value.
        ULONG usbBusTimeDiff = EstimateUSBBusTime(usbBusTimeCurrent, pcDiffUs);

        UpdateElapsedTimeUs(pcDiffUs);

        LONGLONG inCompletedPacket = 0LL;  // IN Number of packets that have been transferred isochronous
        LONGLONG outCompletedPacket = 0LL; // OUT Number of packets that have been transferred isochronous
        GetCompletedPacket(inCompletedPacket, outCompletedPacket);
        // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - in completed packet, out completed packet %llu, %lld", inCompletedPacket, outCompletedPacket);
        m_audioIsochronousEngine->AcquireAsioWaitLock();
        bool handleAsioBuffer = ((streamStatus == c_ioSteady) && (m_audioIsochronousEngine->GetAsioBufferObject() != nullptr) && m_audioIsochronousEngine->GetAsioBufferObject()->IsRecBufferReady() && (m_recoverActive == 0) && (m_outputRequireZeroFill == 0) && !IsFirstWakeUp());

        LONGLONG playReadyPosition = {0};
        if ((m_audioIsochronousEngine->GetAsioBufferObject() != nullptr) && m_audioIsochronousEngine->GetAsioBufferObject()->IsRecBufferReady())
        {
            m_asioReadyPosition += m_audioIsochronousEngine->GetAsioBufferObject()->UpdateReadyPosition();

            if (m_audioIsochronousEngine->GetAsioBufferObject()->IsUserSpaceThreadOutputReady())
            {

                playReadyPosition = m_asioReadyPosition + (m_audioIsochronousEngine->GetAsioBufferObject()->GetBufferPeriod() * 2);
                if (!outputReadyInThisPeriod)
                {
                    outputReadyInThisPeriod = true;
                    prevClientProcessingTimeUs = curClientProcessingTimeUs;
                    curClientProcessingTimeUs = m_asioElapsedTimeUs;
                    LONG thresholdUs = (LONG)((m_audioIsochronousEngine->GetAsioBufferObject()->GetBufferPeriod()) * 1000000 / m_audioIsochronousEngine->GetAudioStreamPropertySet().AudioProperty.SampleRate) + 1500;
                    if (curClientProcessingTimeUs > thresholdUs)
                    {
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "dropout detected. long client processing time %d us, threshold %d us", curClientProcessingTimeUs, thresholdUs);
                        m_audioIsochronousEngine->GetAsioBufferObject()->SetRecDeviceStatus(DeviceStatuses::OverloadDetected);
                        deviceContext->ErrorStatistics->LogErrorOccurrence(ErrorStatus::DropoutDetectedLongClientProcessingTime, curClientProcessingTimeUs - thresholdUs);
                    }
                }
            }
            else
            {
                playReadyPosition = m_asioReadyPosition + m_audioIsochronousEngine->GetAsioBufferObject()->GetBufferPeriod();
            }
        }

        // Analyze and decide which packets to use.
        // The determined packet will be recorded in StreamObject::m_inputEstimatedPacket.
        DeterminePacket(inCompletedPacket, usbBusTimeDiff, inputPacketsPerIrp, inputPacketsPerMs);

        // Counts packets for which isochronous IN processing has been completed and creates a list.
        // The created list is stored in inBuffer, and if there is a remainder (inputRemainder) from the previous thread wakeup, it is allocated to the beginning of that list.
        bool             inProcessRemainder = CreateCompletedInputPacketList(m_inputBuffers, &inRemainder, inputPacketsPerIrp, numIrp);
        ULONG            inBuffersCount = 0;
        ULONG            outBuffersCount = 0;
        PacketLoopReason inLoopExitReason = PacketLoopReason::ContinueLoop;
        PacketLoopReason outLoopExitReason = PacketLoopReason::ContinueLoop;

        if (isProcessIo)
        {
            // Count the number of packets on input up to the boundary of the ASIO buffer.
            // If the ASIO Buffer boundary is found in the middle of the packet, the packet to be processed the next time the thread wakes up is recorded in inputRemainder, and this search process exits.
            while (inBuffersCount < (inputPacketsPerIrp * numIrp))
            {
                ULONG inOffsetFrame = m_audioIsochronousEngine->GetUsbLatency().InputOffsetFrame;

                if (IsInputPacketAtEstimatedPosition(inOffsetFrame))
                {
                    // Processing position reaches current position prediction
                    inLoopExitReason = PacketLoopReason::ExitLoopPacketEstimateReached;
                    break;
                }

                if (handleAsioBuffer && hasInputIsochronousInterface)
                {
                    LONG asioRemainSamples = (LONG)((m_asioReadyPosition + m_audioIsochronousEngine->GetAsioBufferObject()->GetBufferPeriod()) - m_inputAsioBufferedPosition);
                    LONG asioRemainBytes = asioRemainSamples * m_audioIsochronousEngine->GetAudioStreamPropertySet().InputProperty.BytesPerBlock;

                    // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - asio remain samples %d, asio ready position %lld, asio buffer period %u, asio buffered position out %lld, in %lld", asioRemainSamples, m_asioReadyPosition, m_audioIsochronousEngine->GetAsioBufferObject()->GetBufferPeriod(), m_outputAsioBufferedPosition, m_inputAsioBufferedPosition);
                    if (asioRemainSamples <= 0)
                    {
                        // No more ASIO buffers to process
                        inLoopExitReason = PacketLoopReason::ExitLoopNoMoreAsioBuffers;
                        break;
                    }
                    if (asioRemainBytes < (LONG)m_inputBuffers[inBuffersCount].Length)
                    {
                        // ASIO buffer boundary reached
                        inRemainder.Irp = m_inputBuffers[inBuffersCount].Irp;
                        inRemainder.Packet = m_inputBuffers[inBuffersCount].Packet;
                        inRemainder.PacketId = m_inputBuffers[inBuffersCount].PacketId;
                        inRemainder.Length = m_inputBuffers[inBuffersCount].Length - asioRemainBytes;
                        inRemainder.Buffer = m_inputBuffers[inBuffersCount].Buffer;
                        inRemainder.TransferObject = m_inputBuffers[inBuffersCount].TransferObject;
                        inRemainder.Offset = asioRemainBytes;
                        m_inputBuffers[inBuffersCount].Length = asioRemainBytes;
                    }
                    else
                    {
                        inRemainder.Buffer = nullptr;
                    }
                    m_inputAsioBufferedPosition += m_inputBuffers[inBuffersCount].Length / m_audioIsochronousEngine->GetAudioStreamPropertySet().InputProperty.BytesPerBlock;
                }

                if (inBuffersCount != 0 || !inProcessRemainder)
                {
                    IncrementInputProcessedPacket();
                }

                if (m_inputBuffers[inBuffersCount].Length > (m_audioIsochronousEngine->GetAudioStreamPropertySet().InputProperty.MaxSamplesPerPacket * m_audioIsochronousEngine->GetAudioStreamPropertySet().InputProperty.BytesPerBlock))
                {
                    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "IN buffer %u packet size %u exceeded max", inBuffersCount, m_inputBuffers[inBuffersCount].Length);
                }
                ++inBuffersCount;
                ++inBuffersTotalCount;

                if (inRemainder.Buffer != nullptr)
                {
                    // Exit loop if ASIO buffer boundary is reached
                    inLoopExitReason = PacketLoopReason::ExitLoopAtAsioBoundary;
                    break;
                }
            }
            ReportPacketLoopReason("IN loop", inLoopExitReason);

            ULONG outProcessRemainder = 0;

            while (outBuffersCount < (m_audioIsochronousEngine->GetAudioStreamPropertySet().InternalParameters.MaxIrpNumber - 1) * outputPacketsPerIrp)
            {
                if (!safetyOffsetApplied)
                {
                    ULONGLONG outAdjustedBuffersCount = (outBuffersTotalCount << (outputInterval - 1));

                    ULONG safetyOffsetFrame = m_audioIsochronousEngine->GetAsioBufferObject() != nullptr ? (m_audioIsochronousEngine->GetUsbLatency().OutputOffsetFrame) : (outputPacketsPerIrp + m_audioIsochronousEngine->GetUsbLatency().OutputOffsetFrame);
                    // The buffer has not yet been processed by this thread.
                    ULONG dpcOffset = outputPacketsPerIrp;

                    // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - outAdjustedBuffersCount %llu, dpcOffset %lu, InputOffsetFrame %lu, safetyOffsetFrame %lu, %!bool!", outAdjustedBuffersCount, dpcOffset, m_audioIsochronousEngine->GetUsbLatency().InputOffsetFrame, safetyOffsetFrame, (outAdjustedBuffersCount >= (dpcOffset + m_audioIsochronousEngine->GetUsbLatency().InputOffsetFrame + safetyOffsetFrame)));

                    if (outAdjustedBuffersCount >= (dpcOffset + m_audioIsochronousEngine->GetUsbLatency().InputOffsetFrame + safetyOffsetFrame))
                    {
                        // Exit the loop after processing a safety offset
                        outLoopExitReason = PacketLoopReason::ExitLoopAfterSafetyOffset;
                        safetyOffsetApplied = true;
                        break;
                    }
                }
                else if (hasInputIsochronousInterface && hasOutputIsochronousInterface)
                {
                    // Perform the sync evaluation only when both input and output isochronous transfer interfaces are present.
                    ASSERT(inputInterval != 0);
                    ASSERT(outputInterval != 0);

                    ULONGLONG inAdjustedBuffersCount = inBuffersTotalCount;
                    ULONGLONG outAdjustedBuffersCount = outBuffersTotalCount;

                    if (inputInterval > outputInterval)
                    {
                        inAdjustedBuffersCount <<= (inputInterval - outputInterval);
                    }
                    else if (outputInterval > inputInterval)
                    {
                        outAdjustedBuffersCount <<= (outputInterval - inputInterval);
                    }

                    // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - outputInterval %llu, inputInterval %llu, inAdjustedBuffersCount %llu, outAdjustedBuffersCount %llu", outputInterval, inputInterval, inAdjustedBuffersCount, outAdjustedBuffersCount);

                    if (((streamStatus != StreamStatuses::IoSteady) || !handleAsioBuffer) && (outAdjustedBuffersCount >= inAdjustedBuffersCount))
                    {
                        // If do not preceding processing of OUT, exit the loop when synchronized with IN.
                        outLoopExitReason = PacketLoopReason::ExitLoopAtInSync;
                        break;
                    }
                    else
                    {
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - out buffers total count %llu, in buffers total count %llu, ioStable 0x%x", outBuffersTotalCount, inBuffersTotalCount, toInt(streamStatus));
                    }
                }

                if (hasInputIsochronousInterface)
                {
                    // input enable
                    ULONG outLimit = (m_audioIsochronousEngine->GetAudioStreamPropertySet().InternalParameters.MaxIrpNumber - 1) * outputPacketsPerIrp;
                    if (IsOutputPacketOverlapWithEstimatePosition(outLimit, inputInterval, outputInterval))
                    {
                        // Prevents OUT processing from going around once the buffer and reaching the currently processed position
                        outLoopExitReason = PacketLoopReason::ExitLoopToPreventOutOverlap;
                        break;
                    }
                }
                else
                {
                    // input disable
                    if (IsOutputPacketAtEstimatedPosition(inputInterval, outputInterval))
                    {
                        // Processing position reaches current position prediction
                        outLoopExitReason = PacketLoopReason::ExitLoopPacketEstimateReached;
                        break;
                    }
                }
                outProcessRemainder = CreateCompletedOutputPacketList(m_outputBuffers, &outRemainder, outBuffersCount, outputPacketsPerIrp, numIrp);

                if (handleAsioBuffer && hasOutputIsochronousInterface)
                {
                    LONG asioRemain = (LONG)((playReadyPosition - m_outputAsioBufferedPosition) * m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.BytesPerBlock);
                    {
                        if (asioRemain <= 0)
                        {
                            // No more ASIO buffers to process
                            outLoopExitReason = PacketLoopReason::ExitLoopNoMoreAsioBuffers;
                            break;
                        }
                        if (asioRemain < (LONG)m_outputBuffers[outBuffersCount].Length)
                        {
                            // ASIO buffer boundary reached
                            outRemainder.Irp = m_outputBuffers[outBuffersCount].Irp;
                            outRemainder.Packet = m_outputBuffers[outBuffersCount].Packet;
                            outRemainder.PacketId = m_outputBuffers[outBuffersCount].PacketId;
                            outRemainder.Length = m_outputBuffers[outBuffersCount].Length - asioRemain;
                            outRemainder.Buffer = m_outputBuffers[outBuffersCount].Buffer;
                            outRemainder.TransferObject = m_outputBuffers[outBuffersCount].TransferObject;
                            outRemainder.Offset = asioRemain;
                            m_outputBuffers[outBuffersCount].Length = asioRemain;
                        }
                        else
                        {
                            outRemainder.Buffer = nullptr;
                        }
                    }
                    m_outputAsioBufferedPosition += m_outputBuffers[outBuffersCount].Length / m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.BytesPerBlock;
                }

                if (outBuffersCount != 0 || !outProcessRemainder)
                {
                    IncrementOutputProcessedPacket();
                }

                ++outBuffersCount;
                ++outBuffersTotalCount;

                if (outRemainder.Buffer != nullptr)
                {
                    // Exit loop if ASIO buffer boundary is reached
                    outLoopExitReason = PacketLoopReason::ExitLoopAtAsioBoundary;
                    break;
                }
            }
            ReportPacketLoopReason("OUT loop", outLoopExitReason);
        }
        m_audioIsochronousEngine->ReleaseAsioWaitLock();

        if (inBuffersCount == 0)
        {
            LONG inProcessPeriodUs = (LONG)(currentTimePCUs - lastInProcessedPCUs);
            if ((lastInProcessedPCUs != 0) && (inProcessPeriodUs > (LONG)(m_audioIsochronousEngine->GetAudioStreamPropertySet().ClassicFramesPerIrp * numIrp + m_audioIsochronousEngine->GetAudioStreamPropertySet().InternalParameters.FirstPacketLatency) * 1000))
            {
                // If the IN has not been processed for a long period of time, perform a bus reset.
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "Thread interval (%uus) exceeded IRP cycle time (%uus) on first buffer, last loop exit reason in %u, out %u.", inProcessPeriodUs, (m_audioIsochronousEngine->GetAudioStreamPropertySet().ClassicFramesPerIrp * numIrp) * 1000, static_cast<ULONG>(inLoopExitReason), static_cast<ULONG>(outLoopExitReason));
                // RecoverTransferError(StreamObject->DeviceObject, StreamObject);
                break;
            }
            if ((GetStreamStatuses() != c_ioStreaming) ||
                (m_inputTransferObject[0]->GetLockDelayCount() != 0) ||
                (m_outputTransferObject[0]->GetLockDelayCount() != 0))
            {
                lastInProcessedPCUs = currentTimePCUs;
            }
        }
        else
        {
            lastInProcessedPCUs = currentTimePCUs;
        }

        // Dropout Detection
        ULONG outMinOffsetFrame = m_audioIsochronousEngine->GetUsbLatency().OutputOffsetFrame;
        if (outMinOffsetFrame >= (m_audioIsochronousEngine->GetAudioStreamPropertySet().InternalParameters.MaxIrpNumber - 2) * outputPacketsPerIrp)
        {
            outMinOffsetFrame = ((m_audioIsochronousEngine->GetAudioStreamPropertySet().InternalParameters.MaxIrpNumber - 2) * outputPacketsPerIrp) - 1;
        }

        ULONG dpcOffset = outputPacketsPerIrp;
        LONG  safetyOffset = (LONG)(m_outputProcessedPacket - m_inputProcessedPacket) - (LONG)m_audioIsochronousEngine->GetUsbLatency().InputOffsetFrame - (LONG)(dpcOffset);
        m_audioIsochronousEngine->AcquireAsioWaitLock();
        if (safetyOffset < (LONG)(outMinOffsetFrame) &&
            (m_audioIsochronousEngine->GetAsioBufferObject() != nullptr) && m_audioIsochronousEngine->GetAsioBufferObject()->IsRecHeaderRegistered() &&
            (hasOutputIsochronousInterface && hasInputIsochronousInterface))
        {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "dropout detected. Safety offset %d, minimum offset frame %d", safetyOffset, outMinOffsetFrame);
            m_audioIsochronousEngine->GetAsioBufferObject()->SetRecDeviceStatus(DeviceStatuses::OverloadDetected);
            deviceContext->ErrorStatistics->LogErrorOccurrence(ErrorStatus::DropoutDetectedSafetyOffset, 0);
        }

        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - In buffers count %u, ioStable 0x%x, inLoopExitReason %u", inBuffersCount, static_cast<ULONG>(streamStatus), static_cast<ULONG>(inLoopExitReason));
        if ((streamStatus == c_ioSteady) && hasInputIsochronousInterface)
        {
            for (ULONG bufIndex = 0; bufIndex < inBuffersCount; ++bufIndex)
            {
                // ULONG length = m_inputBuffers[bufIndex].length;
                if ((m_audioIsochronousEngine->GetAsioBufferObject() != nullptr) && handleAsioBuffer)
                {
                    m_audioIsochronousEngine->GetAsioBufferObject()->CopyToAsioFromInputData(
                        m_inputBuffers[bufIndex].Buffer + m_inputBuffers[bufIndex].Offset,
                        m_inputBuffers[bufIndex].Length,
                        m_audioIsochronousEngine->GetAudioStreamPropertySet().InputProperty.BytesPerBlock,
                        m_audioIsochronousEngine->GetAudioStreamPropertySet().InputProperty.BytesPerSample
                    );
                }

                if (m_audioIsochronousEngine->GetRtPacketObject() != nullptr)
                {
                    m_audioIsochronousEngine->AcquireStreamEngineWaitLock();
                    for (ULONG deviceIndex = 0; deviceIndex < m_audioIsochronousEngine->GetNumOfInputDevices(); deviceIndex++)
                    {
                        if ((m_audioIsochronousEngine->GetCaptureStreamEngine(deviceIndex) != nullptr) && (m_audioIsochronousEngine->GetCaptureStreamEngine(deviceIndex)->GetCurrentState() == AcxStreamStateRun))
                        {
                            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - buffer index %u, transfer object %p", bufIndex, m_inputBuffers[bufIndex].TransferObject);
                            m_audioIsochronousEngine->GetRtPacketObject()->CopyToRtPacketFromInputData(
                                deviceIndex,
                                m_inputBuffers[bufIndex].Buffer + m_inputBuffers[bufIndex].Offset,
                                m_inputBuffers[bufIndex].Length,
                                m_inputBuffers[bufIndex].TotalProcessedBytesSoFar,
                                m_inputBuffers[bufIndex].TransferObject,
                                m_audioIsochronousEngine->GetAudioStreamPropertySet().InputProperty.BytesPerSample /* ex: 3 */,
                                m_audioIsochronousEngine->GetAudioStreamPropertySet().InputProperty.ValidBitsPerSample /* ex: 24*/,
                                m_audioIsochronousEngine->GetAudioStreamPropertySet().InputProperty.UsbChannels
                            );
                        }
                    }
                    m_audioIsochronousEngine->ReleaseStreamEngineWaitLock();
                }
            }
        }
        ULONG bytesPerBlock = m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.BytesPerBlock;

        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - Out buffers count %u, ioStable 0x%x, outLoopExitReason %u", outBuffersCount, static_cast<int>(streamStatus), static_cast<ULONG>(outLoopExitReason));

        if (hasOutputIsochronousInterface)
        {
            for (ULONG bufIndex = 0; bufIndex < outBuffersCount; ++bufIndex)
            {
                ULONG  transferSize = m_outputBuffers[bufIndex].Length;
                PUCHAR outBufferStart = m_outputBuffers[bufIndex].Buffer + m_outputBuffers[bufIndex].Offset;
                ULONG  outChannels = m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.UsbChannels;
                ULONG  samples = transferSize / bytesPerBlock;

                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - outputBuffers[%u] Irp, Packet, PacketID, TransferObject, Index, %u, %u, %u, %p, %u, %llu, %lld", bufIndex, m_outputBuffers[bufIndex].Irp, m_outputBuffers[bufIndex].Packet, m_outputBuffers[bufIndex].PacketId, m_outputBuffers[bufIndex].TransferObject, m_outputBuffers[bufIndex].TransferObject->GetIndex(), m_outputBuffers[bufIndex].TransferObject->GetQPCPosition(), (bufIndex == 0) ? 0LL : (LONGLONG)(m_outputBuffers[bufIndex].TransferObject->GetQPCPosition()) - (LONGLONG)(m_outputBuffers[bufIndex - 1].TransferObject->GetQPCPosition()));

                StreamObject::ClearOutputBuffer(m_audioIsochronousEngine->GetAudioStreamPropertySet().AudioProperty.CurrentSampleFormat, outBufferStart, outChannels, bytesPerBlock, samples);
                if (streamStatus == c_ioSteady)
                {
                    if ((m_audioIsochronousEngine->GetAsioBufferObject() != nullptr) && handleAsioBuffer)
                    {
                        if (!NT_SUCCESS(m_audioIsochronousEngine->GetAsioBufferObject()->CopyFromAsioToOutputData(
                                outBufferStart,
                                transferSize,
                                bytesPerBlock,
                                m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.BytesPerSample
                            )))
                        {
                            StreamObject::ClearOutputBuffer(m_audioIsochronousEngine->GetAudioStreamPropertySet().AudioProperty.CurrentSampleFormat, outBufferStart, outChannels, bytesPerBlock, samples);
                        }
                    }

                    if (m_audioIsochronousEngine->GetRtPacketObject() != nullptr)
                    {
                        m_audioIsochronousEngine->AcquireStreamEngineWaitLock();
                        for (ULONG deviceIndex = 0; deviceIndex < m_audioIsochronousEngine->GetNumOfOutputDevices(); deviceIndex++)
                        {
                            if ((m_audioIsochronousEngine->GetRenderStreamEngine(deviceIndex) != nullptr) && (m_audioIsochronousEngine->GetRenderStreamEngine(deviceIndex)->GetCurrentState() == AcxStreamStateRun))
                            {
                                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - buffer index %u, transfer object %p", bufIndex, m_outputBuffers[bufIndex].TransferObject);
                                m_audioIsochronousEngine->GetRtPacketObject()->CopyFromRtPacketToOutputData(
                                    deviceIndex,
                                    outBufferStart,
                                    transferSize,
                                    m_outputBuffers[bufIndex].TotalProcessedBytesSoFar,
                                    m_outputBuffers[bufIndex].TransferObject,
                                    m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.BytesPerSample /* ex: 3 */,
                                    m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.ValidBitsPerSample /* ex: 24*/,
                                    m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.UsbChannels
                                );
                            }
                        }
                        m_audioIsochronousEngine->ReleaseStreamEngineWaitLock();
                    }
                }
            }
        }
        if ((m_audioIsochronousEngine->GetAsioBufferObject() != nullptr) && m_audioIsochronousEngine->GetAsioBufferObject()->IsRecBufferReady())
        {
            if (m_audioIsochronousEngine->GetAsioBufferObject()->EvaluatePositionAndNotifyIfNeeded(currentTimePCUs, lastAsioNotifyPCUs, asioNotifyCount, prevAsioMeasuredPeriodUs, curClientProcessingTimeUs, curAsioMeasuredPeriodUs, hasInputIsochronousInterface, hasOutputIsochronousInterface))
            {
                m_asioElapsedTimeUs = 0;
                prevAsioMeasuredPeriodUs = curAsioMeasuredPeriodUs;
                lastAsioNotifyPCUs = currentTimePCUs;
                outputReadyInPrevPeriod = outputReadyInThisPeriod;
                outputReadyInThisPeriod = false;
                ++asioNotifyCount;
            }
        }
        m_audioIsochronousEngine->ReleaseAsioWaitLock();
        if (inBuffersCount != 0 || outBuffersCount != 0)
        {
            if (m_bufferProcessed < 2)
            {
                ++(m_bufferProcessed);
            }
        }
        // LastBusTime = usbBusTimeCurrent;
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
    return;
}

_Use_decl_annotations_
PAGED_CODE_SEG
void StreamObject::MixingEngineThreadMainWithoutASIO(
    PDEVICE_CONTEXT deviceContext
)
{
    ULONGLONG       currentTimePCUs = 0ULL;
    ULONGLONG       currentTimePC = 0ULL;
    const ULONG     inputInterval = m_audioIsochronousEngine->GetInputInterfaceAndPipe().PipeInfo.Interval;
    const ULONG     outputInterval = m_audioIsochronousEngine->GetOutputInterfaceAndPipe().PipeInfo.Interval;
    const ULONG     inputPacketsPerMs = m_deviceContext->FramesPerMs >> (inputInterval - 1);
    const ULONG     outputPacketsPerMs = m_deviceContext->FramesPerMs >> (outputInterval - 1);
    const ULONG     inputPacketsPerIrp = m_audioIsochronousEngine->GetAudioStreamPropertySet().ClassicFramesPerIrp * inputPacketsPerMs;
    const ULONG     outputPacketsPerIrp = m_audioIsochronousEngine->GetAudioStreamPropertySet().ClassicFramesPerIrp * outputPacketsPerMs;
    const ULONG     numIrp = m_audioIsochronousEngine->GetAudioStreamPropertySet().InternalParameters.MaxIrpNumber;
    bool            safetyOffsetApplied = false;
    BUFFER_PROPERTY inRemainder{};
    BUFFER_PROPERTY outRemainder{};
    ULONGLONG       lastInProcessedPCUs = 0ULL;
    LARGE_INTEGER   timerExpired = {0};
    LONGLONG        inBuffersTotalCount = 0ULL;
    LONGLONG        outBuffersTotalCount = 0ULL;
    const bool      hasInputIsochronousInterface = m_audioIsochronousEngine->HasInputIsochronousInterface();
    const bool      hasOutputIsochronousInterface = m_audioIsochronousEngine->HasOutputIsochronousInterface();

    PAGED_CODE();

    for (;;)
    {
        NTSTATUS wakeupReason = STATUS_SUCCESS;
        bool     isProcessIo = false;
        wakeupReason = Wait();

        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "MaxingEngineThreadMain() WakeUp reason = %d", static_cast<int>(wakeupReason));

        // If the wakeup result is an error, exit.
        if (!NT_SUCCESS(wakeupReason) || (wakeupReason == STATUS_WAIT_0) || IsTerminateStream())
        {
            break;
        }

        // Get the current status of stream.
        StreamStatuses streamStatus = GetStreamStatuses(isProcessIo);

        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "GetStreamStatuses() %s, %d", isProcessIo ? "true" : "false", static_cast<int>(streamStatus));

        // Updated valid wake-up count.
        // Since timerExpired and ThreadWakeup are initialized and updated at the same time, they will be made common.
        IncrementWakeUpCount();

        // Gets highly accurate current time based on KeQueryPerformanceCounter.
        currentTimePCUs = USBAudioAcxDriverStreamGetCurrentTimeUs(deviceContext, &currentTimePC);

        SaveWakeUpTimePCUs(currentTimePCUs);

        ULONG pcDiffUs = static_cast<ULONG>(GetWakeUpDiffPCUs());

        LONG inElapsedTimeAfterDpc = 0;

        if (hasInputIsochronousInterface)
        {
            inElapsedTimeAfterDpc = (LONG)((LONGLONG)currentTimePCUs - m_inputIsoRequestCompletionTime.LastTimeUs);
        }
        else
        {
            inElapsedTimeAfterDpc = (LONG)((LONGLONG)currentTimePCUs - m_outputIsoRequestCompletionTime.LastTimeUs);
        }

        // Use WdfUsbTargetDeviceRetrieveCurrentFrameNumber() instead of USB_BUS_INTERFACE_USBDI_V1::QueryBusTime().
        // Use USB bus time for control
        ULONG usbBusTimeCurrent = GetCurrentFrame(deviceContext);

        // Guess USB bus time so that you can respond even if the obtained USB bus time is an abnormal value.
        ULONG usbBusTimeDiff = EstimateUSBBusTime(usbBusTimeCurrent, pcDiffUs);

        UpdateElapsedTimeUs(pcDiffUs);

        LONGLONG inCompletedPacket = 0LL;  // IN Number of packets that have been transferred isochronous
        LONGLONG outCompletedPacket = 0LL; // OUT Number of packets that have been transferred isochronous
        GetCompletedPacket(inCompletedPacket, outCompletedPacket);
        // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - in completed packet, out completed packet %llu, %lld", inCompletedPacket, outCompletedPacket);

        // Analyze and decide which packets to use.
        // The determined packet will be recorded in StreamObject::m_inputEstimatedPacket.
        DeterminePacket(inCompletedPacket, usbBusTimeDiff, inputPacketsPerIrp, inputPacketsPerMs);

        // Counts packets for which isochronous IN processing has been completed and creates a list.
        // The created list is stored in inBuffer, and if there is a remainder (inputRemainder) from the previous thread wakeup, it is allocated to the beginning of that list.
        bool             inProcessRemainder = CreateCompletedInputPacketList(m_inputBuffers, &inRemainder, inputPacketsPerIrp, numIrp);
        ULONG            inBuffersCount = 0;
        ULONG            outBuffersCount = 0;
        PacketLoopReason inLoopExitReason = PacketLoopReason::ContinueLoop;
        PacketLoopReason outLoopExitReason = PacketLoopReason::ContinueLoop;

        if (isProcessIo)
        {
            while (inBuffersCount < (inputPacketsPerIrp * numIrp))
            {
                ULONG inOffsetFrame = m_audioIsochronousEngine->GetUsbLatency().InputOffsetFrame;

                if (IsInputPacketAtEstimatedPosition(inOffsetFrame))
                {
                    // Processing position reaches current position prediction
                    inLoopExitReason = PacketLoopReason::ExitLoopPacketEstimateReached;
                    break;
                }

                if (inBuffersCount != 0 || !inProcessRemainder)
                {
                    IncrementInputProcessedPacket();
                }

                if (m_inputBuffers[inBuffersCount].Length > (m_audioIsochronousEngine->GetAudioStreamPropertySet().InputProperty.MaxSamplesPerPacket * m_audioIsochronousEngine->GetAudioStreamPropertySet().InputProperty.BytesPerBlock))
                {
                    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "IN buffer %u packet size %u exceeded max", inBuffersCount, m_inputBuffers[inBuffersCount].Length);
                }
                ++inBuffersCount;
                ++inBuffersTotalCount;

                if (inRemainder.Buffer != nullptr)
                {
                    // Exit loop if ASIO buffer boundary is reached
                    inLoopExitReason = PacketLoopReason::ExitLoopAtAsioBoundary;
                    break;
                }
            }
            ReportPacketLoopReason("IN loop", inLoopExitReason);

            ULONG outProcessRemainder = 0;

            while (outBuffersCount < (m_audioIsochronousEngine->GetAudioStreamPropertySet().InternalParameters.MaxIrpNumber - 1) * outputPacketsPerIrp)
            {
                if (!safetyOffsetApplied)
                {
                    ULONGLONG outAdjustedBuffersCount = (outBuffersTotalCount << (outputInterval - 1));

                    ULONG safetyOffsetFrame = outputPacketsPerIrp + m_audioIsochronousEngine->GetUsbLatency().OutputOffsetFrame;
                    // The buffer has not yet been processed by this thread.
                    ULONG dpcOffset = outputPacketsPerIrp;

                    // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - outAdjustedBuffersCount %llu, dpcOffset %lu, InputOffsetFrame %lu, safetyOffsetFrame %lu, %!bool!", outAdjustedBuffersCount, dpcOffset, m_audioIsochronousEngine->GetUsbLatency().InputOffsetFrame, safetyOffsetFrame, (outAdjustedBuffersCount >= (dpcOffset + m_audioIsochronousEngine->GetUsbLatency().InputOffsetFrame + safetyOffsetFrame)));

                    if (outAdjustedBuffersCount >= (dpcOffset + m_audioIsochronousEngine->GetUsbLatency().InputOffsetFrame + safetyOffsetFrame))
                    {
                        // Exit the loop after processing a safety offset
                        outLoopExitReason = PacketLoopReason::ExitLoopAfterSafetyOffset;
                        safetyOffsetApplied = true;
                        break;
                    }
                }
                else if (hasInputIsochronousInterface && hasOutputIsochronousInterface)
                {
                    // Perform the sync evaluation only when both input and output isochronous transfer interfaces are present.
                    ASSERT(inputInterval != 0);
                    ASSERT(outputInterval != 0);

                    ULONGLONG inAdjustedBuffersCount = inBuffersTotalCount;
                    ULONGLONG outAdjustedBuffersCount = outBuffersTotalCount;

                    if (inputInterval > outputInterval)
                    {
                        inAdjustedBuffersCount <<= (inputInterval - outputInterval);
                    }
                    else if (outputInterval > inputInterval)
                    {
                        outAdjustedBuffersCount <<= (outputInterval - inputInterval);
                    }

                    // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - outputInterval %llu, inputInterval %llu, inAdjustedBuffersCount %llu, outAdjustedBuffersCount %llu", outputInterval, inputInterval, inAdjustedBuffersCount, outAdjustedBuffersCount);

                    if ((streamStatus != StreamStatuses::IoSteady) && (outAdjustedBuffersCount >= inAdjustedBuffersCount))
                    {
                        // If do not preceding processing of OUT, exit the loop when synchronized with IN.
                        outLoopExitReason = PacketLoopReason::ExitLoopAtInSync;
                        break;
                    }
                    else
                    {
                        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - out buffers total count %llu, in buffers total count %llu, ioStable 0x%x", outBuffersTotalCount, inBuffersTotalCount, toInt(streamStatus));
                    }
                }

                if (hasInputIsochronousInterface)
                {
                    // input enable
                    ULONG outLimit = (m_audioIsochronousEngine->GetAudioStreamPropertySet().InternalParameters.MaxIrpNumber - 1) * outputPacketsPerIrp;
                    if (IsOutputPacketOverlapWithEstimatePosition(outLimit, inputInterval, outputInterval))
                    {
                        // Prevents OUT processing from going around once the buffer and reaching the currently processed position
                        outLoopExitReason = PacketLoopReason::ExitLoopToPreventOutOverlap;
                        break;
                    }
                }
                else
                {
                    // input disable
                    if (IsOutputPacketAtEstimatedPosition(inputInterval, outputInterval))
                    {
                        // Processing position reaches current position prediction
                        outLoopExitReason = PacketLoopReason::ExitLoopPacketEstimateReached;
                        break;
                    }
                }
                outProcessRemainder = CreateCompletedOutputPacketList(m_outputBuffers, &outRemainder, outBuffersCount, outputPacketsPerIrp, numIrp);

                if (outBuffersCount != 0 || !outProcessRemainder)
                {
                    IncrementOutputProcessedPacket();
                }

                ++outBuffersCount;
                ++outBuffersTotalCount;

                if (outRemainder.Buffer != nullptr)
                {
                    // Exit loop if ASIO buffer boundary is reached
                    outLoopExitReason = PacketLoopReason::ExitLoopAtAsioBoundary;
                    break;
                }
            }
            ReportPacketLoopReason("OUT loop", outLoopExitReason);
        }

        if (inBuffersCount == 0)
        {
            LONG inProcessPeriodUs = (LONG)(currentTimePCUs - lastInProcessedPCUs);
            if ((lastInProcessedPCUs != 0) && (inProcessPeriodUs > (LONG)(m_audioIsochronousEngine->GetAudioStreamPropertySet().ClassicFramesPerIrp * numIrp + m_audioIsochronousEngine->GetAudioStreamPropertySet().InternalParameters.FirstPacketLatency) * 1000))
            {
                // If the IN has not been processed for a long period of time, perform a bus reset.
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "Thread interval (%uus) exceeded IRP cycle time (%uus) on first buffer, last loop exit reason in %u, out %u.", inProcessPeriodUs, (m_audioIsochronousEngine->GetAudioStreamPropertySet().ClassicFramesPerIrp * numIrp) * 1000, static_cast<ULONG>(inLoopExitReason), static_cast<ULONG>(outLoopExitReason));
                // RecoverTransferError(StreamObject->DeviceObject, StreamObject);
                break;
            }
            if ((GetStreamStatuses() != c_ioStreaming) ||
                (m_inputTransferObject[0]->GetLockDelayCount() != 0) ||
                (m_outputTransferObject[0]->GetLockDelayCount() != 0))
            {
                lastInProcessedPCUs = currentTimePCUs;
            }
        }
        else
        {
            lastInProcessedPCUs = currentTimePCUs;
        }

        // Dropout Detection
        ULONG outMinOffsetFrame = m_audioIsochronousEngine->GetUsbLatency().OutputOffsetFrame;
        if (outMinOffsetFrame >= (m_audioIsochronousEngine->GetAudioStreamPropertySet().InternalParameters.MaxIrpNumber - 2) * outputPacketsPerIrp)
        {
            outMinOffsetFrame = ((m_audioIsochronousEngine->GetAudioStreamPropertySet().InternalParameters.MaxIrpNumber - 2) * outputPacketsPerIrp) - 1;
        }

        ULONG dpcOffset = outputPacketsPerIrp;
        LONG  safetyOffset = (LONG)(m_outputProcessedPacket - m_inputProcessedPacket) - (LONG)m_audioIsochronousEngine->GetUsbLatency().InputOffsetFrame - (LONG)(dpcOffset);
        if ((safetyOffset < (LONG)(outMinOffsetFrame)) && (hasOutputIsochronousInterface && hasInputIsochronousInterface))
        {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "dropout detected. Safety offset %d, minimum offset frame %d", safetyOffset, outMinOffsetFrame);
            deviceContext->ErrorStatistics->LogErrorOccurrence(ErrorStatus::DropoutDetectedSafetyOffset, 0);
        }

        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, " - In buffers count %u, ioStable 0x%x, inLoopExitReason %u", inBuffersCount, static_cast<ULONG>(streamStatus), static_cast<ULONG>(inLoopExitReason));
        if ((streamStatus == c_ioSteady) && hasInputIsochronousInterface)
        {
            for (ULONG bufIndex = 0; bufIndex < inBuffersCount; ++bufIndex)
            {
                // ULONG length = m_inputBuffers[bufIndex].length;
                if (m_audioIsochronousEngine->GetRtPacketObject() != nullptr)
                {
                    m_audioIsochronousEngine->AcquireStreamEngineWaitLock();
                    for (ULONG deviceIndex = 0; deviceIndex < m_audioIsochronousEngine->GetNumOfInputDevices(); deviceIndex++)
                    {
                        if ((m_audioIsochronousEngine->GetCaptureStreamEngine(deviceIndex) != nullptr) && (m_audioIsochronousEngine->GetCaptureStreamEngine(deviceIndex)->GetCurrentState() == AcxStreamStateRun))
                        {
                            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - buffer index %u, transfer object %p", bufIndex, m_inputBuffers[bufIndex].TransferObject);
                            m_audioIsochronousEngine->GetRtPacketObject()->CopyToRtPacketFromInputData(
                                deviceIndex,
                                m_inputBuffers[bufIndex].Buffer + m_inputBuffers[bufIndex].Offset,
                                m_inputBuffers[bufIndex].Length,
                                m_inputBuffers[bufIndex].TotalProcessedBytesSoFar,
                                m_inputBuffers[bufIndex].TransferObject,
                                m_audioIsochronousEngine->GetAudioStreamPropertySet().InputProperty.BytesPerSample /* ex: 3 */,
                                m_audioIsochronousEngine->GetAudioStreamPropertySet().InputProperty.ValidBitsPerSample /* ex: 24*/,
                                m_audioIsochronousEngine->GetAudioStreamPropertySet().InputProperty.UsbChannels
                            );
                        }
                    }
                    m_audioIsochronousEngine->ReleaseStreamEngineWaitLock();
                }
            }
        }
        ULONG bytesPerBlock = m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.BytesPerBlock;

        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, " - Out buffers count %u, ioStable 0x%x, outLoopExitReason %u", outBuffersCount, static_cast<int>(streamStatus), static_cast<ULONG>(outLoopExitReason));

        if (hasOutputIsochronousInterface)
        {
            for (ULONG bufIndex = 0; bufIndex < outBuffersCount; ++bufIndex)
            {
                ULONG  transferSize = m_outputBuffers[bufIndex].Length;
                PUCHAR outBufferStart = m_outputBuffers[bufIndex].Buffer + m_outputBuffers[bufIndex].Offset;
                ULONG  outChannels = m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.UsbChannels;
                ULONG  samples = transferSize / bytesPerBlock;

                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - outputBuffers[%u] Irp, Packet, PacketID, TransferObject, Index, %u, %u, %u, %p, %u, %llu, %lld", bufIndex, m_outputBuffers[bufIndex].Irp, m_outputBuffers[bufIndex].Packet, m_outputBuffers[bufIndex].PacketId, m_outputBuffers[bufIndex].TransferObject, m_outputBuffers[bufIndex].TransferObject->GetIndex(), m_outputBuffers[bufIndex].TransferObject->GetQPCPosition(), (bufIndex == 0) ? 0LL : (LONGLONG)(m_outputBuffers[bufIndex].TransferObject->GetQPCPosition()) - (LONGLONG)(m_outputBuffers[bufIndex - 1].TransferObject->GetQPCPosition()));

                StreamObject::ClearOutputBuffer(m_audioIsochronousEngine->GetAudioStreamPropertySet().AudioProperty.CurrentSampleFormat, outBufferStart, outChannels, bytesPerBlock, samples);
                if (streamStatus == c_ioSteady)
                {
                    if (m_audioIsochronousEngine->GetRtPacketObject() != nullptr)
                    {
                        m_audioIsochronousEngine->AcquireStreamEngineWaitLock();
                        for (ULONG deviceIndex = 0; deviceIndex < m_audioIsochronousEngine->GetNumOfOutputDevices(); deviceIndex++)
                        {
                            if ((m_audioIsochronousEngine->GetRenderStreamEngine(deviceIndex) != nullptr) && (m_audioIsochronousEngine->GetRenderStreamEngine(deviceIndex)->GetCurrentState() == AcxStreamStateRun))
                            {
                                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - buffer index %u, transfer object %p", bufIndex, m_outputBuffers[bufIndex].TransferObject);
                                m_audioIsochronousEngine->GetRtPacketObject()->CopyFromRtPacketToOutputData(
                                    deviceIndex,
                                    outBufferStart,
                                    transferSize,
                                    m_outputBuffers[bufIndex].TotalProcessedBytesSoFar,
                                    m_outputBuffers[bufIndex].TransferObject,
                                    m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.BytesPerSample /* ex: 3 */,
                                    m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.ValidBitsPerSample /* ex: 24*/,
                                    m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.UsbChannels
                                );
                            }
                        }
                        m_audioIsochronousEngine->ReleaseStreamEngineWaitLock();
                    }
                }
            }
        }
        if (inBuffersCount != 0 || outBuffersCount != 0)
        {
            if (m_bufferProcessed < 2)
            {
                ++(m_bufferProcessed);
            }
        }
        // LastBusTime = usbBusTimeCurrent;
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
    return;
}

_Use_decl_annotations_
PAGED_CODE_SEG
void StreamObject::MixingEngineInputThreadMainWithoutASIO(
    PDEVICE_CONTEXT deviceContext
)
{
    ULONGLONG       currentTimePCUs = 0ULL;
    ULONGLONG       currentTimePC = 0ULL;
    const ULONG     inputInterval = m_audioIsochronousEngine->GetInputInterfaceAndPipe().PipeInfo.Interval;
    const ULONG     inputPacketsPerMs = m_deviceContext->FramesPerMs >> (inputInterval - 1);
    const ULONG     inputPacketsPerIrp = m_audioIsochronousEngine->GetAudioStreamPropertySet().ClassicFramesPerIrp * inputPacketsPerMs;
    const ULONG     numIrp = m_audioIsochronousEngine->GetAudioStreamPropertySet().InternalParameters.MaxIrpNumber;
    BUFFER_PROPERTY inRemainder{};
    ULONGLONG       lastInProcessedPCUs = 0ULL;
    LARGE_INTEGER   timerExpired = {0};
    LONGLONG        inBuffersTotalCount = 0ULL;
    const bool      hasInputIsochronousInterface = m_audioIsochronousEngine->HasInputIsochronousInterface();

    PAGED_CODE();

    for (;;)
    {
        NTSTATUS wakeupReason = STATUS_SUCCESS;
        bool     isProcessIo = false;
        wakeupReason = Wait();

        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "MaxingEngineThreadMain() WakeUp reason = %d", static_cast<int>(wakeupReason));

        // If the wakeup result is an error, exit.
        if (!NT_SUCCESS(wakeupReason) || (wakeupReason == STATUS_WAIT_0) || IsTerminateStream())
        {
            break;
        }

        // Get the current status of stream.
        StreamStatuses streamStatus = GetStreamStatuses(isProcessIo);

        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "GetStreamStatuses() %s, %d", isProcessIo ? "true" : "false", static_cast<int>(streamStatus));

        // Updated valid wake-up count.
        // Since timerExpired and ThreadWakeup are initialized and updated at the same time, they will be made common.
        IncrementWakeUpCount();

        // Gets highly accurate current time based on KeQueryPerformanceCounter.
        currentTimePCUs = USBAudioAcxDriverStreamGetCurrentTimeUs(deviceContext, &currentTimePC);

        SaveWakeUpTimePCUs(currentTimePCUs);

        ULONG pcDiffUs = static_cast<ULONG>(GetWakeUpDiffPCUs());

        // Use WdfUsbTargetDeviceRetrieveCurrentFrameNumber() instead of USB_BUS_INTERFACE_USBDI_V1::QueryBusTime().
        // Use USB bus time for control
        ULONG usbBusTimeCurrent = GetCurrentFrame(deviceContext);

        // Guess USB bus time so that you can respond even if the obtained USB bus time is an abnormal value.
        ULONG usbBusTimeDiff = EstimateUSBBusTime(usbBusTimeCurrent, pcDiffUs);

        UpdateElapsedTimeUs(pcDiffUs);

        LONGLONG inCompletedPacket = 0LL; // IN Number of packets that have been transferred isochronous
        GetCompletedPacket(true, inCompletedPacket);
        // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - in completed packet %llu", inCompletedPacket);

        // Analyze and decide which packets to use.
        // The determined packet will be recorded in StreamObject::m_inputEstimatedPacket.
        DeterminePacket(inCompletedPacket, usbBusTimeDiff, inputPacketsPerIrp, inputPacketsPerMs);

        // Counts packets for which isochronous IN processing has been completed and creates a list.
        // The created list is stored in inBuffer, and if there is a remainder (inputRemainder) from the previous thread wakeup, it is allocated to the beginning of that list.
        bool             inProcessRemainder = CreateCompletedInputPacketList(m_inputBuffers, &inRemainder, inputPacketsPerIrp, numIrp);
        ULONG            inBuffersCount = 0;
        PacketLoopReason inLoopExitReason = PacketLoopReason::ContinueLoop;

        if (isProcessIo)
        {
            while (inBuffersCount < (inputPacketsPerIrp * numIrp))
            {
                ULONG inOffsetFrame = m_audioIsochronousEngine->GetUsbLatency().InputOffsetFrame;

                if (IsInputPacketAtEstimatedPosition(inOffsetFrame))
                {
                    // Processing position reaches current position prediction
                    inLoopExitReason = PacketLoopReason::ExitLoopPacketEstimateReached;
                    break;
                }

                if (inBuffersCount != 0 || !inProcessRemainder)
                {
                    IncrementInputProcessedPacket();
                }

                if (m_inputBuffers[inBuffersCount].Length > (m_audioIsochronousEngine->GetAudioStreamPropertySet().InputProperty.MaxSamplesPerPacket * m_audioIsochronousEngine->GetAudioStreamPropertySet().InputProperty.BytesPerBlock))
                {
                    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "IN buffer %u packet size %u exceeded max", inBuffersCount, m_inputBuffers[inBuffersCount].Length);
                }
                ++inBuffersCount;
                ++inBuffersTotalCount;

                if (inRemainder.Buffer != nullptr)
                {
                    // Exit loop if ASIO buffer boundary is reached
                    inLoopExitReason = PacketLoopReason::ExitLoopAtAsioBoundary;
                    break;
                }
            }
            ReportPacketLoopReason("IN loop", inLoopExitReason);
        }

        if (inBuffersCount == 0)
        {
            LONG inProcessPeriodUs = (LONG)(currentTimePCUs - lastInProcessedPCUs);
            if ((lastInProcessedPCUs != 0) && (inProcessPeriodUs > (LONG)(m_audioIsochronousEngine->GetAudioStreamPropertySet().ClassicFramesPerIrp * numIrp + m_audioIsochronousEngine->GetAudioStreamPropertySet().InternalParameters.FirstPacketLatency) * 1000))
            {
                // If the IN has not been processed for a long period of time, perform a bus reset.
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "Thread interval (%uus) exceeded IRP cycle time (%uus) on first buffer, last loop exit reason in %u.", inProcessPeriodUs, (m_audioIsochronousEngine->GetAudioStreamPropertySet().ClassicFramesPerIrp * numIrp) * 1000, static_cast<ULONG>(inLoopExitReason));
                // RecoverTransferError(StreamObject->DeviceObject, StreamObject);
                break;
            }
            if ((GetStreamStatuses() != c_ioStreaming) ||
                (m_inputTransferObject[0]->GetLockDelayCount() != 0))
            {
                lastInProcessedPCUs = currentTimePCUs;
            }
        }
        else
        {
            lastInProcessedPCUs = currentTimePCUs;
        }

        // Dropout Detection
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, " - In buffers count %u, ioStable 0x%x, inLoopExitReason %u", inBuffersCount, static_cast<ULONG>(streamStatus), static_cast<ULONG>(inLoopExitReason));
        if ((streamStatus == c_ioSteady) && hasInputIsochronousInterface)
        {
            for (ULONG bufIndex = 0; bufIndex < inBuffersCount; ++bufIndex)
            {
                // ULONG length = m_inputBuffers[bufIndex].length;
                if (m_audioIsochronousEngine->GetRtPacketObject() != nullptr)
                {
                    m_audioIsochronousEngine->AcquireStreamEngineWaitLock();
                    for (ULONG deviceIndex = 0; deviceIndex < m_audioIsochronousEngine->GetNumOfInputDevices(); deviceIndex++)
                    {
                        if ((m_audioIsochronousEngine->GetCaptureStreamEngine(deviceIndex) != nullptr) && (m_audioIsochronousEngine->GetCaptureStreamEngine(deviceIndex)->GetCurrentState() == AcxStreamStateRun))
                        {
                            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - buffer index %u, transfer object %p", bufIndex, m_inputBuffers[bufIndex].TransferObject);
                            m_audioIsochronousEngine->GetRtPacketObject()->CopyToRtPacketFromInputData(
                                deviceIndex,
                                m_inputBuffers[bufIndex].Buffer + m_inputBuffers[bufIndex].Offset,
                                m_inputBuffers[bufIndex].Length,
                                m_inputBuffers[bufIndex].TotalProcessedBytesSoFar,
                                m_inputBuffers[bufIndex].TransferObject,
                                m_audioIsochronousEngine->GetAudioStreamPropertySet().InputProperty.BytesPerSample /* ex: 3 */,
                                m_audioIsochronousEngine->GetAudioStreamPropertySet().InputProperty.ValidBitsPerSample /* ex: 24*/,
                                m_audioIsochronousEngine->GetAudioStreamPropertySet().InputProperty.UsbChannels
                            );
                        }
                    }
                    m_audioIsochronousEngine->ReleaseStreamEngineWaitLock();
                }
            }
        }

        if (inBuffersCount != 0)
        {
            if (m_bufferProcessed < 2)
            {
                ++(m_bufferProcessed);
            }
        }
        // LastBusTime = usbBusTimeCurrent;
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
    return;
}

_Use_decl_annotations_
PAGED_CODE_SEG
void StreamObject::MixingEngineOutputThreadMainWithoutASIO(
    PDEVICE_CONTEXT deviceContext
)
{
    ULONGLONG       currentTimePCUs = 0ULL;
    ULONGLONG       currentTimePC = 0ULL;
    const ULONG     inputInterval = m_audioIsochronousEngine->GetInputInterfaceAndPipe().PipeInfo.Interval;
    const ULONG     outputInterval = m_audioIsochronousEngine->GetOutputInterfaceAndPipe().PipeInfo.Interval;
    const ULONG     outputPacketsPerMs = m_deviceContext->FramesPerMs >> (outputInterval - 1);
    const ULONG     outputPacketsPerIrp = m_audioIsochronousEngine->GetAudioStreamPropertySet().ClassicFramesPerIrp * outputPacketsPerMs;
    const ULONG     numIrp = m_audioIsochronousEngine->GetAudioStreamPropertySet().InternalParameters.MaxIrpNumber;
    bool            safetyOffsetApplied = false;
    BUFFER_PROPERTY inRemainder{};
    BUFFER_PROPERTY outRemainder{};
    ULONGLONG       lastInProcessedPCUs = 0ULL;
    LARGE_INTEGER   timerExpired = {0};
    LONGLONG        outBuffersTotalCount = 0ULL;
    const bool      hasOutputIsochronousInterface = m_audioIsochronousEngine->HasOutputIsochronousInterface();

    PAGED_CODE();

    for (;;)
    {
        NTSTATUS wakeupReason = STATUS_SUCCESS;
        bool     isProcessIo = false;
        wakeupReason = Wait();

        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "MaxingEngineThreadMain() WakeUp reason = %d", static_cast<int>(wakeupReason));

        // If the wakeup result is an error, exit.
        if (!NT_SUCCESS(wakeupReason) || (wakeupReason == STATUS_WAIT_0) || IsTerminateStream())
        {
            break;
        }

        // Get the current status of stream.
        StreamStatuses streamStatus = GetStreamStatuses(isProcessIo);

        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "GetStreamStatuses() %s, %d", isProcessIo ? "true" : "false", static_cast<int>(streamStatus));

        // Updated valid wake-up count.
        // Since timerExpired and ThreadWakeup are initialized and updated at the same time, they will be made common.
        IncrementWakeUpCount();

        // Gets highly accurate current time based on KeQueryPerformanceCounter.
        currentTimePCUs = USBAudioAcxDriverStreamGetCurrentTimeUs(deviceContext, &currentTimePC);

        SaveWakeUpTimePCUs(currentTimePCUs);

        ULONG pcDiffUs = static_cast<ULONG>(GetWakeUpDiffPCUs());

        LONG inElapsedTimeAfterDpc = 0;

        {
            inElapsedTimeAfterDpc = (LONG)((LONGLONG)currentTimePCUs - m_outputIsoRequestCompletionTime.LastTimeUs);
        }

        UpdateElapsedTimeUs(pcDiffUs);

        LONGLONG inCompletedPacket = 0LL;  // IN Number of packets that have been transferred isochronous
        LONGLONG outCompletedPacket = 0LL; // OUT Number of packets that have been transferred isochronous
        GetCompletedPacket(inCompletedPacket, outCompletedPacket);
        // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - in completed packet, out completed packet %llu, %lld", inCompletedPacket, outCompletedPacket);

        // Counts packets for which isochronous IN processing has been completed and creates a list.
        // The created list is stored in inBuffer, and if there is a remainder (inputRemainder) from the previous thread wakeup, it is allocated to the beginning of that list.
        ULONG            outBuffersCount = 0;
        PacketLoopReason outLoopExitReason = PacketLoopReason::ContinueLoop;

        if (isProcessIo)
        {
            ULONG outProcessRemainder = 0;

            while (outBuffersCount < (m_audioIsochronousEngine->GetAudioStreamPropertySet().InternalParameters.MaxIrpNumber - 1) * outputPacketsPerIrp)
            {
                if (!safetyOffsetApplied)
                {
                    ULONGLONG outAdjustedBuffersCount = (outBuffersTotalCount << (outputInterval - 1));

                    ULONG safetyOffsetFrame = outputPacketsPerIrp + m_audioIsochronousEngine->GetUsbLatency().OutputOffsetFrame;
                    // The buffer has not yet been processed by this thread.
                    ULONG dpcOffset = outputPacketsPerIrp;

                    // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - outAdjustedBuffersCount %llu, dpcOffset %lu, InputOffsetFrame %lu, safetyOffsetFrame %lu, %!bool!", outAdjustedBuffersCount, dpcOffset, m_audioIsochronousEngine->GetUsbLatency().InputOffsetFrame, safetyOffsetFrame, (outAdjustedBuffersCount >= (dpcOffset + m_audioIsochronousEngine->GetUsbLatency().InputOffsetFrame + safetyOffsetFrame)));

                    if (outAdjustedBuffersCount >= (dpcOffset + m_audioIsochronousEngine->GetUsbLatency().InputOffsetFrame + safetyOffsetFrame))
                    {
                        // Exit the loop after processing a safety offset
                        outLoopExitReason = PacketLoopReason::ExitLoopAfterSafetyOffset;
                        safetyOffsetApplied = true;
                        break;
                    }
                }
                {
                    // input disable
                    if (IsOutputPacketAtEstimatedPosition(inputInterval, outputInterval))
                    {
                        // Processing position reaches current position prediction
                        outLoopExitReason = PacketLoopReason::ExitLoopPacketEstimateReached;
                        break;
                    }
                }
                outProcessRemainder = CreateCompletedOutputPacketList(m_outputBuffers, &outRemainder, outBuffersCount, outputPacketsPerIrp, numIrp);

                if (outBuffersCount != 0 || !outProcessRemainder)
                {
                    IncrementOutputProcessedPacket();
                }

                ++outBuffersCount;
                ++outBuffersTotalCount;

                if (outRemainder.Buffer != nullptr)
                {
                    // Exit loop if ASIO buffer boundary is reached
                    outLoopExitReason = PacketLoopReason::ExitLoopAtAsioBoundary;
                    break;
                }
            }
            ReportPacketLoopReason("OUT loop", outLoopExitReason);
        }

        {
            lastInProcessedPCUs = currentTimePCUs;
        }

        // Dropout Detection
        ULONG outMinOffsetFrame = m_audioIsochronousEngine->GetUsbLatency().OutputOffsetFrame;
        if (outMinOffsetFrame >= (m_audioIsochronousEngine->GetAudioStreamPropertySet().InternalParameters.MaxIrpNumber - 2) * outputPacketsPerIrp)
        {
            outMinOffsetFrame = ((m_audioIsochronousEngine->GetAudioStreamPropertySet().InternalParameters.MaxIrpNumber - 2) * outputPacketsPerIrp) - 1;
        }

        ULONG bytesPerBlock = m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.BytesPerBlock;

        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, " - Out buffers count %u, ioStable 0x%x, outLoopExitReason %u", outBuffersCount, static_cast<int>(streamStatus), static_cast<ULONG>(outLoopExitReason));

        if (hasOutputIsochronousInterface)
        {
            for (ULONG bufIndex = 0; bufIndex < outBuffersCount; ++bufIndex)
            {
                ULONG  transferSize = m_outputBuffers[bufIndex].Length;
                PUCHAR outBufferStart = m_outputBuffers[bufIndex].Buffer + m_outputBuffers[bufIndex].Offset;
                ULONG  outChannels = m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.UsbChannels;
                ULONG  samples = transferSize / bytesPerBlock;

                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - outputBuffers[%u] Irp, Packet, PacketID, TransferObject, Index, %u, %u, %u, %p, %u, %llu, %lld", bufIndex, m_outputBuffers[bufIndex].Irp, m_outputBuffers[bufIndex].Packet, m_outputBuffers[bufIndex].PacketId, m_outputBuffers[bufIndex].TransferObject, m_outputBuffers[bufIndex].TransferObject->GetIndex(), m_outputBuffers[bufIndex].TransferObject->GetQPCPosition(), (bufIndex == 0) ? 0LL : (LONGLONG)(m_outputBuffers[bufIndex].TransferObject->GetQPCPosition()) - (LONGLONG)(m_outputBuffers[bufIndex - 1].TransferObject->GetQPCPosition()));

                StreamObject::ClearOutputBuffer(m_audioIsochronousEngine->GetAudioStreamPropertySet().AudioProperty.CurrentSampleFormat, outBufferStart, outChannels, bytesPerBlock, samples);
                if (streamStatus == c_ioSteady)
                {
                    if (m_audioIsochronousEngine->GetRtPacketObject() != nullptr)
                    {
                        m_audioIsochronousEngine->AcquireStreamEngineWaitLock();
                        for (ULONG deviceIndex = 0; deviceIndex < m_audioIsochronousEngine->GetNumOfOutputDevices(); deviceIndex++)
                        {
                            if ((m_audioIsochronousEngine->GetRenderStreamEngine(deviceIndex) != nullptr) && (m_audioIsochronousEngine->GetRenderStreamEngine(deviceIndex)->GetCurrentState() == AcxStreamStateRun))
                            {
                                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - buffer index %u, transfer object %p", bufIndex, m_outputBuffers[bufIndex].TransferObject);
                                m_audioIsochronousEngine->GetRtPacketObject()->CopyFromRtPacketToOutputData(
                                    deviceIndex,
                                    outBufferStart,
                                    transferSize,
                                    m_outputBuffers[bufIndex].TotalProcessedBytesSoFar,
                                    m_outputBuffers[bufIndex].TransferObject,
                                    m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.BytesPerSample /* ex: 3 */,
                                    m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.ValidBitsPerSample /* ex: 24*/,
                                    m_audioIsochronousEngine->GetAudioStreamPropertySet().OutputProperty.UsbChannels
                                );
                            }
                        }
                        m_audioIsochronousEngine->ReleaseStreamEngineWaitLock();
                    }
                }
            }
        }
        if (outBuffersCount != 0)
        {
            if (m_bufferProcessed < 2)
            {
                ++(m_bufferProcessed);
            }
        }
        // LastBusTime = usbBusTimeCurrent;
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
    return;
}
