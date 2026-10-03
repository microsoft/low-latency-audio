// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

#pragma once

// Plain ETW tracing for diagnosing a customer's PC with a trace. Nothing here is sent anywhere.
class DriverSelectorTraceProvider : public wil::TraceLoggingProvider
{
    IMPLEMENT_TRACELOGGING_CLASS(
        DriverSelectorTraceProvider,
        "Microsoft.LowLatencyAudio.DriverSelector",
        //  PS> [System.Diagnostics.Tracing.EventSource]::new("Microsoft.LowLatencyAudio.DriverSelector").Guid
        // {0b9e5cc5-9731-5b16-7820-790338edd8af}
        (0x0b9e5cc5, 0x9731, 0x5b16, 0x78, 0x20, 0x79, 0x03, 0x38, 0xed, 0xd8, 0xaf))
};

#define DRIVER_SELECTOR_TRACE_EVENT_ERROR       "DriverSelector.Error"
#define DRIVER_SELECTOR_TRACE_EVENT_INFO        "DriverSelector.Info"

#define DRIVER_SELECTOR_TRACE_LOCATION_FIELD    "location"
#define DRIVER_SELECTOR_TRACE_MESSAGE_FIELD     "message"
#define DRIVER_SELECTOR_TRACE_HRESULT_FIELD     "hresult"
#define DRIVER_SELECTOR_TRACE_ERROR_FIELD       "error"

#define DRIVER_SELECTOR_LOG_INFO(messageText)                                                    \
    TraceLoggingWrite(                                                                           \
        DriverSelectorTraceProvider::Provider(),                                                 \
        DRIVER_SELECTOR_TRACE_EVENT_INFO,                                                        \
        TraceLoggingString(__FUNCTION__, DRIVER_SELECTOR_TRACE_LOCATION_FIELD),                  \
        TraceLoggingLevel(WINEVENT_LEVEL_INFO),                                                  \
        TraceLoggingWideString((messageText), DRIVER_SELECTOR_TRACE_MESSAGE_FIELD)               \
    )

#define DRIVER_SELECTOR_LOG_HRESULT_EXCEPTION(ex, messageText)                                   \
    LOG_IF_FAILED(static_cast<HRESULT>((ex).code()));                                            \
    TraceLoggingWrite(                                                                           \
        DriverSelectorTraceProvider::Provider(),                                                 \
        DRIVER_SELECTOR_TRACE_EVENT_ERROR,                                                       \
        TraceLoggingString(__FUNCTION__, DRIVER_SELECTOR_TRACE_LOCATION_FIELD),                  \
        TraceLoggingLevel(WINEVENT_LEVEL_ERROR),                                                 \
        TraceLoggingWideString((messageText), DRIVER_SELECTOR_TRACE_MESSAGE_FIELD),              \
        TraceLoggingHResult(static_cast<HRESULT>((ex).code()), DRIVER_SELECTOR_TRACE_HRESULT_FIELD), \
        TraceLoggingWideString((ex).message().c_str(), DRIVER_SELECTOR_TRACE_ERROR_FIELD)        \
    )

#define DRIVER_SELECTOR_LOG_GENERAL_EXCEPTION(messageText)                                       \
    LOG_IF_FAILED(E_FAIL);                                                                       \
    TraceLoggingWrite(                                                                           \
        DriverSelectorTraceProvider::Provider(),                                                 \
        DRIVER_SELECTOR_TRACE_EVENT_ERROR,                                                       \
        TraceLoggingString(__FUNCTION__, DRIVER_SELECTOR_TRACE_LOCATION_FIELD),                  \
        TraceLoggingLevel(WINEVENT_LEVEL_ERROR),                                                 \
        TraceLoggingWideString((messageText), DRIVER_SELECTOR_TRACE_MESSAGE_FIELD)               \
    )

// Every entry point reached from XAML or from background work is wrapped in this, so that an
// escaping exception can never end the process.
#define DRIVER_SELECTOR_CATCH_AND_LOG(messageText)                                               \
    catch (winrt::hresult_error const& ex)                                                       \
    {                                                                                            \
        DRIVER_SELECTOR_LOG_HRESULT_EXCEPTION(ex, messageText);                                  \
    }                                                                                            \
    catch (...)                                                                                  \
    {                                                                                            \
        DRIVER_SELECTOR_LOG_GENERAL_EXCEPTION(messageText);                                      \
    }
