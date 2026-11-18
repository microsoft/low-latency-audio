// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

#pragma once

// Driver store helpers shared by the Low-Latency Audio Driver Selector and the installer's custom
// actions. Standard C++ and Win32 only, with no precompiled header, so both can compile this file
// as it is. Nothing here throws, and everything here blocks: call it from a worker thread.

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace driverstore
{
    // A driver package that has been added to the driver store.
    struct DriverPackage
    {
        // %windir%\INF\oemNN.inf, the name Windows published the package under
        std::wstring PublishedInfPath{};
        std::wstring PublishedName{};

        // the INF file name the package was built with, such as USBAudio2-ACX.inf
        std::wstring OriginalInfName{};

        // the INF inside the driver store's FileRepository folder, next to the package's files
        std::wstring DriverStoreInfPath{};

        std::wstring Provider{};
        std::wstring Version{};

        // DriverVer packed the same way as SP_DRVINFO_DATA::DriverVersion, so it compares directly
        uint64_t VersionNumber{ 0 };
    };

    // The [Version] section values that identify a package.
    struct InfIdentity
    {
        std::wstring Provider{};
        std::wstring Version{};
        uint64_t VersionNumber{ 0 };
    };

    struct InstallOutcome
    {
        bool Succeeded{ false };
        DWORD Error{ ERROR_SUCCESS };
        bool RebootRequired{ false };

        // the published INF name the device should now be using, such as oem12.inf
        std::wstring InstalledInfName{};
    };

    bool ReadInfIdentity(
        _In_ std::wstring const& infPath,
        _Out_ InfIdentity& identity) noexcept;

    // Every package in the driver store that was built from an INF with this file name, newest
    // first. When provider is not empty, only packages from that provider are returned.
    std::vector<DriverPackage> FindDriverPackages(
        _In_ std::wstring const& originalInfName,
        _In_ std::wstring const& provider) noexcept;

    // Instance ids of the present devices whose driver came from one of these published INFs.
    std::vector<std::wstring> FindDevicesUsingPublishedInfs(
        _In_ std::vector<std::wstring> const& publishedNames) noexcept;

    // Installs the driver in infPath on one device, whether or not the INF lists the device's
    // hardware ids. This is what Device Manager does for "Have Disk" with "Show compatible
    // hardware" turned off: a class driver list built from that one INF. Pass the INF inside the
    // driver store, or any folder that holds the whole package, never the published oemNN.inf.
    InstallOutcome InstallDriverFromInf(
        _In_ std::wstring const& instanceId,
        _In_ std::wstring const& infPath) noexcept;

    // The file name part of a path, for comparing published INF names.
    std::wstring FileNameOnly(_In_ std::wstring const& path) noexcept;

    // Case insensitive comparison, using the same rules as the file system.
    bool EqualsNoCase(
        _In_ std::wstring const& left,
        _In_ std::wstring const& right) noexcept;

    // "1.2.3.4" from a packed driver version.
    std::wstring FormatDriverVersion(_In_ uint64_t versionNumber) noexcept;
}
