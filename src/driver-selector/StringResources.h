// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

#pragma once

namespace driverselector::resources
{
    // Looks up a string from Strings\<language>\Resources.resw. Never throws: if the resource
    // system is unavailable the key itself is returned so the UI still renders.
    winrt::hstring GetString(_In_ std::wstring_view resourceKey) noexcept;

    namespace details
    {
        // std::format has no formatter for winrt::hstring
        inline std::wstring AsFormattable(_In_ winrt::hstring const& value) noexcept
        {
            return std::wstring{ value };
        }

        template <typename TValue>
        inline TValue AsFormattable(_In_ TValue const& value) noexcept
        {
            return value;
        }
    }

    // The format string comes from resources and uses std::format placeholders: {0}, {1}.
    template <typename... TArgs>
    winrt::hstring FormatString(_In_ std::wstring_view resourceKey, TArgs&&... args) noexcept
    {
        try
        {
            auto formatString = GetString(resourceKey);

            // materialized first, because make_wformat_args needs lvalues
            auto values = std::make_tuple(details::AsFormattable(std::forward<TArgs>(args))...);

            return std::apply(
                [&formatString](auto&... unpacked)
                {
                    return winrt::hstring{ std::vformat(
                        std::wstring_view{ formatString }, std::make_wformat_args(unpacked...)) };
                },
                values);
        }
        catch (...)
        {
            return GetString(resourceKey);
        }
    }
}
