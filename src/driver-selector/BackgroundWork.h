// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

#pragma once

namespace driverselector
{
    // Runs the callable on the thread pool and then puts the caller back on the thread that asked
    // for the work, so a continuation that touches XAML is always on the UI thread. Setup API and
    // the configuration manager block, sometimes for many seconds while a device restarts, and
    // none of it may run on the XAML thread.
    winrt::Windows::Foundation::IAsyncAction RunOnBackgroundAsync(_In_ std::function<void()> work) noexcept;
}
