// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

#include "pch.h"
#include "Elevation.h"

namespace driverselector
{
    bool IsProcessElevated() noexcept
    {
        try
        {
            wil::unique_handle token;

            if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, token.put()))
            {
                return false;
            }

            TOKEN_ELEVATION elevation{};
            DWORD returnedSize{ 0 };

            if (!::GetTokenInformation(token.get(), TokenElevation, &elevation, sizeof(elevation), &returnedSize))
            {
                return false;
            }

            return elevation.TokenIsElevated != 0;
        }
        DRIVER_SELECTOR_CATCH_AND_LOG(L"Unable to check the elevation state of this process.")

        return false;
    }

    _Use_decl_annotations_
    bool TryRelaunchElevated(std::wstring const& extraArgument) noexcept
    {
        try
        {
            wchar_t modulePath[MAX_PATH]{};

            auto const length = ::GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath));

            if (length == 0 || length >= ARRAYSIZE(modulePath))
            {
                return false;
            }

            // The tool's only other switch, --noelevate, means nothing to an elevated copy, so the
            // original command line is not carried across.
            SHELLEXECUTEINFOW info{};

            info.cbSize = sizeof(info);
            info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
            info.lpVerb = L"runas";
            info.lpFile = modulePath;
            info.lpParameters = extraArgument.empty() ? nullptr : extraArgument.c_str();
            info.nShow = SW_SHOWNORMAL;

            return ::ShellExecuteExW(&info) != FALSE;
        }
        DRIVER_SELECTOR_CATCH_AND_LOG(L"Unable to relaunch with administrator rights.")

        return false;
    }

    bool TryRestartComputer() noexcept
    {
        try
        {
            wil::unique_handle token;

            if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, token.put()))
            {
                return false;
            }

            TOKEN_PRIVILEGES privileges{};
            privileges.PrivilegeCount = 1;
            privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

            if (!::LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &privileges.Privileges[0].Luid))
            {
                return false;
            }

            if (!::AdjustTokenPrivileges(token.get(), FALSE, &privileges, 0, nullptr, nullptr))
            {
                return false;
            }

            // AdjustTokenPrivileges reports success even when it could not assign the privilege
            if (::GetLastError() != ERROR_SUCCESS)
            {
                return false;
            }

            return ::ExitWindowsEx(
                EWX_REBOOT | EWX_RESTARTAPPS,
                SHTDN_REASON_MAJOR_HARDWARE | SHTDN_REASON_MINOR_INSTALLATION | SHTDN_REASON_FLAG_PLANNED) != FALSE;
        }
        DRIVER_SELECTOR_CATCH_AND_LOG(L"Unable to restart the computer.")

        return false;
    }
}
