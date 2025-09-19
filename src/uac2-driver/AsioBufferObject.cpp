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

    AsioBufferObject.h

Abstract:

    Implement a class for controlling ASIO objects.

Environment:

    Kernel-mode Driver Framework

--*/

#include "Driver.h"
#include "Device.h"
#include "Public.h"
#include "Common.h"
#include "ErrorStatistics.h"
#include "AsioBufferObject.h"
#include "USBAudioDataFormat.h"

#ifndef __INTELLISENSE__
#include "AsioBufferObject.tmh"
#endif

_Use_decl_annotations_
PAGED_CODE_SEG
AsioBufferObject * AsioBufferObject::Create(PDEVICE_CONTEXT DeviceContext)
{
    PAGED_CODE();

    return new (POOL_FLAG_NON_PAGED, DRIVER_TAG) AsioBufferObject(DeviceContext);
}

_Use_decl_annotations_
PAGED_CODE_SEG
AsioBufferObject::AsioBufferObject(
    PDEVICE_CONTEXT deviceContext
)
    : m_deviceContext(deviceContext)
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ASIO, "%!FUNC! Entry");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ASIO, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
AsioBufferObject::~AsioBufferObject()
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ASIO, "%!FUNC! Entry");

    ASSERT(m_playHeader == nullptr);
    ASSERT(m_recHeader == nullptr);

    UnsetBuffer();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ASIO, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
AsioBufferObject::LockAndGetSystemAddress(
    bool    isInput,
    PVOID   virtualAddress,
    ULONG   length,
    PMDL &  mdl,
    bool &  isLocked,
    PVOID & systemAddress
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ASIO, "%!FUNC! Entry, %!bool!", isInput);

    mdl = nullptr;
    isLocked = false;
    systemAddress = nullptr;

    auto setBufferScope = wil::scope_exit([&]() {
        if (!NT_SUCCESS(status) && (status != STATUS_DEVICE_BUSY))
        {
            if (isLocked)
            {
                MmUnlockPages(mdl);
                isLocked = false;
            }
            if (mdl != nullptr)
            {
                IoFreeMdl(mdl);
                mdl = nullptr;
            }
            systemAddress = nullptr;
        }
    });

    mdl = IoAllocateMdl(virtualAddress, length, FALSE, FALSE, nullptr);
    if (mdl == nullptr)
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_ASIO, "failed to allocate MDL for %s buffer", isInput ? "rec" : "play");
        status = STATUS_INSUFFICIENT_RESOURCES;
        return status;
    }

    __try
    {
        MmProbeAndLockPages(mdl, KernelMode, isInput ? IoModifyAccess /* IoWriteAccess */ : IoReadAccess);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        status = GetExceptionCode();
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_ASIO, "failed to lock MDL for play buffer");
    }
    RETURN_NTSTATUS_IF_FAILED(status);
    isLocked = true;

    systemAddress = MmGetSystemAddressForMdlSafe(mdl, LowPagePriority | MdlMappingNoExecute);
    if (systemAddress == nullptr)
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_ASIO, "failed to get system address for %s buffer", isInput ? "rec" : "play");
        status = STATUS_INSUFFICIENT_RESOURCES;
        return status;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ASIO, "%!FUNC! Exit, %!bool!, %p, %u, %p, %!bool!, %p", isInput, virtualAddress, length, mdl, isLocked, systemAddress);

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
void AsioBufferObject::UnlockAndFreeSystemAddress(
    PMDL &  mdl,
    bool &  isLocked,
    PVOID & systemAddress
)
{
    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ASIO, "%!FUNC! Entry");

    if (isLocked)
    {
        MmUnlockPages(mdl);
        isLocked = false;
    }
    if (mdl != nullptr)
    {
        IoFreeMdl(mdl);
        mdl = nullptr;
    }
    systemAddress = nullptr;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ASIO, "%!FUNC! Exit");
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
AsioBufferObject::SetBuffer(
    ULONG recBufferLength,
    PBYTE recBuffer,
    ULONG recBufferOffset,
    ULONG playBufferLength,
    PBYTE playBuffer,
    ULONG playBufferOffset
)
{
    PAGED_CODE();
    NTSTATUS status = STATUS_SUCCESS;
    PVOID    systemAddress = nullptr;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ASIO, "%!FUNC! Entry");
    //    WdfWaitLockAcquire(m_deviceContext->StreamWaitLock, nullptr);

    auto setBufferScope = wil::scope_exit([&]() {
        if (!NT_SUCCESS(status) && (status != STATUS_DEVICE_BUSY))
        {
            UnsetBuffer();
        }
    });

    RETURN_NTSTATUS_IF_TRUE_ACTION(recBufferLength == 0, status = STATUS_INVALID_PARAMETER, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION(playBufferLength == 0, status = STATUS_INVALID_PARAMETER, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION(recBuffer == nullptr, status = STATUS_INVALID_PARAMETER, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION(playBuffer == nullptr, status = STATUS_INVALID_PARAMETER, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION(m_recMdlLocked, status = STATUS_DEVICE_BUSY, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION(m_playMdlLocked, status = STATUS_DEVICE_BUSY, status);

    status = LockAndGetSystemAddress(false, playBuffer + playBufferOffset, playBufferLength - playBufferOffset, m_playMdl, m_playMdlLocked, systemAddress);
    RETURN_NTSTATUS_IF_FAILED(status);

    m_playHeader = static_cast<PUAC_ASIO_PLAY_BUFFER_HEADER>(systemAddress);
    m_playBuffer = static_cast<PBYTE>(systemAddress) + m_playHeader->HeaderLength;
    m_playBufferSize = playBufferLength - playBufferOffset - m_playHeader->HeaderLength;

    RETURN_NTSTATUS_IF_TRUE_ACTION(m_playHeader == nullptr, status = STATUS_INSUFFICIENT_RESOURCES, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION(m_playHeader->HeaderLength < (offsetof(UAC_ASIO_PLAY_BUFFER_HEADER, AsioDriverVersion) + sizeof(ULONG)), status = STATUS_INVALID_BUFFER_SIZE, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION(m_playHeader->AsioDriverVersion != UAC_ASIO_DRIVER_VERSION, status = STATUS_REVISION_MISMATCH, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION(m_playHeader->HeaderLength != sizeof(UAC_ASIO_PLAY_BUFFER_HEADER), status = STATUS_INVALID_BUFFER_SIZE, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION(m_playHeader->PlayChannels > UAC_MAX_ASIO_CHANNELS, status = STATUS_INVALID_PARAMETER, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION(m_playHeader->PlayChannels < UAC_MIN_ASIO_CHANNELS, status = STATUS_INVALID_PARAMETER, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION(m_playHeader->RecChannels > UAC_MAX_ASIO_CHANNELS, status = STATUS_INVALID_PARAMETER, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION(m_playHeader->RecChannels < UAC_MIN_ASIO_CHANNELS, status = STATUS_INVALID_PARAMETER, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION(m_playHeader->PeriodSamples > UAC_MAX_ASIO_PERIOD_SAMPLES, status = STATUS_INVALID_PARAMETER, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION(m_playHeader->PeriodSamples < UAC_MIN_ASIO_PERIOD_SAMPLES, status = STATUS_INVALID_PARAMETER, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION((m_playHeader->RecChannels > m_deviceContext->AudioProperty.InputAsioChannels) || (m_playHeader->PlayChannels > m_deviceContext->AudioProperty.OutputAsioChannels), status = STATUS_INVALID_PARAMETER, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION((m_deviceContext->AudioProperty.CurrentSampleFormat != UACSampleFormat::UAC_SAMPLE_FORMAT_PCM && m_deviceContext->AudioProperty.CurrentSampleFormat != UACSampleFormat::UAC_SAMPLE_FORMAT_IEEE_FLOAT), status = STATUS_NO_MATCH, status);

    systemAddress = nullptr;
    status = LockAndGetSystemAddress(false, recBuffer + recBufferOffset, recBufferLength - recBufferOffset, m_recMdl, m_recMdlLocked, systemAddress);
    RETURN_NTSTATUS_IF_FAILED(status);

    m_recHeader = static_cast<PUAC_ASIO_REC_BUFFER_HEADER>(systemAddress);
    m_recBuffer = static_cast<PBYTE>(systemAddress) + m_recHeader->HeaderLength;
    m_recBufferSize = recBufferLength - recBufferOffset - m_recHeader->HeaderLength;

    RETURN_NTSTATUS_IF_TRUE_ACTION(m_recHeader == nullptr, status = STATUS_INSUFFICIENT_RESOURCES, status);
    RETURN_NTSTATUS_IF_TRUE_ACTION(m_recHeader->HeaderLength != sizeof(UAC_ASIO_REC_BUFFER_HEADER), status = STATUS_INVALID_BUFFER_SIZE, status);

    ULONG bytesPerSample = USBAudioDataFormat::ConverSampleTypeToBytesPerSample(m_deviceContext->AudioProperty.SampleType);
    ULONG bufferSizeBytes = m_playHeader->PeriodSamples;

    bufferSizeBytes *= bytesPerSample;

    //
    // As per the ASIO specifications, double buffering is used alternately.
    // The number of samples on one side of the buffer is specified in
    // m_playHeader->PeriodSamples, so the calculated bufferSizeBytes is
    // multiplied by 2 to indicate double buffering to derive the total size.
    //
    ULONG requiredRecBufferLength = bufferSizeBytes * 2 * m_playHeader->RecChannels;
    ULONG requiredPlayBufferLength = bufferSizeBytes * 2 * m_playHeader->PlayChannels;

    //
    // PlayChannelsMap and RecChannelsMap do not perform range checking
    // because they accept all 64-bit ULONGLONG values.
    //
    m_bufferPeriod = m_playHeader->PeriodSamples;
    m_bufferLength = m_playHeader->PeriodSamples * 2;
    m_playChannels = m_playHeader->PlayChannels;
    m_recChannels = m_playHeader->RecChannels;
    m_playChannelsMap = m_playHeader->PlayChannelsMap;
    m_recChannelsMap = m_playHeader->RecChannelsMap;
    m_recHeader->CurrentSampleRate = m_deviceContext->AudioProperty.SampleRate;
    m_recHeader->CurrentClockSource = m_deviceContext->CurrentClockSource;

    if ((((playBufferLength - playBufferOffset) != (m_playHeader->HeaderLength + requiredPlayBufferLength)) || (recBufferLength - recBufferOffset) != (m_recHeader->HeaderLength + requiredRecBufferLength)))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_ASIO, "invalid buffer length, IN %u, req %u, OUT %u, req %u", m_recHeader->HeaderLength + requiredRecBufferLength, recBufferLength - recBufferOffset, m_playHeader->HeaderLength + requiredPlayBufferLength, playBufferLength - playBufferOffset);
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_ASIO, "playHdr PeriodSamples %u, RecChannels %u, PlayChannels %u, bytesPerSample %u", m_playHeader->PeriodSamples, m_playHeader->RecChannels, m_playHeader->PlayChannels, bytesPerSample);

        if ((playBufferLength < (m_playHeader->HeaderLength + requiredPlayBufferLength)) || (recBufferLength < (m_recHeader->HeaderLength + requiredRecBufferLength)))
        {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        else if ((playBufferLength > (m_playHeader->HeaderLength + requiredPlayBufferLength)) || (recBufferLength > (m_recHeader->HeaderLength + requiredRecBufferLength)))
        {
            status = STATUS_INVALID_BUFFER_SIZE;
        }
        return status;
    }

    //
    // Initialize the ASIO Buffer with zeros.
    //
    RtlZeroMemory(m_playBuffer, m_playBufferSize);
    RtlZeroMemory(m_recBuffer, m_recBufferSize);

    PKEVENT tempNotificationEvent = nullptr;
#ifdef _WIN64
    if (m_playHeader->Is32bitProcess)
    {
        status = ObReferenceObjectByHandle(
            m_playHeader->NotificationEvent.p32,
            EVENT_MODIFY_STATE,
            *ExEventObjectType,
            UserMode,
            (PVOID *)&tempNotificationEvent,
            nullptr
        );
    }
    else
    {
        status = ObReferenceObjectByHandle(
            m_playHeader->NotificationEvent.p64,
            EVENT_MODIFY_STATE,
            *ExEventObjectType,
            UserMode,
            (PVOID *)&tempNotificationEvent,
            nullptr
        );
    }
#else // _WIN64
    status = ObReferenceObjectByHandle(
        m_playHeader->NotificationEvent,
        EVENT_MODIFY_STATE,
        *ExEventObjectType,
        UserMode,
        (PVOID *)&tempNotificationEvent,
        nullptr
    );
#endif
    if (NT_SUCCESS(status))
    {
        m_userNotificationEvent = tempNotificationEvent;
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_ASIO, "failed to reference notification event handle");
        RETURN_NTSTATUS_IF_FAILED(status);
    }

    PKEVENT tempOutputReadyEvent = nullptr;
#ifdef _WIN64
    if (m_playHeader->Is32bitProcess)
    {
        status = ObReferenceObjectByHandle(
            m_playHeader->OutputReadyEvent.p32,
            EVENT_MODIFY_STATE,
            *ExEventObjectType,
            UserMode,
            (PVOID *)&tempOutputReadyEvent,
            nullptr
        );
    }
    else
    {
        status = ObReferenceObjectByHandle(
            m_playHeader->OutputReadyEvent.p64,
            EVENT_MODIFY_STATE,
            *ExEventObjectType,
            UserMode,
            (PVOID *)&tempOutputReadyEvent,
            nullptr
        );
    }
#else // _WIN64
    status = ObReferenceObjectByHandle(
        m_playHeader->OutputReadyEvent,
        EVENT_MODIFY_STATE,
        *ExEventObjectType,
        UserMode,
        (PVOID *)&tempOutputReadyEvent,
        nullptr
    );
#endif
    if (NT_SUCCESS(status))
    {
        m_outputReadyEvent = tempOutputReadyEvent;
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_ASIO, "failed to reference output ready event handle");
        RETURN_NTSTATUS_IF_FAILED(status);
    }

    status = STATUS_SUCCESS;

    m_deviceContext->AudioProperty.AsioBufferPeriod = m_bufferPeriod;
    m_deviceContext->AudioProperty.AsioDriverVersion = m_playHeader->AsioDriverVersion;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
AsioBufferObject::UnsetBuffer()
{
    NTSTATUS status = STATUS_SUCCESS;
    PVOID    systemAddress = nullptr;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ASIO, "%!FUNC! Entry");

    m_isReady = false;
    systemAddress = m_recHeader;
    UnlockAndFreeSystemAddress(m_recMdl, m_recMdlLocked, systemAddress);
    m_recHeader = nullptr;
    m_recBuffer = nullptr;
    m_recBufferSize = 0;

    systemAddress = m_playHeader;
    UnlockAndFreeSystemAddress(m_playMdl, m_playMdlLocked, systemAddress);
    m_playHeader = nullptr;
    m_playBuffer = nullptr;
    m_playBufferSize = 0;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ASIO, "%!FUNC! Exit");

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool AsioBufferObject::IsRecBufferReady() const
{
    PAGED_CODE();
    return m_isReady && (m_recHeader != nullptr) && (m_playHeader != nullptr);
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool AsioBufferObject::IsUserSpaceThreadOutputReady() const
{
    PAGED_CODE();
    ASSERT(m_recHeader != nullptr);

    // Determines when a state change occurs through the user-mode ASIO driver.
    ULONG outputReady = InterlockedCompareExchange(&(m_recHeader->OutputReady), 0, 0);

    return ((outputReady & toInt(UserThreadStatuses::OutputReady)) && (outputReady & toInt(UserThreadStatuses::BufferStart)));
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG AsioBufferObject::UpdateReadyPosition()
{
    PAGED_CODE();
    ASSERT(m_recHeader != nullptr);

    LONG readyBuffers = InterlockedExchange(&m_recHeader->ReadyBuffers, 0);
    return readyBuffers * m_bufferPeriod;
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG AsioBufferObject::GetBufferPeriod() const
{
    PAGED_CODE();
    return m_bufferPeriod;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
AsioBufferObject::CopyFromAsioToOutputData(
    PUCHAR outBuffer,
    ULONG  length,
    ULONG  bytesPerBlock,
    ULONG  usbBytesPerSample
)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG    samples = length / bytesPerBlock;

    PAGED_CODE();

    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ASIO, "%!FUNC! Entry, %u, %u, %u", length, bytesPerBlock, usbBytesPerSample);

    ASSERT(outBuffer != nullptr);
    ASSERT(length != 0);
    ASSERT(m_deviceContext != nullptr);

    IF_TRUE_ACTION_JUMP(outBuffer == nullptr, status = STATUS_INVALID_PARAMETER, CopyFromAsioToOutputData_Exit);
    IF_TRUE_ACTION_JUMP(length == 0, status = STATUS_INVALID_PARAMETER, CopyFromAsioToOutputData_Exit);
    IF_TRUE_ACTION_JUMP(m_deviceContext == nullptr, status = STATUS_UNSUCCESSFUL, CopyFromAsioToOutputData_Exit);

    LONGLONG asioPosition = m_readPosition;
    m_readPosition += samples;
    ULONG asioReadStartIndex = (ULONG)((asioPosition + m_deviceContext->Params.PreSendFrames) % (m_bufferLength));
    ULONG asioReadEndIndex = (ULONG)((asioPosition + samples + m_deviceContext->Params.PreSendFrames) % (m_bufferLength));

    ULONG asioSampleSize = USBAudioDataFormat::ConverSampleTypeToBytesPerSample(m_deviceContext->AudioProperty.SampleType);
    ULONG asioByteOffset = asioSampleSize - usbBytesPerSample;

    //
    // ASIO provides audio samples in a non-interleaved format. These samples
    // are converted and copied into an interleaved format suitable for USB
    // isochronous transfer.
    //
    switch (m_deviceContext->AudioProperty.CurrentSampleFormat)
    {
    case UACSampleFormat::UAC_SAMPLE_FORMAT_PCM: {
        for (ULONG asioCh = 0; asioCh < m_playChannels; ++asioCh)
        {
            ULONG usbCh = asioCh;
            if (usbCh >= m_deviceContext->OutputUsbChannels)
            {
                // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ASIO, "ASIO OUT channel %u is not mapped", asioCh);
                continue;
            }
            ULONG samplesFirst = samples;
            if (asioReadStartIndex > asioReadEndIndex)
            {
                samplesFirst = m_bufferLength - asioReadStartIndex;
            }

            if ((m_playChannelsMap & (1ULL << asioCh)) != 0)
            {
                volatile BYTE * asioBuffer = m_playBuffer + (m_bufferLength * asioSampleSize * asioCh);
                switch (usbBytesPerSample)
                {
                case 1:
                    for (ULONG index = 0; index < samplesFirst; ++index)
                    {
                        outBuffer[index * bytesPerBlock + usbCh * usbBytesPerSample] = asioBuffer[(asioReadStartIndex + index) * asioSampleSize + asioByteOffset];
                    }
                    for (ULONG index = samplesFirst; index < samples; ++index)
                    {
                        volatile BYTE * src = &(asioBuffer[(index - samplesFirst) * asioSampleSize + asioByteOffset]);
                        BYTE *          dst = &(outBuffer[index * bytesPerBlock + usbCh * usbBytesPerSample]);
                        *dst = *src;
                    }
                    break;
                case 2:
                    for (ULONG index = 0; index < samplesFirst; ++index)
                    {
                        *(USHORT *)&(outBuffer[index * bytesPerBlock + usbCh * usbBytesPerSample]) = *(USHORT *)&(asioBuffer[(asioReadStartIndex + index) * asioSampleSize + asioByteOffset]);
                    }
                    for (ULONG index = samplesFirst; index < samples; ++index)
                    {
                        *(USHORT *)&(outBuffer[index * bytesPerBlock + usbCh * usbBytesPerSample]) = *(USHORT *)&(asioBuffer[(index - samplesFirst) * asioSampleSize + asioByteOffset]);
                    }
                    break;
                case 3:
                    for (ULONG index = 0; index < samplesFirst; ++index)
                    {
                        volatile BYTE * src = &(asioBuffer[(asioReadStartIndex + index) * asioSampleSize + asioByteOffset]);
                        BYTE *          dst = &(outBuffer[index * bytesPerBlock + usbCh * usbBytesPerSample]);
                        *dst++ = *src++;
                        *dst++ = *src++;
                        *dst++ = *src++;
                    }
                    for (ULONG index = samplesFirst; index < samples; ++index)
                    {
                        volatile BYTE * src = &(asioBuffer[(index - samplesFirst) * asioSampleSize + asioByteOffset]);
                        BYTE *          dst = &(outBuffer[index * bytesPerBlock + usbCh * usbBytesPerSample]);
                        *dst++ = *src++;
                        *dst++ = *src++;
                        *dst++ = *src++;
                    }
                    break;
                case 4:
                    for (ULONG index = 0; index < samplesFirst; ++index)
                    {
                        *(ULONG *)&(outBuffer[index * bytesPerBlock + usbCh * usbBytesPerSample]) = *(ULONG *)&(asioBuffer[(asioReadStartIndex + index) * asioSampleSize + asioByteOffset]);
                    }
                    for (ULONG index = samplesFirst; index < samples; ++index)
                    {
                        *(ULONG *)&(outBuffer[index * bytesPerBlock + usbCh * usbBytesPerSample]) = *(ULONG *)&(asioBuffer[(index - samplesFirst) * asioSampleSize + asioByteOffset]);
                    }
                    break;
                default:
                    break; // max 32bit
                }
            }
        }
    }
    break;
    case UACSampleFormat::UAC_SAMPLE_FORMAT_IEEE_FLOAT: {
        ASSERT(usbBytesPerSample == 4);
        ASSERT(asioSampleSize == 4);
        for (ULONG asioCh = 0; asioCh < m_playChannels; ++asioCh)
        {
            ULONG usbCh = asioCh;
            if (usbCh >= m_deviceContext->OutputUsbChannels)
            {
                // TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ASIO, "ASIO OUT channel %u is not mapped", asioCh);
                continue;
            }
            ULONG samplesFirst = samples;
            if (asioReadStartIndex > asioReadEndIndex)
            {
                samplesFirst = m_bufferLength - asioReadStartIndex;
            }

            if ((m_playChannelsMap & (1ULL << asioCh)) != 0)
            {
                volatile BYTE * asioBuffer = m_playBuffer + (m_bufferLength * asioSampleSize * asioCh);
                for (ULONG index = 0; index < samplesFirst; ++index)
                {
                    *(float *)&(outBuffer[index * bytesPerBlock + usbCh * usbBytesPerSample]) = *(float *)&(asioBuffer[(asioReadStartIndex + index) * asioSampleSize + asioByteOffset]);
                }
                for (ULONG index = samplesFirst; index < samples; ++index)
                {
                    *(float *)&(outBuffer[index * bytesPerBlock + usbCh * usbBytesPerSample]) = *(float *)&(asioBuffer[(index - samplesFirst) * asioSampleSize + asioByteOffset]);
                }
            }
        }
    }
    break;
    default:
        break;
    }

    _InterlockedExchange64((volatile LONG64 *)&m_recHeader->PlayBufferPosition, asioPosition + samples);

CopyFromAsioToOutputData_Exit:
    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ASIO, "%!FUNC! Exit");

    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
AsioBufferObject::CopyToAsioFromInputData(
    PUCHAR inBuffer,
    ULONG  length,
    ULONG  bytesPerBlock,
    ULONG  usbBytesPerSample
)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG    samples = length / bytesPerBlock;

    PAGED_CODE();

    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ASIO, "%!FUNC! Entry, %u, %u, %u", length, bytesPerBlock, usbBytesPerSample);

    ASSERT(inBuffer != nullptr);
    ASSERT(length != 0);
    ASSERT(m_deviceContext != nullptr);

    IF_TRUE_ACTION_JUMP(inBuffer == nullptr, status = STATUS_INVALID_PARAMETER, CopyToAsioFromInputData_Exit);
    IF_TRUE_ACTION_JUMP(length == 0, status = STATUS_INVALID_PARAMETER, CopyToAsioFromInputData_Exit);
    IF_TRUE_ACTION_JUMP(m_deviceContext == nullptr, status = STATUS_UNSUCCESSFUL, CopyToAsioFromInputData_Exit);

    LONGLONG asioPosition = m_writePosition;
    m_writePosition += samples;

    const ULONG asioWriteStartIndex = (ULONG)((asioPosition) % (m_bufferLength));
    const ULONG asioWriteEndIndex = (ULONG)((asioPosition + samples) % (m_bufferLength));

    ULONG asioSampleSize = USBAudioDataFormat::ConverSampleTypeToBytesPerSample(m_deviceContext->AudioProperty.SampleType);
    ULONG asioByteOffset = asioSampleSize - usbBytesPerSample;

    switch (m_deviceContext->AudioProperty.CurrentSampleFormat)
    {
    case UACSampleFormat::UAC_SAMPLE_FORMAT_PCM: {
        for (ULONG asioCh = 0; asioCh < m_recChannels; ++asioCh)
        {
            ULONG usbCh = asioCh;
            if (usbCh >= m_deviceContext->InputUsbChannels)
            {
                // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ASIO, "ASIO IN channel %u is not mapped", asioCh);
                continue;
            }
            if ((m_recChannelsMap & (1ULL << asioCh)) != 0)
            {
                ULONG samplesFirst = samples;
                PBYTE asioBuffer = (PBYTE)m_recBuffer + (m_bufferLength * asioSampleSize * asioCh);

                // Since asioSampleSize and usbBytesPerSample are usually the same,
                // zero-clearing is not necessary. However, if asioSampleSize is larger,
                // we clear the entire buffer once.
                //
                // To improve efficiency, instead of writing zeros sparsely,
                // we use RtlZeroMemory to clear the entire buffer at once.
                if (asioSampleSize > usbBytesPerSample)
                {
                    if (asioWriteStartIndex > asioWriteEndIndex)
                    {
                        samplesFirst = m_bufferLength - asioWriteStartIndex;
                        RtlZeroMemory(&(asioBuffer[asioWriteStartIndex * asioSampleSize + asioByteOffset]), samplesFirst * usbBytesPerSample);
                        RtlZeroMemory(&(asioBuffer[asioByteOffset]), (samples - samplesFirst) * usbBytesPerSample);
                    }
                    else
                    {
                        RtlZeroMemory(&(asioBuffer[asioWriteStartIndex * asioSampleSize + asioByteOffset]), samplesFirst * usbBytesPerSample);
                    }
                }
                switch (usbBytesPerSample)
                {
                case 1:
                    for (ULONG index = 0; index < samplesFirst; ++index)
                    {
                        BYTE * src = &(inBuffer[index * bytesPerBlock + usbCh * usbBytesPerSample]);
                        BYTE * dst = &(asioBuffer[(asioWriteStartIndex + index) * asioSampleSize + asioByteOffset]);
                        *dst = *src;
                    }
                    for (ULONG index = samplesFirst; index < samples; ++index)
                    {
                        BYTE * src = &(inBuffer[index * bytesPerBlock + usbCh * usbBytesPerSample]);
                        BYTE * dst = &(asioBuffer[(index - samplesFirst) * asioSampleSize + asioByteOffset]);
                        *dst = *src;
                    }
                    break;
                case 2:
                    for (ULONG index = 0; index < samplesFirst; ++index)
                    {
                        *(USHORT *)&(asioBuffer[(asioWriteStartIndex + index) * asioSampleSize + asioByteOffset]) = *(USHORT *)&(inBuffer[index * bytesPerBlock + usbCh * usbBytesPerSample]);
                    }
                    for (ULONG index = samplesFirst; index < samples; ++index)
                    {
                        *(USHORT *)&(asioBuffer[(index - samplesFirst) * asioSampleSize + asioByteOffset]) = *(USHORT *)&(inBuffer[index * bytesPerBlock + usbCh * usbBytesPerSample]);
                    }
                    break;
                case 3:
                    for (ULONG index = 0; index < samplesFirst; ++index)
                    {
                        BYTE * src = &(inBuffer[index * bytesPerBlock + usbCh * usbBytesPerSample]);
                        BYTE * dst = &(asioBuffer[(asioWriteStartIndex + index) * asioSampleSize + asioByteOffset]);
                        *dst++ = *src++;
                        *dst++ = *src++;
                        *dst++ = *src++;
                    }
                    for (ULONG index = samplesFirst; index < samples; ++index)
                    {
                        BYTE * src = &(inBuffer[index * bytesPerBlock + usbCh * usbBytesPerSample]);
                        BYTE * dst = &(asioBuffer[(index - samplesFirst) * asioSampleSize + asioByteOffset]);
                        *dst++ = *src++;
                        *dst++ = *src++;
                        *dst++ = *src++;
                    }
                    break;
                case 4:
                    for (ULONG index = 0; index < samplesFirst; ++index)
                    {
                        *(ULONG *)&(asioBuffer[(asioWriteStartIndex + index) * asioSampleSize + asioByteOffset]) = *(ULONG *)&(inBuffer[index * bytesPerBlock + usbCh * usbBytesPerSample]);
                    }
                    for (ULONG index = samplesFirst; index < samples; ++index)
                    {
                        *(ULONG *)&(asioBuffer[(index - samplesFirst) * asioSampleSize + asioByteOffset]) = *(ULONG *)&(inBuffer[index * bytesPerBlock + usbCh * usbBytesPerSample]);
                    }
                    break;
                default:
                    break; // max 32bit
                }
            }
        }
    }
    break;
    case UACSampleFormat::UAC_SAMPLE_FORMAT_IEEE_FLOAT: {
        ASSERT(usbBytesPerSample == 4);
        ASSERT(asioSampleSize == 4);
        for (ULONG asioCh = 0; asioCh < m_recChannels; ++asioCh)
        {
            ULONG usbCh = asioCh;
            if (usbCh >= m_deviceContext->InputUsbChannels)
            {
                // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ASIO, "ASIO IN channel %u is not mapped", asioCh);
                continue;
            }
            if ((m_recChannelsMap & (1ULL << asioCh)) != 0)
            {
                ULONG samplesFirst = samples;
                PBYTE asioBuffer = (PBYTE)m_recBuffer + (m_bufferLength * asioSampleSize * asioCh);
                if (usbBytesPerSample == 4)
                {
                    for (ULONG index = 0; index < samplesFirst; ++index)
                    {
                        *(float *)&(asioBuffer[(asioWriteStartIndex + index) * asioSampleSize + asioByteOffset]) = *(float *)&(inBuffer[index * bytesPerBlock + usbCh * usbBytesPerSample]);
                    }
                    for (ULONG index = samplesFirst; index < samples; ++index)
                    {
                        *(float *)&(asioBuffer[(index - samplesFirst) * asioSampleSize + asioByteOffset]) = *(float *)&(inBuffer[index * bytesPerBlock + usbCh * usbBytesPerSample]);
                    }
                }
            }
        }
    }
    break;
    default:
        break;
    }

    _InterlockedExchange64((volatile LONG64 *)&m_recHeader->RecCurrentPosition, asioPosition + samples);

CopyToAsioFromInputData_Exit:
    // TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ASIO, "%!FUNC! Exit");

    return status;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void AsioBufferObject::SetRecDeviceStatus(
    DeviceStatuses statuses
)
{
    InterlockedOr((PLONG)&m_recHeader->DeviceStatus, toInt(statuses));
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool AsioBufferObject::EvaluatePositionAndNotifyIfNeeded(
    ULONGLONG currentTimePCUs,
    ULONGLONG lastAsioNotifyPCUs,
    ULONGLONG asioNotifyCount,
    LONG      prevAsioMeasuredPeriodUs,
    LONG      curClientProcessingTimeUs,
    LONG &    curAsioMeasuredPeriodUs
)
{
    bool     asioNotify = false;
    LONGLONG asioNotifyPosition = m_notifyPosition;

    PAGED_CODE();

    curAsioMeasuredPeriodUs = 0;

    if (((m_writePosition - asioNotifyPosition) >= m_bufferPeriod) &&
        ((m_readPosition - asioNotifyPosition) >= m_bufferPeriod))
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ASIO, " - asio notify: write position %llu, read position %llu, notify position %llu, buffer period %u, current time %llu us, last asio notify %llu us, notify count %llu", m_writePosition, m_readPosition, m_notifyPosition, m_bufferPeriod, currentTimePCUs, lastAsioNotifyPCUs, asioNotifyCount);
        asioNotify = true;
        m_notifyPosition += m_bufferPeriod;
        // Notify the position before counting up.
        _InterlockedExchange64((volatile LONG64 *)&m_recHeader->RecBufferPosition, asioNotifyPosition);
        _InterlockedExchange64((volatile LONG64 *)&m_recHeader->NotifySystemTime, currentTimePCUs);
        KeSetEvent(m_userNotificationEvent, IO_SOUND_INCREMENT, FALSE);
        curAsioMeasuredPeriodUs = (LONG)(currentTimePCUs - lastAsioNotifyPCUs);
        ULONG minimumPeriod = m_deviceContext->AudioProperty.SampleRate / 1000;
        if (minimumPeriod < m_bufferPeriod)
        {
            minimumPeriod = m_bufferPeriod;
        }
        LONG thresholdUs = (LONG)((LONGLONG)(minimumPeriod + (m_deviceContext->UsbLatency.OutputDriverBuffer)) * 1000000LL / m_deviceContext->AudioProperty.SampleRate);
        if ((m_bufferLength * 1000 >= m_bufferPeriod) && (asioNotifyCount >= 2) && (curAsioMeasuredPeriodUs > thresholdUs))
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ASIO, "dropout detected. Callback period now %dus, last %dus, threshold %dus, processing %dus.", curAsioMeasuredPeriodUs, prevAsioMeasuredPeriodUs, thresholdUs, curClientProcessingTimeUs);

            m_deviceContext->ErrorStatistics->LogErrorOccurrence(ErrorStatus::DropoutDetectedCallbackPeriod, curAsioMeasuredPeriodUs);
        }
    }

    return asioNotify;
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
bool AsioBufferObject::IsRecHeaderRegistered() const
{
    return (m_recHeader != nullptr);
}

_Use_decl_annotations_
PAGED_CODE_SEG
void AsioBufferObject::SetReady()
{
    PAGED_CODE();

    m_isReady = true;
}
