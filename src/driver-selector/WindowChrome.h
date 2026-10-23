// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

#pragma once

namespace driverselector
{
    // The parts of the window's XAML tree the chrome drives.
    struct WindowChromeElements
    {
        winrt::Microsoft::UI::Xaml::Window Window{ nullptr };
        winrt::Microsoft::UI::Xaml::FrameworkElement Root{ nullptr };
        winrt::Microsoft::UI::Xaml::UIElement TitleBar{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition LeftInset{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition RightInset{ nullptr };
    };

    // Custom title bar, window icon and the window's size and position between runs. The
    // same layout the Windows MIDI Services tools use. Nothing here throws.
    class WindowChrome
    {
    public:
        WindowChrome() = default;
        ~WindowChrome();

        // owns the window's icons
        WindowChrome(WindowChrome const&) = delete;
        WindowChrome& operator=(WindowChrome const&) = delete;

        // call once the root element has loaded
        void Initialize(_In_ WindowChromeElements const& elements) noexcept;

        // caption button colors follow the theme
        void ApplyTitleBarColors() noexcept;

        // insets are in physical pixels and change with the display scale
        void UpdateTitleBarInsets() noexcept;

        // Taskbar, Alt-Tab and title bar icon from the icon embedded in the executable.
        void SetWindowIconFromResource(_In_ uint16_t resourceId) noexcept;

        // the same icon, for the image drawn in the custom title bar
        static winrt::Microsoft::UI::Xaml::Media::ImageSource LoadIconImageSource(
            _In_ uint16_t resourceId,
            _In_ int32_t sizePixels) noexcept;

        // Static, because this runs before the window is activated and before Initialize.
        static void RestorePlacement(
            _In_ winrt::Microsoft::UI::Xaml::Window const& window,
            _In_ int32_t defaultWidth,
            _In_ int32_t defaultHeight) noexcept;

        void SavePlacement() noexcept;

        HWND WindowHandle() const noexcept;

    private:
        WindowChromeElements m_elements{};

        HICON m_largeIcon{ nullptr };
        HICON m_smallIcon{ nullptr };
    };
}
