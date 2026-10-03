// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

#pragma once

namespace driverselector
{
    // Changing a device's driver needs administrator rights, so the app asks once at startup
    // instead of failing one action at a time.
    bool IsProcessElevated() noexcept;

    // Starts another copy of this executable through the shell "runas" verb, with extraArgument
    // as its only argument. Returns false when the customer declines the prompt or the launch
    // fails, in which case the caller carries on without administrator rights.
    bool TryRelaunchElevated(_In_ std::wstring const& extraArgument) noexcept;

    // Restarts Windows. Only ever called after the customer has said yes.
    bool TryRestartComputer() noexcept;
}
