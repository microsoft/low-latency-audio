// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.g.cpp"

#include "App.xaml.h"
#include "BackgroundWork.h"
#include "Elevation.h"
#include "SelectorConfig.h"
#include "StringResources.h"
#include "resource.h"

namespace native = ::driverselector;
namespace res = ::driverselector::resources;

namespace winrt::LowLatencyDriverSelector::implementation
{
    namespace
    {
        // The data context of a control inside a DataTemplate is the row it was realized for.
        LowLatencyDriverSelector::AudioDeviceItem ItemFromSender(_In_ foundation::IInspectable const& sender) noexcept
        {
            try
            {
                if (auto const element = sender.try_as<xaml::FrameworkElement>())
                {
                    return element.DataContext().try_as<LowLatencyDriverSelector::AudioDeviceItem>();
                }
            }
            catch (...)
            {
            }

            return nullptr;
        }

        winrt::hstring OrUnknown(_In_ std::wstring const& value) noexcept
        {
            return value.empty() ? res::GetString(L"ValueUnknown") : winrt::hstring{ value };
        }

        winrt::hstring DescribeCurrentDriver(_In_ native::AudioDevice const& device) noexcept
        {
            switch (device.CurrentKind)
            {
            case native::DriverKind::LowLatency:
                return res::FormatString(L"DriverLowLatencyFormat", OrUnknown(device.CurrentVersion));

            case native::DriverKind::WindowsUsbAudio2:
                return res::FormatString(L"DriverWindowsFormat", OrUnknown(device.CurrentVersion));

            case native::DriverKind::Manufacturer:
                return res::FormatString(
                    device.CurrentIsWholeDevice ? L"DriverManufacturerWholeDeviceFormat" : L"DriverManufacturerFormat",
                    OrUnknown(device.CurrentProvider),
                    OrUnknown(device.CurrentVersion));

            case native::DriverKind::Other:
                return res::FormatString(L"DriverOtherFormat",
                    OrUnknown(device.CurrentInfName),
                    OrUnknown(device.CurrentVersion));

            default:
                return res::GetString(L"DriverNone");
            }
        }

        winrt::hstring DescribeNote(_In_ native::AudioDevice const& device) noexcept
        {
            if (device.CurrentIsWholeDevice)
            {
                return res::GetString(device.CurrentKind == native::DriverKind::LowLatency ?
                    L"NoteLowLatencyWholeDevice" : L"NoteWholeDevice");
            }

            if (device.LowLatencyUpdateAvailable)
            {
                return res::FormatString(L"NoteUpdateFormat", OrUnknown(device.LowLatency.Version));
            }

            return {};
        }

        winrt::hstring ConfirmationText(
            _In_ native::AudioDevice const& device,
            _In_ bool const lowLatency,
            _In_ bool const windows) noexcept
        {
            // what a manufacturer's driver provided may stop working, which the customer is told
            auto const manufacturerHasWholeDevice =
                device.CurrentIsWholeDevice && device.CurrentKind == native::DriverKind::Manufacturer;

            if (lowLatency)
            {
                return res::FormatString(
                    manufacturerHasWholeDevice ? L"ConfirmLowLatencyWholeDeviceFormat" : L"ConfirmLowLatencyFormat",
                    device.Name);
            }

            if (windows)
            {
                return res::FormatString(
                    manufacturerHasWholeDevice ? L"ConfirmWindowsWholeDeviceFormat" : L"ConfirmWindowsFormat",
                    device.Name);
            }

            return res::FormatString(
                device.Manufacturer.WholeDevice ? L"ConfirmManufacturerWholeDeviceFormat" : L"ConfirmManufacturerFormat",
                device.Name,
                OrUnknown(device.Manufacturer.Provider));
        }
    }

    MainWindow::MainWindow()
    {
        InitializeComponent();
    }

    void MainWindow::RestoreWindowPlacement() noexcept
    {
        native::WindowChrome::RestorePlacement(*this, 1100, 780);
    }

    _Use_decl_annotations_
    void MainWindow::OnRootSizeChanged(foundation::IInspectable const&, xaml::SizeChangedEventArgs const&)
    {
        m_chrome.UpdateTitleBarInsets();
    }

    _Use_decl_annotations_
    void MainWindow::OnRootLoaded(foundation::IInspectable const&, xaml::RoutedEventArgs const&)
    {
        try
        {
            Title(res::GetString(L"AppTitle"));
            AppTitleTextBlock().Text(res::GetString(L"AppTitle"));

            PreviewChiclet().Visibility(native::config::IsPreviewBuild() ?
                xaml::Visibility::Visible : xaml::Visibility::Collapsed);

            native::WindowChromeElements elements{};

            elements.Window = *this;
            elements.Root = RootGrid();
            elements.TitleBar = AppTitleBar();
            elements.LeftInset = TitleBarLeftInsetColumn();
            elements.RightInset = TitleBarRightInsetColumn();

            m_chrome.Initialize(elements);
            m_chrome.SetWindowIconFromResource(IDI_APPICON);

            // 32px source for a 16px slot, so it stays crisp on a high DPI display
            AppTitleBarIcon().Source(native::WindowChrome::LoadIconImageSource(IDI_APPICON, 32));

            RootGrid().ActualThemeChanged([weak = get_weak()](auto&&, auto&&)
                {
                    if (auto strong = weak.get())
                    {
                        strong->m_chrome.ApplyTitleBarColors();
                    }
                });

            Closed([weak = get_weak()](auto&&, auto&&)
                {
                    if (auto strong = weak.get())
                    {
                        strong->m_closing = true;
                        strong->m_chrome.SavePlacement();
                    }
                });

            DevicesListView().ItemsSource(m_devices);

            m_elevated = App::IsElevated();
            ElevationBar().IsOpen(!m_elevated);

            RefreshDevicesAsync();
        }
        DRIVER_SELECTOR_CATCH_AND_LOG(L"Unable to finish loading the window.")
    }

    _Use_decl_annotations_
    winrt::fire_and_forget MainWindow::OnRefreshClick(foundation::IInspectable, xaml::RoutedEventArgs)
    {
        auto lifetime = get_strong();

        try
        {
            co_await RefreshDevicesAsync();
        }
        DRIVER_SELECTOR_CATCH_AND_LOG(L"Unable to refresh the device list.")
    }

    foundation::IAsyncAction MainWindow::RefreshDevicesAsync()
    {
        auto lifetime = get_strong();

        // A refresh asked for while one is running is folded into it, so the list always ends
        // up showing the state after the last request.
        if (m_refreshing)
        {
            m_refreshAgain = true;
            co_return;
        }

        m_refreshing = true;

        try
        {
            // runs on the closing and exception paths too, so a failed scan cannot leave the
            // page spinning with its refresh button dead
            auto const clearBusy = wil::scope_exit([this]() noexcept
                {
                    try
                    {
                        m_refreshing = false;

                        if (!m_closing)
                        {
                            RefreshButton().IsEnabled(true);

                            if (!m_changing)
                            {
                                StatusProgressRing().IsActive(false);
                            }
                        }
                    }
                    catch (...)
                    {
                    }
                });

            RefreshButton().IsEnabled(false);
            StatusProgressRing().IsActive(true);
            StatusText().Text(res::GetString(L"StatusScanning"));

            do
            {
                m_refreshAgain = false;

                std::vector<native::AudioDevice> devices{};
                native::LowLatencyPackage package{};

                co_await native::RunOnBackgroundAsync([&devices, &package]()
                    {
                        package = native::FindLowLatencyPackage();
                        devices = native::EnumerateAudioDevices();
                    });

                if (m_closing)
                {
                    co_return;
                }

                ApplyDevices(devices, package);
            }
            while (m_refreshAgain);
        }
        DRIVER_SELECTOR_CATCH_AND_LOG(L"Unable to list the USB audio devices.")
    }

    _Use_decl_annotations_
    void MainWindow::ApplyDevices(
        std::vector<native::AudioDevice> const& devices,
        native::LowLatencyPackage const& package) noexcept
    {
        try
        {
            m_devices.Clear();

            for (auto const& device : devices)
            {
                AudioDeviceItem::Text text{};

                text.Name = winrt::hstring{ device.Name };
                text.Driver = DescribeCurrentDriver(device);
                text.Note = DescribeNote(device);

                auto const& instanceId = device.FunctionInstanceId.empty() ?
                    device.ParentInstanceId : device.FunctionInstanceId;

                // without a vendor and product ID, the hardware text already is the instance ID
                text.Detail = device.HardwareText == instanceId ?
                    winrt::hstring{ instanceId } :
                    res::FormatString(L"DetailFormat", device.HardwareText, instanceId);

                if (device.HasProblem)
                {
                    text.Problem = res::FormatString(L"ProblemFormat", device.ProblemCode);
                }

                auto item = winrt::make<AudioDeviceItem>();
                winrt::get_self<AudioDeviceItem>(item)->Initialize(device, text);

                m_devices.Append(item);
            }

            PackageMissingBar().IsOpen(!package.Installed);

            StatusText().Text(devices.empty() ?
                res::GetString(L"StatusNoDevices") :
                res::FormatString(L"StatusDeviceCountFormat", static_cast<uint32_t>(devices.size())));
        }
        DRIVER_SELECTOR_CATCH_AND_LOG(L"Unable to show the USB audio devices.")
    }

    _Use_decl_annotations_
    winrt::fire_and_forget MainWindow::OnUseLowLatencyClick(foundation::IInspectable sender, xaml::RoutedEventArgs)
    {
        auto lifetime = get_strong();

        try
        {
            co_await ChangeDriverAsync(ItemFromSender(sender), DriverAction::LowLatency);
        }
        DRIVER_SELECTOR_CATCH_AND_LOG(L"Unable to switch to the low-latency driver.")
    }

    _Use_decl_annotations_
    winrt::fire_and_forget MainWindow::OnUseWindowsClick(foundation::IInspectable sender, xaml::RoutedEventArgs)
    {
        auto lifetime = get_strong();

        try
        {
            co_await ChangeDriverAsync(ItemFromSender(sender), DriverAction::Windows);
        }
        DRIVER_SELECTOR_CATCH_AND_LOG(L"Unable to switch to the Windows driver.")
    }

    _Use_decl_annotations_
    winrt::fire_and_forget MainWindow::OnUseManufacturerClick(foundation::IInspectable sender, xaml::RoutedEventArgs)
    {
        auto lifetime = get_strong();

        try
        {
            co_await ChangeDriverAsync(ItemFromSender(sender), DriverAction::Manufacturer);
        }
        DRIVER_SELECTOR_CATCH_AND_LOG(L"Unable to switch to the manufacturer's driver.")
    }

    _Use_decl_annotations_
    foundation::IAsyncAction MainWindow::ChangeDriverAsync(
        LowLatencyDriverSelector::AudioDeviceItem item,
        DriverAction action)
    {
        auto lifetime = get_strong();

        if (item == nullptr || item.IsBusy() || m_changing)
        {
            co_return;
        }

        try
        {
            if (!RequireElevation())
            {
                co_return;
            }

            // a copy, because the list is rebuilt while the change runs
            auto const device = winrt::get_self<AudioDeviceItem>(item)->Device();

            auto const confirmed = co_await ConfirmAsync(
                res::GetString(L"ConfirmTitle"),
                ConfirmationText(device, action == DriverAction::LowLatency, action == DriverAction::Windows));

            if (!confirmed || m_closing || m_changing)
            {
                co_return;
            }

            m_changing = true;
            item.IsBusy(true);

            // fires on the closing and exception paths too, so neither the row nor the page can
            // be left marked busy
            auto const clearBusy = wil::scope_exit([this, item]() noexcept
                {
                    try
                    {
                        m_changing = false;
                        item.IsBusy(false);

                        if (!m_closing && !m_refreshing)
                        {
                            StatusProgressRing().IsActive(false);
                        }
                    }
                    catch (...)
                    {
                    }
                });

            FollowUpBar().IsOpen(false);
            StatusProgressRing().IsActive(true);
            StatusText().Text(res::GetString(L"StatusChanging"));

            native::DriverOperationResult result{};

            co_await native::RunOnBackgroundAsync([&result, &device, action]()
                {
                    switch (action)
                    {
                    case DriverAction::LowLatency:
                        result = native::UseLowLatencyDriver(device);
                        break;

                    case DriverAction::Windows:
                        result = native::UseWindowsDriver(device);
                        break;

                    default:
                        result = native::UseManufacturerDriver(device);
                        break;
                    }
                });

            if (m_closing)
            {
                co_return;
            }

            ShowOutcome(result);

            co_await RefreshDevicesAsync();

            if (result.Succeeded && !m_closing)
            {
                co_await OfferFollowUpAsync(result.FollowUp);
            }
        }
        DRIVER_SELECTOR_CATCH_AND_LOG(L"Unable to change a device's driver.")
    }

    _Use_decl_annotations_
    void MainWindow::ShowOutcome(native::DriverOperationResult const& result) noexcept
    {
        try
        {
            auto severity = controls::InfoBarSeverity::Success;
            std::wstring_view titleKey{ L"OutcomeReadyTitle" };

            if (!result.Succeeded)
            {
                severity = controls::InfoBarSeverity::Error;
                titleKey = L"OutcomeFailedTitle";
            }
            else if (result.FollowUp == native::DriverChangeFollowUp::RestartWindows)
            {
                severity = controls::InfoBarSeverity::Warning;
                titleKey = L"OutcomeRestartTitle";
            }
            else if (result.FollowUp == native::DriverChangeFollowUp::ReplugDevice)
            {
                severity = controls::InfoBarSeverity::Warning;
                titleKey = L"OutcomeReplugTitle";
            }

            FollowUpBar().Severity(severity);
            FollowUpBar().Title(res::GetString(titleKey));
            FollowUpBar().Message(winrt::hstring{ result.Message });
            FollowUpBar().IsOpen(true);
        }
        DRIVER_SELECTOR_CATCH_AND_LOG(L"Unable to show the outcome of a driver change.")
    }

    _Use_decl_annotations_
    foundation::IAsyncAction MainWindow::OfferFollowUpAsync(native::DriverChangeFollowUp const followUp)
    {
        auto lifetime = get_strong();

        if (m_openDialog != nullptr)
        {
            co_return;
        }

        try
        {
            if (followUp == native::DriverChangeFollowUp::RestartWindows)
            {
                RestartComputerDialogText().Text(FollowUpBar().Message());
                RestartComputerDialog().XamlRoot(Content().XamlRoot());

                m_openDialog = RestartComputerDialog();

                auto const answer = co_await RestartComputerDialog().ShowAsync();

                m_openDialog = nullptr;

                // Nothing restarts this PC without the customer saying so here.
                if (answer == controls::ContentDialogResult::Primary)
                {
                    native::TryRestartComputer();
                }
            }
            else if (followUp == native::DriverChangeFollowUp::ReplugDevice)
            {
                ReplugDeviceDialogText().Text(FollowUpBar().Message());
                ReplugDeviceDialog().XamlRoot(Content().XamlRoot());

                m_openDialog = ReplugDeviceDialog();

                co_await ReplugDeviceDialog().ShowAsync();

                m_openDialog = nullptr;
            }
        }
        catch (...)
        {
            m_openDialog = nullptr;

            DRIVER_SELECTOR_LOG_GENERAL_EXCEPTION(L"Unable to offer the follow-up for a driver change.");
        }
    }

    _Use_decl_annotations_
    foundation::IAsyncOperation<bool> MainWindow::ConfirmAsync(winrt::hstring title, winrt::hstring message)
    {
        auto lifetime = get_strong();

        if (m_openDialog != nullptr)
        {
            co_return false;
        }

        try
        {
            ConfirmDialog().Title(winrt::box_value(title));
            ConfirmDialogText().Text(message);
            ConfirmDialog().XamlRoot(Content().XamlRoot());

            m_openDialog = ConfirmDialog();

            auto const result = co_await ConfirmDialog().ShowAsync();

            m_openDialog = nullptr;

            co_return result == controls::ContentDialogResult::Primary;
        }
        catch (...)
        {
            m_openDialog = nullptr;

            DRIVER_SELECTOR_LOG_GENERAL_EXCEPTION(L"Unable to show the confirmation.");
        }

        co_return false;
    }

    _Use_decl_annotations_
    void MainWindow::OnRestartElevatedClick(foundation::IInspectable const&, xaml::RoutedEventArgs const&)
    {
        try
        {
            if (native::TryRelaunchElevated(L"--relaunched"))
            {
                Close();
            }
            else
            {
                ElevationBar().Message(res::GetString(L"ElevationDeclined"));
            }
        }
        DRIVER_SELECTOR_CATCH_AND_LOG(L"Unable to relaunch with administrator rights.")
    }

    bool MainWindow::RequireElevation() noexcept
    {
        if (m_elevated)
        {
            return true;
        }

        try
        {
            ElevationBar().IsOpen(true);
            ElevationBar().Message(res::GetString(L"ElevationRequiredForAction"));
        }
        DRIVER_SELECTOR_CATCH_AND_LOG(L"Unable to show the elevation requirement.")

        return false;
    }
}
