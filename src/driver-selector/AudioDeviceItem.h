// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

#pragma once

#include "AudioDeviceItem.g.h"
#include "DriverTools.h"

namespace winrt::LowLatencyDriverSelector::implementation
{
    struct AudioDeviceItem : AudioDeviceItemT<AudioDeviceItem>
    {
        // the strings shown on the row, already formatted
        struct Text
        {
            winrt::hstring Name{};
            winrt::hstring Driver{};
            winrt::hstring Note{};
            winrt::hstring Detail{};
            winrt::hstring Problem{};
        };

        AudioDeviceItem() = default;

        winrt::hstring Name() const noexcept { return m_text.Name; }
        winrt::hstring DriverText() const noexcept { return m_text.Driver; }
        winrt::hstring NoteText() const noexcept { return m_text.Note; }
        winrt::hstring DetailText() const noexcept { return m_text.Detail; }
        winrt::hstring ProblemText() const noexcept { return m_text.Problem; }

        bool CanUseLowLatency() const noexcept { return m_canUseLowLatency && !m_isBusy; }
        bool CanUseWindows() const noexcept { return m_canUseWindows && !m_isBusy; }
        bool CanUseManufacturer() const noexcept { return m_canUseManufacturer && !m_isBusy; }

        bool IsBusy() const noexcept { return m_isBusy; }
        void IsBusy(_In_ bool value) noexcept;

        xaml::Visibility NoteVisibility() const noexcept
        {
            return m_text.Note.empty() ? xaml::Visibility::Collapsed : xaml::Visibility::Visible;
        }

        xaml::Visibility ProblemVisibility() const noexcept
        {
            return m_text.Problem.empty() ? xaml::Visibility::Collapsed : xaml::Visibility::Visible;
        }

        winrt::event_token PropertyChanged(_In_ xaml::Data::PropertyChangedEventHandler const& handler)
        {
            return m_propertyChanged.add(handler);
        }

        void PropertyChanged(_In_ winrt::event_token const& token) noexcept
        {
            m_propertyChanged.remove(token);
        }

        // not projected: what the buttons act on
        ::driverselector::AudioDevice const& Device() const noexcept { return m_device; }

        void Initialize(
            _In_ ::driverselector::AudioDevice const& device,
            _In_ Text const& text) noexcept;

    private:
        void RaisePropertyChanged(_In_ std::wstring_view const name) noexcept;

        ::driverselector::AudioDevice m_device{};
        Text m_text{};

        bool m_canUseLowLatency{ false };
        bool m_canUseWindows{ false };
        bool m_canUseManufacturer{ false };
        bool m_isBusy{ false };

        winrt::event<xaml::Data::PropertyChangedEventHandler> m_propertyChanged{};
    };
}

namespace winrt::LowLatencyDriverSelector::factory_implementation
{
    struct AudioDeviceItem : AudioDeviceItemT<AudioDeviceItem, implementation::AudioDeviceItem>
    {
    };
}
