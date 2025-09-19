/*++

  Copyright(C) 2024 Yamaha Corporation
  Licensed under the MIT License
  ============================================================================
  This is part of the Microsoft Low-Latency Audio driver project.
  Further information: https://aka.ms/asio
  ============================================================================
  ASIO is a trademark and software of Steinberg Media Technologies GmbH


Module Name:

    UAC_User.h

Abstract:

    Define the necessary definitions for communication between user space and kernel space.

Environment:

    Both kernel and user mode

--*/

#ifndef _UAC_USER_H_
#define _UAC_USER_H_

#include <initguid.h>

#define UAC_MAX_PRODUCT_NAME_LENGTH              128
#define UAC_MAX_SERIAL_NUMBER_LENGTH             128
#define UAC_MAX_CHANNEL_NAME_LENGTH              32
#define UAC_MAX_CLOCK_SOURCE_NAME_LENGTH         32

#define UAC_DEFAULT_SAMPLE_RATE                  44100
#define UAC_DEFAULT_FIRST_PACKET_LATENCY         20
#define UAC_DEFAULT_PRE_SEND_FRAMES              0
#define UAC_DEFAULT_OUTPUT_FRAME_DELAY           0
#define UAC_DEFAULT_DELAYED_OUTPUT_BUFFER_SWITCH 0
#define UAC_DEFAULT_ASIO_BUFFER_SIZE             512
#define UAC_DEFAULT_IN_BUFFER_OPERATION_OFFSET   0x90000000
#define UAC_DEFAULT_IN_HUB_OFFSET                0
#define UAC_DEFAULT_OUT_HUB_OFFSET               0
#define UAC_DEFAULT_DROPOUT_DETECTION            1
#define UAC_DEFAULT_BUFFER_THREAD_PRIORITY       30

#if defined(_M_ARM64EC) || defined(_M_ARM64)
#define UAC_DEFAULT_CLASSIC_FRAMES_PER_IRP      4
#define UAC_DEFAULT_MAX_IRP_NUMBER              8
#define UAC_DEFAULT_OUT_BUFFER_OPERATION_OFFSET 0x90000010
#else
#define UAC_DEFAULT_CLASSIC_FRAMES_PER_IRP      4
#define UAC_DEFAULT_MAX_IRP_NUMBER              4
#define UAC_DEFAULT_OUT_BUFFER_OPERATION_OFFSET 0x90000004
#endif

#define UAC_MAX_ASIO_PERIOD_SAMPLES 8192
#define UAC_MIN_ASIO_PERIOD_SAMPLES 8
#define UAC_MAX_ASIO_CHANNELS       64
#define UAC_MIN_ASIO_CHANNELS       1

enum class UACSampleFormat : ULONG
{
    UAC_SAMPLE_FORMAT_PCM = 0, // FORMAT_TYPE_I
    UAC_SAMPLE_FORMAT_DSD_SINGLE = 1,
    UAC_SAMPLE_FORMAT_DSD_DOUBLE = 2,
    UAC_SAMPLE_FORMAT_DSD_NATIVE = 3,
    UAC_SAMPLE_FORMAT_PCM8 = 4,                     // FORMAT_TYPE_I
    UAC_SAMPLE_FORMAT_IEEE_FLOAT = 5,               // FORMAT_TYPE_I
    UAC_SAMPLE_FORMAT_IEC61937_AC_3 = 6,            // FORMAT_TYPE_III
    UAC_SAMPLE_FORMAT_IEC61937_MPEG_2_AAC_ADTS = 7, // FORMAT_TYPE_III
    UAC_SAMPLE_FORMAT_IEC61937_DTS_I = 8,           // FORMAT_TYPE_III
    UAC_SAMPLE_FORMAT_IEC61937_DTS_II = 9,          // FORMAT_TYPE_III
    UAC_SAMPLE_FORMAT_IEC61937_DTS_III = 10,        // FORMAT_TYPE_III
    UAC_SAMPLE_FORMAT_TYPE_III_WMA = 11,            // FORMAT_TYPE_III
    UAC_SAMPLE_FORMAT_LAST_ENTRY
};

constexpr ULONG toULong(UACSampleFormat sampleFormat)
{
    return static_cast<ULONG>(sampleFormat);
}

// User - Kernel For version check
#define UAC_KERNEL_DRIVER_VERSION 0x00010000
#define UAC_ASIO_DRIVER_VERSION   0x00010000

enum class DeviceStatuses
{
    ResetRequired = 1 << 0,      //  UAC_DEVICE_STATUS_RESET_REQUIRED       0x00000001
    SampleRateChanged = 1 << 1,  //  UAC_DEVICE_STATUS_SAMPLE_RATE_CHANGED  0x00000002
    ClockSourceChanged = 1 << 2, //  UAC_DEVICE_STATUS_CLOCK_SOURCE_CHANGED 0x00000004
    OverloadDetected = 1 << 3,   //  UAC_DEVICE_STATUS_OVERLOAD_DETECTED    0x00000008
    LatencyChanged = 1 << 4,     //  UAC_DEVICE_STATUS_LATENCY_CHANGED      0x00000010
};

constexpr int toInt(DeviceStatuses Status)
{
    return static_cast<int>(Status);
}

// {016AF08F-F499-4637-B7A5-AFC01C86276F}
DEFINE_GUID(KSPROPSETID_LowLatencyAudio, 0x16af08f, 0xf499, 0x4637, 0xb7, 0xa5, 0xaf, 0xc0, 0x1c, 0x86, 0x27, 0x6f);

enum class KsPropertyUACLowLatencyAudio
{
    GetAudioProperty,
    GetChannelInfo,
    GetClockInfo,
    GetLatencyOffsetOfSampleRate,
    SetClockSource,
    SetFlags,
    SetSampleFormat,
    ChangeSampleRate,
    GetAsioOwnership,
    StartAsioStream,
    StopAsioStream,
    SetAsioBuffer,
    UnsetAsioBuffer,
    ReleaseAsioOwnership
};

constexpr int toInt(KsPropertyUACLowLatencyAudio Property)
{
    return static_cast<int>(Property);
}

enum class UserThreadStatuses
{
    OutputReady = (1 << 0),
    BufferStart = (1 << 1),
    BufferEnd = (1 << 2),
    OutputReadyDelay = (1 << 3)
};

constexpr int toInt(UserThreadStatuses Statuses)
{
    return static_cast<int>(Statuses);
}

enum class UACSampleType : ULONG
{
    UACSTInt16MSB = 0,
    UACSTInt24MSB = 1,
    UACSTInt32MSB = 2,
    UACSTFloat32MSB = 3,
    UACSTFloat64MSB = 4,
    UACSTInt32MSB16 = 8,
    UACSTInt32MSB18 = 9,
    UACSTInt32MSB20 = 10,
    UACSTInt32MSB24 = 11,
    UACSTInt16LSB = 16,
    UACSTInt24LSB = 17,
    UACSTInt32LSB = 18,
    UACSTFloat32LSB = 19,
    UACSTFloat64LSB = 20,
    UACSTInt32LSB16 = 24,
    UACSTInt32LSB18 = 25,
    UACSTInt32LSB20 = 26,
    UACSTInt32LSB24 = 27,
    UACSTDSDInt8LSB1 = 32,
    UACSTDSDInt8MSB1 = 33,
    UACSTDSDInt8NER8 = 40,
    UACSTLastEntry
};

constexpr int toInt(UACSampleType sampleType)
{
    return static_cast<int>(sampleType);
}

typedef struct _UAC_AUDIO_PROPERTY
{
    USHORT VendorId;                                 // Vendor ID obtained from USB
    USHORT ProductId;                                // Product ID obtained from USB
    USHORT DeviceRelease;                            // Device Release Number obtained from USB
    ULONG  PacketsPerSec;                            // ISO (Micro) Frames per second
    WCHAR  ProductName[UAC_MAX_PRODUCT_NAME_LENGTH]; // iProduct string obtained from USB
    ULONG  SampleRate;                               // Current sampling frequency
    ULONG  SamplesPerPacket;                         // Number of samples per ISO Frame (truncated)
    ULONG  SupportedSampleRate;                      // Supported sampling frequencies (bitmask)
    ULONG  AsioDriverVersion;                        // Current ASIO driver version
    ULONG  AsioBufferPeriod;                         // ASIO buffer size

    UACSampleType   SampleType;                      // Sample type (ASIO compliant)
    ULONG           SupportedSampleFormats;          //
    UACSampleFormat CurrentSampleFormat;             //

    ULONG          InputAsioChannels;                // Number of input channels
    UCHAR          InputInterfaceNumber;             // Currently selected input interface number
    UCHAR          InputAlternateSetting;            // Currently selected input alternate setting number
    UCHAR          InputEndpointNumber;              // Currently selected input endpoint number
    ULONG          InputBytesPerBlock;               // Bytes per block for input (usually InChannels * BytesPerSample)
    ULONG          InputMaxSamplesPerPacket;         // Number of frames transferable per microframe for input
    LONG           InputLatencyOffset;               // Input latency compensation
    ULONG          InputFormatType;
    ULONG          InputFormat;
    ULONG          InputBytesPerSample;              // Bytes per sample
    ULONG          InputValidBitsPerSample;          // Valid bits per sample
    volatile ULONG InputMeasuredSampleRate;          // Measured input sampling rate (1-second average)
    ULONG          InputDeviceLatency;
    ULONG          InputDriverBuffer;                //

    ULONG          OutputAsioChannels;               // Number of output channels
    UCHAR          OutputInterfaceNumber;            // Currently selected output interface number
    UCHAR          OutputAlternateSetting;           // Currently selected output alternate setting number
    UCHAR          OutputEndpointNumber;             // Currently selected output endpoint number
    ULONG          OutputFormatType;
    ULONG          OutputFormat;
    ULONG          OutputBytesPerBlock;              // Bytes per block for output (usually OutChannels * BytesPerSample)
    ULONG          OutputMaxSamplesPerPacket;        // Number of frames transferable per microframe for output
    LONG           OutputLatencyOffset;              // Output latency compensation
    ULONG          OutputBytesPerSample;             // Bytes per sample
    ULONG          OutputValidBitsPerSample;         // Valid bits per sample
    volatile ULONG OutputMeasuredSampleRate;         // Measured output sampling rate (1-second average)
    ULONG          OutputDeviceLatency;
    ULONG          OutputDriverBuffer;               //

    UCHAR   AudioControlInterfaceNumber;             // Audio Control interface number
    ULONG   ClockSources;                            //
    BOOLEAN IsAccessible;
} UAC_AUDIO_PROPERTY, *PUAC_AUDIO_PROPERTY;

typedef struct UAC_CHANNEL_INFO_
{
    LONG  Index;
    BOOL  IsInput;
    BOOL  IsActive;
    LONG  ChannelGroup;
    WCHAR Name[UAC_MAX_CHANNEL_NAME_LENGTH];
} UAC_CHANNEL_INFO, *PUAC_CHANNEL_INFO;

typedef struct UAC_GET_CHANNEL_INFO_CONTEXT_
{
    ULONG            NumChannels;
    UAC_CHANNEL_INFO Channel[1];
} UAC_GET_CHANNEL_INFO_CONTEXT, *PUAC_GET_CHANNEL_INFO_CONTEXT;

typedef struct UAC_CLOCK_INFO_
{
    LONG  Index;
    LONG  AssociatedChannel;
    LONG  AssociatedGroup;
    BOOL  IsCurrentSource;
    BOOL  IsLocked;
    WCHAR Name[UAC_MAX_CLOCK_SOURCE_NAME_LENGTH];
} UAC_CLOCK_INFO, *PUAC_CLOCK_INFO;

typedef struct UAC_GET_CLOCK_INFO_CONTEXT_
{
    ULONG          NumClockSource;
    UAC_CLOCK_INFO ClockSource[1];
} UAC_GET_CLOCK_INFO_CONTEXT, *PUAC_GET_CLOCK_INFO_CONTEXT;

typedef struct UAC_SET_CLOCK_SOURCE_CONTEXT_
{
    ULONG Index;
} UAC_SET_CLOCK_SOURCE_CONTEXT, *PUAC_SET_CLOCK_SOURCE_CONTEXT;

typedef struct UAC_SET_FLAGS_CONTEXT_
{
    ULONG FirstPacketLatency;
    ULONG ClassicFramesPerIrp;
    ULONG MaxIrpNumber;
    ULONG PreSendFrames;
    LONG  OutputFrameDelay;
    ULONG DelayedOutputBufferSwitch;
    ULONG Reserved;
    ULONG InputBufferOperationOffset;
    ULONG InputHubOffset;
    ULONG OutputBufferOperationOffset;
    ULONG OutputHubOffset;
    ULONG BufferThreadPriority;
    ULONG ClassicFramesPerIrp2;
    ULONG SuggestedBufferPeriod;
    ULONG Reserved2;
} UAC_SET_FLAGS_CONTEXT, *PUAC_SET_FLAGS_CONTEXT;

typedef struct UAC_ASIO_PLAY_BUFFER_HEADER_
{
    // ASIO only, expandable
    ULONG HeaderLength;      // Header length = sizeof(UAC_ASIO_PLAY_BUFFER_HEADER)
    ULONG AsioDriverVersion; // ASIO driver version
    ULONG PeriodSamples;     // Required event notification interval(The buffer size is twice this)
    ULONG RecChannels;       // Required number of recording channels
    ULONG PlayChannels;      // Required number of playback channels
    ULONG Training;          // 1 for latency measurement
    __declspec(align(8)) union {
        HANDLE            p64;
        VOID * POINTER_32 p32;
    } NotificationEvent; // Buffer switching timing notification event handle
    __declspec(align(8)) union {
        HANDLE            p64;
        VOID * POINTER_32 p32;
    } OutputReadyEvent; // ASIO OutputReady notification event handle
    __declspec(align(8)) union {
        HANDLE            p64;
        VOID * POINTER_32 p32;
    } DeviceReadyEvent; // Device-side stream ready event handle
    __declspec(align(8)) ULONGLONG RecChannelsMap;
    __declspec(align(8)) ULONGLONG PlayChannelsMap;
    LONG                           Reserved1;
    LONG                           Is32bitProcess; // 0: 64bit process, 1: 32bit process  https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/how-drivers-identify-32-bit-callers
    LONG                           Reserved2;
} UAC_ASIO_PLAY_BUFFER_HEADER, *PUAC_ASIO_PLAY_BUFFER_HEADER;

typedef struct UAC_ASIO_REC_BUFFER_HEADER_
{
    // ASIO only, expandable
    ULONG HeaderLength; // Header length = sizeof(UAC_ASIO_REC_BUFFER_HEADER)
    ULONG DeviceStatus; // Device Status bit0:Client reinitialization required
    ULONG CurrentSampleRate;
    ULONG CurrentClockSource;
    __declspec(align(8)) LONGLONG
        PlayCurrentPosition;                          // Currently playing frame position (URB processing has been completed and transfer to device has been completed)
    __declspec(align(8)) LONGLONG
        PlayBufferPosition;                           // Currently playing frame position (data has been transferred to URB and preparation for transfer has been completed)
    __declspec(align(8)) LONGLONG
        RecCurrentPosition;                           // Current recording frame position (URB processing is complete and transfer from device is complete)
    __declspec(align(8)) LONGLONG
        RecBufferPosition;                            // Current recording frame position (last Event notification)
    __declspec(align(8)) LONGLONG
                                   PlayReadyPosition; // RecBufferPotision when OutputReady was last issued
    __declspec(align(8)) ULONGLONG NotifySystemTime;
    __declspec(align(4)) LONG      OutputReady;
    __declspec(align(4)) LONG      ReadyBuffers;
    __declspec(align(4)) LONG      CallbackRemain;
    __declspec(align(4)) LONG      AsioProcessStart;
    __declspec(align(4)) LONG      AsioProcessComplete;
    LONG                           Reserved;
} UAC_ASIO_REC_BUFFER_HEADER, *PUAC_ASIO_REC_BUFFER_HEADER;

#endif
