// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================
// ASIO is a trademark and software of Steinberg Media Technologies GmbH

/*++

Module Name:

    UsbDevice.h

Abstract:

    This file defines functions that communicate with device drivers supporting USB devices.

Environment:

    ASIO Driver

--*/

#pragma once

#include <windows.h>
#include "UAC_User.h"

HANDLE OpenUsbDevice(
    _In_ const LPGUID      classGuid,
    _In_ const TCHAR *     serviceName,
    _In_ const TCHAR *     referenceString,
    _In_opt_ const TCHAR * desiredPath
);

BOOL GetAudioProperty(
    _In_ HANDLE                deviceHandle,
    _Out_ UAC_AUDIO_PROPERTY * audioProperty
);

BOOL GetChannelInfo(
    _In_ HANDLE                           deviceHandle,
    _Out_ PUAC_GET_CHANNEL_INFO_CONTEXT * channelInfo
);

BOOL GetClockInfo(
    _In_ HANDLE                         deviceHandle,
    _Out_ PUAC_GET_CLOCK_INFO_CONTEXT * clockInfo
);

BOOL SetClockSource(
    _In_ HANDLE deviceHandle,
    _In_ ULONG  index
);

BOOL SetFlags(
    _In_ HANDLE                        deviceHandle,
    _In_ const UAC_SET_FLAGS_CONTEXT & flags
);

BOOL SetSampleFormat(
    _In_ HANDLE deviceHandle,
    _In_ ULONG  sampleFormat
);

BOOL ChangeSampleRate(
    _In_ HANDLE deviceHandle,
    _In_ ULONG  sampleRate
);

BOOL GetAsioOwnership(
    _In_ HANDLE deviceHandle
);

BOOL StartAsioStream(
    _In_ HANDLE deviceHandle
);

BOOL StopAsioStream(
    _In_ HANDLE deviceHandle
);

BOOL SetAsioBuffer(
    _In_ HANDLE                                                  deviceHandle,
    _In_reads_bytes_(driverPlayBufferWithKsPropertySize) UCHAR * driverPlayBufferWithKsProperty,
    _In_ ULONG                                                   driverPlayBufferWithKsPropertySize,
    _In_reads_bytes_(driverRecBufferSize) UCHAR *                driverRecBuffer,
    _In_ ULONG                                                   driverRecBufferSize
);

BOOL UnsetAsioBuffer(
    _In_ HANDLE deviceHandle
);

BOOL ReleaseAsioOwnership(
    _In_ HANDLE deviceHandle
);
