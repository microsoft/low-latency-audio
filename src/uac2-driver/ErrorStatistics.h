// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

/*++

Module Name:

    ErrorStatistics.h

Abstract:

    Define a class for error statistics.

Environment:

    Kernel-mode Driver Framework

--*/

#ifndef _ERROR_STATISTICS_H_
#define _ERROR_STATISTICS_H_

#include <acx.h>

enum class ErrorStatus
{
    IllegalBusTime = 1,
    VendorControlFailed,
    DropoutDetectedInDPC,
    DropoutDetectedLongClientProcessingTime,
    DropoutDetectedSafetyOffset,
    DropoutDetectedCallbackPeriod,
    DropoutDetectedElapsedTime,
    UrbFailed,
};

enum class DeviceInternalStatuses
{
    BandWidthError = 1 << 0, // #define UAC_DEVICE_STATUS_BANDWIDTH            0x00000001
    BusError = 1 << 2,       // #define UAC_DEVICE_STATUS_BUS_ERROR            0x00000002
};

constexpr int toInt(DeviceInternalStatuses status)
{
    return static_cast<int>(status);
}

class ErrorStatistics
{
  public:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ErrorStatistics();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual ~ErrorStatistics();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void LogErrorOccurrence(
        _In_ ErrorStatus errorStatus,
        _In_ ULONG       option
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void SetBandWidthError();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void ClearBandWidthError();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void Report();

    static __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ErrorStatistics * Create();

  private:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    const char * ErrorStatistics::GetStatusString(int index) const;

    ULONG m_deviceStatus{0};
    ULONG m_totalDriverError{0};
    ULONG m_totalBusError{0};
    ULONG m_driverError[UAC_MAX_DETECTED_ERROR]{};
    ULONG m_driverErrorOption[UAC_MAX_DETECTED_ERROR]{};
    ULONG m_busError[UAC_MAX_DETECTED_ERROR]{};
};

#endif
