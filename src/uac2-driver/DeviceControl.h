// Copyright (c) Microsoft Corporation.
// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================



/*++

Module Name:

    DeviceControl.h

Abstract:

    Defines control transfers for USB device.

Environment:

    Kernel-mode Driver Framework

--*/

#ifndef _DEVICE_CONTROL_H_
#define _DEVICE_CONTROL_H_

#include <windef.h>
#include <ks.h>

#include "public.h"
#include "UAC_User.h"
#include "USBAudio.h"

EXTERN_C_START
#include "usbdi.h"
#include "usbdlib.h"
#include <wdfusb.h>

EXTERN_C_END

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS ControlRequestGetSampleFrequency(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           interfaceNumber,
    _In_ UCHAR           entityID,
    _In_ ULONG &         sampleRate
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS ControlRequestSetSampleFrequency(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           interfaceNumber,
    _In_ UCHAR           entityID,
    _In_ ULONG           desiredSampleRate
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS ControlRequestGetSampleFrequencyRange(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           interfaceNumber,
    _In_ UCHAR           entityID,
    _Out_ WDFMEMORY &    memory,
    _Out_ NS_USBAudio0200::PCONTROL_RANGE_PARAMETER_BLOCK_LAYOUT3 & parameterBlock
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS ControlRequestGetClockSelector(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           interfaceNumber,
    _In_ UCHAR           entityID,
    _In_ UCHAR &         clockSelectorIndex
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS ControlRequestSetClockSelector(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           interfaceNumber,
    _In_ UCHAR           entityID,
    _In_ UCHAR           clockSelectorIndex
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS
ControlRequestGetACTValAltSettingsControl(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           interfaceNumber,
    _Inout_ ULONG &      validAlternateSettingMap
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS
ControlRequestGetACTAltSettingsControl(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           interfaceNumber,
    _Inout_ UCHAR &      activeAlternateSetting
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS
ControlRequestGetAudioDataFormat(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           interfaceNumber,
    _Inout_ ULONG &      audioDataFormat
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS
ControlRequestSetAudioDataFormat(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           interfaceNumber,
    _Inout_ ULONG        audioDataFormat
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS ControlRequestGetMute(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           interfaceNumber,
    _In_ UCHAR           entityID,
    _In_ UCHAR           channel,
    _In_ bool &          mute
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS ControlRequestSetMute(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           interfaceNumber,
    _In_ UCHAR           entityID,
    _In_ UCHAR           channel,
    _In_ bool            mute
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS ControlRequestGetVolume(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           interfaceNumber,
    _In_ UCHAR           entityID,
    _In_ UCHAR           channel,
    _In_ USHORT &        volume
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS ControlRequestSetVolume(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           interfaceNumber,
    _In_ UCHAR           entityID,
    _In_ UCHAR           channel,
    _In_ USHORT          volume
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS ControlRequestGetVolumeRange(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           interfaceNumber,
    _In_ UCHAR           entityID,
    _In_ UCHAR           channel,
    _Out_ WDFMEMORY &    memory,
    _Out_ NS_USBAudio0200::PCONTROL_RANGE_PARAMETER_BLOCK_LAYOUT2 & parameterBlock
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS ControlRequestGetAudioDataFormat(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           interfaceNumber,
    _In_ UCHAR           entityID,
    _In_ UCHAR           channel,
    _In_ ULONG &         format
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS ControlRequestGetAutoGainControl(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           interfaceNumber,
    _In_ UCHAR           entityID,
    _In_ UCHAR           channel,
    _In_ bool &          autoGain
);

__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS ControlRequestSetAutoGainControl(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           interfaceNumber,
    _In_ UCHAR           entityID,
    _In_ UCHAR           channel,
    _In_ bool            autoGain
);

#if 0
// UAC 1.0 only
__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS
ControlRequestSetSampleRate(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           endpointAddress,
    _In_ ULONG           desiredSampleRate
);

// UAC 1.0 only
__drv_maxIRQL(PASSIVE_LEVEL)
PAGED_CODE_SEG
NTSTATUS
ControlRequestGetSampleRate(
    _In_ PDEVICE_CONTEXT deviceContext,
    _In_ UCHAR           endpointAddress,
    _Inout_ ULONG &      currentSampleRate
);
#endif

#endif
