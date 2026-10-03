// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

#pragma once

#include "App.xaml.g.h"

namespace winrt::LowLatencyDriverSelector::implementation
{
    struct App : AppT<App>
    {
        App();

        void OnLaunched(_In_ xaml::LaunchActivatedEventArgs const& args);

        // False when the customer declined the elevation prompt, which is what the read-only
        // banner on the main window keys off.
        static bool IsElevated() noexcept { return s_isElevated; }

    private:
        void OnUnhandledException(
            _In_ foundation::IInspectable const& sender,
            _In_ xaml::UnhandledExceptionEventArgs const& args);

        static bool s_isElevated;

        xaml::Window m_window{ nullptr };
    };
}
