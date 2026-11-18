// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

#pragma once

#include "MainWindow.g.h"

#include "DriverTools.h"
#include "WindowChrome.h"

namespace winrt::LowLatencyDriverSelector::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        // called by App before Activate, so the window opens where it was last closed
        void RestoreWindowPlacement() noexcept;

        void OnRootLoaded(
            _In_ foundation::IInspectable const& sender,
            _In_ xaml::RoutedEventArgs const& args);

        void OnRootSizeChanged(
            _In_ foundation::IInspectable const& sender,
            _In_ xaml::SizeChangedEventArgs const& args);

        winrt::fire_and_forget OnRefreshClick(
            _In_ foundation::IInspectable sender,
            _In_ xaml::RoutedEventArgs args);

        void OnRestartElevatedClick(
            _In_ foundation::IInspectable const& sender,
            _In_ xaml::RoutedEventArgs const& args);

        winrt::fire_and_forget OnUseLowLatencyClick(
            _In_ foundation::IInspectable sender,
            _In_ xaml::RoutedEventArgs args);

        winrt::fire_and_forget OnUseWindowsClick(
            _In_ foundation::IInspectable sender,
            _In_ xaml::RoutedEventArgs args);

        winrt::fire_and_forget OnUseManufacturerClick(
            _In_ foundation::IInspectable sender,
            _In_ xaml::RoutedEventArgs args);

    private:
        enum class DriverAction
        {
            LowLatency = 0,
            Windows,
            Manufacturer
        };

        foundation::IAsyncAction RefreshDevicesAsync();

        foundation::IAsyncAction ChangeDriverAsync(
            _In_ LowLatencyDriverSelector::AudioDeviceItem item,
            _In_ DriverAction action);

        foundation::IAsyncOperation<bool> ConfirmAsync(
            _In_ winrt::hstring title,
            _In_ winrt::hstring message);

        foundation::IAsyncAction OfferFollowUpAsync(_In_ ::driverselector::DriverChangeFollowUp followUp);

        void ApplyDevices(
            _In_ std::vector<::driverselector::AudioDevice> const& devices,
            _In_ ::driverselector::LowLatencyPackage const& package) noexcept;

        void ShowOutcome(_In_ ::driverselector::DriverOperationResult const& result) noexcept;

        bool RequireElevation() noexcept;

        ::driverselector::WindowChrome m_chrome{};

        collections::IObservableVector<LowLatencyDriverSelector::AudioDeviceItem> m_devices{
            winrt::single_threaded_observable_vector<LowLatencyDriverSelector::AudioDeviceItem>() };

        // only one dialog can be open at a time
        controls::ContentDialog m_openDialog{ nullptr };

        bool m_elevated{ false };
        bool m_closing{ false };

        // one driver change at a time across the whole window; Windows serializes them anyway
        bool m_changing{ false };

        bool m_refreshing{ false };
        bool m_refreshAgain{ false };
    };
}

namespace winrt::LowLatencyDriverSelector::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
