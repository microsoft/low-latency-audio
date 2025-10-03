// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

/*++

Module Name:

    RtPacketObject.cpp

Abstract:

    Implement a class to handle RtPacket processing.

Environment:

    Kernel-mode Driver Framework

--*/

#include "Driver.h"
#include "Device.h"
#include "Public.h"
#include "Common.h"
#include "USBAudio.h"
#include "RtPacketObject.h"
#include "ContiguousMemory.h"
#include "TransferObject.h"
#include "StreamEngine.h"

#ifndef __INTELLISENSE__
#include "RtPacketObject.tmh"
#endif

#if !defined(STATIC_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
#define STATIC_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT \
    DEFINE_WAVEFORMATEX_GUID(WAVE_FORMAT_IEEE_FLOAT)
DEFINE_GUIDSTRUCT("00000003-0000-0010-8000-00aa00389b71", KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
#define KSDATAFORMAT_SUBTYPE_IEEE_FLOAT DEFINE_GUIDNAMED(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
#endif

_Use_decl_annotations_
PAGED_CODE_SEG
RtPacketObject * RtPacketObject::Create(
    PDEVICE_CONTEXT deviceContext
)
{
    PAGED_CODE();
    return new (POOL_FLAG_NON_PAGED, DRIVER_TAG) RtPacketObject(deviceContext);
}

_Use_decl_annotations_
PAGED_CODE_SEG
RtPacketObject::RtPacketObject(
    PDEVICE_CONTEXT deviceContext
)
    : m_deviceContext(deviceContext)

{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
RtPacketObject::~RtPacketObject()
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    if (m_inputWaveFormat)
    {
        ExFreePoolWithTag(m_inputWaveFormat, DRIVER_TAG);
        m_inputWaveFormat = nullptr;
    }

    if (m_outputWaveFormat)
    {
        ExFreePoolWithTag(m_outputWaveFormat, DRIVER_TAG);
        m_outputWaveFormat = nullptr;
    }

    if (m_inputRtPacketInfoMemory != nullptr)
    {
        WdfObjectDelete(m_inputRtPacketInfoMemory);
        m_inputRtPacketInfoMemory = nullptr;
    }

    if (m_outputRtPacketInfoMemory != nullptr)
    {
        WdfObjectDelete(m_outputRtPacketInfoMemory);
        m_outputRtPacketInfoMemory = nullptr;
    }
    m_inputRtPacketInfo = m_outputRtPacketInfo = nullptr;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
RtPacketObject::SetDataFormat(
    bool          isInput,
    ACXDATAFORMAT dataFormat
)
{
    PAGED_CODE();
    NTSTATUS status = STATUS_INVALID_PARAMETER;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(dataFormat == nullptr, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE(!IsEqualGUID(KSDATAFORMAT_TYPE_AUDIO, AcxDataFormatGetMajorFormat(dataFormat)), STATUS_INVALID_PARAMETER);

    // KsDataFormat* ksDataFormat = AcxDataFormatGetKsDataFormat(dataFormat);
    GUID subFormat = AcxDataFormatGetSubFormat(dataFormat);

    PWAVEFORMATEX &                waveFormat = isInput ? m_inputWaveFormat : m_outputWaveFormat;
    PWAVEFORMATEX                  waveFormatEx = static_cast<PWAVEFORMATEX>(AcxDataFormatGetWaveFormatEx(dataFormat));
    PWAVEFORMATEXTENSIBLE          waveFormatExtensible = static_cast<PWAVEFORMATEXTENSIBLE>(AcxDataFormatGetWaveFormatExtensible(dataFormat));
    PWAVEFORMATEXTENSIBLE_IEC61937 waveFormatExtensibleIEC61937 = static_cast<PWAVEFORMATEXTENSIBLE_IEC61937>(AcxDataFormatGetWaveFormatExtensibleIec61937(dataFormat));

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - waveFormatEx = %p, waveFormatExtensible = %p, waveFormatExtensibleIEC61937 = %p", waveFormatEx, waveFormatExtensible, waveFormatExtensibleIEC61937);

    if (waveFormatEx)
    {
        // Free the previously allocated waveformat
        if (waveFormat)
        {
            ExFreePoolWithTag(waveFormat, DRIVER_TAG);
        }

        waveFormat = (PWAVEFORMATEX)ExAllocatePool2(
            POOL_FLAG_NON_PAGED,
            (waveFormatEx->wFormatTag == WAVE_FORMAT_PCM) ? sizeof(PCMWAVEFORMAT) : sizeof(WAVEFORMATEX) + waveFormatEx->cbSize,
            DRIVER_TAG
        );

        if (waveFormat)
        {
            RtlCopyMemory(waveFormat, waveFormatEx, (waveFormatEx->wFormatTag == WAVE_FORMAT_PCM) ? sizeof(PCMWAVEFORMAT) : sizeof(WAVEFORMATEX) + waveFormatEx->cbSize);
            if (isInput)
            {
                m_inputBytesPerSample = waveFormat->wBitsPerSample / 8;
            }
            else
            {
                m_outputBytesPerSample = waveFormat->wBitsPerSample / 8;
            }
            // m_channels = AcxDataFormatGetChannelsCount(dataFormat);

            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - wFormatTag      = 0x%x", waveFormat->wFormatTag);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - nChannels       = %d  ", waveFormat->nChannels);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - nSamplesPerSec  = %d  ", waveFormat->nSamplesPerSec);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - nAvgBytesPerSec = %d  ", waveFormat->nAvgBytesPerSec);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - nBlockAlign     = %d  ", waveFormat->nBlockAlign);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - wBitsPerSample  = %d  ", waveFormat->wBitsPerSample);
            status = STATUS_SUCCESS;
        }
        else
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void RtPacketObject::SetIsoPacketInfo(
    IsoDirection direction,
    ULONG        isoPacketSize,
    ULONG        numIsoPackets
)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry, %s, %d, %d", (direction == IsoDirection::Out) ? "eIsoOut" : "In", isoPacketSize, numIsoPackets);

    RT_PACKET_INFO * rtPacketInfo = nullptr;
    ULONG            numOfDevices = 0;

    if (direction == IsoDirection::In)
    {
        rtPacketInfo = m_inputRtPacketInfo;
        numOfDevices = m_numOfInputDevices;
    }
    else
    {
        rtPacketInfo = m_outputRtPacketInfo;
        numOfDevices = m_numOfOutputDevices;
    }

    {
        for (UCHAR deviceIndex = 0; deviceIndex < numOfDevices; deviceIndex++)
        {
            WdfSpinLockAcquire(rtPacketInfo[deviceIndex].PositionSpinLock);
            rtPacketInfo[deviceIndex].IsoPacketSize = isoPacketSize;
            rtPacketInfo[deviceIndex].NumIsoPackets = numIsoPackets;
            // rtPacketInfo[deviceIndex].BufferLength  = isoPacketSize * numIsoPackets * UAC_MAX_IRP_NUMBER;
            WdfSpinLockRelease(rtPacketInfo[deviceIndex].PositionSpinLock);
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void RtPacketObject::Reset(
    bool  isInput,
    ULONG deviceIndex
)
{
    RT_PACKET_INFO * rtPacketInfo = nullptr;
    ULONG            numOfDevices = 0;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    if (isInput)
    {
        rtPacketInfo = m_inputRtPacketInfo;
        numOfDevices = m_numOfInputDevices;
    }
    else
    {
        rtPacketInfo = m_outputRtPacketInfo;
        numOfDevices = m_numOfOutputDevices;
    }

    if (deviceIndex < numOfDevices)
    {
        WdfSpinLockAcquire(rtPacketInfo[deviceIndex].PositionSpinLock);

        rtPacketInfo[deviceIndex].IsoPacketSize = 0;
        rtPacketInfo[deviceIndex].NumIsoPackets = 0;
        rtPacketInfo[deviceIndex].RtPacketPosition = 0;
        rtPacketInfo[deviceIndex].RtPacketCurrentPacket = 0;
        rtPacketInfo[deviceIndex].LastPacketStartQpcPosition = 0;

        WdfSpinLockRelease(rtPacketInfo[deviceIndex].PositionSpinLock);
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void RtPacketObject::Reset(
    bool isInput
)
{
    ULONG numOfDevices = 0;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    if (isInput)
    {
        numOfDevices = m_numOfInputDevices;
    }
    else
    {
        numOfDevices = m_numOfOutputDevices;
    }

    for (ULONG deviceIndex = 0; deviceIndex < numOfDevices; deviceIndex++)
    {
        Reset(isInput, deviceIndex);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void RtPacketObject::FeedOutputWriteBytes(
    ULONG /* ulByteCount */
)
{
    // WdfSpinLockAcquire(m_outputRtPacketInfo[deviceIndex].PositionSpinLock);
    // WdfSpinLockRelease(m_outputRtPacketInfo[deviceIndex].PositionSpinLock);
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
RtPacketObject::SetRtPackets(
    bool    isInput,
    ULONG   deviceIndex,
    PVOID * rtPackets,
    ULONG   rtPacketsCount,
    ULONG   rtPacketSize,
    ULONG   channel,
    ULONG   numOfChannelsPerDevice
)
{
    NTSTATUS         status = STATUS_SUCCESS;
    RT_PACKET_INFO * rtPacketInfo = nullptr;
    ULONG            numOfDevices = 0;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry, %s, rtPacketsCount = %d, rtPacketSize = %d", isInput ? "Input" : "Output", rtPacketsCount, rtPacketSize);

    if (isInput)
    {
        rtPacketInfo = m_inputRtPacketInfo;
        numOfDevices = m_numOfInputDevices;
    }
    else
    {
        rtPacketInfo = m_outputRtPacketInfo;
        numOfDevices = m_numOfOutputDevices;
    }

    RETURN_NTSTATUS_IF_TRUE(deviceIndex >= numOfDevices, STATUS_INVALID_PARAMETER);
    RETURN_NTSTATUS_IF_TRUE(rtPacketInfo[deviceIndex].RtPackets != nullptr, STATUS_INVALID_DEVICE_STATE);

    rtPacketInfo[deviceIndex].RtPackets = rtPackets;
    rtPacketInfo[deviceIndex].RtPacketsCount = rtPacketsCount;
    rtPacketInfo[deviceIndex].RtPacketSize = rtPacketSize;
    rtPacketInfo[deviceIndex].usbChannel = channel;
    rtPacketInfo[deviceIndex].channels = numOfChannelsPerDevice;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit %!STATUS!", status);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
void RtPacketObject::UnsetRtPackets(
    bool  isInput,
    ULONG deviceIndex
)
{
    RT_PACKET_INFO * rtPacketInfo = nullptr;
    ULONG            numOfDevices = 0;

    PAGED_CODE();

    if (isInput)
    {
        rtPacketInfo = m_inputRtPacketInfo;
        numOfDevices = m_numOfInputDevices;
    }
    else
    {
        rtPacketInfo = m_outputRtPacketInfo;
        numOfDevices = m_numOfOutputDevices;
    }

    if (deviceIndex < numOfDevices)
    {
        rtPacketInfo[deviceIndex].RtPackets = nullptr;
        rtPacketInfo[deviceIndex].RtPacketsCount = 0;
        rtPacketInfo[deviceIndex].RtPacketSize = 0;
    }
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
RtPacketObject::CopyFromRtPacketToOutputData(
    ULONG            deviceIndex,
    PUCHAR           buffer,
    ULONG            length,
    ULONG            totalProcessedBytesSoFar,
    TransferObject * transferObject,
    ULONG            usbBytesPerSample,
    ULONG /* usbValidBitsPerSample */,
    ULONG usbChannels
)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG    bytesCopiedSrcData = 0;
    ULONG    bytesCopiedDstData = 0;
    ULONG    bytesCopiedUpToBoundary = 0;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry, RtPacketPosition, rtPacketSize, rtPacketsCount = %llu, %u, %u", m_outputRtPacketInfo[deviceIndex].RtPacketPosition, m_outputRtPacketInfo[deviceIndex].RtPacketSize, m_outputRtPacketInfo[deviceIndex].RtPacketsCount);

    RETURN_NTSTATUS_IF_TRUE_ACTION(deviceIndex >= m_numOfOutputDevices, status = STATUS_INVALID_PARAMETER, status);

    ASSERT(buffer != nullptr);
    ASSERT(length != 0);
    ASSERT(transferObject != nullptr);
    ASSERT(m_deviceContext != nullptr);
    ASSERT(m_deviceContext->RenderStreamEngine != nullptr);
    ASSERT(transferObject->GetTransferredBytesInThisIrp() != 0);
    ASSERT(m_outputRtPacketInfo[deviceIndex].RtPacketSize != 0);

    RT_PACKET_INFO * rtPacketInfo = &(m_outputRtPacketInfo[deviceIndex]);

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - TransferredBytesInThisIrp = %u", transferObject->GetTransferredBytesInThisIrp());
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - m_outputRtPacketInfo[deviceIndex].rtPacketSize = %u", m_outputRtPacketInfo[deviceIndex].RtPacketSize);

    IF_TRUE_ACTION_JUMP(buffer == nullptr, status = STATUS_INVALID_PARAMETER, CopyFromRtPacketToOutputData_Exit);
    IF_TRUE_ACTION_JUMP(length == 0, status = STATUS_INVALID_PARAMETER, CopyFromRtPacketToOutputData_Exit);
    IF_TRUE_ACTION_JUMP(transferObject == nullptr, status = STATUS_INVALID_PARAMETER, CopyFromRtPacketToOutputData_Exit);
    IF_TRUE_ACTION_JUMP(m_deviceContext == nullptr, status = STATUS_UNSUCCESSFUL, CopyFromRtPacketToOutputData_Exit);
    IF_TRUE_ACTION_JUMP(m_deviceContext->RenderStreamEngine == nullptr, status = STATUS_UNSUCCESSFUL, CopyFromRtPacketToOutputData_Exit);
    IF_TRUE_ACTION_JUMP(transferObject->GetTransferredBytesInThisIrp() == 0, status = STATUS_UNSUCCESSFUL, CopyFromRtPacketToOutputData_Exit);
    IF_TRUE_ACTION_JUMP(m_outputRtPacketInfo[deviceIndex].RtPacketSize == 0, status = STATUS_UNSUCCESSFUL, CopyFromRtPacketToOutputData_Exit);

    bool fedRtPacket = false;

    switch (m_deviceContext->AudioProperty.CurrentSampleFormat)
    {
    case UACSampleFormat::UAC_SAMPLE_FORMAT_PCM: {
        for (ULONG acxCh = 0; acxCh < rtPacketInfo->channels; acxCh++)
        {
            ULONG rtPacketIndex = (rtPacketInfo->RtPacketPosition / rtPacketInfo->RtPacketSize) % rtPacketInfo->RtPacketsCount;
            ULONG srcIndexInRtPacket = rtPacketInfo->RtPacketPosition % rtPacketInfo->RtPacketSize + acxCh * m_outputBytesPerSample;
            PBYTE srcData = (PBYTE)rtPacketInfo->RtPackets[rtPacketIndex];
            PBYTE dstData = (PBYTE)buffer;

            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - acxCh, rtPacketIndex, srcIndexInRtPacket, %u, %u, %u", acxCh, rtPacketIndex, srcIndexInRtPacket);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - dstData, buffer, length = %p, %p, %u", dstData, buffer, length);

            for (ULONG dstIndex = (acxCh + rtPacketInfo->usbChannel) * usbBytesPerSample; dstIndex < length;)
            {
                // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - srcIndexInRtPacket, dstIndex = %u, %u", srcIndexInRtPacket, dstIndex);

                // To accommodate differing specifications between the bytesPerSample of the device and ACX audio, the following code modifications are necessary.
                if (m_outputBytesPerSample == 2)
                {
                    PSHORT outSample = (PSHORT)(dstData + dstIndex);
                    LONG   thisSample = (LONG)(*outSample) + *(PSHORT)(srcData + srcIndexInRtPacket);
                    if (thisSample > 0x7fff)
                    {
                        *outSample = 0x7fff;
                    }
                    else if (thisSample < -0x8000)
                    {
                        *outSample = -0x8000;
                    }

                    {
                        *outSample = (SHORT)thisSample;
                    }
                }
                else if (m_outputBytesPerSample == 3)
                {
                    PUCHAR          outSample = (PUCHAR)(dstData + dstIndex);
                    volatile BYTE * wdmSample = (volatile BYTE *)(srcData + srcIndexInRtPacket);
                    LONG            thisSample = (LONG)((ULONG)outSample[0] + ((ULONG)outSample[1] << 8)) + ((LONG)((PCHAR)outSample)[2] << 16) + (LONG)((ULONG)wdmSample[0] + ((ULONG)wdmSample[1] << 8)) + ((LONG)((PCHAR)wdmSample)[2] << 16);
                    if (thisSample > 0x7fffff)
                    {
                        outSample[0] = 0xff;
                        outSample[1] = 0xff;
                        outSample[2] = 0x7f;
                    }
                    else if (thisSample < -0x800000)
                    {
                        outSample[0] = 0x00;
                        outSample[1] = 0x00;
                        outSample[2] = 0x80;
                    }
                    else
                    {
                        outSample[0] = ((PUCHAR)(&thisSample))[0];
                        outSample[1] = ((PUCHAR)(&thisSample))[1];
                        outSample[2] = ((PUCHAR)(&thisSample))[2];
                    }
                }
                else if (m_outputBytesPerSample == 4)
                {
                    PUCHAR          outSample = (PUCHAR)(dstData + dstIndex);
                    volatile BYTE * wdmSample = (volatile BYTE *)(srcData + srcIndexInRtPacket);
                    LONGLONG        thisSample = (LONGLONG) * ((LONG *)outSample) + (LONGLONG) * ((LONG *)wdmSample);

                    if (thisSample > 0x7fffffffLL)
                    {
                        outSample[0] = 0xff;
                        outSample[1] = 0xff;
                        outSample[2] = 0xff;
                        outSample[3] = 0x7f;
                    }
                    else if (thisSample < -0x80000000LL)
                    {
                        outSample[0] = 0x00;
                        outSample[1] = 0x00;
                        outSample[2] = 0x00;
                        outSample[3] = 0x80;
                    }
                    else
                    {
                        outSample[0] = ((PUCHAR)(&thisSample))[0];
                        outSample[1] = ((PUCHAR)(&thisSample))[1];
                        outSample[2] = ((PUCHAR)(&thisSample))[2];
                        outSample[3] = ((PUCHAR)(&thisSample))[3];
                    }
                }
                dstIndex += (usbBytesPerSample * usbChannels);
                srcIndexInRtPacket += m_outputBytesPerSample * rtPacketInfo->channels;
                bytesCopiedDstData += m_outputBytesPerSample;
                bytesCopiedSrcData += m_outputBytesPerSample;
                if (srcIndexInRtPacket >= rtPacketInfo->RtPacketSize)
                {
                    // {
                    // 	CHAR outputString[100] = "";
                    // 	sprintf_s(outputString, sizeof(outputString), "DumpByteArray rtPackets[%d]", rtPacketIndex);
                    // 	DumpByteArray(outputString, (UCHAR*)rtPacketInfo->RtPackets[rtPacketIndex], rtPacketInfo->RtPacketSize);
                    // }
                    bytesCopiedUpToBoundary = totalProcessedBytesSoFar + bytesCopiedDstData;
                    fedRtPacket = true;
                    srcIndexInRtPacket = acxCh * m_outputBytesPerSample;
                    rtPacketIndex++;
                    rtPacketIndex %= rtPacketInfo->RtPacketsCount;
                    srcData = ((PBYTE)rtPacketInfo->RtPackets[rtPacketIndex]);
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - acxCh, rtPacketIndex, srcIndexInRtPacket, %u, %u, %u", acxCh, rtPacketIndex, srcIndexInRtPacket);
                }
            }
        }
    }
    break;
    case UACSampleFormat::UAC_SAMPLE_FORMAT_IEEE_FLOAT: {
        for (ULONG acxCh = 0; acxCh < rtPacketInfo->channels; acxCh++)
        {
            ULONG rtPacketIndex = (rtPacketInfo->RtPacketPosition / rtPacketInfo->RtPacketSize) % rtPacketInfo->RtPacketsCount;
            ULONG srcIndexInRtPacket = rtPacketInfo->RtPacketPosition % rtPacketInfo->RtPacketSize + acxCh * m_outputBytesPerSample;
            PBYTE srcData = (PBYTE)rtPacketInfo->RtPackets[rtPacketIndex];
            PBYTE dstData = (PBYTE)buffer;

            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - acxCh, rtPacketIndex, srcIndexInRtPacket, %u, %u, %u", acxCh, rtPacketIndex, srcIndexInRtPacket);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - dstData, buffer, length = %p, %p, %u", dstData, buffer, length);

            for (ULONG dstIndex = (acxCh + rtPacketInfo->usbChannel) * usbBytesPerSample; dstIndex < length;)
            {
                // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - srcIndexInRtPacket, dstIndex = %u, %u", srcIndexInRtPacket, dstIndex);

                float * outSample = (float *)(dstData + dstIndex);
                *outSample = *outSample + *(float *)(srcData + srcIndexInRtPacket);

                dstIndex += (usbBytesPerSample * usbChannels);
                srcIndexInRtPacket += m_outputBytesPerSample * rtPacketInfo->channels;
                bytesCopiedDstData += m_outputBytesPerSample;
                bytesCopiedSrcData += m_outputBytesPerSample;
                if (srcIndexInRtPacket >= rtPacketInfo->RtPacketSize)
                {
                    // {
                    // 	CHAR outputString[100] = "";
                    // 	sprintf_s(outputString, sizeof(outputString), "DumpByteArray rtPackets[%d]", rtPacketIndex);
                    // 	DumpByteArray(outputString, (UCHAR*)rtPacketInfo->RtPackets[rtPacketIndex], rtPacketInfo->RtPacketSize);
                    // }
                    bytesCopiedUpToBoundary = totalProcessedBytesSoFar + bytesCopiedDstData;
                    fedRtPacket = true;
                    srcIndexInRtPacket = acxCh * m_outputBytesPerSample;
                    rtPacketIndex++;
                    rtPacketIndex %= rtPacketInfo->RtPacketsCount;
                    srcData = ((PBYTE)rtPacketInfo->RtPackets[rtPacketIndex]);
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - acxCh, rtPacketIndex, srcIndexInRtPacket, %u, %u, %u", acxCh, rtPacketIndex, srcIndexInRtPacket);
                }
            }
        }
    }
    default:
        break;
    }

    rtPacketInfo->RtPacketPosition += bytesCopiedSrcData;
    if (fedRtPacket)
    { // Calculate the time when filling is completed in RtPacket.
        // The ratio used in the calculation is based on the amount of data transferred within this URB. For output, use DST.
        ULONGLONG estimatedQPCPosition = transferObject->CalculateEstimatedQPCPosition(bytesCopiedUpToBoundary);

        // Passing the incremented value to AcxRtStreamNotifyPacketComplete will overwrite the waveform being transferred.
        ULONGLONG completedRtPacket = (ULONG)InterlockedIncrement((PLONG)&rtPacketInfo->RtPacketCurrentPacket) - 1;
        InterlockedExchange64((LONG64 *)&rtPacketInfo->LastPacketStartQpcPosition, estimatedQPCPosition);

        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - index, completedRtPacket, estimatedQPCPosition, qpcPosition, PeriodQPCPosition, bytesCopiedUpToBoundary, TransferredBytesInThisIrp, %d, %llu, %llu, %llu, %llu, %u, %u", transferObject->GetIndex(), completedRtPacket, estimatedQPCPosition, transferObject->GetQPCPosition(), transferObject->GetPeriodQPCPosition(), bytesCopiedUpToBoundary, transferObject->GetTransferredBytesInThisIrp());

        // Tell ACX we've completed the packet.
        if ((m_deviceContext->RenderStreamEngine[deviceIndex] != nullptr) && (m_deviceContext->RenderStreamEngine[deviceIndex]->GetACXStream() != nullptr))
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "call AcxRtStreamNotifyPacketComplete(%p, %llu, %llu)", m_deviceContext->RenderStreamEngine[deviceIndex] != nullptr ? m_deviceContext->RenderStreamEngine[deviceIndex]->GetACXStream() : (void *)(1), completedRtPacket, estimatedQPCPosition);
            (void)AcxRtStreamNotifyPacketComplete(m_deviceContext->RenderStreamEngine[deviceIndex]->GetACXStream(), completedRtPacket, estimatedQPCPosition);
        }
    }

CopyFromRtPacketToOutputData_Exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit, RtPacketPosition, bytesCopiedSrcData, bytesCopiedUpToBoundary = %llu, %u, %u", rtPacketInfo->RtPacketPosition, bytesCopiedSrcData, bytesCopiedUpToBoundary);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
RtPacketObject::CopyToRtPacketFromInputData(
    ULONG            deviceIndex,
    PUCHAR           buffer,
    ULONG            length,
    ULONG            totalProcessedBytesSoFar,
    TransferObject * transferObject,
    ULONG            usbBytesPerSample,
    ULONG /* usbValidBitsPerSample */,
    ULONG usbChannels
)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG    bytesCopiedSrcData = 0;
    ULONG    bytesCopiedDstData = 0;
    ULONG    bytesCopiedUpToBoundary = 0;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry, RtPacketPosition, rtPacketSize, rtPacketsCount = %llu, %u, %u", m_inputRtPacketInfo[deviceIndex].RtPacketPosition, m_inputRtPacketInfo[deviceIndex].RtPacketSize, m_inputRtPacketInfo[deviceIndex].RtPacketsCount);

    RETURN_NTSTATUS_IF_TRUE_ACTION(deviceIndex >= m_numOfInputDevices, status = STATUS_INVALID_PARAMETER, status);

    ASSERT(buffer != nullptr);
    ASSERT(length != 0);
    ASSERT(transferObject != nullptr);
    ASSERT(m_deviceContext != nullptr);
    ASSERT(m_deviceContext->CaptureStreamEngine != nullptr);
    ASSERT(transferObject->GetTransferredBytesInThisIrp() != 0);
    ASSERT(m_inputRtPacketInfo[deviceIndex].RtPacketSize != 0);

    RT_PACKET_INFO * rtPacketInfo = &(m_inputRtPacketInfo[deviceIndex]);

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - TransferredBytesInThisIrp = %u", transferObject->GetTransferredBytesInThisIrp());
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - m_inputRtPacketInfo[deviceIndex].rtPacketSize = %u", m_inputRtPacketInfo[deviceIndex].RtPacketSize);

    IF_TRUE_ACTION_JUMP(buffer == nullptr, status = STATUS_INVALID_PARAMETER, CopyToRtPacketFromInputData_Exit);
    IF_TRUE_ACTION_JUMP(length == 0, status = STATUS_INVALID_PARAMETER, CopyToRtPacketFromInputData_Exit);
    IF_TRUE_ACTION_JUMP(transferObject == nullptr, status = STATUS_INVALID_PARAMETER, CopyToRtPacketFromInputData_Exit);
    IF_TRUE_ACTION_JUMP(m_deviceContext == nullptr, status = STATUS_UNSUCCESSFUL, CopyToRtPacketFromInputData_Exit);
    IF_TRUE_ACTION_JUMP(m_deviceContext->CaptureStreamEngine == nullptr, status = STATUS_UNSUCCESSFUL, CopyToRtPacketFromInputData_Exit);
    IF_TRUE_ACTION_JUMP(transferObject->GetTransferredBytesInThisIrp() == 0, status = STATUS_UNSUCCESSFUL, CopyToRtPacketFromInputData_Exit);
    IF_TRUE_ACTION_JUMP(m_inputRtPacketInfo[deviceIndex].RtPacketSize == 0, status = STATUS_UNSUCCESSFUL, CopyToRtPacketFromInputData_Exit);

    bool filledRtPacket = false;

    switch (m_deviceContext->AudioProperty.CurrentSampleFormat)
    {
    case UACSampleFormat::UAC_SAMPLE_FORMAT_PCM: {
        for (ULONG acxCh = 0; acxCh < rtPacketInfo->channels; acxCh++)
        {
            ULONG rtPacketIndex = (rtPacketInfo->RtPacketPosition / rtPacketInfo->RtPacketSize) % rtPacketInfo->RtPacketsCount;
            ULONG dstIndexInRtPacket = rtPacketInfo->RtPacketPosition % rtPacketInfo->RtPacketSize + acxCh * m_inputBytesPerSample;
            PBYTE srcData = (PBYTE)buffer;
            PBYTE dstData = (PBYTE)rtPacketInfo->RtPackets[rtPacketIndex];

            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - acxCh, rtPacketIndex, dstIndexInRtPacket, %u, %u, %u", acxCh, rtPacketIndex, dstIndexInRtPacket);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - srcData, buffer, length = %p, %p, %u", srcData, buffer, length);

            for (ULONG srcIndex = (acxCh + rtPacketInfo->usbChannel) * usbBytesPerSample; srcIndex < length;)
            {
                // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - dstIndexInRtPacket, dstIndex = %u, %u", dstIndexInRtPacket, srcIndex);

                // To accommodate differing specifications between the bytesPerSample of the device and ACX audio, the following code modifications are necessary.
                if (m_inputBytesPerSample == 2)
                {
                    *(PSHORT)(dstData + dstIndexInRtPacket) = *(PSHORT)(srcData + srcIndex);
                }
                else if (m_inputBytesPerSample == 3)
                {
                    *(dstData + dstIndexInRtPacket) = *(srcData + srcIndex);
                    *(dstData + dstIndexInRtPacket + 1) = *(srcData + srcIndex + 1);
                    *(dstData + dstIndexInRtPacket + 2) = *(srcData + srcIndex + 2);
                }
                else if (m_inputBytesPerSample == 4)
                {
                    *((LONG *)(dstData + dstIndexInRtPacket)) = *((LONG *)(srcData + srcIndex));
                }
                srcIndex += (usbBytesPerSample * usbChannels);
                dstIndexInRtPacket += m_inputBytesPerSample * rtPacketInfo->channels;
                bytesCopiedDstData += m_inputBytesPerSample;
                bytesCopiedSrcData += m_inputBytesPerSample;
                if (dstIndexInRtPacket >= rtPacketInfo->RtPacketSize)
                {
                    bytesCopiedUpToBoundary = totalProcessedBytesSoFar + bytesCopiedSrcData;
                    filledRtPacket = true;
                    dstIndexInRtPacket = acxCh * m_inputBytesPerSample;
                    rtPacketIndex++;
                    rtPacketIndex %= rtPacketInfo->RtPacketsCount;
                    dstData = ((PBYTE)rtPacketInfo->RtPackets[rtPacketIndex]);
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - acxCh, rtPacketIndex, dstIndexInRtPacket, %u, %u, %u", acxCh, rtPacketIndex, dstIndexInRtPacket);
                }
            }
        }
    }
    break;
    case UACSampleFormat::UAC_SAMPLE_FORMAT_IEEE_FLOAT: {
        for (ULONG acxCh = 0; acxCh < rtPacketInfo->channels; acxCh++)
        {
            ULONG rtPacketIndex = (rtPacketInfo->RtPacketPosition / rtPacketInfo->RtPacketSize) % rtPacketInfo->RtPacketsCount;
            ULONG dstIndexInRtPacket = rtPacketInfo->RtPacketPosition % rtPacketInfo->RtPacketSize + acxCh * m_inputBytesPerSample;
            PBYTE srcData = (PBYTE)buffer;
            PBYTE dstData = (PBYTE)rtPacketInfo->RtPackets[rtPacketIndex];

            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - acxCh, rtPacketIndex, dstIndexInRtPacket, %u, %u, %u", acxCh, rtPacketIndex, dstIndexInRtPacket);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - srcData, buffer, length = %p, %p, %u", srcData, buffer, length);
            for (ULONG srcIndex = (acxCh + rtPacketInfo->usbChannel) * usbBytesPerSample; srcIndex < length;)
            {
                // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - dstIndexInRtPacket, dstIndex = %u, %u", dstIndexInRtPacket, srcIndex);

                *((float *)(dstData + dstIndexInRtPacket)) = *((float *)(srcData + srcIndex));
                srcIndex += (usbBytesPerSample * usbChannels);
                dstIndexInRtPacket += m_inputBytesPerSample * rtPacketInfo->channels;
                bytesCopiedDstData += m_inputBytesPerSample;
                bytesCopiedSrcData += m_inputBytesPerSample;
                if (dstIndexInRtPacket >= rtPacketInfo->RtPacketSize)
                {
                    bytesCopiedUpToBoundary = totalProcessedBytesSoFar + bytesCopiedSrcData;
                    filledRtPacket = true;
                    dstIndexInRtPacket = acxCh * m_inputBytesPerSample;
                    rtPacketIndex++;
                    rtPacketIndex %= rtPacketInfo->RtPacketsCount;
                    dstData = ((PBYTE)rtPacketInfo->RtPackets[rtPacketIndex]);
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - acxCh, rtPacketIndex, dstIndexInRtPacket, %u, %u, %u", acxCh, rtPacketIndex, dstIndexInRtPacket);
                }
            }
        }
    }
    break;
    default:
        break;
    }

    rtPacketInfo->RtPacketPosition += bytesCopiedDstData;
    if (filledRtPacket)
    { // Calculate the time when filling is completed in RtPacket.
        // The ratio used in the calculation is based on the amount of data transferred within this URB. For input, use SRC.
        ULONGLONG estimatedQPCPosition = transferObject->CalculateEstimatedQPCPosition(bytesCopiedUpToBoundary);

        ULONGLONG completedRtPacket = (ULONG)InterlockedIncrement((PLONG)&rtPacketInfo->RtPacketCurrentPacket) - 1;
        InterlockedExchange64((LONG64 *)&rtPacketInfo->LastPacketStartQpcPosition, estimatedQPCPosition);

        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - completedRtPacket, estimatedQPCPosition, qpcPosition, PeriodQPCPosition, bytesCopiedUpToBoundary, TransferredBytesInThisIrp, m_index, %llu, %llu, %llu, %llu, %u, %u, %u", completedRtPacket, estimatedQPCPosition, transferObject->GetQPCPosition(), transferObject->GetPeriodQPCPosition(), bytesCopiedUpToBoundary, transferObject->GetTransferredBytesInThisIrp(), transferObject->GetIndex());

        // Tell ACX we've completed the packet.
        if ((m_deviceContext->CaptureStreamEngine[deviceIndex] != nullptr) && (m_deviceContext->CaptureStreamEngine[deviceIndex]->GetACXStream() != nullptr))
        {
            (void)AcxRtStreamNotifyPacketComplete(m_deviceContext->CaptureStreamEngine[deviceIndex]->GetACXStream(), completedRtPacket, estimatedQPCPosition);
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "call AcxRtStreamNotifyPacketComplete(%p, %llu, %llu)", m_deviceContext->CaptureStreamEngine[deviceIndex] != nullptr ? m_deviceContext->CaptureStreamEngine[deviceIndex]->GetACXStream() : (void *)(1), completedRtPacket, estimatedQPCPosition);
        }
        else
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "can't call AcxRtStreamNotifyPacketComplete, %p, %p", m_deviceContext->CaptureStreamEngine, (m_deviceContext->CaptureStreamEngine != nullptr) ? m_deviceContext->CaptureStreamEngine[deviceIndex]->GetACXStream() : nullptr);
        }
    }

CopyToRtPacketFromInputData_Exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit, RtPacketPosition, bytesCopiedUpToBoundary, bytesCopiedDstData = %llu, %u, %u", rtPacketInfo->RtPacketPosition, bytesCopiedUpToBoundary, bytesCopiedDstData);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
RtPacketObject::GetCurrentPacket(
    bool   isInput,
    ULONG  deviceIndex,
    PULONG currentPacket
)
{

    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(deviceIndex >= (isInput ? m_numOfInputDevices : m_numOfOutputDevices), STATUS_INVALID_PARAMETER);

    if (isInput)
    {
        *currentPacket = (ULONG)InterlockedCompareExchange((LONG *)&m_inputRtPacketInfo[deviceIndex].RtPacketCurrentPacket, -1, -1);
    }
    else
    {
        *currentPacket = (ULONG)InterlockedCompareExchange((LONG *)&m_outputRtPacketInfo[deviceIndex].RtPacketCurrentPacket, -1, -1);

        // {
        // 	ULONG rtPacketIndex = 0;
        // 	CHAR outputString[100] = "";
        // 	sprintf_s(outputString, sizeof(outputString), "DumpByteArray SetRenderPacket rtPackets[%d]", rtPacketIndex);
        // 	DumpByteArray(outputString, (UCHAR*)m_outputRtPacketInfo[deviceIndex].rtPackets[rtPacketIndex], m_outputRtPacketInfo[deviceIndex].rtPacketSize);
        // }

        // {
        // 	ULONG rtPacketIndex = 1;
        // 	CHAR outputString[100] = "";
        // 	sprintf_s(outputString, sizeof(outputString), "DumpByteArray SetRenderPacket rtPackets[%d]", rtPacketIndex);
        // 	DumpByteArray(outputString, (UCHAR*)m_outputRtPacketInfo[deviceIndex].rtPackets[rtPacketIndex], m_outputRtPacketInfo[deviceIndex].rtPacketSize);
        // }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
RtPacketObject::ResetCurrentPacket(
    bool  isInput,
    ULONG deviceIndex
)
{

    NTSTATUS         status = STATUS_SUCCESS;
    RT_PACKET_INFO * rtPacketInfo = nullptr;
    ULONG            numOfDevices = 0;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    if (isInput)
    {
        rtPacketInfo = m_inputRtPacketInfo;
        numOfDevices = m_numOfInputDevices;
    }
    else
    {
        rtPacketInfo = m_outputRtPacketInfo;
        numOfDevices = m_numOfOutputDevices;
    }
    RETURN_NTSTATUS_IF_TRUE(deviceIndex >= numOfDevices, STATUS_INVALID_PARAMETER);

    InterlockedExchange((PLONG)&rtPacketInfo[deviceIndex].RtPacketCurrentPacket, 0);
    InterlockedExchange64((LONG64 *)&rtPacketInfo[deviceIndex].RtPacketPosition, 0);
    InterlockedExchange64((LONG64 *)&rtPacketInfo[deviceIndex].LastPacketStartQpcPosition, 0);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
RtPacketObject::GetCapturePacket(
    ULONG      deviceIndex,
    PULONG     lastCapturePacket,
    PULONGLONG qpcPacketStart
)
{

    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    RETURN_NTSTATUS_IF_TRUE(deviceIndex >= m_numOfInputDevices, STATUS_INVALID_PARAMETER);

    *lastCapturePacket = (ULONG)InterlockedCompareExchange((LONG *)&m_inputRtPacketInfo[deviceIndex].RtPacketCurrentPacket, -1, -1) - 1;
    *qpcPacketStart = InterlockedCompareExchange64((LONG64 *)&m_inputRtPacketInfo[deviceIndex].LastPacketStartQpcPosition, -1, -1);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
RtPacketObject::GetPresentationPosition(
    bool       isInput,
    ULONG      deviceIndex,
    PULONGLONG positionInBlocks,
    PULONGLONG qpcPosition
)
{
    NTSTATUS         status = STATUS_SUCCESS;
    ULONGLONG        qpcPositionNow = KeQueryPerformanceCounter(nullptr).QuadPart;
    RT_PACKET_INFO * rtPacketInfo = nullptr;
    ULONG            numOfDevices;
    ULONG            bytesPerSample;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    if (isInput)
    {
        rtPacketInfo = m_inputRtPacketInfo;
        numOfDevices = m_numOfInputDevices;
        bytesPerSample = m_inputBytesPerSample;
    }
    else
    {
        rtPacketInfo = m_outputRtPacketInfo;
        numOfDevices = m_numOfOutputDevices;
        bytesPerSample = m_outputBytesPerSample;
    }

    RETURN_NTSTATUS_IF_TRUE(deviceIndex >= numOfDevices, STATUS_INVALID_PARAMETER);

    ULONG     blockAlign = (bytesPerSample * rtPacketInfo[deviceIndex].channels);
    ULONGLONG rtPacketPosition = InterlockedCompareExchange64((LONG64 *)&rtPacketInfo[deviceIndex].RtPacketPosition, -1, -1);
    ULONGLONG lastPacketStartQpcPosition = InterlockedCompareExchange64((LONG64 *)&rtPacketInfo[deviceIndex].LastPacketStartQpcPosition, -1, -1);
    ULONG     bytesPerSecond = (isInput ? m_deviceContext->AudioProperty.InputMeasuredSampleRate : m_deviceContext->AudioProperty.OutputMeasuredSampleRate) * blockAlign;

    if (bytesPerSecond == 0)
    {
        if (isInput)
        {
            bytesPerSecond = m_inputWaveFormat->nAvgBytesPerSec;
        }
        else
        {
            bytesPerSecond = m_outputWaveFormat->nAvgBytesPerSec;
        }
    }

    RETURN_NTSTATUS_IF_TRUE(blockAlign == 0, STATUS_UNSUCCESSFUL);

    *positionInBlocks = (rtPacketPosition + ((qpcPositionNow - lastPacketStartQpcPosition) * bytesPerSecond / HNS_PER_SEC)) / blockAlign;
    *qpcPosition = qpcPositionNow;

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - *positionInBlocks, rtPacketPosition, bytesPerSecond, blockAlign = %llu, %llu, %u, %u", *positionInBlocks, rtPacketPosition, bytesPerSecond, blockAlign);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, " - *qpcPosition = %llu", *qpcPosition);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
RtPacketObject::AssignDevices(
    ULONG numOfInputDevices,
    ULONG numOfOutputDevices
)
{
    NTSTATUS              status = STATUS_SUCCESS;
    RT_PACKET_INFO *      inputRtPacketInfo = nullptr;
    RT_PACKET_INFO *      outputRtPacketInfo = nullptr;
    WDFMEMORY             inputRtPacketInfoMemory = nullptr;
    WDFMEMORY             outputRtPacketInfoMemory = nullptr;
    WDF_OBJECT_ATTRIBUTES attributes;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    auto assignDevicesScope = wil::scope_exit([&]() {
        if (inputRtPacketInfoMemory != nullptr)
        {
            WdfObjectDelete(inputRtPacketInfoMemory);
            inputRtPacketInfoMemory = nullptr;
            inputRtPacketInfo = nullptr;
        }

        if (outputRtPacketInfoMemory != nullptr)
        {
            WdfObjectDelete(outputRtPacketInfoMemory);
            outputRtPacketInfoMemory = nullptr;
            outputRtPacketInfo = nullptr;
        }
    });

    if (numOfInputDevices != 0)
    {
        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = m_deviceContext->Device;

        RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(RT_PACKET_INFO) * numOfInputDevices, &inputRtPacketInfoMemory, (PVOID *)&inputRtPacketInfo));
        RtlZeroMemory(inputRtPacketInfo, sizeof(RT_PACKET_INFO) * numOfInputDevices);
    }

    if (numOfOutputDevices != 0)
    {
        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = m_deviceContext->Device;

        RETURN_NTSTATUS_IF_FAILED(WdfMemoryCreate(&attributes, NonPagedPoolNx, DRIVER_TAG, sizeof(RT_PACKET_INFO) * numOfOutputDevices, &outputRtPacketInfoMemory, (PVOID *)&outputRtPacketInfo));
        RtlZeroMemory(outputRtPacketInfo, sizeof(RT_PACKET_INFO) * numOfOutputDevices);
    }

    m_inputRtPacketInfo = inputRtPacketInfo;
    m_outputRtPacketInfo = outputRtPacketInfo;

    m_inputRtPacketInfoMemory = inputRtPacketInfoMemory;
    m_outputRtPacketInfoMemory = outputRtPacketInfoMemory;

    inputRtPacketInfo = outputRtPacketInfo = nullptr;
    inputRtPacketInfoMemory = outputRtPacketInfoMemory = nullptr;

    if ((m_numOfInputDevices == 0) && (numOfInputDevices != 0))
    {
        m_numOfInputDevices = numOfInputDevices;

        for (ULONG deviceIndex = 0; deviceIndex < m_numOfInputDevices; deviceIndex++)
        {
            WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
            attributes.ParentObject = m_deviceContext->Device;
            RETURN_NTSTATUS_IF_FAILED(WdfSpinLockCreate(&attributes, &m_inputRtPacketInfo[deviceIndex].PositionSpinLock));
        }
    }

    if ((m_numOfOutputDevices == 0) && (numOfOutputDevices != 0))
    {
        m_numOfOutputDevices = numOfOutputDevices;

        for (ULONG deviceIndex = 0; deviceIndex < m_numOfOutputDevices; deviceIndex++)
        {
            WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
            attributes.ParentObject = m_deviceContext->Device;
            RETURN_NTSTATUS_IF_FAILED(WdfSpinLockCreate(&attributes, &m_outputRtPacketInfo[deviceIndex].PositionSpinLock));
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit %!STATUS!", status);

    return status;
}
