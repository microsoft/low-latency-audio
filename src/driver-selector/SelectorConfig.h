// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

#pragma once

// ============================================================================
// Everything here describes the low-latency driver package or this build of the tool. If the
// driver's INF file name or its service name changes, change it here and nowhere else.
// ============================================================================

// 1 shows the PREVIEW badge in the title bar. Nothing else depends on it.
#define LOW_LATENCY_DRIVER_SELECTOR_PREVIEW_BUILD 1

namespace driverselector::config
{
    // The file name of the low-latency driver's INF, as it is built. The driver store keeps this
    // name, which is how the tool finds the package after Windows publishes it as oemNN.inf.
    inline constexpr wchar_t LowLatencyInfName[] = L"USBAudio2-ACX.inf";

    // The kernel service the INF installs. The ASIO driver opens the device by this name too.
    inline constexpr wchar_t LowLatencyServiceName[] = L"USBAudio2-ACX";

    // The in-box drivers the tool switches back to.
    inline constexpr wchar_t WindowsUsbAudio2InfName[] = L"usbaudio2.inf";
    inline constexpr wchar_t WindowsUsbCompositeInfName[] = L"usb.inf";
    inline constexpr wchar_t WindowsUsbCompositeServiceName[] = L"usbccgp";

    // Per user window placement.
    inline constexpr wchar_t SettingsKeyPath[] = LR"(Software\Microsoft\Low-Latency Audio\Driver Selector)";

    inline constexpr bool IsPreviewBuild() noexcept
    {
        return LOW_LATENCY_DRIVER_SELECTOR_PREVIEW_BUILD != 0;
    }
}
