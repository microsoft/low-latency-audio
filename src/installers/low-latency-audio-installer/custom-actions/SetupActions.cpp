// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

// Custom actions for the Low-Latency Audio installer.
//
// They add the low-latency driver package to the driver store, and take it out again on
// uninstall. They never put the driver on a device by themselves: which device uses which driver
// is the customer's choice, made in the Low-Latency Audio Driver Selector. The one exception is
// an upgrade, where a device the customer had already moved to an older copy of the driver moves
// to the new copy.
//
// Both actions are deferred and run without impersonation, so they run as LocalSystem. Both take
// the full path of the installed INF as their CustomActionData.

#include <windows.h>
#include <msi.h>
#include <msiquery.h>
#include <setupapi.h>
#include <newdev.h>

#include <algorithm>
#include <format>
#include <string>
#include <vector>

#include "DriverStore.h"

namespace
{
    // Lines in a verbose MSI log (msiexec /l*v) start with this, so they are easy to find.
    void Log(
        _In_ MSIHANDLE const install,
        _In_ std::wstring const& message) noexcept
    {
        try
        {
            PMSIHANDLE record{ ::MsiCreateRecord(1) };

            if (record == 0)
            {
                return;
            }

            auto const text = std::format(L"LowLatencyAudioSetup: {}", message);

            // The text goes in field 1 and the template only refers to it, so brackets and braces
            // in a path or a device instance ID are never read as MSI formatting.
            ::MsiRecordSetStringW(record, 0, L"[1]");
            ::MsiRecordSetStringW(record, 1, text.c_str());

            ::MsiProcessMessage(install, INSTALLMESSAGE_INFO, record);
        }
        catch (...)
        {
        }
    }

    std::wstring ErrorText(_In_ DWORD const error)
    {
        return std::format(L"0x{:08X}", error);
    }

    std::wstring GetCustomActionData(_In_ MSIHANDLE const install)
    {
        wchar_t empty[1]{};
        DWORD length{ 0 };

        if (::MsiGetPropertyW(install, L"CustomActionData", empty, &length) != ERROR_MORE_DATA)
        {
            return {};
        }

        // the length reported does not count the terminating null
        std::wstring value(static_cast<size_t>(length) + 1, L'\0');
        length = static_cast<DWORD>(value.size());

        if (::MsiGetPropertyW(install, L"CustomActionData", value.data(), &length) != ERROR_SUCCESS)
        {
            return {};
        }

        value.resize(length);

        return value;
    }

    bool ReadIdentity(
        _In_ MSIHANDLE const install,
        _In_ std::wstring const& infPath,
        _Out_ driverstore::InfIdentity& identity)
    {
        identity = {};

        if (!driverstore::ReadInfIdentity(infPath, identity) || identity.Provider.empty())
        {
            Log(install, std::format(L"Could not read the provider and version from {}.", infPath));
            return false;
        }

        return true;
    }

    // Packages built from the same INF file name by the same provider are copies of this driver,
    // whatever their version. The provider check keeps the actions away from a package someone
    // else built from the same open source.
    std::vector<driverstore::DriverPackage> FindOurPackages(
        _In_ std::wstring const& infPath,
        _In_ driverstore::InfIdentity const& identity)
    {
        return driverstore::FindDriverPackages(driverstore::FileNameOnly(infPath), identity.Provider);
    }
}

// Adds the driver package to the driver store. On an upgrade, also moves every device that used
// an older copy to this one, and then removes the older copies nothing uses anymore.
extern "C" UINT __stdcall InstallDriverPackage(_In_ MSIHANDLE install) noexcept
{
    try
    {
        auto const infPath = GetCustomActionData(install);

        if (infPath.empty())
        {
            Log(install, L"No INF path was passed in CustomActionData.");
            return ERROR_INSTALL_FAILURE;
        }

        driverstore::InfIdentity identity{};

        if (!ReadIdentity(install, infPath, identity))
        {
            return ERROR_INSTALL_FAILURE;
        }

        Log(install, std::format(L"Adding {} to the driver store. Provider {}, version {}.",
            infPath, identity.Provider, identity.Version));

        // SPOST_PATH keeps the package's other files with the INF. A package that is already in the
        // driver store with the same contents comes back under its existing published name.
        wchar_t publishedPath[MAX_PATH]{};

        if (!::SetupCopyOEMInfW(infPath.c_str(), nullptr, SPOST_PATH, 0,
            publishedPath, ARRAYSIZE(publishedPath), nullptr, nullptr))
        {
            auto const error = ::GetLastError();

            Log(install, std::format(L"SetupCopyOEMInf failed with {}.", ErrorText(error)));

            return ERROR_INSTALL_FAILURE;
        }

        auto const publishedName = driverstore::FileNameOnly(publishedPath);

        Log(install, std::format(L"The driver package is published as {}.", publishedName));

        auto const packages = FindOurPackages(infPath, identity);

        auto const current = std::find_if(packages.begin(), packages.end(),
            [&publishedName](driverstore::DriverPackage const& package)
            {
                return driverstore::EqualsNoCase(package.PublishedName, publishedName);
            });

        if (current == packages.end())
        {
            // The package is in the driver store, which is all a first install needs.
            Log(install, L"Could not find the new package in the driver store listing, so older copies were left alone.");
            return ERROR_SUCCESS;
        }

        std::vector<std::wstring> older{};

        for (auto const& package : packages)
        {
            if (!driverstore::EqualsNoCase(package.PublishedName, publishedName))
            {
                older.push_back(package.PublishedName);
            }
        }

        bool rebootRequired{ false };

        if (!older.empty())
        {
            for (auto const& instanceId : driverstore::FindDevicesUsingPublishedInfs(older))
            {
                auto const outcome = driverstore::InstallDriverFromInf(instanceId, current->DriverStoreInfPath);

                if (outcome.Succeeded)
                {
                    rebootRequired = rebootRequired || outcome.RebootRequired;

                    Log(install, std::format(L"Moved {} to {}.", instanceId, publishedName));
                }
                else
                {
                    Log(install, std::format(L"Could not move {} to {}: {}. It keeps the older driver.",
                        instanceId, publishedName, ErrorText(outcome.Error)));
                }
            }

            for (auto const& name : older)
            {
                // Without SUOI_FORCEDELETE, a package that a device still uses stays where it is.
                if (::SetupUninstallOEMInfW(name.c_str(), 0, nullptr))
                {
                    Log(install, std::format(L"Removed the older package {}.", name));
                }
                else
                {
                    Log(install, std::format(L"Kept the older package {}: {}.", name, ErrorText(::GetLastError())));
                }
            }
        }

        if (rebootRequired)
        {
            Log(install, L"Windows needs a restart to finish moving devices to the new driver.");
            ::MsiSetMode(install, MSIRUNMODE_REBOOTATEND, TRUE);
        }

        return ERROR_SUCCESS;
    }
    catch (...)
    {
        Log(install, L"InstallDriverPackage failed unexpectedly.");
    }

    return ERROR_INSTALL_FAILURE;
}

// Removes every copy of the driver package from the driver store. Each device using one moves to
// the next best driver first, which for a USB Audio 2.0 interface is the Windows driver.
//
// Never fails the uninstall: a product that cannot be removed is worse than a package left in the
// driver store, and the log says which one it was.
extern "C" UINT __stdcall RemoveDriverPackages(_In_ MSIHANDLE install) noexcept
{
    try
    {
        auto const infPath = GetCustomActionData(install);

        if (infPath.empty())
        {
            Log(install, L"No INF path was passed in CustomActionData. Nothing was removed.");
            return ERROR_SUCCESS;
        }

        driverstore::InfIdentity identity{};

        if (!ReadIdentity(install, infPath, identity))
        {
            return ERROR_SUCCESS;
        }

        bool rebootRequired{ false };

        for (auto const& package : FindOurPackages(infPath, identity))
        {
            BOOL needReboot{ FALSE };

            if (::DiUninstallDriverW(nullptr, package.DriverStoreInfPath.c_str(), 0, &needReboot))
            {
                rebootRequired = rebootRequired || (needReboot != FALSE);

                Log(install, std::format(L"Removed {} ({}), version {}.",
                    package.PublishedName, package.DriverStoreInfPath, package.Version));
            }
            else
            {
                Log(install, std::format(L"Could not remove {}: {}.",
                    package.PublishedName, ErrorText(::GetLastError())));
            }
        }

        if (rebootRequired)
        {
            Log(install, L"Windows needs a restart to finish removing the driver.");
            ::MsiSetMode(install, MSIRUNMODE_REBOOTATEND, TRUE);
        }
    }
    catch (...)
    {
        Log(install, L"RemoveDriverPackages failed unexpectedly.");
    }

    return ERROR_SUCCESS;
}
