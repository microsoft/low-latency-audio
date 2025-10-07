// Copyright (c) Yamaha Corporation.
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

    Define a class for controlling ASIO objects.

Environment:

    Kernel-mode Driver Framework

--*/

#ifndef _ASIO_BUFFER_OBJECT_H_
#define _ASIO_BUFFER_OBJECT_H_

#include <acx.h>
#include "UAC_User.h"

class AsioBufferObject
{
  public:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    AsioBufferObject(
        _In_ PDEVICE_CONTEXT deviceContext
    );

    virtual __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ~AsioBufferObject();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS SetBuffer(
        _In_ ULONG    recBufferLength,
        _Inout_ PBYTE recBuffer,
        _In_ ULONG    recBufferOffset,
        _In_ ULONG    playBufferLength,
        _In_ PBYTE    playBuffer,
        _In_ ULONG    playBufferOffset
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS UnsetBuffer();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    bool IsRecBufferReady() const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    bool IsUserSpaceThreadOutputReady() const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ULONG UpdateReadyPosition();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ULONG GetBufferPeriod() const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    CopyFromAsioToOutputData(
        _Inout_updates_bytes_(length) PUCHAR outBuffer,
        _In_ ULONG                           length,
        _In_ ULONG                           bytesPerBlock,
        _In_ ULONG                           usbBytesPerSample
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    CopyToAsioFromInputData(
        _In_reads_bytes_(length) PUCHAR inBuffer,
        _In_ ULONG                      length,
        _In_ ULONG                      bytesPerBlock,
        _In_ ULONG                      usbBytesPerSample
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void
    SetRecDeviceStatus(
        _In_ DeviceStatuses DeviceStatuses
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    bool EvaluatePositionAndNotifyIfNeeded(
        _In_ ULONGLONG  currentTimePCUs,
        _In_ ULONGLONG  lastAsioNotifyPCUs,
        _In_ ULONGLONG  asioNotifyCount,
        _In_ LONG       prevAsioMeasuredPeriodUs,
        _In_ LONG       curClientProcessingTimeUs,
        _Out_ LONG &    curAsioMeasuredPeriodUs,
        _In_ const bool hasInputIsochronousInterface,
        _In_ const bool hasOutputIsochronousInterface
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    bool IsRecHeaderRegistered() const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void SetReady();

    static __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    AsioBufferObject * Create(_In_ PDEVICE_CONTEXT deviceContext);

  protected:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    LockAndGetSystemAddress(
        _In_ bool     isInput,
        _In_ PVOID    virtualAddress,
        _In_ ULONG    length,
        _Out_ PMDL &  mdl,
        _Out_ bool &  isLocked,
        _Out_ PVOID & systemAddress
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void
    UnlockAndFreeSystemAddress(
        _Inout_ PMDL &  mdl,
        _Inout_ bool &  isLocked,
        _Inout_ PVOID & systemAddress
    );

    const PDEVICE_CONTEXT                 m_deviceContext;
    bool                                  m_isReady{false};
    PMDL                                  m_recMdl{nullptr};
    bool                                  m_recMdlLocked{false};
    PBYTE                                 m_recBuffer{nullptr};
    ULONG                                 m_recBufferSize{0};
    volatile PUAC_ASIO_REC_BUFFER_HEADER  m_recHeader{nullptr};
    ULONG                                 m_recChannels{0};
    PMDL                                  m_playMdl{nullptr};
    bool                                  m_playMdlLocked{false};
    volatile PUAC_ASIO_PLAY_BUFFER_HEADER m_playHeader{nullptr};
    PBYTE                                 m_playBuffer{nullptr};
    ULONG                                 m_playBufferSize{0};
    ULONG                                 m_playChannels{0};
    ULONG                                 m_bufferLength{0};
    ULONG                                 m_bufferPeriod{0};
    LONGLONG                              m_position{0LL};
    LONGLONG                              m_notifyPosition{0LL};
    LONGLONG                              m_readPosition{0LL};
    LONGLONG                              m_writePosition{0LL};
    WDFSPINLOCK                           m_positionSpinLock{nullptr};
    PKEVENT                               m_userNotificationEvent{nullptr};
    PKEVENT                               m_outputReadyEvent{nullptr};
    ULONGLONG                             m_playChannelsMap{0ULL};
    ULONGLONG                             m_recChannelsMap{0ULL};
};

#endif
