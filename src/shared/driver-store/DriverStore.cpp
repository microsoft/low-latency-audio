// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

#include "DriverStore.h"

// INITGUID has to be in effect before devpkey.h, or the property keys are only declared and the
// link fails. The keys are selectany, so other files may define them too.
#include <initguid.h>
#include <devpkey.h>

#include <setupapi.h>
#include <newdev.h>
#include <cfgmgr32.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cwchar>

namespace driverstore
{
    namespace
    {
        class DeviceInfoSet
        {
        public:
            explicit DeviceInfoSet(_In_ HDEVINFO const handle) noexcept : m_handle(handle) {}

            ~DeviceInfoSet()
            {
                if (m_handle != INVALID_HANDLE_VALUE)
                {
                    ::SetupDiDestroyDeviceInfoList(m_handle);
                }
            }

            DeviceInfoSet(DeviceInfoSet const&) = delete;
            DeviceInfoSet& operator=(DeviceInfoSet const&) = delete;

            HDEVINFO Get() const noexcept { return m_handle; }
            bool IsValid() const noexcept { return m_handle != INVALID_HANDLE_VALUE; }

        private:
            HDEVINFO m_handle{ INVALID_HANDLE_VALUE };
        };

        class InfFile
        {
        public:
            explicit InfFile(_In_ HINF const handle) noexcept : m_handle(handle) {}

            ~InfFile()
            {
                if (m_handle != INVALID_HANDLE_VALUE)
                {
                    ::SetupCloseInfFile(m_handle);
                }
            }

            InfFile(InfFile const&) = delete;
            InfFile& operator=(InfFile const&) = delete;

            HINF Get() const noexcept { return m_handle; }
            bool IsValid() const noexcept { return m_handle != INVALID_HANDLE_VALUE; }

        private:
            HINF m_handle{ INVALID_HANDLE_VALUE };
        };

        class FindHandle
        {
        public:
            explicit FindHandle(_In_ HANDLE const handle) noexcept : m_handle(handle) {}

            ~FindHandle()
            {
                if (m_handle != INVALID_HANDLE_VALUE)
                {
                    ::FindClose(m_handle);
                }
            }

            FindHandle(FindHandle const&) = delete;
            FindHandle& operator=(FindHandle const&) = delete;

            HANDLE Get() const noexcept { return m_handle; }
            bool IsValid() const noexcept { return m_handle != INVALID_HANDLE_VALUE; }

        private:
            HANDLE m_handle{ INVALID_HANDLE_VALUE };
        };

        // A driver list has to be destroyed against the same set and device it was built for.
        class DriverList
        {
        public:
            DriverList(
                _In_ HDEVINFO const set,
                _In_ SP_DEVINFO_DATA& device,
                _In_ DWORD const type) noexcept :
                m_set(set),
                m_device(&device),
                m_type(type)
            {
            }

            ~DriverList()
            {
                ::SetupDiDestroyDriverInfoList(m_set, m_device, m_type);
            }

            DriverList(DriverList const&) = delete;
            DriverList& operator=(DriverList const&) = delete;

        private:
            HDEVINFO m_set{ INVALID_HANDLE_VALUE };
            SP_DEVINFO_DATA* m_device{ nullptr };
            DWORD m_type{ SPDIT_NODRIVER };
        };

        std::wstring WindowsInfFolder()
        {
            std::array<wchar_t, MAX_PATH> windows{};

            auto const length = ::GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()));

            if (length == 0 || length >= windows.size())
            {
                return {};
            }

            return std::wstring{ windows.data() } + L"\\INF\\";
        }

        std::wstring ReadVersionField(
            _In_ HINF const inf,
            _In_ wchar_t const* const key,
            _In_ DWORD const fieldIndex)
        {
            INFCONTEXT context{};

            if (!::SetupFindFirstLineW(inf, L"Version", key, &context))
            {
                return {};
            }

            // %token% references are resolved through [Strings] by SetupAPI itself
            DWORD required{ 0 };
            ::SetupGetStringFieldW(&context, fieldIndex, nullptr, 0, &required);

            if (required == 0)
            {
                return {};
            }

            std::wstring value(required, L'\0');

            if (!::SetupGetStringFieldW(&context, fieldIndex, value.data(), required, &required))
            {
                return {};
            }

            value.resize(::wcsnlen(value.c_str(), value.size()));

            return value;
        }

        // "1.2.3.4" packed as four 16 bit fields, most significant first. Missing parts are zero.
        uint64_t ParseDriverVersion(_In_ std::wstring const& version)
        {
            uint64_t packed{ 0 };
            size_t position{ 0 };

            for (int part = 0; part < 4; part++)
            {
                uint64_t value{ 0 };

                while (position < version.size() && version[position] >= L'0' && version[position] <= L'9')
                {
                    value = (value * 10) + static_cast<uint64_t>(version[position] - L'0');

                    if (value > 0xFFFF)
                    {
                        return 0;
                    }

                    position++;
                }

                packed |= value << (48 - (16 * part));

                if (position >= version.size() || version[position] != L'.')
                {
                    break;
                }

                position++;
            }

            return packed;
        }

        std::wstring OriginalInfName(_In_ std::wstring const& publishedPath)
        {
            DWORD required{ 0 };

            ::SetupGetInfInformationW(publishedPath.c_str(), INFINFO_INF_NAME_IS_ABSOLUTE, nullptr, 0, &required);

            if (required == 0)
            {
                return {};
            }

            std::vector<std::byte> buffer(required);

            auto* const information = reinterpret_cast<PSP_INF_INFORMATION>(buffer.data());

            if (!::SetupGetInfInformationW(
                publishedPath.c_str(), INFINFO_INF_NAME_IS_ABSOLUTE, information, required, &required))
            {
                return {};
            }

            SP_ORIGINAL_FILE_INFO_W original{};
            original.cbSize = sizeof(original);

            if (!::SetupQueryInfOriginalFileInformationW(information, 0, nullptr, &original))
            {
                return {};
            }

            return std::wstring{ original.OriginalInfName };
        }

        std::wstring DriverStoreLocation(_In_ std::wstring const& publishedPath)
        {
            std::array<wchar_t, MAX_PATH> buffer{};
            DWORD required{ 0 };

            if (::SetupGetInfDriverStoreLocationW(
                publishedPath.c_str(), nullptr, nullptr, buffer.data(), static_cast<DWORD>(buffer.size()), &required))
            {
                return std::wstring{ buffer.data() };
            }

            if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER || required == 0)
            {
                return {};
            }

            std::wstring longer(required, L'\0');

            if (!::SetupGetInfDriverStoreLocationW(
                publishedPath.c_str(), nullptr, nullptr, longer.data(), required, &required))
            {
                return {};
            }

            longer.resize(::wcsnlen(longer.c_str(), longer.size()));

            return longer;
        }

        std::wstring GetDeviceString(
            _In_ HDEVINFO const set,
            _In_ SP_DEVINFO_DATA& device,
            _In_ DEVPROPKEY const& key)
        {
            DEVPROPTYPE type{ 0 };
            DWORD required{ 0 };

            ::SetupDiGetDevicePropertyW(set, &device, &key, &type, nullptr, 0, &required, 0);

            if (required == 0 || type != DEVPROP_TYPE_STRING)
            {
                return {};
            }

            std::vector<std::byte> buffer(required);

            if (!::SetupDiGetDevicePropertyW(
                set, &device, &key, &type, reinterpret_cast<PBYTE>(buffer.data()), required, &required, 0))
            {
                return {};
            }

            auto const* const text = reinterpret_cast<wchar_t const*>(buffer.data());

            return std::wstring{ text, ::wcsnlen(text, required / sizeof(wchar_t)) };
        }

        std::vector<std::wstring> GetDeviceStringList(
            _In_ HDEVINFO const set,
            _In_ SP_DEVINFO_DATA& device,
            _In_ DEVPROPKEY const& key)
        {
            std::vector<std::wstring> values{};

            DEVPROPTYPE type{ 0 };
            DWORD required{ 0 };

            ::SetupDiGetDevicePropertyW(set, &device, &key, &type, nullptr, 0, &required, 0);

            if (required == 0 || type != DEVPROP_TYPE_STRING_LIST)
            {
                return values;
            }

            // one extra terminator, so a list the driver forgot to double terminate cannot run on
            std::vector<wchar_t> buffer((required / sizeof(wchar_t)) + 2, L'\0');

            if (!::SetupDiGetDevicePropertyW(
                set, &device, &key, &type, reinterpret_cast<PBYTE>(buffer.data()), required, &required, 0))
            {
                return values;
            }

            for (auto const* current = buffer.data(); *current != L'\0'; current += ::wcslen(current) + 1)
            {
                values.emplace_back(current);
            }

            return values;
        }

        // Hardware id and INF of one entry in a driver list.
        struct DriverNodeDetail
        {
            std::wstring HardwareId{};
            std::wstring InfFileName{};
        };

        bool GetDriverNodeDetail(
            _In_ HDEVINFO const set,
            _In_ SP_DEVINFO_DATA& device,
            _In_ SP_DRVINFO_DATA_W& driver,
            _Out_ DriverNodeDetail& result)
        {
            result = DriverNodeDetail{};

            DWORD required{ 0 };

            ::SetupDiGetDriverInfoDetailW(set, &device, &driver, nullptr, 0, &required);

            if (required < sizeof(SP_DRVINFO_DETAIL_DATA_W))
            {
                required = sizeof(SP_DRVINFO_DETAIL_DATA_W);
            }

            // room for a terminator after the hardware id list, whatever the driver wrote
            std::vector<std::byte> buffer(required + (2 * sizeof(wchar_t)));

            auto* const detail = reinterpret_cast<SP_DRVINFO_DETAIL_DATA_W*>(buffer.data());
            detail->cbSize = sizeof(SP_DRVINFO_DETAIL_DATA_W);

            if (!::SetupDiGetDriverInfoDetailW(set, &device, &driver, detail, required, &required))
            {
                return false;
            }

            result.InfFileName = detail->InfFileName;
            result.HardwareId = detail->HardwareID;

            return true;
        }
    }

    _Use_decl_annotations_
    std::wstring FileNameOnly(std::wstring const& path) noexcept
    {
        try
        {
            auto const separator = path.find_last_of(L"\\/");

            return separator == std::wstring::npos ? path : path.substr(separator + 1);
        }
        catch (...)
        {
            return {};
        }
    }

    _Use_decl_annotations_
    bool EqualsNoCase(std::wstring const& left, std::wstring const& right) noexcept
    {
        return ::CompareStringOrdinal(
            left.c_str(), static_cast<int>(left.size()),
            right.c_str(), static_cast<int>(right.size()),
            TRUE) == CSTR_EQUAL;
    }

    _Use_decl_annotations_
    std::wstring FormatDriverVersion(uint64_t const versionNumber) noexcept
    {
        try
        {
            return std::to_wstring((versionNumber >> 48) & 0xFFFF) + L"." +
                std::to_wstring((versionNumber >> 32) & 0xFFFF) + L"." +
                std::to_wstring((versionNumber >> 16) & 0xFFFF) + L"." +
                std::to_wstring(versionNumber & 0xFFFF);
        }
        catch (...)
        {
            return {};
        }
    }

    _Use_decl_annotations_
    bool ReadInfIdentity(std::wstring const& infPath, InfIdentity& identity) noexcept
    {
        identity = InfIdentity{};

        try
        {
            InfFile const inf{ ::SetupOpenInfFileW(infPath.c_str(), nullptr, INF_STYLE_WIN4, nullptr) };

            if (!inf.IsValid())
            {
                return false;
            }

            identity.Provider = ReadVersionField(inf.Get(), L"Provider", 1);

            // DriverVer = mm/dd/yyyy,w.x.y.z
            identity.Version = ReadVersionField(inf.Get(), L"DriverVer", 2);
            identity.VersionNumber = ParseDriverVersion(identity.Version);

            return true;
        }
        catch (...)
        {
            identity = InfIdentity{};
            return false;
        }
    }

    _Use_decl_annotations_
    std::vector<DriverPackage> FindDriverPackages(
        std::wstring const& originalInfName,
        std::wstring const& provider) noexcept
    {
        std::vector<DriverPackage> packages{};

        try
        {
            auto const infFolder = WindowsInfFolder();

            if (infFolder.empty())
            {
                return packages;
            }

            WIN32_FIND_DATAW findData{};

            FindHandle const find{ ::FindFirstFileW((infFolder + L"oem*.inf").c_str(), &findData) };

            if (!find.IsValid())
            {
                return packages;
            }

            do
            {
                if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                {
                    continue;
                }

                auto const publishedPath = infFolder + findData.cFileName;

                // The published oemNN.inf name is assigned in install order and says nothing
                // about which driver it is, so packages are matched on their original name.
                auto const originalName = OriginalInfName(publishedPath);

                if (!EqualsNoCase(originalName, originalInfName))
                {
                    continue;
                }

                InfIdentity identity{};

                if (!ReadInfIdentity(publishedPath, identity))
                {
                    continue;
                }

                if (!provider.empty() && !EqualsNoCase(identity.Provider, provider))
                {
                    continue;
                }

                DriverPackage package{};

                package.PublishedInfPath = publishedPath;
                package.PublishedName = findData.cFileName;
                package.OriginalInfName = originalName;
                package.DriverStoreInfPath = DriverStoreLocation(publishedPath);
                package.Provider = identity.Provider;
                package.Version = identity.Version;
                package.VersionNumber = identity.VersionNumber;

                // a published INF without its package folder cannot be installed from
                if (package.DriverStoreInfPath.empty())
                {
                    continue;
                }

                packages.push_back(std::move(package));
            }
            while (::FindNextFileW(find.Get(), &findData));

            std::sort(packages.begin(), packages.end(),
                [](DriverPackage const& left, DriverPackage const& right)
                {
                    return left.VersionNumber > right.VersionNumber;
                });
        }
        catch (...)
        {
            packages.clear();
        }

        return packages;
    }

    _Use_decl_annotations_
    std::vector<std::wstring> FindDevicesUsingPublishedInfs(std::vector<std::wstring> const& publishedNames) noexcept
    {
        std::vector<std::wstring> devices{};

        try
        {
            if (publishedNames.empty())
            {
                return devices;
            }

            DeviceInfoSet const set{ ::SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT) };

            if (!set.IsValid())
            {
                return devices;
            }

            for (DWORD index = 0;; index++)
            {
                SP_DEVINFO_DATA device{};
                device.cbSize = sizeof(device);

                if (!::SetupDiEnumDeviceInfo(set.Get(), index, &device))
                {
                    break;
                }

                auto const infName = GetDeviceString(set.Get(), device, DEVPKEY_Device_DriverInfPath);

                if (infName.empty())
                {
                    continue;
                }

                auto const inUse = std::any_of(publishedNames.begin(), publishedNames.end(),
                    [&infName](std::wstring const& name)
                    {
                        return EqualsNoCase(FileNameOnly(name), infName);
                    });

                if (!inUse)
                {
                    continue;
                }

                std::array<wchar_t, MAX_DEVICE_ID_LEN> instanceId{};

                if (::SetupDiGetDeviceInstanceIdW(
                    set.Get(), &device, instanceId.data(), static_cast<DWORD>(instanceId.size()), nullptr))
                {
                    devices.emplace_back(instanceId.data());
                }
            }
        }
        catch (...)
        {
            devices.clear();
        }

        return devices;
    }

    _Use_decl_annotations_
    InstallOutcome InstallDriverFromInf(std::wstring const& instanceId, std::wstring const& infPath) noexcept
    {
        InstallOutcome outcome{};

        try
        {
            if (infPath.empty() || infPath.size() >= MAX_PATH)
            {
                outcome.Error = ERROR_BAD_PATHNAME;
                return outcome;
            }

            DeviceInfoSet const set{ ::SetupDiCreateDeviceInfoList(nullptr, nullptr) };

            if (!set.IsValid())
            {
                outcome.Error = ::GetLastError();
                return outcome;
            }

            SP_DEVINFO_DATA device{};
            device.cbSize = sizeof(device);

            if (!::SetupDiOpenDeviceInfoW(set.Get(), instanceId.c_str(), nullptr, 0, &device))
            {
                outcome.Error = ::GetLastError();
                return outcome;
            }

            SP_DEVINSTALL_PARAMS_W parameters{};
            parameters.cbSize = sizeof(parameters);

            if (!::SetupDiGetDeviceInstallParamsW(set.Get(), &device, &parameters))
            {
                outcome.Error = ::GetLastError();
                return outcome;
            }

            ::wcscpy_s(parameters.DriverPath, infPath.c_str());

            // Only this INF, and its models even when they are hidden from Device Manager's list.
            parameters.Flags |= DI_ENUMSINGLEINF;
            parameters.FlagsEx |= DI_FLAGSEX_ALLOWEXCLUDEDDRVS;

            if (!::SetupDiSetDeviceInstallParamsW(set.Get(), &device, &parameters))
            {
                outcome.Error = ::GetLastError();
                return outcome;
            }

            // A class driver list, not a compatible one: the INF does not have to name this device.
            if (!::SetupDiBuildDriverInfoList(set.Get(), &device, SPDIT_CLASSDRIVER))
            {
                outcome.Error = ::GetLastError();
                return outcome;
            }

            DriverList const cleanup{ set.Get(), device, SPDIT_CLASSDRIVER };

            auto deviceIds = GetDeviceStringList(set.Get(), device, DEVPKEY_Device_HardwareIds);

            for (auto const& id : GetDeviceStringList(set.Get(), device, DEVPKEY_Device_CompatibleIds))
            {
                deviceIds.push_back(id);
            }

            auto const expectedInfName = FileNameOnly(infPath);

            SP_DRVINFO_DATA_W chosen{};
            bool found{ false };
            bool chosenMatchesDevice{ false };

            for (DWORD index = 0;; index++)
            {
                SP_DRVINFO_DATA_W driver{};
                driver.cbSize = sizeof(driver);

                if (!::SetupDiEnumDriverInfoW(set.Get(), &device, SPDIT_CLASSDRIVER, index, &driver))
                {
                    break;
                }

                DriverNodeDetail detail{};

                if (!GetDriverNodeDetail(set.Get(), device, driver, detail) ||
                    !EqualsNoCase(FileNameOnly(detail.InfFileName), expectedInfName))
                {
                    continue;
                }

                // Every model in the INF installs the same way. One that names this device is
                // preferred only because it also carries the description written for it.
                auto const matchesDevice = std::any_of(deviceIds.begin(), deviceIds.end(),
                    [&detail](std::wstring const& id)
                    {
                        return EqualsNoCase(id, detail.HardwareId);
                    });

                if (!found || (matchesDevice && !chosenMatchesDevice))
                {
                    chosen = driver;
                    found = true;
                    chosenMatchesDevice = matchesDevice;
                }
            }

            if (!found)
            {
                outcome.Error = ERROR_NOT_FOUND;
                return outcome;
            }

            if (!::SetupDiSetSelectedDriverW(set.Get(), &device, &chosen))
            {
                outcome.Error = ::GetLastError();
                return outcome;
            }

            BOOL rebootRequired{ FALSE };

            if (!::DiInstallDevice(nullptr, set.Get(), &device, &chosen, DIIDFLAG_NOFINISHINSTALLUI, &rebootRequired))
            {
                outcome.Error = ::GetLastError();
                return outcome;
            }

            outcome.Succeeded = true;
            outcome.RebootRequired = rebootRequired != FALSE;

            SP_DEVINSTALL_PARAMS_W after{};
            after.cbSize = sizeof(after);

            if (::SetupDiGetDeviceInstallParamsW(set.Get(), &device, &after) &&
                (after.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART)) != 0)
            {
                outcome.RebootRequired = true;
            }

            outcome.InstalledInfName = GetDeviceString(set.Get(), device, DEVPKEY_Device_DriverInfPath);
        }
        catch (...)
        {
            outcome.Succeeded = false;

            if (outcome.Error == ERROR_SUCCESS)
            {
                outcome.Error = ERROR_GEN_FAILURE;
            }
        }

        return outcome;
    }
}
