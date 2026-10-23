// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

#include "pch.h"
#include "AudioDeviceItem.h"

#include "AudioDeviceItem.g.cpp"

namespace native = ::driverselector;

namespace winrt::LowLatencyDriverSelector::implementation
{
    _Use_decl_annotations_
    void AudioDeviceItem::Initialize(native::AudioDevice const& device, Text const& text) noexcept
    {
        try
        {
            m_device = device;
            m_text = text;

            // A button stays enabled only when it would change something. The low-latency driver on
            // a whole device still moves to the audio function, where this tool puts it.
            m_canUseLowLatency = device.LowLatency.IsAvailable() &&
                (device.CurrentKind != native::DriverKind::LowLatency ||
                 device.LowLatencyUpdateAvailable ||
                 device.CurrentIsWholeDevice);

            m_canUseWindows = device.Windows.IsAvailable() &&
                device.CurrentKind != native::DriverKind::WindowsUsbAudio2;

            m_canUseManufacturer = device.Manufacturer.IsAvailable() &&
                device.CurrentKind != native::DriverKind::Manufacturer;
        }
        DRIVER_SELECTOR_CATCH_AND_LOG(L"Unable to fill in a device row.")
    }

    _Use_decl_annotations_
    void AudioDeviceItem::IsBusy(bool const value) noexcept
    {
        if (m_isBusy == value)
        {
            return;
        }

        m_isBusy = value;

        RaisePropertyChanged(L"IsBusy");
        RaisePropertyChanged(L"CanUseLowLatency");
        RaisePropertyChanged(L"CanUseWindows");
        RaisePropertyChanged(L"CanUseManufacturer");
    }

    _Use_decl_annotations_
    void AudioDeviceItem::RaisePropertyChanged(std::wstring_view const name) noexcept
    {
        try
        {
            m_propertyChanged(*this, xaml::Data::PropertyChangedEventArgs{ name });
        }
        catch (...)
        {
            // a failing notification must never take down a UI callback
        }
    }
}
