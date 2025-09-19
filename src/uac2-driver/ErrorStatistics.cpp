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

    Implement a class for error statistics.

Environment:

    Kernel-mode Driver Framework

--*/

#include "Driver.h"
#include "Device.h"
#include "Public.h"
#include "Common.h"
#include "ErrorStatistics.h"

#ifndef __INTELLISENSE__
#include "ErrorStatistics.tmh"
#endif

_Use_decl_annotations_
PAGED_CODE_SEG
ErrorStatistics *
ErrorStatistics::Create()
{
    PAGED_CODE();

    return new (POOL_FLAG_NON_PAGED, DRIVER_TAG) ErrorStatistics();
}

_Use_decl_annotations_
PAGED_CODE_SEG
ErrorStatistics::ErrorStatistics()
{
    PAGED_CODE();
}

_Use_decl_annotations_
PAGED_CODE_SEG
ErrorStatistics::~ErrorStatistics()
{
    PAGED_CODE();
}

_Use_decl_annotations_
NONPAGED_CODE_SEG
void ErrorStatistics::LogErrorOccurrence(ErrorStatus errorStatus, ULONG option)
{
    int errorStatusIndex = static_cast<int>(errorStatus);

    switch (errorStatus)
    {
    case ErrorStatus::IllegalBusTime:
        InterlockedIncrement((PLONG)&m_driverError[errorStatusIndex]);
        break;
    case ErrorStatus::VendorControlFailed:
    case ErrorStatus::DropoutDetectedInDPC:
    case ErrorStatus::DropoutDetectedLongClientProcessingTime:
    case ErrorStatus::DropoutDetectedElapsedTime:
    case ErrorStatus::UrbFailed:
        InterlockedIncrement((PLONG)&m_totalDriverError);
        InterlockedIncrement((PLONG)&m_driverError[0]);
        InterlockedIncrement((PLONG)&m_driverError[errorStatusIndex]);
        m_driverErrorOption[errorStatusIndex] = option;
        break;
    case ErrorStatus::DropoutDetectedSafetyOffset: // only m_totalDriverError, m_driverError
        InterlockedIncrement((PLONG)&m_totalDriverError);
        InterlockedIncrement((PLONG)&m_driverError[errorStatusIndex]);
        m_driverErrorOption[errorStatusIndex] = option;
        break;
    case ErrorStatus::DropoutDetectedCallbackPeriod: // only m_driverError
        InterlockedIncrement((PLONG)&m_driverError[errorStatusIndex]);
        m_driverErrorOption[errorStatusIndex] = option;
        break;
    default:
        break;
    }
}

_Use_decl_annotations_
PAGED_CODE_SEG
void ErrorStatistics::SetBandWidthError()
{
    PAGED_CODE();

    m_deviceStatus |= toInt(DeviceInternalStatuses::BandWidthError);
}

_Use_decl_annotations_
PAGED_CODE_SEG
void ErrorStatistics::ClearBandWidthError()
{
    PAGED_CODE();

    m_deviceStatus &= ~toInt(DeviceInternalStatuses::BandWidthError);
}

_Use_decl_annotations_
PAGED_CODE_SEG
void ErrorStatistics::Report()
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_WARNING, TRACE_DEVICE, " ErrorStatistics 0x%x, 0x%x, 0x%x", m_deviceStatus, m_totalDriverError, m_totalBusError);

    for (ULONG index = 0; index < UAC_MAX_DETECTED_ERROR; index++)
    {
        if ((m_driverError[index] != 0) ||
            (m_driverErrorOption[index] != 0) ||
            (m_busError[index] != 0))
        {
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_DEVICE, " - [%d], 0x%x, 0x%x, 0x%x, %s", index, m_driverError[index], m_driverErrorOption[index], m_busError[index], GetStatusString(index));
        }
    }
}

_Use_decl_annotations_
PAGED_CODE_SEG
const char * ErrorStatistics::GetStatusString(int index) const
{
    char * string = "";
    PAGED_CODE();

    switch ((ErrorStatus)index)
    {
    case ErrorStatus::IllegalBusTime:
        string = "illegal bus time";
        break;
    case ErrorStatus::VendorControlFailed:
        string = "vendor control failed";
        break;
    case ErrorStatus::DropoutDetectedInDPC:
        string = "dropout detected in DPC";
        break;
    case ErrorStatus::DropoutDetectedLongClientProcessingTime:
        string = "dropout detected long client processing time";
        break;
    case ErrorStatus::DropoutDetectedSafetyOffset:
        string = "dropout detected safety offset";
        break;
    case ErrorStatus::DropoutDetectedCallbackPeriod:
        string = "dropout detected callback period";
        break;
    case ErrorStatus::DropoutDetectedElapsedTime:
        string = "dropout detected elapsed time";
        break;
    case ErrorStatus::UrbFailed:
        string = "urb failed";
        break;
    default:
        break;
    }
    return string;
}
