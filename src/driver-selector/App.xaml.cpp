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
    namespace
    {
        // --noelevate keeps the tool read-only, for testing without a prompt. --relaunched marks
        // the elevated copy, so a failed elevation can never loop.
        bool HasCommandLineSwitch(_In_ wchar_t const* const name) noexcept
        {
            try
            {
                int count{ 0 };
                wil::unique_hlocal_ptr<PWSTR[]> values{ ::CommandLineToArgvW(::GetCommandLineW(), &count) };

                if (!values)
                {
                    return false;
                }

                for (int i = 1; i < count; i++)
                {
                    if (::CompareStringOrdinal(values[i], -1, name, -1, TRUE) == CSTR_EQUAL)
                    {
                        return true;
                    }
                }
            }
            DRIVER_SELECTOR_CATCH_AND_LOG(L"Unable to read the command line.")

            return false;
        }
    }

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
            s_isElevated = ::driverselector::IsProcessElevated();

            // Everything that changes a driver needs administrator rights, so the tool asks once
            // here. A declined prompt still leaves a tool that shows what each device is using.
            if (!s_isElevated &&
                !HasCommandLineSwitch(L"--noelevate") &&
                !HasCommandLineSwitch(L"--relaunched"))
            {
                if (::driverselector::TryRelaunchElevated(L"--relaunched"))
                {
                    Exit();
                    return;
                }
            }

            auto window = winrt::make_self<MainWindow>();

            // sized and positioned before the first paint, so it does not visibly jump
            window->RestoreWindowPlacement();

            m_window = window.as<xaml::Window>();
            m_window.Activate();
        }
        DRIVER_SELECTOR_CATCH_AND_LOG(L"Unable to create the main window.")
    }
}
