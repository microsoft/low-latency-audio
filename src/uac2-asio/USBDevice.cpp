// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================
// ASIO is a trademark and software of Steinberg Media Technologies GmbH

/*++

Module Name:

    UsbDevice.cpp

Abstract:

    This file communicates with device drivers that support USB devices.

Environment:

    ASIO Driver

--*/

#include "framework.h"
#include <devguid.h> // Device guids
#include <devioctl.h>
#include <ks.h>
#include <ksmedia.h>
#include <setupapi.h>
#include "USBDevice.h"
#include "print_.h"
#include <shlwapi.h>

static HANDLE OpenUsbDeviceCore(
    _In_ const LPGUID      classGuid,
    _In_ const TCHAR *     /* serviceName */,
    _In_ const TCHAR *     referenceString,
    _In_opt_ const TCHAR * desiredPath
)
{
    BOOL result = FALSE;

    HDEVINFO deviceInfo = SetupDiGetClassDevs(classGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (deviceInfo == INVALID_HANDLE_VALUE)
    {
        return INVALID_HANDLE_VALUE;
    }

    for (DWORD index = 0;; index++)
    {
        SP_DEVICE_INTERFACE_DATA deviceInterfaceData;
        deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

        result = SetupDiEnumDeviceInterfaces(deviceInfo, nullptr, classGuid, index, &deviceInterfaceData);

        if (!result)
        {
            if (ERROR_NO_MORE_ITEMS != GetLastError())
            {
                goto OpenUsbDeviceCore_Error;
            }
            else
            {
                break;
            }
        }

        BOOL            success = TRUE;
        DWORD           deviceInterfaceDetailDataSize = 0;
        TCHAR           serviceName[128];
        SP_DEVINFO_DATA deviceInfoData;
        deviceInfoData.cbSize = sizeof(deviceInfoData);

        success = SetupDiGetDeviceInterfaceDetail(deviceInfo, &deviceInterfaceData, nullptr, 0, &deviceInterfaceDetailDataSize, &deviceInfoData);
        // Check for error.
        if ((GetLastError() != ERROR_INSUFFICIENT_BUFFER) || (!deviceInterfaceDetailDataSize))
        {
            SetupDiDestroyDeviceInfoList(deviceInfo);
            break;
        }

        if (SetupDiGetDeviceRegistryProperty(deviceInfo, &deviceInfoData, SPDRP_SERVICE, nullptr, (PBYTE)serviceName, sizeof(serviceName), nullptr))
        {
            if (_tcsicmp(serviceName, serviceName) == 0)
            {
                // Allocate the buffer for the device interface detail data.
                PSP_DEVICE_INTERFACE_DETAIL_DATA deviceInterfaceDetailData = nullptr;

                deviceInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA) new BYTE[deviceInterfaceDetailDataSize];

                if (!deviceInterfaceDetailData)
                {
                    break;
                }
                (deviceInterfaceDetailData)->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

                if (SetupDiGetDeviceInterfaceDetail(deviceInfo, &deviceInterfaceData, deviceInterfaceDetailData, deviceInterfaceDetailDataSize, nullptr, nullptr))
                {
                    info_print_("compare %s, %s\n", deviceInterfaceDetailData->DevicePath, referenceString);
                    if (StrStrI(deviceInterfaceDetailData->DevicePath, referenceString) != nullptr)
                    {
                        if ((desiredPath == nullptr) || ((desiredPath != nullptr && _tcsicmp(deviceInterfaceDetailData->DevicePath, desiredPath) == 0)))
                        {
                            HANDLE targetHandle = CreateFile(deviceInterfaceDetailData->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);

                            if (targetHandle != INVALID_HANDLE_VALUE)
                            {
                                UAC_AUDIO_PROPERTY audioProp;
                                result = GetAudioProperty(targetHandle, &audioProp);
                                if (result && (audioProp.OutputAsioChannels != 0))
                                {
                                    info_print_("successfully opened %s\n", deviceInterfaceDetailData->DevicePath);
                                    delete[] (BYTE *)deviceInterfaceDetailData;
                                    deviceInterfaceDetailData = nullptr;
                                    SetupDiDestroyDeviceInfoList(deviceInfo);
                                    deviceInfo = INVALID_HANDLE_VALUE;
                                    return targetHandle;
                                }
                                else
                                {
                                    CloseHandle(targetHandle);
                                    targetHandle = INVALID_HANDLE_VALUE;
                                }
                            }
                        }
                    }
                }
                if (deviceInterfaceDetailData != nullptr)
                {
                    delete[] (BYTE *)deviceInterfaceDetailData;
                    deviceInterfaceDetailData = nullptr;
                }
            }
        }
    }

OpenUsbDeviceCore_Error:
    if (deviceInfo != INVALID_HANDLE_VALUE)
    {
        SetupDiDestroyDeviceInfoList(deviceInfo);
        deviceInfo = INVALID_HANDLE_VALUE;
    }

    return INVALID_HANDLE_VALUE;
}

_Use_decl_annotations_
HANDLE OpenUsbDevice(
    const LPGUID  classGuid,
    const TCHAR * serviceName,
    const TCHAR * referenceString,
    const TCHAR * desiredPath
)
{
    HANDLE targetHandle = OpenUsbDeviceCore(classGuid, serviceName, referenceString, desiredPath);

    if ((targetHandle == INVALID_HANDLE_VALUE) && (desiredPath != nullptr))
    {
        // force open
        targetHandle = OpenUsbDeviceCore(classGuid, serviceName, referenceString, nullptr);
    }
    return targetHandle;
}

_Use_decl_annotations_
BOOL GetAudioProperty(
    HANDLE               deviceHandle,
    UAC_AUDIO_PROPERTY * audioProperty
)
{
    BOOL       result = FALSE;
    KSPROPERTY privateProperty{};
    ULONG      bytesReturned = 0;

    privateProperty.Set = KSPROPSETID_LowLatencyAudio;
    privateProperty.Flags = KSPROPERTY_TYPE_GET;
    privateProperty.Id = toInt(KsPropertyUACLowLatencyAudio::GetAudioProperty);

    result = DeviceIoControl(deviceHandle, IOCTL_KS_PROPERTY, &privateProperty, sizeof(KSPROPERTY), audioProperty, sizeof(UAC_AUDIO_PROPERTY), &bytesReturned, nullptr);

    return result;
}

_Use_decl_annotations_
BOOL GetChannelInfo(
    HANDLE                          deviceHandle,
    PUAC_GET_CHANNEL_INFO_CONTEXT * channelInfo
)
{
    BOOL       result = FALSE;
    KSPROPERTY privateProperty{};
    ULONG      bytesReturned = 0;

    if (channelInfo == nullptr)
    {
        return result;
    }

    *channelInfo = {};

    privateProperty.Set = KSPROPSETID_LowLatencyAudio;
    privateProperty.Flags = KSPROPERTY_TYPE_GET;
    privateProperty.Id = toInt(KsPropertyUACLowLatencyAudio::GetChannelInfo);

    result = DeviceIoControl(deviceHandle, IOCTL_KS_PROPERTY, &privateProperty, sizeof(KSPROPERTY), nullptr, 0, &bytesReturned, nullptr);

    if (!result)
    {
        DWORD error = GetLastError();
        if ((error == ERROR_MORE_DATA) && (bytesReturned >= sizeof(UAC_GET_CHANNEL_INFO_CONTEXT)))
        {
            *channelInfo = (PUAC_GET_CHANNEL_INFO_CONTEXT)(new BYTE[bytesReturned]);
            if (channelInfo != nullptr)
            {
                result = DeviceIoControl(deviceHandle, IOCTL_KS_PROPERTY, &privateProperty, sizeof(KSPROPERTY), *channelInfo, bytesReturned, &bytesReturned, nullptr);
                if (!result)
                {
                    delete[] (BYTE *)(*channelInfo);
                    *channelInfo = nullptr;
                }
            }
            else
            {
                result = FALSE;
            }
        }
    }

    return result;
}

_Use_decl_annotations_
BOOL GetClockInfo(
    HANDLE                        deviceHandle,
    PUAC_GET_CLOCK_INFO_CONTEXT * clockInfo
)
{
    BOOL       result = FALSE;
    KSPROPERTY privateProperty{};
    ULONG      bytesReturned = 0;

    if (clockInfo == nullptr)
    {
        return result;
    }

    *clockInfo = {};

    privateProperty.Set = KSPROPSETID_LowLatencyAudio;
    privateProperty.Flags = KSPROPERTY_TYPE_GET;
    privateProperty.Id = toInt(KsPropertyUACLowLatencyAudio::GetClockInfo);

    result = DeviceIoControl(deviceHandle, IOCTL_KS_PROPERTY, &privateProperty, sizeof(KSPROPERTY), nullptr, 0, &bytesReturned, nullptr);

    if (!result)
    {
        DWORD error = GetLastError();
        if ((error == ERROR_MORE_DATA) && (bytesReturned >= sizeof(UAC_GET_CLOCK_INFO_CONTEXT)))
        {
            *clockInfo = (PUAC_GET_CLOCK_INFO_CONTEXT)(new BYTE[bytesReturned]);
            if (clockInfo != nullptr)
            {
                result = DeviceIoControl(deviceHandle, IOCTL_KS_PROPERTY, &privateProperty, sizeof(KSPROPERTY), *clockInfo, bytesReturned, &bytesReturned, nullptr);
                if (!result)
                {
                    delete[] (BYTE *)(*clockInfo);
                    *clockInfo = nullptr;
                }
            }
            else
            {
                result = FALSE;
            }
        }
        else
        {
            result = FALSE;
        }
    }

    return result;
}

_Use_decl_annotations_
BOOL SetClockSource(
    HANDLE deviceHandle,
    ULONG  index
)
{
    BOOL                         result = FALSE;
    KSPROPERTY                   privateProperty{};
    ULONG                        bytesReturned = 0;
    UAC_SET_CLOCK_SOURCE_CONTEXT setClockSourceContext = {index};

    privateProperty.Set = KSPROPSETID_LowLatencyAudio;
    privateProperty.Flags = KSPROPERTY_TYPE_SET;
    privateProperty.Id = toInt(KsPropertyUACLowLatencyAudio::SetClockSource);

    result = DeviceIoControl(deviceHandle, IOCTL_KS_PROPERTY, &privateProperty, sizeof(KSPROPERTY), &setClockSourceContext, sizeof(UAC_SET_CLOCK_SOURCE_CONTEXT), &bytesReturned, nullptr);

    return result;
}

_Use_decl_annotations_
BOOL SetFlags(
    HANDLE                        deviceHandle,
    const UAC_SET_FLAGS_CONTEXT & flags
)
{
    BOOL                  result = FALSE;
    KSPROPERTY            privateProperty{};
    ULONG                 bytesReturned = 0;
    UAC_SET_FLAGS_CONTEXT setFlagsContext = flags;

    privateProperty.Set = KSPROPSETID_LowLatencyAudio;
    privateProperty.Flags = KSPROPERTY_TYPE_SET;
    privateProperty.Id = toInt(KsPropertyUACLowLatencyAudio::SetFlags);

    result = DeviceIoControl(deviceHandle, IOCTL_KS_PROPERTY, &privateProperty, sizeof(KSPROPERTY), &setFlagsContext, sizeof(UAC_SET_FLAGS_CONTEXT), &bytesReturned, nullptr);

    return result;
}

_Use_decl_annotations_
BOOL SetSampleFormat(
    HANDLE deviceHandle,
    ULONG  sampleFormat
)
{
    BOOL       result = FALSE;
    KSPROPERTY privateProperty{};
    ULONG      bytesReturned = 0;

    privateProperty.Set = KSPROPSETID_LowLatencyAudio;
    privateProperty.Flags = KSPROPERTY_TYPE_SET;
    privateProperty.Id = toInt(KsPropertyUACLowLatencyAudio::SetSampleFormat);

    result = DeviceIoControl(deviceHandle, IOCTL_KS_PROPERTY, &privateProperty, sizeof(KSPROPERTY), &sampleFormat, sizeof(ULONG), &bytesReturned, nullptr);

    return result;
}

_Use_decl_annotations_
BOOL ChangeSampleRate(
    HANDLE deviceHandle,
    ULONG  sampleRate
)
{
    BOOL       result = FALSE;
    KSPROPERTY privateProperty{};
    ULONG      bytesReturned = 0;

    privateProperty.Set = KSPROPSETID_LowLatencyAudio;
    privateProperty.Flags = KSPROPERTY_TYPE_SET;
    privateProperty.Id = toInt(KsPropertyUACLowLatencyAudio::ChangeSampleRate);

    result = DeviceIoControl(deviceHandle, IOCTL_KS_PROPERTY, &privateProperty, sizeof(KSPROPERTY), &sampleRate, sizeof(ULONG), &bytesReturned, nullptr);

    return result;
}

_Use_decl_annotations_
BOOL GetAsioOwnership(
    HANDLE deviceHandle
)
{
    BOOL       result = FALSE;
    KSPROPERTY privateProperty{};
    ULONG      bytesReturned = 0;

    privateProperty.Set = KSPROPSETID_LowLatencyAudio;
    privateProperty.Flags = KSPROPERTY_TYPE_SET;
    privateProperty.Id = toInt(KsPropertyUACLowLatencyAudio::GetAsioOwnership);

    result = DeviceIoControl(deviceHandle, IOCTL_KS_PROPERTY, &privateProperty, sizeof(KSPROPERTY), nullptr, 0, &bytesReturned, nullptr);

    return result;
}

_Use_decl_annotations_
BOOL StartAsioStream(
    HANDLE deviceHandle
)
{
    BOOL       result = FALSE;
    KSPROPERTY privateProperty{};
    ULONG      bytesReturned = 0;

    privateProperty.Set = KSPROPSETID_LowLatencyAudio;
    privateProperty.Flags = KSPROPERTY_TYPE_SET;
    privateProperty.Id = toInt(KsPropertyUACLowLatencyAudio::StartAsioStream);

    result = DeviceIoControl(deviceHandle, IOCTL_KS_PROPERTY, &privateProperty, sizeof(KSPROPERTY), nullptr, 0, &bytesReturned, nullptr);

    return result;
}

_Use_decl_annotations_
BOOL StopAsioStream(
    HANDLE deviceHandle
)
{
    BOOL       result = FALSE;
    KSPROPERTY privateProperty{};
    ULONG      bytesReturned = 0;

    privateProperty.Set = KSPROPSETID_LowLatencyAudio;
    privateProperty.Flags = KSPROPERTY_TYPE_SET;
    privateProperty.Id = toInt(KsPropertyUACLowLatencyAudio::StopAsioStream);

    result = DeviceIoControl(deviceHandle, IOCTL_KS_PROPERTY, &privateProperty, sizeof(KSPROPERTY), nullptr, 0, &bytesReturned, nullptr);

    return result;
}

_Use_decl_annotations_
BOOL SetAsioBuffer(
    HANDLE  deviceHandle,
    UCHAR * driverPlayBufferWithKsProperty,
    ULONG   driverPlayBufferWithKsPropertySize,
    UCHAR * driverRecBuffer,
    ULONG   driverRecBufferSize
)
{
    BOOL        result = FALSE;
    PKSPROPERTY privateProperty = (PKSPROPERTY)driverPlayBufferWithKsProperty;
    ULONG       bytesReturned = 0;

    if (driverPlayBufferWithKsPropertySize > sizeof(KSPROPERTY))
    {
        privateProperty->Set = KSPROPSETID_LowLatencyAudio;
        privateProperty->Flags = KSPROPERTY_TYPE_SET;
        privateProperty->Id = toInt(KsPropertyUACLowLatencyAudio::SetAsioBuffer);

        result = DeviceIoControl(deviceHandle, IOCTL_KS_PROPERTY, privateProperty, driverPlayBufferWithKsPropertySize, driverRecBuffer, driverRecBufferSize, &bytesReturned, nullptr);
    }

    return result;
}

_Use_decl_annotations_
BOOL UnsetAsioBuffer(
    HANDLE deviceHandle
)
{
    BOOL       result = FALSE;
    KSPROPERTY privateProperty{};
    ULONG      bytesReturned = 0;

    privateProperty.Set = KSPROPSETID_LowLatencyAudio;
    privateProperty.Flags = KSPROPERTY_TYPE_SET;
    privateProperty.Id = toInt(KsPropertyUACLowLatencyAudio::UnsetAsioBuffer);

    result = DeviceIoControl(deviceHandle, IOCTL_KS_PROPERTY, &privateProperty, sizeof(KSPROPERTY), nullptr, 0, &bytesReturned, nullptr);

    return result;
}

_Use_decl_annotations_
BOOL ReleaseAsioOwnership(
    HANDLE deviceHandle
)
{
    BOOL       result = FALSE;
    KSPROPERTY privateProperty{};
    ULONG      bytesReturned = 0;

    privateProperty.Set = KSPROPSETID_LowLatencyAudio;
    privateProperty.Flags = KSPROPERTY_TYPE_SET;
    privateProperty.Id = toInt(KsPropertyUACLowLatencyAudio::ReleaseAsioOwnership);

    result = DeviceIoControl(deviceHandle, IOCTL_KS_PROPERTY, &privateProperty, sizeof(KSPROPERTY), nullptr, 0, &bytesReturned, nullptr);

    return result;
}
