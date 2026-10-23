// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

#pragma once

namespace driverselector
{
    enum class DriverKind
    {
        Unknown = 0,

        // the low-latency USB Audio 2.0 driver from this project
        LowLatency,

        // usbaudio2.inf, the in-box USB Audio 2.0 class driver
        WindowsUsbAudio2,

        // usb.inf, the in-box composite driver that gives each USB function its own device
        WindowsComposite,

        // a driver package the device's manufacturer supplied
        Manufacturer,

        // any other driver, including the in-box ones this tool does not offer
        Other
    };

    // A driver that one of the buttons can install. Kind is Unknown when there is no such driver.
    struct DriverChoice
    {
        DriverKind Kind{ DriverKind::Unknown };

        // published INF name, such as oem12.inf or usbaudio2.inf
        std::wstring InfName{};
        std::wstring Provider{};
        std::wstring Version{};

        // installs on the whole USB device rather than on its audio function
        bool WholeDevice{ false };

        bool IsAvailable() const noexcept { return Kind != DriverKind::Unknown; }
    };

    // One USB Audio 2.0 interface, or one whole device a manufacturer's driver has taken over.
    struct AudioDevice
    {
        // The USB Audio 2.0 function, which is what the low-latency driver runs on. Empty when a
        // manufacturer's driver controls the whole device, because Windows then never creates one.
        std::wstring FunctionInstanceId{};

        // the USB device the function belongs to
        std::wstring ParentInstanceId{};

        std::wstring Name{};
        std::wstring HardwareText{};

        DriverKind CurrentKind{ DriverKind::Unknown };
        bool CurrentIsWholeDevice{ false };
        std::wstring CurrentInfName{};
        std::wstring CurrentProvider{};
        std::wstring CurrentVersion{};

        bool HasProblem{ false };
        uint32_t ProblemCode{ 0 };

        // a newer low-latency driver is installed than the one this device is using
        bool LowLatencyUpdateAvailable{ false };

        DriverChoice LowLatency{};
        DriverChoice Windows{};
        DriverChoice Manufacturer{};
    };

    // What still has to happen before the new driver is really in use, cheapest first.
    enum class DriverChangeFollowUp
    {
        // the device restarted on the new driver and is running
        None = 0,

        // the device did not restart on the new driver, which reconnecting it fixes
        ReplugDevice,

        // Windows could not finish the change while it is running
        RestartWindows
    };

    struct DriverOperationResult
    {
        bool Succeeded{ false };
        DriverChangeFollowUp FollowUp{ DriverChangeFollowUp::None };

        // localized, ready to show
        std::wstring Message{};
    };

    // The newest copy of the low-latency driver in the driver store.
    struct LowLatencyPackage
    {
        bool Installed{ false };
        std::wstring PublishedName{};
        std::wstring Version{};
    };

    // Blocking. Everything below runs on a background thread.

    LowLatencyPackage FindLowLatencyPackage() noexcept;

    // USB Audio 2.0 functions, plus USB Audio 2.0 devices a driver package has taken over whole.
    // USB Audio 1.0 devices are left out: the low-latency driver doesn't support them. Reading
    // needs no administrator rights; changing a driver does.
    std::vector<AudioDevice> EnumerateAudioDevices() noexcept;

    DriverOperationResult UseLowLatencyDriver(_In_ AudioDevice const& device) noexcept;
    DriverOperationResult UseWindowsDriver(_In_ AudioDevice const& device) noexcept;
    DriverOperationResult UseManufacturerDriver(_In_ AudioDevice const& device) noexcept;
}
