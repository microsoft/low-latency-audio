// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

#include "pch.h"
#include "DriverTools.h"

#include "DriverStore.h"
#include "SelectorConfig.h"
#include "StringResources.h"

// INITGUID has to be in effect before devpkey.h, devguid.h and usbiodef.h or the property keys
// and GUIDs are only declared, not defined, and the link fails.
#include <initguid.h>
#include <devpkey.h>
#include <devguid.h>

#include <setupapi.h>
#include <newdev.h>
#include <cfgmgr32.h>

#include <winioctl.h>
#include <usbiodef.h>
#include <usbioctl.h>
#include <usbspec.h>

namespace driverselector
{
    namespace
    {
        namespace res = ::driverselector::resources;

        // Compatible ids are built by the USB stack from the device's own descriptors.

        // a USB Audio 2.0 function; USB Audio 1.0 has protocol 0, which the driver doesn't support
        constexpr wchar_t UsbAudio2FunctionId[] = LR"(USB\Class_01&SubClass_00&Prot_20)";
        constexpr uint8_t UsbAudio2FunctionProtocol = 0x20;

        // a device with more than one interface, which Windows splits into functions
        constexpr wchar_t UsbCompositeId[] = LR"(USB\COMPOSITE)";

        // How long the audio function gets to appear after the whole device goes back to Windows.
        // Windows installs a driver for every function of the device in that time.
        constexpr auto FunctionArrivalTimeout = std::chrono::seconds{ 30 };

        struct DeviceNode
        {
            std::wstring InstanceId{};
            std::wstring ParentInstanceId{};
            std::vector<std::wstring> CompatibleIds{};
            std::wstring Service{};
            std::wstring InfName{};
            std::wstring Provider{};
            std::wstring Version{};
            std::wstring FriendlyName{};
            std::wstring Description{};
            std::wstring BusReportedName{};
            GUID ClassGuid{};
            bool HasProblem{ false };
            uint32_t ProblemCode{ 0 };
        };

        // One entry in a device's compatible driver list.
        struct CompatibleDriver
        {
            std::wstring InfName{};
            std::wstring Provider{};
            std::wstring Version{};
            uint64_t VersionNumber{ 0 };

            // lower is a better match
            DWORD Rank{ 0xFFFFFFFF };
        };

        using DriverPredicate = std::function<bool(CompatibleDriver const&)>;

        bool StartsWithNoCase(
            _In_ std::wstring const& value,
            _In_ std::wstring_view const prefix) noexcept
        {
            return value.size() >= prefix.size() &&
                ::CompareStringOrdinal(
                    value.c_str(), static_cast<int>(prefix.size()),
                    prefix.data(), static_cast<int>(prefix.size()),
                    TRUE) == CSTR_EQUAL;
        }

        bool ContainsNoCase(
            _In_ std::vector<std::wstring> const& values,
            _In_ std::wstring const& wanted) noexcept
        {
            return std::any_of(values.begin(), values.end(),
                [&wanted](std::wstring const& value)
                {
                    return driverstore::EqualsNoCase(value, wanted);
                });
        }

        std::wstring GetNodeString(
            _In_ DEVINST const devInst,
            _In_ DEVPROPKEY const& key)
        {
            DEVPROPTYPE type{ 0 };
            ULONG size{ 0 };

            if (::CM_Get_DevNode_PropertyW(devInst, &key, &type, nullptr, &size, 0) != CR_BUFFER_SMALL ||
                type != DEVPROP_TYPE_STRING || size == 0)
            {
                return {};
            }

            std::vector<wchar_t> buffer((size / sizeof(wchar_t)) + 1, L'\0');

            if (::CM_Get_DevNode_PropertyW(
                devInst, &key, &type, reinterpret_cast<PBYTE>(buffer.data()), &size, 0) != CR_SUCCESS)
            {
                return {};
            }

            return std::wstring{ buffer.data() };
        }

        std::vector<std::wstring> GetNodeStringList(
            _In_ DEVINST const devInst,
            _In_ DEVPROPKEY const& key)
        {
            std::vector<std::wstring> values{};

            DEVPROPTYPE type{ 0 };
            ULONG size{ 0 };

            if (::CM_Get_DevNode_PropertyW(devInst, &key, &type, nullptr, &size, 0) != CR_BUFFER_SMALL ||
                type != DEVPROP_TYPE_STRING_LIST || size == 0)
            {
                return values;
            }

            // two extra terminators, so a list that is not double terminated cannot run on
            std::vector<wchar_t> buffer((size / sizeof(wchar_t)) + 2, L'\0');

            if (::CM_Get_DevNode_PropertyW(
                devInst, &key, &type, reinterpret_cast<PBYTE>(buffer.data()), &size, 0) != CR_SUCCESS)
            {
                return values;
            }

            for (auto const* current = buffer.data(); *current != L'\0'; current += ::wcslen(current) + 1)
            {
                values.emplace_back(current);
            }

            return values;
        }

        std::wstring GetInstanceIdOf(_In_ DEVINST const devInst)
        {
            std::array<wchar_t, MAX_DEVICE_ID_LEN> buffer{};

            if (::CM_Get_Device_IDW(devInst, buffer.data(), static_cast<ULONG>(buffer.size()), 0) != CR_SUCCESS)
            {
                return {};
            }

            return std::wstring{ buffer.data() };
        }

        // Present devices only. A device that was unplugged is still known to Windows, but its
        // driver cannot be changed.
        bool LocateDevice(
            _In_ std::wstring const& instanceId,
            _Out_ DEVINST& devInst)
        {
            devInst = 0;

            if (instanceId.empty())
            {
                return false;
            }

            return ::CM_Locate_DevNodeW(
                &devInst, const_cast<DEVINSTID_W>(instanceId.c_str()), CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS;
        }

        std::vector<std::wstring> GetPresentUsbInstanceIds()
        {
            constexpr ULONG filter = CM_GETIDLIST_FILTER_ENUMERATOR | CM_GETIDLIST_FILTER_PRESENT;

            // A device can arrive between asking for the size and asking for the list.
            for (int attempt = 0; attempt < 4; attempt++)
            {
                ULONG size{ 0 };

                if (::CM_Get_Device_ID_List_SizeW(&size, L"USB", filter) != CR_SUCCESS || size == 0)
                {
                    return {};
                }

                std::vector<wchar_t> buffer(static_cast<size_t>(size) + 2, L'\0');

                auto const result = ::CM_Get_Device_ID_ListW(
                    L"USB", buffer.data(), static_cast<ULONG>(buffer.size()), filter);

                if (result == CR_BUFFER_SMALL)
                {
                    continue;
                }

                if (result != CR_SUCCESS)
                {
                    return {};
                }

                std::vector<std::wstring> ids{};

                for (auto const* current = buffer.data(); *current != L'\0'; current += ::wcslen(current) + 1)
                {
                    ids.emplace_back(current);
                }

                return ids;
            }

            return {};
        }

        std::optional<DeviceNode> ReadNode(_In_ std::wstring const& instanceId)
        {
            DEVINST devInst{ 0 };

            if (!LocateDevice(instanceId, devInst))
            {
                return std::nullopt;
            }

            DeviceNode node{};

            node.InstanceId = instanceId;
            node.CompatibleIds = GetNodeStringList(devInst, DEVPKEY_Device_CompatibleIds);
            node.Service = GetNodeString(devInst, DEVPKEY_Device_Service);
            node.InfName = GetNodeString(devInst, DEVPKEY_Device_DriverInfPath);
            node.Provider = GetNodeString(devInst, DEVPKEY_Device_DriverProvider);
            node.Version = GetNodeString(devInst, DEVPKEY_Device_DriverVersion);
            node.FriendlyName = GetNodeString(devInst, DEVPKEY_Device_FriendlyName);
            node.Description = GetNodeString(devInst, DEVPKEY_Device_DeviceDesc);
            node.BusReportedName = GetNodeString(devInst, DEVPKEY_Device_BusReportedDeviceDesc);

            DEVPROPTYPE type{ 0 };
            ULONG size{ sizeof(node.ClassGuid) };

            if (::CM_Get_DevNode_PropertyW(
                devInst, &DEVPKEY_Device_ClassGuid, &type, reinterpret_cast<PBYTE>(&node.ClassGuid), &size, 0) != CR_SUCCESS ||
                type != DEVPROP_TYPE_GUID)
            {
                node.ClassGuid = GUID{};
            }

            DEVINST parent{ 0 };

            if (::CM_Get_Parent(&parent, devInst, 0) == CR_SUCCESS)
            {
                node.ParentInstanceId = GetInstanceIdOf(parent);
            }

            ULONG status{ 0 };
            ULONG problem{ 0 };

            if (::CM_Get_DevNode_Status(&status, &problem, devInst, 0) == CR_SUCCESS)
            {
                node.HasProblem = (status & DN_HAS_PROBLEM) != 0;
                node.ProblemCode = problem;
            }

            return node;
        }

        bool IsAudioFunction(_In_ DeviceNode const& node)
        {
            // a function of a composite device always has an interface number in its instance id
            return node.InstanceId.find(L"&MI_") != std::wstring::npos &&
                ContainsNoCase(node.CompatibleIds, UsbAudio2FunctionId);
        }

        bool IsCompositeDevice(_In_ DeviceNode const& node)
        {
            return ContainsNoCase(node.CompatibleIds, UsbCompositeId);
        }

        // A driver package that replaced the in-box composite driver for the whole device, so
        // Windows never created a separate device for the audio function. That is usually a
        // manufacturer's driver. It can also be the low-latency driver, when an earlier INF listed
        // the device itself. Whether the device is USB Audio 2.0 is read from its descriptors.
        bool IsWholeDeviceAudio(_In_ DeviceNode const& node)
        {
            return node.InstanceId.find(L"&MI_") == std::wstring::npos &&
                IsCompositeDevice(node) &&
                !node.Service.empty() &&
                !driverstore::EqualsNoCase(node.Service, config::WindowsUsbCompositeServiceName) &&
                StartsWithNoCase(node.InfName, L"oem") &&
                ::IsEqualGUID(node.ClassGuid, GUID_DEVCLASS_MEDIA);
        }

        // configuration 0, read through the device's hub so it works whatever driver the device has
        std::vector<uint8_t> ReadConfigurationDescriptor(_In_ DEVINST const devInst)
        {
            DEVINST hub{ 0 };

            if (::CM_Get_Parent(&hub, devInst, 0) != CR_SUCCESS)
            {
                return {};
            }

            // a USB device's address is the number of the hub port it's plugged into
            DEVPROPTYPE type{ 0 };
            ULONG port{ 0 };
            ULONG size{ sizeof(port) };

            if (::CM_Get_DevNode_PropertyW(
                devInst, &DEVPKEY_Device_Address, &type, reinterpret_cast<PBYTE>(&port), &size, 0) != CR_SUCCESS ||
                type != DEVPROP_TYPE_UINT32 || port == 0)
            {
                return {};
            }

            auto const hubInstanceId = GetInstanceIdOf(hub);
            ULONG listSize{ 0 };

            if (hubInstanceId.empty() ||
                ::CM_Get_Device_Interface_List_SizeW(&listSize, const_cast<LPGUID>(&GUID_DEVINTERFACE_USB_HUB),
                    const_cast<DEVINSTID_W>(hubInstanceId.c_str()), CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS ||
                listSize <= 1)
            {
                return {};
            }

            std::vector<wchar_t> interfaces(static_cast<size_t>(listSize) + 1, L'\0');

            if (::CM_Get_Device_Interface_ListW(const_cast<LPGUID>(&GUID_DEVINTERFACE_USB_HUB),
                const_cast<DEVINSTID_W>(hubInstanceId.c_str()), interfaces.data(), listSize,
                CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS || interfaces[0] == L'\0')
            {
                return {};
            }

            wil::unique_hfile hubHandle{ ::CreateFileW(
                interfaces.data(), GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr) };

            if (!hubHandle)
            {
                return {};
            }

            auto const request = [&hubHandle, port](_In_ USHORT const length)
                {
                    std::vector<uint8_t> buffer(sizeof(USB_DESCRIPTOR_REQUEST) + length, 0);

                    auto* const descriptorRequest = reinterpret_cast<USB_DESCRIPTOR_REQUEST*>(buffer.data());
                    descriptorRequest->ConnectionIndex = port;
                    descriptorRequest->SetupPacket.wValue = static_cast<USHORT>(USB_CONFIGURATION_DESCRIPTOR_TYPE << 8);
                    descriptorRequest->SetupPacket.wLength = length;

                    DWORD returned{ 0 };

                    if (!::DeviceIoControl(hubHandle.get(), IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,
                        buffer.data(), static_cast<DWORD>(buffer.size()), buffer.data(), static_cast<DWORD>(buffer.size()),
                        &returned, nullptr) ||
                        returned < sizeof(USB_DESCRIPTOR_REQUEST) || returned > buffer.size())
                    {
                        return std::vector<uint8_t>{};
                    }

                    return std::vector<uint8_t>(buffer.begin() + sizeof(USB_DESCRIPTOR_REQUEST), buffer.begin() + returned);
                };

            auto const header = request(sizeof(USB_CONFIGURATION_DESCRIPTOR));

            if (header.size() < sizeof(USB_CONFIGURATION_DESCRIPTOR))
            {
                return {};
            }

            // wTotalLength, little endian
            auto const totalLength = static_cast<USHORT>(header[2] | (header[3] << 8));

            if (totalLength < sizeof(USB_CONFIGURATION_DESCRIPTOR))
            {
                return {};
            }

            return request(totalLength);
        }

        bool HasUsbAudio2Function(_In_ std::vector<uint8_t> const& configuration)
        {
            size_t offset{ 0 };

            // the device wrote these, so no length is trusted before it's checked
            while (offset + 2 <= configuration.size())
            {
                auto const length = configuration[offset];
                auto const type = configuration[offset + 1];

                if (length < 2 || offset + length > configuration.size())
                {
                    break;
                }

                if (type == USB_INTERFACE_ASSOCIATION_DESCRIPTOR_TYPE && length >= sizeof(USB_INTERFACE_ASSOCIATION_DESCRIPTOR))
                {
                    USB_INTERFACE_ASSOCIATION_DESCRIPTOR association{};
                    ::memcpy(&association, configuration.data() + offset, sizeof(association));

                    if (association.bFunctionClass == USB_DEVICE_CLASS_AUDIO &&
                        association.bFunctionSubClass == 0x00 &&
                        association.bFunctionProtocol == UsbAudio2FunctionProtocol)
                    {
                        return true;
                    }
                }

                offset += length;
            }

            return false;
        }

        // Windows creates no function for a device taken over whole, so its descriptors are read
        bool IsUsbAudio2Device(_In_ std::wstring const& instanceId)
        {
            DEVINST devInst{ 0 };

            return LocateDevice(instanceId, devInst) && HasUsbAudio2Function(ReadConfigurationDescriptor(devInst));
        }

        // VID_xxxx, PID_xxxx and MI_xx out of a USB instance id. Empty when absent.
        std::wstring IdField(
            _In_ std::wstring const& instanceId,
            _In_ std::wstring_view const tag,
            _In_ size_t const length)
        {
            auto const upper = [&instanceId]()
                {
                    std::wstring copy{ instanceId };
                    std::transform(copy.begin(), copy.end(), copy.begin(),
                        [](wchar_t c) { return static_cast<wchar_t>(::towupper(c)); });
                    return copy;
                }();

            auto const position = upper.find(tag);

            if (position == std::wstring::npos || position + tag.size() + length > upper.size())
            {
                return {};
            }

            return upper.substr(position + tag.size(), length);
        }

        std::wstring HardwareText(_In_ std::wstring const& instanceId)
        {
            auto const vendor = IdField(instanceId, L"VID_", 4);
            auto const product = IdField(instanceId, L"PID_", 4);
            auto const function = IdField(instanceId, L"&MI_", 2);

            if (vendor.empty() || product.empty())
            {
                return instanceId;
            }

            return function.empty() ?
                std::wstring{ res::FormatString(L"HardwareTextFormat", vendor, product) } :
                std::wstring{ res::FormatString(L"HardwareTextInterfaceFormat", vendor, product, function) };
        }

        // The product name the device itself reports is what is printed on the box. The names
        // Windows assigns, such as "USB Composite Device", are only used when it reports none.
        std::wstring DeviceName(
            _In_ DeviceNode const& node,
            _In_opt_ DeviceNode const* const parent,
            _In_ bool const severalFunctions)
        {
            std::wstring name{};

            for (auto const* candidate : {
                parent != nullptr ? &parent->BusReportedName : nullptr,
                &node.BusReportedName,
                &node.FriendlyName,
                &node.Description })
            {
                if (candidate != nullptr && !candidate->empty())
                {
                    name = *candidate;
                    break;
                }
            }

            if (name.empty())
            {
                name = node.InstanceId;
            }

            auto const function = IdField(node.InstanceId, L"&MI_", 2);

            if (severalFunctions && !function.empty())
            {
                name = res::FormatString(L"DeviceNameInterfaceFormat", name, function);
            }

            return name;
        }

        std::vector<CompatibleDriver> GetCompatibleDrivers(_In_ std::wstring const& instanceId)
        {
            std::vector<CompatibleDriver> drivers{};

            auto const set = ::SetupDiCreateDeviceInfoList(nullptr, nullptr);

            if (set == INVALID_HANDLE_VALUE)
            {
                return drivers;
            }

            auto const destroySet = wil::scope_exit([set]() noexcept { ::SetupDiDestroyDeviceInfoList(set); });

            SP_DEVINFO_DATA device{};
            device.cbSize = sizeof(device);

            if (!::SetupDiOpenDeviceInfoW(set, instanceId.c_str(), nullptr, 0, &device) ||
                !::SetupDiBuildDriverInfoList(set, &device, SPDIT_COMPATDRIVER))
            {
                return drivers;
            }

            auto const destroyList = wil::scope_exit([set, &device]() noexcept
                {
                    ::SetupDiDestroyDriverInfoList(set, &device, SPDIT_COMPATDRIVER);
                });

            for (DWORD index = 0;; index++)
            {
                SP_DRVINFO_DATA_W driver{};
                driver.cbSize = sizeof(driver);

                if (!::SetupDiEnumDriverInfoW(set, &device, SPDIT_COMPATDRIVER, index, &driver))
                {
                    break;
                }

                DWORD required{ 0 };
                ::SetupDiGetDriverInfoDetailW(set, &device, &driver, nullptr, 0, &required);

                if (required < sizeof(SP_DRVINFO_DETAIL_DATA_W))
                {
                    required = sizeof(SP_DRVINFO_DETAIL_DATA_W);
                }

                std::vector<std::byte> buffer(required);

                auto* const detail = reinterpret_cast<SP_DRVINFO_DETAIL_DATA_W*>(buffer.data());
                detail->cbSize = sizeof(SP_DRVINFO_DETAIL_DATA_W);

                if (!::SetupDiGetDriverInfoDetailW(set, &device, &driver, detail, required, &required))
                {
                    continue;
                }

                SP_DRVINSTALL_PARAMS parameters{};
                parameters.cbSize = sizeof(parameters);

                CompatibleDriver entry{};

                entry.InfName = driverstore::FileNameOnly(detail->InfFileName);
                entry.Provider = driver.ProviderName;
                entry.VersionNumber = driver.DriverVersion;
                entry.Version = driverstore::FormatDriverVersion(driver.DriverVersion);

                if (::SetupDiGetDriverInstallParamsW(set, &device, &driver, &parameters))
                {
                    entry.Rank = parameters.Rank;
                }

                drivers.push_back(std::move(entry));
            }

            return drivers;
        }

        // The entry Windows itself would pick among those the predicate accepts: best rank first,
        // then the newest.
        std::optional<CompatibleDriver> BestMatch(
            _In_ std::vector<CompatibleDriver> const& drivers,
            _In_ DriverPredicate const& predicate)
        {
            std::optional<CompatibleDriver> best{};

            for (auto const& driver : drivers)
            {
                if (!predicate(driver))
                {
                    continue;
                }

                if (!best.has_value() ||
                    driver.Rank < best->Rank ||
                    (driver.Rank == best->Rank && driver.VersionNumber > best->VersionNumber))
                {
                    best = driver;
                }
            }

            return best;
        }

        bool IsWindowsUsbAudio2Driver(_In_ CompatibleDriver const& driver)
        {
            return driverstore::EqualsNoCase(driver.InfName, config::WindowsUsbAudio2InfName);
        }

        bool IsWindowsCompositeDriver(_In_ CompatibleDriver const& driver)
        {
            return driverstore::EqualsNoCase(driver.InfName, config::WindowsUsbCompositeInfName);
        }

        bool IsLowLatencyInf(
            _In_ std::wstring const& infName,
            _In_ std::vector<driverstore::DriverPackage> const& packages)
        {
            return std::any_of(packages.begin(), packages.end(),
                [&infName](driverstore::DriverPackage const& package)
                {
                    return driverstore::EqualsNoCase(package.PublishedName, infName);
                });
        }

        // Third-party packages are published as oemNN.inf. Packages Microsoft publishes the same
        // way, through Windows Update, are not the manufacturer's.
        DriverPredicate ManufacturerDriverPredicate(_In_ std::vector<driverstore::DriverPackage> const& packages)
        {
            return [packages](CompatibleDriver const& driver)
                {
                    return StartsWithNoCase(driver.InfName, L"oem") &&
                        !IsLowLatencyInf(driver.InfName, packages) &&
                        !StartsWithNoCase(driver.Provider, L"Microsoft");
                };
        }

        DriverChoice ChoiceFrom(
            _In_ std::optional<CompatibleDriver> const& driver,
            _In_ DriverKind const kind,
            _In_ bool const wholeDevice)
        {
            if (!driver.has_value())
            {
                return {};
            }

            DriverChoice choice{};

            choice.Kind = kind;
            choice.InfName = driver->InfName;
            choice.Provider = driver->Provider;
            choice.Version = driver->Version;
            choice.WholeDevice = wholeDevice;

            return choice;
        }

        DriverChoice LowLatencyChoice(
            _In_ std::vector<driverstore::DriverPackage> const& packages,
            _In_ bool const wholeDevice)
        {
            if (packages.empty())
            {
                return {};
            }

            // newest first
            auto const& newest = packages.front();

            DriverChoice choice{};

            choice.Kind = DriverKind::LowLatency;
            choice.InfName = newest.PublishedName;
            choice.Provider = newest.Provider;
            choice.Version = newest.Version;
            choice.WholeDevice = wholeDevice;

            return choice;
        }

        // True when the device uses an older copy of the low-latency driver than the newest one
        // in the driver store.
        bool IsLowLatencyUpdateAvailable(
            _In_ std::wstring const& infName,
            _In_ std::vector<driverstore::DriverPackage> const& packages)
        {
            auto const current = std::find_if(packages.begin(), packages.end(),
                [&infName](driverstore::DriverPackage const& package)
                {
                    return driverstore::EqualsNoCase(package.PublishedName, infName);
                });

            return current != packages.end() && current != packages.begin() &&
                packages.front().VersionNumber > current->VersionNumber;
        }

        AudioDevice DescribeFunction(
            _In_ DeviceNode const& node,
            _In_opt_ DeviceNode const* const parent,
            _In_ bool const severalFunctions,
            _In_ std::vector<driverstore::DriverPackage> const& packages)
        {
            AudioDevice device{};

            device.FunctionInstanceId = node.InstanceId;
            device.ParentInstanceId = node.ParentInstanceId;
            device.Name = DeviceName(node, parent, severalFunctions);
            device.HardwareText = HardwareText(node.InstanceId);
            device.HasProblem = node.HasProblem;
            device.ProblemCode = node.ProblemCode;
            device.CurrentInfName = node.InfName;
            device.CurrentProvider = node.Provider;
            device.CurrentVersion = node.Version;

            if (IsLowLatencyInf(node.InfName, packages) ||
                driverstore::EqualsNoCase(node.Service, config::LowLatencyServiceName))
            {
                device.CurrentKind = DriverKind::LowLatency;
                device.LowLatencyUpdateAvailable = IsLowLatencyUpdateAvailable(node.InfName, packages);
            }
            else if (driverstore::EqualsNoCase(node.InfName, config::WindowsUsbAudio2InfName))
            {
                device.CurrentKind = DriverKind::WindowsUsbAudio2;
            }
            else if (StartsWithNoCase(node.InfName, L"oem"))
            {
                device.CurrentKind = DriverKind::Manufacturer;
            }
            else
            {
                device.CurrentKind = node.InfName.empty() ? DriverKind::Unknown : DriverKind::Other;
            }

            device.LowLatency = LowLatencyChoice(packages, false);

            auto const drivers = GetCompatibleDrivers(node.InstanceId);
            auto const manufacturer = ManufacturerDriverPredicate(packages);

            device.Windows = ChoiceFrom(
                BestMatch(drivers, IsWindowsUsbAudio2Driver), DriverKind::WindowsUsbAudio2, false);

            device.Manufacturer = ChoiceFrom(BestMatch(drivers, manufacturer), DriverKind::Manufacturer, false);

            // Most manufacturers' drivers take over the whole device rather than its audio
            // function, so they are matched against the composite device the function belongs to.
            if (!device.Manufacturer.IsAvailable() && parent != nullptr)
            {
                device.Manufacturer = ChoiceFrom(
                    BestMatch(GetCompatibleDrivers(parent->InstanceId), manufacturer), DriverKind::Manufacturer, true);
            }

            return device;
        }

        AudioDevice DescribeWholeDevice(
            _In_ DeviceNode const& node,
            _In_ std::vector<driverstore::DriverPackage> const& packages)
        {
            AudioDevice device{};

            device.ParentInstanceId = node.InstanceId;
            device.Name = DeviceName(node, nullptr, false);
            device.HardwareText = HardwareText(node.InstanceId);
            device.HasProblem = node.HasProblem;
            device.ProblemCode = node.ProblemCode;
            device.CurrentIsWholeDevice = true;
            device.CurrentInfName = node.InfName;
            device.CurrentProvider = node.Provider;
            device.CurrentVersion = node.Version;

            auto const drivers = GetCompatibleDrivers(node.InstanceId);

            // Choosing the low-latency driver again moves it to the audio function, which is
            // where this tool always puts it.
            device.LowLatency = LowLatencyChoice(packages, true);

            device.Windows = ChoiceFrom(
                BestMatch(drivers, IsWindowsCompositeDriver), DriverKind::WindowsComposite, true);

            if (IsLowLatencyInf(node.InfName, packages) ||
                driverstore::EqualsNoCase(node.Service, config::LowLatencyServiceName))
            {
                device.CurrentKind = DriverKind::LowLatency;
                device.LowLatencyUpdateAvailable = IsLowLatencyUpdateAvailable(node.InfName, packages);

                device.Manufacturer = ChoiceFrom(
                    BestMatch(drivers, ManufacturerDriverPredicate(packages)), DriverKind::Manufacturer, true);
            }
            else
            {
                device.CurrentKind = DriverKind::Manufacturer;
            }

            return device;
        }

        std::wstring SystemErrorText(_In_ DWORD const error)
        {
            wil::unique_hlocal_string text{};

            auto const length = ::FormatMessageW(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,
                error,
                0,
                reinterpret_cast<LPWSTR>(text.put()),
                0,
                nullptr);

            if (length == 0 || text == nullptr)
            {
                return {};
            }

            std::wstring message{ text.get(), length };

            while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' '))
            {
                message.pop_back();
            }

            return message;
        }

        // Setup API failures are mostly 0xE000xxxx codes, which have no system text.
        std::wstring FailureMessage(
            _In_ std::wstring_view const resourceKey,
            _In_ DWORD const error)
        {
            auto const text = SystemErrorText(error);
            auto const code = std::format(L"0x{:08X}", error);

            return text.empty() ?
                std::wstring{ res::FormatString(resourceKey, code) } :
                std::wstring{ res::FormatString(L"ErrorWithSystemTextFormat", res::FormatString(resourceKey, code), text) };
        }

        DriverOperationResult Failed(_In_ std::wstring_view const resourceKey)
        {
            DriverOperationResult result{};

            result.Message = res::GetString(resourceKey);

            return result;
        }

        DriverOperationResult FailedWithError(
            _In_ std::wstring_view const resourceKey,
            _In_ DWORD const error)
        {
            DriverOperationResult result{};

            result.Message = FailureMessage(resourceKey, error);

            return result;
        }

        // Installs the best entry the predicate accepts from the device's compatible driver list.
        driverstore::InstallOutcome InstallCompatibleDriver(
            _In_ std::wstring const& instanceId,
            _In_ DriverPredicate const& predicate)
        {
            driverstore::InstallOutcome outcome{};

            auto const set = ::SetupDiCreateDeviceInfoList(nullptr, nullptr);

            if (set == INVALID_HANDLE_VALUE)
            {
                outcome.Error = ::GetLastError();
                return outcome;
            }

            auto const destroySet = wil::scope_exit([set]() noexcept { ::SetupDiDestroyDeviceInfoList(set); });

            SP_DEVINFO_DATA device{};
            device.cbSize = sizeof(device);

            if (!::SetupDiOpenDeviceInfoW(set, instanceId.c_str(), nullptr, 0, &device) ||
                !::SetupDiBuildDriverInfoList(set, &device, SPDIT_COMPATDRIVER))
            {
                outcome.Error = ::GetLastError();
                return outcome;
            }

            auto const destroyList = wil::scope_exit([set, &device]() noexcept
                {
                    ::SetupDiDestroyDriverInfoList(set, &device, SPDIT_COMPATDRIVER);
                });

            SP_DRVINFO_DATA_W chosen{};
            std::optional<CompatibleDriver> best{};

            for (DWORD index = 0;; index++)
            {
                SP_DRVINFO_DATA_W driver{};
                driver.cbSize = sizeof(driver);

                if (!::SetupDiEnumDriverInfoW(set, &device, SPDIT_COMPATDRIVER, index, &driver))
                {
                    break;
                }

                DWORD required{ 0 };
                ::SetupDiGetDriverInfoDetailW(set, &device, &driver, nullptr, 0, &required);

                if (required < sizeof(SP_DRVINFO_DETAIL_DATA_W))
                {
                    required = sizeof(SP_DRVINFO_DETAIL_DATA_W);
                }

                std::vector<std::byte> buffer(required);

                auto* const detail = reinterpret_cast<SP_DRVINFO_DETAIL_DATA_W*>(buffer.data());
                detail->cbSize = sizeof(SP_DRVINFO_DETAIL_DATA_W);

                if (!::SetupDiGetDriverInfoDetailW(set, &device, &driver, detail, required, &required))
                {
                    continue;
                }

                CompatibleDriver entry{};

                entry.InfName = driverstore::FileNameOnly(detail->InfFileName);
                entry.Provider = driver.ProviderName;
                entry.VersionNumber = driver.DriverVersion;

                SP_DRVINSTALL_PARAMS parameters{};
                parameters.cbSize = sizeof(parameters);

                if (::SetupDiGetDriverInstallParamsW(set, &device, &driver, &parameters))
                {
                    entry.Rank = parameters.Rank;
                }

                if (!predicate(entry))
                {
                    continue;
                }

                if (!best.has_value() ||
                    entry.Rank < best->Rank ||
                    (entry.Rank == best->Rank && entry.VersionNumber > best->VersionNumber))
                {
                    best = entry;
                    chosen = driver;
                }
            }

            if (!best.has_value())
            {
                outcome.Error = ERROR_NOT_FOUND;
                return outcome;
            }

            if (!::SetupDiSetSelectedDriverW(set, &device, &chosen))
            {
                outcome.Error = ::GetLastError();
                return outcome;
            }

            BOOL rebootRequired{ FALSE };

            // The customer is deliberately overriding the ranking here, which is why this is
            // DiInstallDevice with a selected driver rather than a driver search.
            if (!::DiInstallDevice(nullptr, set, &device, &chosen, DIIDFLAG_NOFINISHINSTALLUI, &rebootRequired))
            {
                outcome.Error = ::GetLastError();
                return outcome;
            }

            outcome.Succeeded = true;
            outcome.RebootRequired = rebootRequired != FALSE;
            outcome.InstalledInfName = best->InfName;

            SP_DEVINSTALL_PARAMS_W after{};
            after.cbSize = sizeof(after);

            if (::SetupDiGetDeviceInstallParamsW(set, &device, &after) &&
                (after.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART)) != 0)
            {
                outcome.RebootRequired = true;
            }

            return outcome;
        }

        // The installer's reboot flag is a reliable yes and an unreliable no, so the device itself
        // is asked as well, and anything that cannot be read is treated as the worse case.
        DriverChangeFollowUp EvaluateFollowUp(
            _In_ std::wstring const& instanceId,
            _In_ std::wstring const& expectedInfName,
            _In_ bool const rebootRequired)
        {
            if (rebootRequired)
            {
                return DriverChangeFollowUp::RestartWindows;
            }

            DEVINST devInst{ 0 };

            if (!LocateDevice(instanceId, devInst))
            {
                return DriverChangeFollowUp::ReplugDevice;
            }

            ULONG status{ 0 };
            ULONG problem{ 0 };

            if (::CM_Get_DevNode_Status(&status, &problem, devInst, 0) != CR_SUCCESS)
            {
                return DriverChangeFollowUp::RestartWindows;
            }

            if ((status & DN_NEED_RESTART) != 0 || problem == CM_PROB_NEED_RESTART)
            {
                return DriverChangeFollowUp::RestartWindows;
            }

            if ((status & DN_STARTED) == 0 || (status & DN_HAS_PROBLEM) != 0)
            {
                return DriverChangeFollowUp::ReplugDevice;
            }

            if (!expectedInfName.empty() &&
                !driverstore::EqualsNoCase(GetNodeString(devInst, DEVPKEY_Device_DriverInfPath), expectedInfName))
            {
                return DriverChangeFollowUp::ReplugDevice;
            }

            return DriverChangeFollowUp::None;
        }

        DriverOperationResult Completed(
            _In_ std::wstring const& instanceId,
            _In_ driverstore::InstallOutcome const& outcome,
            _In_ std::wstring const& expectedInfName)
        {
            DriverOperationResult result{};

            result.Succeeded = true;
            result.FollowUp = EvaluateFollowUp(instanceId, expectedInfName, outcome.RebootRequired);

            switch (result.FollowUp)
            {
            case DriverChangeFollowUp::RestartWindows:
                result.Message = res::GetString(L"DriverChangedRestartWindows");
                break;

            case DriverChangeFollowUp::ReplugDevice:
                result.Message = res::GetString(L"DriverChangedReplug");
                break;

            default:
                result.Message = res::GetString(L"DriverChangedReady");
                break;
            }

            return result;
        }

        bool FindAudioFunctionUnder(
            _In_ std::wstring const& parentInstanceId,
            _Out_ std::wstring& functionInstanceId)
        {
            functionInstanceId.clear();

            DEVINST parent{ 0 };

            if (!LocateDevice(parentInstanceId, parent))
            {
                return false;
            }

            DEVINST child{ 0 };

            if (::CM_Get_Child(&child, parent, 0) != CR_SUCCESS)
            {
                return false;
            }

            do
            {
                if (ContainsNoCase(GetNodeStringList(child, DEVPKEY_Device_CompatibleIds), UsbAudio2FunctionId))
                {
                    functionInstanceId = GetInstanceIdOf(child);
                    return !functionInstanceId.empty();
                }
            }
            while (::CM_Get_Sibling(&child, child, 0) == CR_SUCCESS);

            return false;
        }

        // Once the whole device is back on the in-box composite driver, Windows creates a device
        // for each function and installs a driver on every one of them.
        std::wstring WaitForAudioFunction(_In_ std::wstring const& parentInstanceId)
        {
            auto const deadline = std::chrono::steady_clock::now() + FunctionArrivalTimeout;

            while (std::chrono::steady_clock::now() < deadline)
            {
                auto const remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now());

                ::CMP_WaitNoPendingInstallEvents(static_cast<DWORD>(std::clamp<int64_t>(remaining.count(), 0, 2000)));

                std::wstring functionInstanceId{};

                if (FindAudioFunctionUnder(parentInstanceId, functionInstanceId))
                {
                    // let the in-box driver install on it finish before replacing it
                    ::CMP_WaitNoPendingInstallEvents(5000);

                    return functionInstanceId;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds{ 500 });
            }

            return {};
        }
    }

    LowLatencyPackage FindLowLatencyPackage() noexcept
    {
        LowLatencyPackage result{};

        try
        {
            auto const packages = driverstore::FindDriverPackages(config::LowLatencyInfName, {});

            if (!packages.empty())
            {
                result.Installed = true;
                result.PublishedName = packages.front().PublishedName;
                result.Version = packages.front().Version;
            }
        }
        DRIVER_SELECTOR_CATCH_AND_LOG(L"Unable to look for the low-latency driver package.")

        return result;
    }

    std::vector<AudioDevice> EnumerateAudioDevices() noexcept
    {
        std::vector<AudioDevice> devices{};

        try
        {
            auto const packages = driverstore::FindDriverPackages(config::LowLatencyInfName, {});

            std::vector<DeviceNode> nodes{};

            for (auto const& instanceId : GetPresentUsbInstanceIds())
            {
                if (auto node = ReadNode(instanceId))
                {
                    nodes.push_back(std::move(*node));
                }
            }

            auto const findNode = [&nodes](std::wstring const& instanceId) -> DeviceNode const*
                {
                    auto const found = std::find_if(nodes.begin(), nodes.end(),
                        [&instanceId](DeviceNode const& node)
                        {
                            return driverstore::EqualsNoCase(node.InstanceId, instanceId);
                        });

                    return found == nodes.end() ? nullptr : &*found;
                };

            auto const functionsUnder = [&nodes](std::wstring const& parentInstanceId)
                {
                    return std::count_if(nodes.begin(), nodes.end(),
                        [&parentInstanceId](DeviceNode const& node)
                        {
                            return IsAudioFunction(node) &&
                                driverstore::EqualsNoCase(node.ParentInstanceId, parentInstanceId);
                        });
                };

            for (auto const& node : nodes)
            {
                if (IsAudioFunction(node))
                {
                    // Only a composite device is a function's parent. Anything else is a hub,
                    // whose name and drivers have nothing to do with the audio device.
                    auto const* parent = findNode(node.ParentInstanceId);

                    if (parent != nullptr && !IsCompositeDevice(*parent))
                    {
                        parent = nullptr;
                    }

                    devices.push_back(DescribeFunction(
                        node,
                        parent,
                        parent != nullptr && functionsUnder(node.ParentInstanceId) > 1,
                        packages));
                }
                else if (IsWholeDeviceAudio(node) &&
                    functionsUnder(node.InstanceId) == 0 &&
                    IsUsbAudio2Device(node.InstanceId))
                {
                    devices.push_back(DescribeWholeDevice(node, packages));
                }
            }

            std::sort(devices.begin(), devices.end(),
                [](AudioDevice const& left, AudioDevice const& right)
                {
                    auto const byName = ::CompareStringEx(
                        LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE,
                        left.Name.c_str(), static_cast<int>(left.Name.size()),
                        right.Name.c_str(), static_cast<int>(right.Name.size()),
                        nullptr, nullptr, 0);

                    return byName == CSTR_EQUAL ? left.HardwareText < right.HardwareText : byName == CSTR_LESS_THAN;
                });
        }
        DRIVER_SELECTOR_CATCH_AND_LOG(L"Unable to enumerate the USB audio devices.")

        return devices;
    }

    _Use_decl_annotations_
    DriverOperationResult UseLowLatencyDriver(AudioDevice const& device) noexcept
    {
        try
        {
            auto const packages = driverstore::FindDriverPackages(config::LowLatencyInfName, {});

            if (packages.empty())
            {
                return Failed(L"ErrorLowLatencyNotInstalled");
            }

            auto const& package = packages.front();

            DEVINST devInst{ 0 };
            std::wstring functionInstanceId{ device.FunctionInstanceId };

            if (functionInstanceId.empty() || !LocateDevice(functionInstanceId, devInst))
            {
                // No audio function means a manufacturer's driver has the whole device. The
                // low-latency driver runs on the function, so the device goes back to the
                // in-box composite driver first, which creates it.
                if (!LocateDevice(device.ParentInstanceId, devInst))
                {
                    return Failed(L"ErrorDeviceNotFound");
                }

                auto const composite = InstallCompatibleDriver(device.ParentInstanceId, IsWindowsCompositeDriver);

                if (!composite.Succeeded)
                {
                    return FailedWithError(L"ErrorCompositeFailedFormat", composite.Error);
                }

                if (composite.RebootRequired)
                {
                    DriverOperationResult result{};

                    result.Succeeded = true;
                    result.FollowUp = DriverChangeFollowUp::RestartWindows;
                    result.Message = res::GetString(L"CompositeNeedsRestart");

                    return result;
                }

                functionInstanceId = WaitForAudioFunction(device.ParentInstanceId);

                if (functionInstanceId.empty())
                {
                    DriverOperationResult result{};

                    result.Succeeded = true;
                    result.FollowUp = DriverChangeFollowUp::ReplugDevice;
                    result.Message = res::GetString(L"CompositeFunctionMissing");

                    return result;
                }
            }

            auto const outcome = driverstore::InstallDriverFromInf(functionInstanceId, package.DriverStoreInfPath);

            if (!outcome.Succeeded)
            {
                return FailedWithError(L"ErrorInstallFailedFormat", outcome.Error);
            }

            return Completed(functionInstanceId, outcome, package.PublishedName);
        }
        catch (...)
        {
            DRIVER_SELECTOR_LOG_GENERAL_EXCEPTION(L"Unable to switch a device to the low-latency driver.");
        }

        return Failed(L"ErrorUnexpected");
    }

    _Use_decl_annotations_
    DriverOperationResult UseWindowsDriver(AudioDevice const& device) noexcept
    {
        try
        {
            DEVINST devInst{ 0 };

            if (!device.FunctionInstanceId.empty() && LocateDevice(device.FunctionInstanceId, devInst))
            {
                auto const outcome = InstallCompatibleDriver(device.FunctionInstanceId, IsWindowsUsbAudio2Driver);

                if (!outcome.Succeeded)
                {
                    return outcome.Error == ERROR_NOT_FOUND ?
                        Failed(L"ErrorDriverNotAvailable") :
                        FailedWithError(L"ErrorInstallFailedFormat", outcome.Error);
                }

                return Completed(device.FunctionInstanceId, outcome, outcome.InstalledInfName);
            }

            // the manufacturer's driver has the whole device, so the whole device goes back
            if (!LocateDevice(device.ParentInstanceId, devInst))
            {
                return Failed(L"ErrorDeviceNotFound");
            }

            auto const outcome = InstallCompatibleDriver(device.ParentInstanceId, IsWindowsCompositeDriver);

            if (!outcome.Succeeded)
            {
                return outcome.Error == ERROR_NOT_FOUND ?
                    Failed(L"ErrorDriverNotAvailable") :
                    FailedWithError(L"ErrorInstallFailedFormat", outcome.Error);
            }

            return Completed(device.ParentInstanceId, outcome, outcome.InstalledInfName);
        }
        catch (...)
        {
            DRIVER_SELECTOR_LOG_GENERAL_EXCEPTION(L"Unable to switch a device to the Windows driver.");
        }

        return Failed(L"ErrorUnexpected");
    }

    _Use_decl_annotations_
    DriverOperationResult UseManufacturerDriver(AudioDevice const& device) noexcept
    {
        try
        {
            auto const packages = driverstore::FindDriverPackages(config::LowLatencyInfName, {});
            auto const manufacturer = ManufacturerDriverPredicate(packages);

            DEVINST devInst{ 0 };

            if (!device.FunctionInstanceId.empty() && LocateDevice(device.FunctionInstanceId, devInst) &&
                BestMatch(GetCompatibleDrivers(device.FunctionInstanceId), manufacturer).has_value())
            {
                auto const outcome = InstallCompatibleDriver(device.FunctionInstanceId, manufacturer);

                if (!outcome.Succeeded)
                {
                    return FailedWithError(L"ErrorInstallFailedFormat", outcome.Error);
                }

                return Completed(device.FunctionInstanceId, outcome, outcome.InstalledInfName);
            }

            if (!LocateDevice(device.ParentInstanceId, devInst))
            {
                return Failed(L"ErrorDeviceNotFound");
            }

            // The audio function disappears once the manufacturer's driver takes the whole
            // device, so it is the device that is checked afterwards.
            auto const outcome = InstallCompatibleDriver(device.ParentInstanceId, manufacturer);

            if (!outcome.Succeeded)
            {
                return outcome.Error == ERROR_NOT_FOUND ?
                    Failed(L"ErrorDriverNotAvailable") :
                    FailedWithError(L"ErrorInstallFailedFormat", outcome.Error);
            }

            return Completed(device.ParentInstanceId, outcome, outcome.InstalledInfName);
        }
        catch (...)
        {
            DRIVER_SELECTOR_LOG_GENERAL_EXCEPTION(L"Unable to switch a device to the manufacturer's driver.");
        }

        return Failed(L"ErrorUnexpected");
    }
}
