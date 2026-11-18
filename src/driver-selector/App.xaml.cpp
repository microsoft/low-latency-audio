// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

#include "Elevation.h"

namespace winrt::LowLatencyDriverSelector::implementation
{
    bool App::s_isElevated{ false };

    App::App()
    {
        // XAML objects must not call InitializeComponent during construction; winrt::make does it
        UnhandledException({ this, &App::OnUnhandledException });
    }

    _Use_decl_annotations_
    void App::OnUnhandledException(
        foundation::IInspectable const& sender,
        xaml::UnhandledExceptionEventArgs const& args)
    {
        UNREFERENCED_PARAMETER(sender);

        try
        {
            TraceLoggingWrite(
                DriverSelectorTraceProvider::Provider(),
                DRIVER_SELECTOR_TRACE_EVENT_ERROR,
                TraceLoggingString(__FUNCTION__, DRIVER_SELECTOR_TRACE_LOCATION_FIELD),
                TraceLoggingLevel(WINEVENT_LEVEL_ERROR),
                TraceLoggingWideString(L"Unhandled XAML exception. Continuing.", DRIVER_SELECTOR_TRACE_MESSAGE_FIELD),
                TraceLoggingHResult(static_cast<HRESULT>(args.Exception()), DRIVER_SELECTOR_TRACE_HRESULT_FIELD),
                TraceLoggingWideString(args.Message().c_str(), DRIVER_SELECTOR_TRACE_ERROR_FIELD));

            // the app stays usable rather than closing in front of the customer
            args.Handled(true);
        }
        catch (...)
        {
        }
    }

    _Use_decl_annotations_
    void App::OnLaunched(xaml::LaunchActivatedEventArgs const& args)
    {
        UNREFERENCED_PARAMETER(args);

        try
        {
            // The elevation relaunch happens in wWinMain, before the single-instance mutex is
            // created; here the app only records the state the process ended up with.
            s_isElevated = ::driverselector::IsProcessElevated();

            auto window = winrt::make_self<MainWindow>();

            // sized and positioned before the first paint, so it does not visibly jump
            window->RestoreWindowPlacement();

            m_window = window.as<xaml::Window>();
            m_window.Activate();
        }
        DRIVER_SELECTOR_CATCH_AND_LOG(L"Unable to create the main window.")
    }
}
