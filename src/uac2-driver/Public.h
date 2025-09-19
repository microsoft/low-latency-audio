// Copyright (c) Microsoft Corporation.
// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================
// ASIO is a trademark and software of Steinberg Media Technologies GmbH

/*++

Module Name:

    Public.h

Abstract:

    This module contains the common declarations shared by driver
    and user applications.

Environment:

    user and kernel

--*/

#ifndef _PUBLIC_H_
#define _PUBLIC_H_

/* make prototypes usable from C++ */
#ifdef __cplusplus
EXTERN_C_START
#endif

#include <initguid.h>
#include <ntddk.h>
#include <ntstrsafe.h>
#include <ntintsafe.h>
#include "Trace.h"

#include <wdf.h>
#include <acx.h>

#ifndef PAGED_CODE_SEG
#define INIT_CODE_SEG     __declspec(code_seg("INIT"))
#define PAGED_CODE_SEG    __declspec(code_seg("PAGE"))
#define NONPAGED_CODE_SEG __declspec(code_seg("NOPAGE"))
#endif

#define UAC_MAX_SAMPLE_FREQUENCY             768000
#define UAC_MIN_SAMPLE_FREQUENCY             11025

#define UAC_MAX_CLASSIC_FRAMES_PER_IRP       32
#define UAC_MIN_CLASSIC_FRAMES_PER_IRP       1
#define UAC_MIN_MAX_IRP_NUMBER               2
#define UAC_MAX_PRE_SEND_FRAMES              1000
#define UAC_MAX_OUTPUT_FRAME_DELAY           16
#define UAC_MIN_OUTPUT_FRAME_DELAY           0
#define UAC_MAX_DELAYED_OUTPUT_BUFFER_SWITCH 1
#define UAC_MAX_BUFFER_OPERATION_THREAD      31
#define UAC_MAX_IN_BUFFER_OPERATION_OFFSET   128
#define UAC_MAX_OUT_BUFFER_OPERATION_OFFSET  512
#define UAC_MAX_DROPOUT_DETECTION            1
#define UAC_MAX_USE_DEVICE_SAMPLE_FORMAT     1

#define UAC_MAX_ASIO_CHANNEL                 64
#define UAC_MAX_CLOCK_SOURCE                 32

#define UAC_11025HZ_SUPPORTED                0x00000001
#define UAC_22050HZ_SUPPORTED                0x00000002
#define UAC_32000HZ_SUPPORTED                0x00000004
#define UAC_44100HZ_SUPPORTED                0x00000008
#define UAC_48000HZ_SUPPORTED                0x00000010
#define UAC_88200HZ_SUPPORTED                0x00000020
#define UAC_96000HZ_SUPPORTED                0x00000040
#define UAC_176400HZ_SUPPORTED               0x00000080
#define UAC_192000HZ_SUPPORTED               0x00000100
#define UAC_352800HZ_SUPPORTED               0x00000200
#define UAC_384000HZ_SUPPORTED               0x00000400
#define UAC_705600HZ_SUPPORTED               0x00000800
#define UAC_768000HZ_SUPPORTED               0x00001000

#define UAC_MAX_SERIAL_NUMBER_LENGTH         128
#define UAC_MAX_CHANNEL_NAME_LENGTH          32
#define UAC_MAX_CLOCK_SOURCE_NAME_LENGTH     32
#define UAC_MAX_DETECTED_ERROR               9

#define UAC_SUB_DEVICE_NAME_LENGTH           32

#define UAC_DEVICE_STATUS_BANDWIDTH          0x00000001
#define UAC_DEVICE_STATUS_BUS_ERROR          0x00000002

#define DSD_ZERO_BYTE                        0x96
#define DSD_ZERO_WORD                        0x9696

#define AUDIO_CONTROL_DIRECTION_IN           0
#define AUDIO_CONTROL_DIRECTION_OUT          1

#define AC_NOTIFICATION_SAMPLE_RATE_CHANGED  1
#define AC_NOTIFICATION_CLOCK_SOURCE_CHANGED 2
#define AC_NOTIFICATION_FORCE_RESET_STREAM   4
#define AC_NOTIFICATION_LATENCY_CHANGED      8

#define AC_NOTIFICATION_IRP_NUMBER           2

//
// Define an Interface Guid so that app can find the device and talk to it.
//

// Number of msec for idle timeout.
#define IDLE_POWER_TIMEOUT                   5000

//
// Define CODEC device context.
//
// Do not use CODEC_DEVICE_CONTEXT, use DEVICE_CONTEXT.
// typedef struct _CODEC_DEVICE_CONTEXT
// {
//     ACXCIRCUIT    Render;
//     ACXCIRCUIT    Capture;
//     WDF_TRI_STATE ExcludeD3Cold;
// } CODEC_DEVICE_CONTEXT, *PCODEC_DEVICE_CONTEXT;

//
// Codec driver prototypes.
//
// EVT_WDF_DRIVER_DEVICE_ADD Codec_EvtBusDeviceAdd;
// DRIVER_INITIALIZE         DriverEntry;
// EVT_WDF_DRIVER_UNLOAD     AudioCodecDriverUnload;

//
// Codec device callbacks.
//
// EVT_WDF_DEVICE_PREPARE_HARDWARE Codec_EvtDevicePrepareHardware;
// EVT_WDF_DEVICE_RELEASE_HARDWARE Codec_EvtDeviceReleaseHardware;
// EVT_WDF_DEVICE_D0_ENTRY         Codec_EvtDeviceD0Entry;
// EVT_WDF_DEVICE_D0_EXIT          Codec_EvtDeviceD0Exit;
// EVT_WDF_DEVICE_CONTEXT_CLEANUP  Codec_EvtDeviceContextCleanup;

// WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(CODEC_DEVICE_CONTEXT, GetCodecDeviceContext)

/* make internal prototypes usable from C++ */
#ifdef __cplusplus
EXTERN_C_END
#endif

PAGED_CODE_SEG
NTSTATUS
CodecR_AddStaticRender(
    _In_ WDFDEVICE              Device,
    _In_ const GUID *           ComponentGuid,
    _In_ const UNICODE_STRING * CircuitName
);

PAGED_CODE_SEG
NTSTATUS
CodecC_AddStaticCapture(
    _In_ WDFDEVICE              Device,
    _In_ const GUID *           ComponentGuid,
    _In_ const GUID *           MicCustomName,
    _In_ const UNICODE_STRING * CircuitName
);

PAGED_CODE_SEG
NTSTATUS
CodecC_CircuitCleanup(
    _In_ ACXCIRCUIT Device
);

#endif // #ifndef _PUBLIC_H_
