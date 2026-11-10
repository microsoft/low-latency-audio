// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

#include "pch.h"
#include "App.xaml.h"

// The XAML compiler emits its own wWinMain; this one replaces it so startup stays under our
// control. The apartment must stay STA: an MTA UI thread breaks UI Automation, which would leave
// the app unusable with a screen reader. The device work runs on the thread pool instead.
int __stdcall wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)
{
    // Ensure only a single instance of the application can run at a time. A named mutex is
    // shared across all sessions on the machine; if it already exists, another instance is
    // already running, so this process exits immediately without initializing the UI.
    HANDLE singleInstanceMutex = ::CreateMutexW(nullptr, TRUE, L"Local\\LowLatencyAudioDriverSelector-Mutex");
    if (singleInstanceMutex == nullptr || ::GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (singleInstanceMutex != nullptr)
        {
            ::CloseHandle(singleInstanceMutex);
        }
        return 0;
    }

    winrt::init_apartment(winrt::apartment_type::single_threaded);

    ::winrt::Microsoft::UI::Xaml::Application::Start([](auto&&)
        {
            ::winrt::make<::winrt::LowLatencyDriverSelector::implementation::App>();
        });

    ::CloseHandle(singleInstanceMutex);

    return 0;
}
