// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

#include "pch.h"
#include "WindowChrome.h"
#include "SelectorConfig.h"

#include <robuffer.h>

namespace wux = ::winrt::Microsoft::UI::Xaml;
namespace wuw = ::winrt::Microsoft::UI::Windowing;

// the linker supplies this; it is the HINSTANCE of the module holding the icon resource
extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace driverselector
{
    namespace
    {
        constexpr wchar_t ValueWindowX[] = L"WindowX";
        constexpr wchar_t ValueWindowY[] = L"WindowY";
        constexpr wchar_t ValueWindowWidth[] = L"WindowWidth";
        constexpr wchar_t ValueWindowHeight[] = L"WindowHeight";
        constexpr wchar_t ValueWindowMaximized[] = L"WindowMaximized";

        // smaller than this is a collapsed or broken placement, not worth restoring to
        constexpr int32_t MinimumWindowWidth = 640;
        constexpr int32_t MinimumWindowHeight = 400;

        uint32_t ReadDword(
            _In_ wchar_t const* const valueName,
            _In_ uint32_t const defaultValue) noexcept
        {
            DWORD value{ 0 };
            DWORD size{ sizeof(value) };

            if (::RegGetValueW(HKEY_CURRENT_USER, config::SettingsKeyPath, valueName,
                RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS)
            {
                return defaultValue;
            }

            return value;
        }

        void WriteDword(
            _In_ wchar_t const* const valueName,
            _In_ uint32_t const value) noexcept
        {
            wil::unique_hkey key{};

            if (::RegCreateKeyExW(HKEY_CURRENT_USER, config::SettingsKeyPath, 0, nullptr,
                REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, key.put(), nullptr) != ERROR_SUCCESS)
            {
                return;
            }

            ::RegSetValueExW(key.get(), valueName, 0, REG_DWORD,
                reinterpret_cast<BYTE const*>(&value), sizeof(value));
        }

        wux::Media::Imaging::WriteableBitmap IconToBitmap(
            _In_ HICON const icon,
            _In_ int32_t const sizePixels)
        {
            ICONINFO info{};

            if (!::GetIconInfo(icon, &info))
            {
                return nullptr;
            }

            wil::unique_hbitmap colorBitmap{ info.hbmColor };
            wil::unique_hbitmap maskBitmap{ info.hbmMask };

            if (!colorBitmap)
            {
                return nullptr;
            }

            BITMAPINFO bitmapInfo{};
            bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bitmapInfo.bmiHeader.biWidth = sizePixels;
            bitmapInfo.bmiHeader.biHeight = -sizePixels;      // negative for a top down DIB
            bitmapInfo.bmiHeader.biPlanes = 1;
            bitmapInfo.bmiHeader.biBitCount = 32;
            bitmapInfo.bmiHeader.biCompression = BI_RGB;

            std::vector<uint8_t> pixels(static_cast<size_t>(sizePixels) * sizePixels * 4);

            wil::unique_hdc memoryDC{ ::CreateCompatibleDC(nullptr) };

            if (!memoryDC)
            {
                return nullptr;
            }

            if (::GetDIBits(memoryDC.get(), colorBitmap.get(), 0, sizePixels,
                pixels.data(), &bitmapInfo, DIB_RGB_COLORS) == 0)
            {
                return nullptr;
            }

            // WriteableBitmap wants premultiplied alpha; an icon carries straight alpha
            for (size_t i = 0; i < pixels.size(); i += 4)
            {
                auto const alpha = pixels[i + 3];

                pixels[i + 0] = static_cast<uint8_t>((pixels[i + 0] * alpha) / 255);
                pixels[i + 1] = static_cast<uint8_t>((pixels[i + 1] * alpha) / 255);
                pixels[i + 2] = static_cast<uint8_t>((pixels[i + 2] * alpha) / 255);
            }

            wux::Media::Imaging::WriteableBitmap bitmap{ sizePixels, sizePixels };

            auto buffer = bitmap.PixelBuffer();
            auto byteAccess = buffer.as<::Windows::Storage::Streams::IBufferByteAccess>();

            uint8_t* destination{ nullptr };

            if (FAILED(byteAccess->Buffer(&destination)) ||
                destination == nullptr ||
                buffer.Capacity() < pixels.size())
            {
                return nullptr;
            }

            memcpy(destination, pixels.data(), pixels.size());

            bitmap.Invalidate();

            return bitmap;
        }
    }

    WindowChrome::~WindowChrome()
    {
        if (m_largeIcon != nullptr)
        {
            ::DestroyIcon(m_largeIcon);
        }

        if (m_smallIcon != nullptr)
        {
            ::DestroyIcon(m_smallIcon);
        }
    }

    HWND WindowChrome::WindowHandle() const noexcept
    {
        HWND handle{ nullptr };

        try
        {
            if (m_elements.Window != nullptr)
            {
                if (auto const native = m_elements.Window.try_as<::IWindowNative>())
                {
                    LOG_IF_FAILED(native->get_WindowHandle(&handle));
                }
            }
        }
        DRIVER_SELECTOR_CATCH_AND_LOG(L"Unable to get the window handle.")

        return handle;
    }

    _Use_decl_annotations_
    void WindowChrome::Initialize(WindowChromeElements const& elements) noexcept
    {
        try
        {
            m_elements = elements;

            m_elements.Window.ExtendsContentIntoTitleBar(true);

            if (m_elements.TitleBar != nullptr)
            {
                m_elements.Window.SetTitleBar(m_elements.TitleBar);
            }

            ApplyTitleBarColors();
            UpdateTitleBarInsets();
        }
        DRIVER_SELECTOR_CATCH_AND_LOG(L"Unable to set up the title bar.")
    }

    void WindowChrome::ApplyTitleBarColors() noexcept
    {
        try
        {
            if (m_elements.Window == nullptr || m_elements.Root == nullptr)
            {
                return;
            }

            auto titleBar = m_elements.Window.AppWindow().TitleBar();

            auto const transparent = winrt::Windows::UI::Colors::Transparent();

            titleBar.ButtonBackgroundColor(transparent);
            titleBar.ButtonInactiveBackgroundColor(transparent);

            auto const dark = m_elements.Root.ActualTheme() == wux::ElementTheme::Dark;
            auto const foreground = dark ? winrt::Windows::UI::Colors::White() : winrt::Windows::UI::Colors::Black();

            titleBar.ButtonForegroundColor(foreground);
            titleBar.ButtonHoverForegroundColor(foreground);
            titleBar.ButtonPressedForegroundColor(foreground);
            titleBar.ButtonInactiveForegroundColor(
                dark ? winrt::Windows::UI::Colors::Gray() : winrt::Windows::UI::Colors::DimGray());
        }
        DRIVER_SELECTOR_CATCH_AND_LOG(L"Unable to color the title bar buttons.")
    }

    void WindowChrome::UpdateTitleBarInsets() noexcept
    {
        try
        {
            if (m_elements.Window == nullptr || m_elements.LeftInset == nullptr || m_elements.RightInset == nullptr)
            {
                return;
            }

            auto const titleBar = m_elements.Window.AppWindow().TitleBar();

            auto scale = 1.0;

            if (m_elements.Root != nullptr)
            {
                if (auto const xamlRoot = m_elements.Root.XamlRoot())
                {
                    scale = xamlRoot.RasterizationScale();
                }
            }

            if (scale <= 0.0)
            {
                scale = 1.0;
            }

            m_elements.LeftInset.Width(wux::GridLength{ titleBar.LeftInset() / scale, wux::GridUnitType::Pixel });
            m_elements.RightInset.Width(wux::GridLength{ titleBar.RightInset() / scale, wux::GridUnitType::Pixel });
        }
        DRIVER_SELECTOR_CATCH_AND_LOG(L"Unable to update the title bar insets.")
    }

    _Use_decl_annotations_
    void WindowChrome::SetWindowIconFromResource(uint16_t const resourceId) noexcept
    {
        try
        {
            auto const handle = WindowHandle();

            if (handle == nullptr)
            {
                return;
            }

            auto const instance = reinterpret_cast<HINSTANCE>(&__ImageBase);

            // each size loaded on its own, because a stretched bitmap makes small icons muddy
            m_largeIcon = static_cast<HICON>(::LoadImageW(instance, MAKEINTRESOURCEW(resourceId),
                IMAGE_ICON, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));

            m_smallIcon = static_cast<HICON>(::LoadImageW(instance, MAKEINTRESOURCEW(resourceId),
                IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));

            if (m_largeIcon != nullptr)
            {
                ::SendMessageW(handle, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(m_largeIcon));
            }

            if (m_smallIcon != nullptr)
            {
                ::SendMessageW(handle, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(m_smallIcon));
            }
        }
        DRIVER_SELECTOR_CATCH_AND_LOG(L"Unable to set the window icon.")
    }

    _Use_decl_annotations_
    wux::Media::ImageSource WindowChrome::LoadIconImageSource(uint16_t const resourceId, int32_t const sizePixels) noexcept
    {
        try
        {
            if (sizePixels <= 0)
            {
                return nullptr;
            }

            auto const instance = reinterpret_cast<HINSTANCE>(&__ImageBase);

            wil::unique_hicon icon{ static_cast<HICON>(::LoadImageW(
                instance, MAKEINTRESOURCEW(resourceId), IMAGE_ICON, sizePixels, sizePixels, LR_DEFAULTCOLOR)) };

            if (!icon)
            {
                return nullptr;
            }

            return IconToBitmap(icon.get(), sizePixels);
        }
        DRIVER_SELECTOR_CATCH_AND_LOG(L"Unable to load the title bar icon.")

        return nullptr;
    }

    _Use_decl_annotations_
    void WindowChrome::RestorePlacement(
        wux::Window const& window,
        int32_t const defaultWidth,
        int32_t const defaultHeight) noexcept
    {
        try
        {
            if (window == nullptr)
            {
                return;
            }

            auto appWindow = window.AppWindow();

            winrt::Windows::Graphics::RectInt32 bounds{
                static_cast<int32_t>(ReadDword(ValueWindowX, 0)),
                static_cast<int32_t>(ReadDword(ValueWindowY, 0)),
                static_cast<int32_t>(ReadDword(ValueWindowWidth, 0)),
                static_cast<int32_t>(ReadDword(ValueWindowHeight, 0)) };

            if (bounds.Width < MinimumWindowWidth || bounds.Height < MinimumWindowHeight)
            {
                appWindow.Resize(winrt::Windows::Graphics::SizeInt32{ defaultWidth, defaultHeight });
                return;
            }

            // The saved display may be gone or smaller now, so pull the window back onto one
            // that exists before showing it.
            auto const display = wuw::DisplayArea::GetFromRect(bounds, wuw::DisplayAreaFallback::Nearest);

            if (display != nullptr)
            {
                auto const work = display.WorkArea();

                bounds.Width = std::min(bounds.Width, work.Width);
                bounds.Height = std::min(bounds.Height, work.Height);
                bounds.X = std::clamp(bounds.X, work.X, work.X + work.Width - bounds.Width);
                bounds.Y = std::clamp(bounds.Y, work.Y, work.Y + work.Height - bounds.Height);
            }

            appWindow.MoveAndResize(bounds);

            if (ReadDword(ValueWindowMaximized, 0) != 0)
            {
                if (auto presenter = appWindow.Presenter().try_as<wuw::OverlappedPresenter>())
                {
                    presenter.Maximize();
                }
            }
        }
        DRIVER_SELECTOR_CATCH_AND_LOG(L"Unable to restore the window placement.")
    }

    void WindowChrome::SavePlacement() noexcept
    {
        try
        {
            auto const handle = WindowHandle();

            if (handle == nullptr)
            {
                return;
            }

            WINDOWPLACEMENT placement{};
            placement.length = sizeof(placement);

            if (!::GetWindowPlacement(handle, &placement))
            {
                return;
            }

            // restore bounds, so a maximized window still reopens at its previous size
            auto const width = placement.rcNormalPosition.right - placement.rcNormalPosition.left;
            auto const height = placement.rcNormalPosition.bottom - placement.rcNormalPosition.top;

            if (width < MinimumWindowWidth || height < MinimumWindowHeight)
            {
                return;
            }

            WriteDword(ValueWindowX, static_cast<uint32_t>(placement.rcNormalPosition.left));
            WriteDword(ValueWindowY, static_cast<uint32_t>(placement.rcNormalPosition.top));
            WriteDword(ValueWindowWidth, static_cast<uint32_t>(width));
            WriteDword(ValueWindowHeight, static_cast<uint32_t>(height));
            WriteDword(ValueWindowMaximized, placement.showCmd == SW_SHOWMAXIMIZED ? 1u : 0u);
        }
        DRIVER_SELECTOR_CATCH_AND_LOG(L"Unable to save the window placement.")
    }
}
