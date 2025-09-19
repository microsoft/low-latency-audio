// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

/*++

Module Name:

    USBAudio.h

Abstract:

    Defines structures and constants related to USB Audio.

Environment:

    Kernel-mode Driver Framework

References:
    Universal Serial Bus Device Class Definition for Audio Devices		Release 1.0
    Universal Serial Bus Device Class Definition for Audio Data Formats	Release 1.0
    Universal Serial Bus Device Class Definition for Audio Devices		Release 2.0
    Universal Serial Bus Device Class Definition for Audio Data Formats	Release 1.0
    Universal Serial Bus Device Class Definition for Terminal Types     Release 2.0

--*/

#ifndef _USBAUDIO_H_
#define _USBAUDIO_H_

namespace NS_USBAudio
{

enum
{
    SIZE_OF_USB_CONFIGURATION_DESC_HEADER = 4,       // sizeof(StandardUSB::ConfigurationDescriptor)
    SIZE_OF_USB_DESCRIPTOR_HEADER = 2,               // sizeof(StandardUSB::Descriptor)
    SIZE_OF_USB_ENDPOINT_DESCRIPTOR = 7,             // sizeof(StandardUSB::EndpointDescriptor)
    SIZE_OF_USB_SSENDPOINT_COMPANION_DESCRIPTOR = 6, // sizeof(StandardUSB::SuperSpeedEndpointCompanionDescriptor)
    SIZE_OF_USB_INTERFACE_DESCRIPTOR = 9,            // sizeof(StandardUSB::InterfaceDescriptor)
};

enum
{
    ENDPOINT_ADDRESS_IN = (1 << 7),
    ENDPOINT_ADDRESS_MASK = (0x1f),
};

enum
{
    SYNCHRONIZATION_TYPE_ASYNCHRONOUS = (0x01 << 2),
    SYNCHRONIZATION_TYPE_ADAPTIVE = (0x02 << 2),
    SYNCHRONIZATION_TYPE_SYNCHRONOUS = (0x03 << 2),
    SYNCHRONIZATION_TYPE_MASK = (0x03 << 2),
};

enum
{
    ALTERNATE_SETTING_ROOT = 0
};

enum
{
    SET_CUR = 0x01,
    GET_CUR = 0x81,
    GET_MIN = 0x82,
    GET_MAX = 0x83,
    GET_RES = 0x84
};

enum
{
    USB_FRAMES_PER_ONE_SECOND = 1000
};

#pragma pack(push, 1)
typedef struct STANDARD_ENDPOINT_DESCRIPTOR
{
    UCHAR  bLength;
    UCHAR  bDescriptorType;
    UCHAR  bEndpointAddress;
    UCHAR  bmAttributes;
    USHORT wMaxPacketSize;
    UCHAR  bInterval;
    UCHAR  bRefresh;
    UCHAR  bSynchAddress;
} STANDARD_ENDPOINT_DESCRIPTOR, *PSTANDARD_ENDPOINT_DESCRIPTOR;

typedef struct CS_GENERIC_AUDIO_DESCRIPTOR
{
    UCHAR bLength;
    UCHAR bDescriptorType;
    UCHAR bDescriptorSubtype;
} CS_GENERIC_AUDIO_DESCRIPTOR, *PCS_GENERIC_AUDIO_DESCRIPTOR;

typedef struct AUDIO_CHANNEL_CLUSTER_DESCRIPTOR
{
    UCHAR bNrChannels;
    ULONG bmChannelConfig;
    UCHAR iChannelNames;
} AUDIO_CHANNEL_CLUSTER_DESCRIPTOR, *PAUDIO_CHANNEL_CLUSTER_DESCRIPTOR;

#pragma pack(pop)

}; // namespace NS_USBAudio

// Defines according to USB Audio Release 1.0.
namespace NS_USBAudio0100
{
// "Universal Serial Bus Device Class Definition for  Audio Devices [USB Audio Release 1.0]"
// Table A-1: Audio Data Format Type I Codes
enum
{
    TYPE_I_UNDEFINED = 0x0000,
    PCM = 0x0001,
    PCM8 = 0x0002,
    IEEE_FLOAT = 0x0003,
    ALAW = 0x0004,
    MULAW = 0x0005
};

// Table A-4: Audio Class-specific Descriptor Types
enum
{
    CS_UNDEFINED = 0x20,
    CS_DEVICE = 0x21,
    CS_CONFIGURATION = 0x22,
    CS_STRING = 0x23,
    CS_INTERFACE = 0x24,
    CS_ENDPOINT = 0x25
};

// Table A-5: Audio Class-Specific AC Interface Descriptor Subtypes
enum
{
    AC_DESCRIPTOR_UNDEFINED = 0x00,
    HEADER = 0x01,
    INPUT_TERMINAL = 0x02,
    OUTPUT_TERMINAL = 0x03,
    MIXER_UNIT = 0x04,
    SELECTOR_UNIT = 0x05,
    FEATURE_UNIT = 0x06,
    PROCESSING_UNIT = 0x07,
    EXTENSION_UNIT = 0x08
};

// Table A-6: Audio Class-Specific AS Interface Descriptor Subtypes
enum
{
    AS_DESCRIPTOR_UNDEFINED = 0x00,
    AS_GENERAL = 0x01,
    FORMAT_TYPE = 0x02,
    FORMAT_SPECIFIC = 0x03
};

// Table A-8: Audio Class-Specific Endpoint Descriptor Subtypes
enum
{
    DESCRIPTOR_UNDEFINED = 0x00,
    EP_GENERAL = 0x01
};

// Table A-19: Endpoint Control Selectors
enum
{
    EP_CONTROL_UNDEFINED = 0x00,
    SAMPLING_FREQ_CONTROL = 0x01,
    PITCH_CONTROL = 0x02
};

enum
{
    SIZE_OF_CS_AS_TYPE_I_FORMAT_TYPE_DESCRIPTOR = 11, // sizeof(CS_AS_TYPE_I_FORMAT_TYPE_DESCRIPTOR )
};

// "Universal Serial Bus Device Class Definition for Audio Data Formats [USB Audio Release 1.0]"

// Table A-4: Format Type Codes
enum
{
    FORMAT_TYPE_UNDEFINED = 0x00,
    FORMAT_TYPE_I = 0x01,
    FORMAT_TYPE_II = 0x02,
    FORMAT_TYPE_III = 0x03
};

#pragma pack(push, 1)
// Table 2-1: Type I Format Type Descriptor
typedef struct CS_AS_TYPE_I_FORMAT_TYPE_DESCRIPTOR
{
    UCHAR bLength;            // Size of this descriptor, in bytes: 8+(ns*3)
    UCHAR bDescriptorType;    // CS_INTERFACE descriptor type.
    UCHAR bDescriptorSubtype; // FORMAT_TYPE descriptor subtype.
    UCHAR bFormatType;        // FORMAT_TYPE_I. Constant identifying
    UCHAR bNrChannels;
    UCHAR bSubframeSize;
    UCHAR bBitResolution;
    UCHAR bSamFreqType;
    union {
        struct
        {
            UCHAR tLowerSamFreq[3]; // 24bit data
            UCHAR tUpperSamFreq[3]; // 24bit data
        } continuous;
        struct
        {
            UCHAR tSamFreq[3]; // 24bit data
        } discreate[1];
    } u;

} CS_AS_TYPE_I_FORMAT_TYPE_DESCRIPTOR, *PCS_AS_TYPE_I_FORMAT_TYPE_DESCRIPTOR;

// Table 4-19: Class-Specific AS Interface Descriptor
typedef struct CS_AS_INTERFACE_DESCRIPTOR
{
    UCHAR  bLength;            // Number Size of this descriptor in bytes: 7
    UCHAR  bDescriptorType;    // Constant CS_INTERFACE descriptor type.
    UCHAR  bDescriptorSubtype; // Constant AS_GENERAL descriptor subtype.
    UCHAR  bTerminalLink;
    UCHAR  bDelay;
    USHORT wFormatTag;
} CS_AS_INTERFACE_DESCRIPTOR, *PCS_AS_INTERFACE_DESCRIPTOR;

// Table 4-21: Class-Specific AS Isochronous Audio Data Endpoint Descriptor
enum
{
    ATTRIBUTES_SAMPLING_FREQUENCY_BIT = (1 << 0),
    ATTRIBUTES_PITCHBIT = (1 << 1),
};

// Table 4-21: Class-Specific AS Isochronous Audio Data Endpoint Descriptor
enum
{
    LOCK_DELAY_UNIT_MILLISECONDS = 1,
    LOCK_DELAY_UNIT_DECODED_PCM_SAMPLES = 2
};

// Table 4-21: Class-Specific AS Isochronous Audio Data Endpoint Descriptor
typedef struct CS_AS_ISOCHRONOUS_AUDIO_DATA_ENDPOINT_DESCRIPTOR
{
    UCHAR  bLength;            // Number Size of this descriptor, in bytes: 7
    UCHAR  bDescriptorType;    // Constant CS_ENDPOINT descriptor type.
    UCHAR  bDescriptorSubtype; // Constant EP_GENERAL descriptor subtype.
    UCHAR  bmAttributes;
    UCHAR  bLockDelayUnits;
    USHORT wLockDelay;
} CS_AS_ISOCHRONOUS_AUDIO_DATA_ENDPOINT_DESCRIPTOR, *PCS_AS_ISOCHRONOUS_AUDIO_DATA_ENDPOINT_DESCRIPTOR;

#pragma pack(pop)
}; // namespace NS_USBAudio0100

// Defines according to USB Audio Release 2.0.
namespace NS_USBAudio0200
{
// "Universal Serial Bus Device Class Definition for Audio Data Formats Release 2.0"
// Table A-2: Audio Data Format Type I Bit Allocations
enum
{
    PCM = (1 << 0),
    PCM8 = (1 << 1),
    IEEE_FLOAT = (1 << 2),
    ALAW = (1 << 3),
    MULAW = (1 << 4),
    TYPE_I_RAW_DATA = (1 << 31)
};

// Table A-3: Audio Data Format Type II Bit Allocations
enum
{
    MPEG = (1 << 0),
    AC_3 = (1 << 1),
    WMA = (1 << 2),
    DTS = (1 << 3),
    TYPE_II_RAW_DATA = (1 << 31)
};

// Table A-4: Audio Data Format Type III Bit Allocations
enum
{	
	IEC61937_AC_3 = (1 << 0),
	IEC61937_MPEG_1_Layer1 = (1 << 1),
	IEC61937_MPEG_1_Layer2_3 = (1 << 2), // or IEC61937_MPEG_2_NOEXT
	IEC61937_MPEG_2_EXT = (1 << 3),
	IEC61937_MPEG_2_AAC_ADTS = (1 << 4),
	IEC61937_MPEG_2_Layer1_LS = (1 << 5),
	IEC61937_MPEG_2_Layer2_3_LS = (1 << 6),
	IEC61937_DTS_I = (1 << 7),
	IEC61937_DTS_II = (1 << 8),
	IEC61937_DTS_III = (1 << 9),
	IEC61937_ATRAC = (1 << 10),
	IEC61937_ATRAC2_3 = (1 << 11),
	TYPE_III_WMA = (1 << 12)
};

// CS_AC_CLOCK_SOURCE_DESCRIPTOR::bmAttributes
enum
{
    // The actual clock source sampling frequency can be obtained using the "Get Sampling Frequency Request" (CS_SAM_FREQ_CONTROL).
    CLOCK_TYPE_EXTERNAL_CLOCK = 0x00,
    CLOCK_TYPE_INTERNAL_FIXED_CLOCK = 0x01,
    // Additionally, the current value of the Sampling Clock can be queried using the "Get Clock Validity Request" (CS_CLOCK_VALID_CONTROL).
    CLOCK_TYPE_INTERNAL_VARIABLE_CLOCK = 0x02,
    // For programmable frequencies, the clock source sampling frequency can be set using the "Set Sampling Frequency Request" (CS_SAM_FREQ_CONTROL).
    CLOCK_TYPE_INTERNAL_PROGRAMMABLE_CLOCK = 0x03,
    CLOCK_TYPE_MASK = 0x03
};

// CS_AC_CLOCK_SOURCE_DESCRIPTOR::bmControls
enum
{
    CLOCK_FREQUENCY_CONTROL_NONE = 0x00,
    CLOCK_FREQUENCY_CONTROL_READ = 0x01,
    CLOCK_FREQUENCY_CONTROL_READ_WRITE = 0x03,

    CLOCK_FREQUENCY_CONTROL_MASK = 0x03,
    CLOCK_VALIDITY_CONTROL_MASK = (0x03 << 2)
};

// CS_AS_INTERFACE_DESCRIPTOR::bmControls
enum
{
    AS_ACT_ALT_SETTING_CONTROL_READ = 0x01,
    AS_VAL_ALT_SETTINGS_CONTROL_READ = (0x01 << 2),

    AS_ACT_ALT_SETTING_CONTROL_MASK = 0x03,
    AS_VAL_ALT_SETTINGS_CONTROL_MASK = (0x03 << 2)
};

// "Universal Serial Bus Device Class Definition for Audio Devices [USB Audio Release 2.0]"
// A.3 Audio Function Protocol Codes
// Table A-3: Audio Function Protocol Codes
enum
{
    FUNCTION_PROTOCOL_UNDEFINED = 0x00,
    AF_VERSION_02_00 = 0x20
};

// A.6 Audio Interface Protocol Codes
// Table A-6: Audio Interface Protocol Codes
enum
{
    INTERFACE_PROTOCOL_UNDEFINED = 0x00,
    IP_VERSION_02_00 = 0x20
};

// A.8 Audio Class-Specific Descriptor Types
// Table A-8: Audio Class-specific Descriptor Types
enum
{
    CS_UNDEFINED = 0x20,
    CS_DEVICE = 0x21,
    CS_CONFIGURATION = 0x22,
    CS_STRING = 0x23,
    CS_INTERFACE = 0x24,
    CS_ENDPOINT = 0x25
};

// Audio Class-Specific AC Interface Descriptor Subtypes
// Table A-9: Audio Class-Specific AC Interface Descriptor Subtypes
enum
{
    AC_DESCRIPTOR_UNDEFINED = 0x00,
    HEADER = 0x01,
    INPUT_TERMINAL = 0x02,
    OUTPUT_TERMINAL = 0x03,
    MIXER_UNIT = 0x04,
    SELECTOR_UNIT = 0x05,
    FEATURE_UNIT = 0x06,
    EFFECT_UNIT = 0x07,
    PROCESSING_UNIT = 0x08,
    EXTENSION_UNIT = 0x09,
    CLOCK_SOURCE = 0x0A,
    CLOCK_SELECTOR = 0x0B,
    CLOCK_MULTIPLIER = 0x0C,
    SAMPLE_RATE_CONVERTER = 0x0D
};

// Audio Class-Specific AS Interface Descriptor Subtypes
// Table A-10: Audio Class-Specific AS Interface Descriptor Subtypes
enum
{
    AS_DESCRIPTOR_UNDEFINED = 0x00,
    AS_GENERAL = 0x01,
    FORMAT_TYPE = 0x02,
    ENCODER = 0x03,
    DECODER = 0x04
};

// Table A-13: Audio Class-Specific Endpoint Descriptor Subtypes
enum
{
    DESCRIPTOR_UNDEFINED = 0x00,
    EP_GENERAL = 0x01
};

// A.14 Audio Class-Specific Request Codes
// Table A-14: Audio Class-Specific Request Codes
enum
{
    REQUEST_CODE_UNDEFINED = 0x00,
    CUR = 0x01,
    RANGE = 0x02,
    MEM = 0x03
};

// A.17.1 Clock Source Control Selectors
// Table A-17: Clock Source Control Selectors
enum
{
    CS_CONTROL_UNDEFINED = 0x00,
    CS_SAM_FREQ_CONTROL = 0x01,
    CS_CLOCK_VALID_CONTROL = 0x02
};

// A.17.2 Clock Selector Control Selectors
// Table A-18: Clock Selector Control Selectors
enum
{
    CX_CONTROL_UNDEFINED = 0x00,
    CX_CLOCK_SELECTOR_CONTROL = 0x01
};

// A.17.3 Clock Multiplier Control Selectors
// Table A-19: Clock Multiplier Control Selectors
enum
{
    CM_CONTROL_UNDEFINED = 0x00,
    CM_NUMERATOR_CONTROL = 0x01,
    CM_DENOMINATOR_CONTROL = 0x02
};

// A.17.4 Terminal Control Selectors
// Table A-20: Terminal Control Selectors
enum
{
    TE_CONTROL_UNDEFINED = 0x00,
    TE_COPY_PROTECT_CONTROL = 0x01,
    TE_CONNECTOR_CONTROL = 0x02,
    TE_OVERLOAD_CONTROL = 0x03,
    TE_CLUSTER_CONTROL = 0x04,
    TE_UNDERFLOW_CONTROL = 0x05,
    TE_OVERFLOW_CONTROL = 0x06,
    TE_LATENCY_CONTROL = 0x07
};

// Mixer Control Selectors
// Table A-21: Mixer Control Selectors
enum
{
    MU_CONTROL_UNDEFINED = 0x00,
    MU_MIXER_CONTROL = 0x01,
    MU_CLUSTER_CONTROL = 0x02,
    MU_UNDERFLOW_CONTROL = 0x03,
    MU_OVERFLOW_CONTROL = 0x04,
    MU_LATENCY_CONTROL = 0x05
};

// A.17.6 Selector Control Selectors
// Table A-22: Selector Control Selectors
enum
{
    SU_CONTROL_UNDEFINED = 0x00,
    SU_SELECTOR_CONTROL = 0x01,
    SU_LATENCY_CONTROL = 0x02
};

// A.17.7 Feature Unit Control Selectors
// Table A-23: Feature Unit Control Selectors
enum
{
    FU_CONTROL_UNDEFINED = 0x00,
    FU_MUTE_CONTROL = 0x01,
    FU_VOLUME_CONTROL = 0x02,
    FU_BASS_CONTROL = 0x03,
    FU_MID_CONTROL = 0x04,
    FU_TREBLE_CONTROL = 0x05,
    FU_GRAPHIC_EQUALIZER_CONTROL = 0x06,
    FU_AUTOMATIC_GAIN_CONTROL = 0x07,
    FU_DELAY_CONTROL = 0x08,
    FU_BASS_BOOST_CONTROL = 0x09,
    FU_LOUDNESS_CONTROL = 0x0A,
    FU_INPUT_GAIN_CONTROL = 0x0B,
    FU_INPUT_GAIN_PAD_CONTROL = 0x0C,
    FU_PHASE_INVERTER_CONTROL = 0x0D,
    FU_UNDERFLOW_CONTROL = 0x0E,
    FU_OVERFLOW_CONTROL = 0x0F,
    FU_LATENCY_CONTROL = 0x10
};

// A.17.11 AudioStreaming Interface Control Selectors
// Table A-32: AudioStreaming Interface Control Selectors
enum
{
    AS_CONTROL_UNDEFINED = 0x00,
    AS_ACT_ALT_SETTING_CONTROL = 0x01,
    AS_VAL_ALT_SETTINGS_CONTROL = 0x02,
    AS_AUDIO_DATA_FORMAT_CONTROL = 0x03
};

#pragma pack(push, 1)
// Table 4-5: Class-Specific AC Interface Header Descriptor
typedef struct CS_AC_INTERFACE_HEADER_DESCRIPTOR
{
    UCHAR  bLength;            // Size of this descriptor, in bytes: 9
    UCHAR  bDescriptorType;    // CS_INTERFACE descriptor type.
    UCHAR  bDescriptorSubtype; // HEADER descriptor subtype. .
    UCHAR  bcdADC[2];
    UCHAR  bCategory;
    USHORT wTotalLength;
    UCHAR  bmControls;
} CS_AC_INTERFACE_HEADER_DESCRIPTOR, *PCS_AC_INTERFACE_HEADER_DESCRIPTOR;

// Table 4-6: Clock Source Descriptor
typedef struct CS_AC_CLOCK_SOURCE_DESCRIPTOR
{
    UCHAR bLength;            // Size of this descriptor, in bytes: 8
    UCHAR bDescriptorType;    // CS_INTERFACE descriptor type.
    UCHAR bDescriptorSubtype; // CLOCK_SOURCE descriptor subtype.
    UCHAR bClockID;
    UCHAR bmAttributes;
    UCHAR bmControls;
    UCHAR bAssocTerminal;
    UCHAR iClockSource;
} CS_AC_CLOCK_SOURCE_DESCRIPTOR, *PCS_AC_CLOCK_SOURCE_DESCRIPTOR;

// Table 4-7: Clock Selector Descriptor
typedef struct CS_AC_CLOCK_SELECTOR_DESCRIPTOR
{
    UCHAR bLength;            // Size of this descriptor, in bytes: 7+p
    UCHAR bDescriptorType;    // CS_INTERFACE descriptor type.
    UCHAR bDescriptorSubtype; // CLOCK_SELECTOR descriptor subtype.
    UCHAR bClockID;
    UCHAR bNrInPins;          // Number of Input Pins of this Unit: p
    UCHAR baCSourceID[1];
    // In the case of baCSourceID[1], baCSourceID[2] will be 1 byte behind.
    UCHAR bmControls;
    UCHAR iClockSelector;
} CS_AC_CLOCK_SELECTOR_DESCRIPTOR, *PCS_AC_CLOCK_SELECTOR_DESCRIPTOR;

// Table 4-8: Clock Multiplier Descriptor
typedef struct CS_AC_CLOCK_MULTIPLIER_DESCRIPTOR
{
    UCHAR bLength;            // Size of this descriptor, in bytes: 7
    UCHAR bDescriptorType;    // CS_INTERFACE descriptor type.
    UCHAR bDescriptorSubtype; // CLOCK_MULTIPLIER descriptor subtype.
    UCHAR bClockID;
    UCHAR bCSourceID;
    UCHAR bmControls;
    UCHAR iClockMultiplier;
} CS_AC_CLOCK_MULTIPLIER_DESCRIPTOR, *PCS_AC_CLOCK_MULTIPLIER_DESCRIPTOR;

// Table 4-9: Input Terminal Descriptor
typedef struct CS_AC_INPUT_TERMINAL_DESCRIPTOR
{
    UCHAR  bLength;            // Size of this descriptor, in bytes: 17
    UCHAR  bDescriptorType;    // CS_INTERFACE descriptor type.
    UCHAR  bDescriptorSubtype; // INPUT_TERMINAL descriptor subtype.
    UCHAR  bTerminalID;
    USHORT wTerminalType;
    UCHAR  bAssocTerminal;
    UCHAR  bCSourceID;
    UCHAR  bNrChannels;
    UCHAR  bmChannelConfig[4];
    UCHAR  iChannelNames;
    UCHAR  bmControls[2];
    UCHAR  iTerminal;
} CS_AC_INPUT_TERMINAL_DESCRIPTOR, *PCS_AC_INPUT_TERMINAL_DESCRIPTOR;

// Table 4-10: Output Terminal Descriptor
typedef struct CS_AC_OUTPUT_TERMINAL_DESCRIPTOR
{
    UCHAR  bLength;            // Size of this descriptor, in bytes: 12
    UCHAR  bDescriptorType;    // CS_INTERFACE descriptor type.
    UCHAR  bDescriptorSubtype; // OUTPUT_TERMINAL descriptor subtype.
    UCHAR  bTerminalID;
    USHORT wTerminalType;
    UCHAR  bAssocTerminal;
    UCHAR  bSourceID;
    UCHAR  bCSourceID;
    UCHAR  bmControls[2];
    UCHAR  iTerminal;
} CS_AC_OUTPUT_TERMINAL_DESCRIPTOR, *PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR;

// Table 4-11: Mixer Unit Descriptor
typedef struct CS_AC_MIXER_UNIT_DESCRIPTOR_COMMON {
    UCHAR		bLength;						// Size of this descriptor, in bytes: 13+p+N
    UCHAR		bDescriptorType;				// CS_INTERFACE descriptor type.
    UCHAR		bDescriptorSubtype;				// MIXER_UNIT descriptor subtype.
    UCHAR		bUnitID;
    UCHAR		bNrInPins;
} CS_AC_MIXER_UNIT_DESCRIPTOR_COMMON, *PCS_AC_MIXER_UNIT_DESCRIPTOR_COMMON;

/* Commented out because it cannot be expressed in a struct definition.
typedef struct CS_AC_MIXER_UNIT_DESCRIPTOR {
    UCHAR		bLength;						// Size of this descriptor, in bytes: 13+p+N
    UCHAR		bDescriptorType;				// CS_INTERFACE descriptor type.
    UCHAR		bDescriptorSubtype;				// MIXER_UNIT descriptor subtype.
    UCHAR		bUnitID;
    UCHAR		bNrInPins;
    struct {
        UCHAR	baSourceID;
        UCHAR	bNrChannels;
        UCHAR	bmChannelConfig[4];
        UCHAR	iChannelNames[1];
        UCHAR	bmMixerControls[N];				// caution! Size = N. Bit map indicating which Mixer Controls are programmable.
        UCHAR	bmControls;
        UCHAR	iMixer;
    } pin[1];
} CS_AC_MIXER_UNIT_DESCRIPTOR, *PCS_AC_MIXER_UNIT_DESCRIPTOR;
*/

// Table 4-12: Selector Unit Descriptor
typedef struct CS_AC_SELECTOR_UNIT_DESCRIPTOR
{
    UCHAR bLength;            // Size of this descriptor, in bytes: 7+p
    UCHAR bDescriptorType;    // CS_INTERFACE descriptor type.
    UCHAR bDescriptorSubtype; // SELECTOR_UNIT descriptor subtype.
    UCHAR bUnitID;
    UCHAR bNrInPins;
    struct
    {
        UCHAR baSourceID;
        UCHAR bmControls;
        UCHAR iSelector;
    } pin[1];
} CS_AC_SELECTOR_UNIT_DESCRIPTOR, *PCS_AC_SELECTOR_UNIT_DESCRIPTOR;

// Table 4-13: Feature Unit Descriptor
enum
{
    FEATURE_UNIT_BMA_MUTE_CONTROL_MASK = 3 << 0,
    FEATURE_UNIT_BMA_VOLUME_CONTROL_MASK = 3 << 2,
    FEATURE_UNIT_BMA_BASS_CONTROL_MASK = 3 << 4,
    FEATURE_UNIT_BMA_MID_CONTROL_MASK = 3 << 6,
    FEATURE_UNIT_BMA_TREBLE_CONTROL_MASK = 3 << 8,
    FEATURE_UNIT_BMA_GRAPHIC_EQUALIZER_CONTROL_MASK = 3 << 10,
    FEATURE_UNIT_BMA_AUTOMATIC_GAIN_CONTROL_MASK = 3 << 12,
    FEATURE_UNIT_BMA_DELAY_CONTROL_MASK = 3 << 14,
    FEATURE_UNIT_BMA_BASS_BOOST_CONTROL_MASK = 3 << 16,
    FEATURE_UNIT_BMA_LOUDNESS_CONTROL_MASK = 3 << 18,
    FEATURE_UNIT_BMA_INPUT_GAIN_CONTROL_MASK = 3 << 20,
    FEATURE_UNIT_BMA_INPUT_GAIN_PAD_CONTROL_MASK = 3 << 22,
    FEATURE_UNIT_BMA_PHASE_INVERTER_CONTROL_MASK = 3 << 24,
    FEATURE_UNIT_BMA_UNDERFLOW_CONTROL_MASK = 3 << 26,
    FEATURE_UNIT_BMA_OVERFOW_CONTROL_MASK = 3 << 28,
    FEATURE_UNIT_BMA_RESERVED = 3 << 30
};

typedef struct CS_AC_FEATURE_UNIT_DESCRIPTOR
{
    UCHAR bLength;            // Size of this descriptor, in bytes:
    UCHAR bDescriptorType;    // CS_INTERFACE descriptor type.
    UCHAR bDescriptorSubtype; // FEATURE_UNIT descriptor subtype.
    UCHAR bUnitID;
    UCHAR bSourceID;
    struct
    {
        UCHAR bmaControls[4];
    } ch[1];
    // UCHAR		iFeature;                       // offset = 5 + (ch + 1) * 4
} CS_AC_FEATURE_UNIT_DESCRIPTOR, *PCS_AC_FEATURE_UNIT_DESCRIPTOR;

// Table 4-14: Sampling Rate Converter Unit Descriptor
typedef struct CS_AC_SAMPLING_RATE_CONVERTER_UNIT_DESCRIPTOR
{
    UCHAR bLength;            // Size of this descriptor, in bytes: 8
    UCHAR bDescriptorType;    // CS_INTERFACE descriptor type.
    UCHAR bDescriptorSubtype; // SAMPLE_RATE_CONVERTER descriptor
    UCHAR bUnitID;
    UCHAR bCSourceInID;
    UCHAR bCSourceOutID;
    UCHAR iSRC;
} CS_AC_SAMPLING_RATE_CONVERTER_UNIT_DESCRIPTOR, *PCS_AC_SAMPLING_RATE_CONVERTER_UNIT_DESCRIPTOR;

// Table 4-15: Common Part of the Effect Unit Descriptor
/* Commented out because it cannot be expressed in a struct definition.
typedef struct CS_AC_COMMON_PART_OF_EFFECT_UNIT_DESCRIPTOR {
    UCHAR		bLength;						// Size of this descriptor, in bytes: 16+(ch*4)
    UCHAR		bDescriptorType;				// CS_INTERFACE descriptor type.
    UCHAR		bDescriptorSubtype;				// EFFECT_UNIT descriptor subtype.
    UCHAR		bUnitID;
    USHORT		wEffectType;
    UCHAR		bSourceID;
    typedef struct {
        UCHAR	bmaControls[4];
    } ch[1];
    UCHAR		iEffects;						// caution!
} CS_AC_COMMON_PART_OF_EFFECT_UNIT_DESCRIPTOR, *PCS_AC_COMMON_PART_OF_EFFECT_UNIT_DESCRIPTOR;
*/

// Table 4-16: Parametric Equalizer Section Effect Unit Descriptor
// Table 4-17: Reverberation Effect Unit Descriptor
// Table 4-18: Modulation Delay Effect Unit Descriptor
// Table 4-19: Dynamic Range Compressor Effect Unit Descriptor

// Table 4-20: Common Part of the Processing Unit Descriptor
/* Commented out because it cannot be expressed in a struct definition.
typedef struct CS_AC_COMMON_PART_OF_PROCESSING_UNIT_DESCRIPTOR
{
    UCHAR		bLength;						// Size of this descriptor, in bytes: 17+p+x
    UCHAR		bDescriptorType;				// CS_INTERFACE descriptor type.
    UCHAR		bDescriptorSubtype;				// PROCESSING_UNIT descriptor subtype.
    UCHAR		bUnitID;
    USHORT		wProcessType;
    UCHAR		bNrInPins;
    struct {
        UCHAR	baSourceID;
        UCHAR	bNrChannels;
        UCHAR	bmChannelConfig[4];
        UCHAR	iChannelNames;
        UCHAR	bmControls[2];
        UCHAR	iProcessing;
        UCHAR	Process-specific[x];
    } pin[1];
} CS_AC_COMMON_PART_OF_PROCESSING_UNIT_DESCRIPTOR, *PCS_AC_COMMON_PART_OF_PROCESSING_UNIT_DESCRIPTOR;
*/

// Table 4-21: Up/Down-mix Processing Unit Descriptor
// Table 4-22: Dolby Prologic Processing Unit Descriptor
// Table 4-23: Stereo Extender Processing Unit Descriptor

// Table 4-24: Extension Unit Descriptor
typedef struct CS_AC_EXTENSION_UNIT_DESCRIPTOR
{
    UCHAR  bLength;            // Size of this descriptor, in bytes: 16+p
    UCHAR  bDescriptorType;    // CS_INTERFACE descriptor type.
    UCHAR  bDescriptorSubtype; // EXTENSION_UNIT descriptor subtype.
    UCHAR  bUnitID;
    USHORT wExtensionCode;
    UCHAR  bNrInPins;
    struct
    {
        UCHAR baSourceID;
        UCHAR bNrChannels;
        UCHAR bmChannelConfig[4];
        UCHAR iChannelNames;
        UCHAR bmControls;
        UCHAR iExtension;
    } pin[1];
} CS_AC_EXTENSION_UNIT_DESCRIPTOR, *PCS_AC_EXTENSION_UNIT_DESCRIPTOR;

// Table 4-27: Class-Specific AS Interface Descriptor
typedef struct CS_AS_INTERFACE_DESCRIPTOR
{
    UCHAR bLength;            // Size of this descriptor in bytes: 16
    UCHAR bDescriptorType;    // CS_INTERFACE descriptor type.
    UCHAR bDescriptorSubtype; // AS_GENERAL descriptor subtype.
    UCHAR bTerminalLink;
    UCHAR bmControls;
    UCHAR bFormatType;
    UCHAR bmFormats[4];
    UCHAR bNrChannels;
    UCHAR bmChannelConfig[4];
    UCHAR iChannelNames;
} CS_AS_INTERFACE_DESCRIPTOR, *PCS_AS_INTERFACE_DESCRIPTOR;

// Table 4-28: Encoder Descriptor
typedef struct CS_AS_ENCODER_DESCRIPTOR
{
    UCHAR bLength;            // Size of this descriptor, in bytes: 21
    UCHAR bDescriptorType;    // CS_INTERFACE descriptor type.
    UCHAR bDescriptorSubtype; // ENCODER descriptor subtype.
    UCHAR bEncoderID;
    UCHAR bEncoder;
    UCHAR bmControls[4];
    UCHAR iParam1;
    UCHAR iParam2;
    UCHAR iParam3;
    UCHAR iParam4;
    UCHAR iParam5;
    UCHAR iParam6;
    UCHAR iParam7;
    UCHAR iParam8;
    UCHAR iEncoder;
} CS_AS_ENCODER_DESCRIPTOR, *PCS_AS_ENCODER_DESCRIPTOR;

// Table 4-29: MPEG Decoder Descriptor
// Table 4-30: AC-3 Decoder Descriptor
// Table 4-31: WMA Decoder Descriptor
// Table 4-32: DTS Decoder Descriptor

// Table 4-34: Class-Specific AS Isochronous Audio Data Endpoint Descriptor
enum
{
    LOCK_DELAY_UNIT_MILLISECONDS = 1,
    LOCK_DELAY_UNIT_DECODED_PCM_SAMPLES = 2
};

// Table 4-34: Class-Specific AS Isochronous Audio Data Endpoint Descriptor
typedef struct CS_AS_ISOCHRONOUS_AUDIO_DATA_ENDPOINT_DESCRIPTOR
{
    UCHAR  bLength;            // Size of this descriptor, in bytes: 8
    UCHAR  bDescriptorType;    // CS_ENDPOINT descriptor type.
    UCHAR  bDescriptorSubtype; // EP_GENERAL descriptor subtype.
    UCHAR  bmAttributes;
    UCHAR  bmControls;
    UCHAR  bLockDelayUnits;
    USHORT wLockDelay;
} CS_AS_ISOCHRONOUS_AUDIO_DATA_ENDPOINT_DESCRIPTOR, *PCS_AS_ISOCHRONOUS_AUDIO_DATA_ENDPOINT_DESCRIPTOR;

// 5.2.3.1 Layout 1 Parameter Block
// Table 5-2: 1-byte Control CUR Parameter Block
typedef struct
{               // wLength = 1
    UCHAR bCUR; // The setting for the CUR attribute of the addressed Control
} CONTROL_CUR_PARAMETER_BLOCK_LAYOUT1;

// Table 5-3: 1-byte Control RANGE Parameter Block
typedef struct
{ // wLength = 2 + 3 * n
    USHORT wNumSubRanges;
    struct
    {
        UCHAR bMIN;
        UCHAR bMAX;
        UCHAR bRES;
    } subrange[1];
} CONTROL_RANGE_PARAMETER_BLOCK_LAYOUT1, *PCONTROL_RANGE_PARAMETER_BLOCK_LAYOUT1;

// 5.2.3.2 Layout 2 Parameter Block
// Table 5-4: 2-byte Control CUR Parameter Block
typedef struct
{ // wLength = 2
    USHORT wCUR;
} CONTROL_CUR_PARAMETER_BLOCK_LAYOUT2;

// Table 5-5: 2 byte Control RANGE Parameter Block
typedef struct
{ // wLength = 2+6*n
    USHORT wNumSubRanges;
    struct
    {
        USHORT wMIN;
        USHORT wMAX;
        USHORT wRES;
    } subrange[1];
} CONTROL_RANGE_PARAMETER_BLOCK_LAYOUT2, *PCONTROL_RANGE_PARAMETER_BLOCK_LAYOUT2;

// 5.2.3.3 Layout 3 Parameter Block
// Table 5-6: 4-byte Control CUR Parameter Block
typedef struct
{ // wLength = 4
    ULONG dCUR;
} CONTROL_CUR_PARAMETER_BLOCK_LAYOUT3;

// Table 5-7: 4-byte Control RANGE Parameter Block
typedef struct
{ // wLength = 2+12*n
    USHORT wNumSubRanges;
    struct
    {
        ULONG dMIN;
        ULONG dMAX;
        ULONG dRES;
    } subrange[1];
} CONTROL_RANGE_PARAMETER_BLOCK_LAYOUT3, *PCONTROL_RANGE_PARAMETER_BLOCK_LAYOUT3;

// Table 5-14: Valid Alternate Settings Control CUR Parameter Block
typedef struct
{ // wLength = 1 * n
    UCHAR bControlSize;
    UCHAR bmValidAltSettings[1];
} CONTROL_CUR_AS_CONTROL_CUR_PARAMETER_BLOCK, *PCONTROL_CUR_AS_CONTROL_CUR_PARAMETER_BLOCK;

// Table 6-1: Interrupt Data Message Format
typedef struct INTERRUPT_DATA_MESSAGE_FORMAT
{
    UCHAR  bInfo;
    UCHAR  bAttribute;
    USHORT wValue;
    USHORT wIndex;
} INTERRUPT_DATA_MESSAGE_FORMAT, *PINTERRUPT_DATA_MESSAGE_FORMAT;

#pragma pack(pop)
// "Universal Serial Bus Device Class Definition for Audio Data Formats Release 2.0 [USB Audio Release 2.0]"
// Table A-1: Format Type Codes
enum
{
    FORMAT_TYPE_UNDEFINED = 0x00,
    FORMAT_TYPE_I = 0x01,
    FORMAT_TYPE_II = 0x02,
    FORMAT_TYPE_III = 0x03,
    FORMAT_TYPE_IV = 0x04,
    EXT_FORMAT_TYPE_I = 0x81,
    EXT_FORMAT_TYPE_II = 0x82,
    EXT_FORMAT_TYPE_III = 0x83,
};

#pragma pack(push, 1)
// Table 2-2: Type I Format Type Descriptor
typedef struct CS_AS_TYPE_I_FORMAT_TYPE_DESCRIPTOR
{
    UCHAR bLength;            // Size of this descriptor, in bytes: 6
    UCHAR bDescriptorType;    // CS_INTERFACE descriptor type.
    UCHAR bDescriptorSubtype; // FORMAT_TYPE descriptor subtype.
    UCHAR bFormatType;        // FORMAT_TYPE_I. Constant identifying the Format Type the AudioStreaming interface is using.
    UCHAR bSubslotSize;       // The number of bytes occupied by one audio subslot. Can be 1, 2, 3 or 4.
    UCHAR bBitResolution;	  // The number of effectively used bits from the available bits in an audio subslot.
} CS_AS_TYPE_I_FORMAT_TYPE_DESCRIPTOR, *PCS_AS_TYPE_I_FORMAT_TYPE_DESCRIPTOR;

enum
{
    SIZE_OF_CS_AS_TYPE_I_FORMAT_TYPE_DESCRIPTOR = sizeof(CS_AS_TYPE_I_FORMAT_TYPE_DESCRIPTOR)
};

// Table 2-3: Type II Format Type Descriptor
typedef struct CS_AS_TYPE_II_FORMAT_TYPE_DESCRIPTOR
{
    UCHAR bLength;            // Number Size of this descriptor, in bytes: 8
    UCHAR bDescriptorType;    // Constant CS_INTERFACE descriptor type.
    UCHAR bDescriptorSubtype; // Constant FORMAT_TYPE descriptor subtype.
    UCHAR bFormatType;        // Constant FORMAT_TYPE_II. Constant identifying the Format Type the AudioStreaming interface is using.
    USHORT wMaxBitRate;       // Number Indicates the maximum number of bits per second this interface can handle. Expressed in kbits/s.
    USHORT wSlotsPerFrame;    // Number Indicates the number of PCM audio slots
} CS_AS_TYPE_II_FORMAT_TYPE_DESCRIPTOR, *PCS_AS_TYPE_II_FORMAT_TYPE_DESCRIPTOR;

enum
{
    SIZE_OF_CS_AS_TYPE_II_FORMAT_TYPE_DESCRIPTOR = sizeof(CS_AS_TYPE_II_FORMAT_TYPE_DESCRIPTOR)
};

// Table 2-4: Type III Format Type Descriptor
typedef struct CS_AS_TYPE_III_FORMAT_TYPE_DESCRIPTOR
{
	UCHAR bLength;            // Number Size of this descriptor, in bytes: 6
	UCHAR bDescriptorType;    // Constant CS_INTERFACE descriptor type.
	UCHAR bDescriptorSubtype; // Constant FORMAT_TYPE descriptor subtype.
	UCHAR bFormatType;        // Constant FORMAT_TYPE_III. Constant identifying the Format Type the AudioStreaming interface is using.
	UCHAR bSubslotSize;       // Number The number of bytes occupied by one audio subslot. Must be set to two.
	UCHAR bBitResolution;     // Number The number of effectively used bits from the available bits in an audio subframe.
} CS_AS_TYPE_III_FORMAT_TYPE_DESCRIPTOR, *PCS_AS_TYPE_III_FORMAT_TYPE_DESCRIPTOR;

// Universal Serial Bus Device Class Definition for Terminal Types     Release 2.0
// Table 2-1: USB Terminal Types
enum
{
    USB_UNDEFINED = 0x0100,
    USB_STREAMING = 0x0101,
    USB_VENDOR_SPECIFIC = 0x01FF
};

// Table 2-2: Input Terminal Types
enum
{
    INPUT_UNDEFINED = 0x0200,
    MICROPHONE = 0x0201,
    DESKTOP_MICROPHONE = 0x0202,
    PERSONAL_MICROPHONE = 0x0203,
    OMNI_DIRECTIONAL_MICROPHONE = 0x0204,
    MICROPHONE_ARRAY = 0x0205,
    PROCESSING_MICROPHONE_ARRAY = 0x0206
};

// Table 2-3: Output Terminal Types
enum
{
    OUTPUT_UNDEFINED = 0x0300,
    SPEAKER = 0x0301,
    HEADPHONES = 0x0302,
    HEAD_MOUNTED_DISPLAY_AUDIO = 0x0303,
    DESKTOP_SPEAKER = 0x0304,
    ROOM_SPEAKER = 0x0305,
    COMMUNICATION_SPEAKER = 0x0306,
    LOW_FREQUENCY_EFFECTS_SPEAKER = 0x0307
};

// Table 2-4: Bi-directional Terminal Types
enum 
{
	BI_DIRECTIONAL_UNDEFINED = 0x0400,
	HANDSET = 0x0401,
	HEADSET = 0x0402,
	SPEAKERPHONE_NO_ECHO_REDUCTION = 0x0403,
	ECHO_SUPPRESSING_SPEAKERPHONE =  0x0404,
	ECHO_CANCELING_SPEAKERPHONE =  0x0405
};

// Table 2-5: Telephony Terminal Types
enum 
{
	TELEPHONY_UNDEFINED = 0x0500,
	PHONE_LINE = 0x0501,
	TELEPHONE = 0x0502,
	DOWN_LINE_PHONE  = 0x0503
};

// Table 2-6: External Terminal Types
enum
{
    EXTERNAL_UNDEFINED = 0x0600,
    ANALOG_CONNECTOR = 0x0601,
    DIGITAL_AUDIO_INTERFACE = 0x0602,
    LINE_CONNECTOR = 0x0603,
    LEGACY_AUDIO_CONNECTOR = 0x0604,
    SPDIF_INTERFACE = 0x0605,
    _1394_DA_STREAM = 0x0606,
    _1394_DV_STREAM_SOUNDTRACK = 0x0607,
    ADAT_LIGHTPIPE = 0x0608,
    TDIF = 0x0609,
    MADI = 0x060A
};

#pragma pack(pop)
}; // namespace NS_USBAudio0200

//
// Interface Class
// Constants for Interface classes (bInterfaceClass).
//
enum
{
    USB_AUDIO_CLASS = 1, // deprecated
    USB_AUDIO_INTERFACE_CLASS = 1,

    USB_COMMUNICATION_CONTROL_INTERFACE_CLASS = 2,
    USB_COMMUNICATION_DATA_INTERFACE_CLASS = 10,

    USB_HID_CLASS = 3,
    USB_HID_INTERFACE_CLASS = 3,

    USB_PHYSICAL_INTERFACE_CLASS = 5,

    USB_IMAGE_INTERFACE_CLASS = 6,

    USB_PRINTING_CLASS = 7,     // deprecated
    USB_PRINTING_INTERFACE_CLASS = 7,

    USB_MASS_STORAGE_CLASS = 8, // deprecated
    USB_MASS_STORAGE_INTERFACE_CLASS = 8,

    USB_CHIP_SMART_CARD_INTERFACE_CLASS = 11,

    USB_CONTENT_SECURITY_INTERFACE_CLASS = 13,

    USB_VIDEO_INTERFACE_CLASS = 14,

    USB_PERSONAL_HEALTHCARE_INTERFACE_CLASS = 15,

    USB_DIAGNOSTIC_DEVICE_INTERFACE_CLASS = 220,

    USB_WIRELESS_CONTROLLER_INTERFACE_CLASS = 224,

    USB_APPLICATION_SPECIFIC_INTERFACE_CLASS = 254,

    USB_VENDOR_SPECIFIC_INTERFACE_CLASS = 255
};

// Interface SubClass
// Constants for USB Interface SubClasses (bInterfaceSubClass).
//
enum
{
    USB_SUB_CLASS_UNDEFINED = 0,

    USB_COMPOSITE_SUB_CLASS = 0,

    USB_HUB_SUB_CLASS = 0,

    // For the USB_AUDIO_INTERFACE_CLASS
    //
    USB_AUDIO_CONTROL_SUB_CLASS = 0x01,
    USB_AUDIO_STREAMING_SUB_CLASS = 0x02,
    USB_MIDI_STREAMING_SUB_CLASS = 0x03,

    // For the USB_ApplicationSpecificInterfaceClass
    //
    USB_DFU_SUB_CLASS = 0x01,
    USB_IrDA_BRIDGE_SUB_CLASS = 0x02,
    USB_TEST_MEASUREMENT_SUB_CLASS = 0x03,

    // For the USB_MASS_STORAGE_INTERFACE_CLASS
    //
    USB_MASS_STORAGE_RBC_SUB_CLASS = 0x01,
    USB_MASS_STORAGE_ATAPI_SUB_CLASS = 0x02,
    USB_MASS_STORAGE_QIC157_SUB_CLASS = 0x03,
    USB_MASS_STORAGE_UFI_SUB_CLASS = 0x04,
    USB_MASS_STORAGE_SFF8070i_SUB_CLASS = 0x05,
    USB_MASS_STORAGE_SCSI_SUB_CLASS = 0x06,

    // For the USB_HID_INTERFACE_CLASS
    //
    USB_HID_Boot_INTERFACE_SUB_CLASS = 0x01,

    // For the USB_COMMUNICATION_DATA_INTERFACE_CLASS
    //
    USB_COMM_DIRECTLINE_SUB_CLASS = 0x01,
    USB_COMM_ABSTRACT_SUB_CLASS = 0x02,
    USB_COMM_TELEPHONE_SUB_CLASS = 0x03,
    USB_COMM_MULTICHANNEL_SUB_CLASS = 0x04,
    USB_COMM_CAPI_SUB_CLASS = 0x05,
    USB_COMM_ETHERNET_NETWORKING_SUB_CLASS = 0x06,
    USB_ATM_NETWORKING_SUB_CLASS = 0x07,

    // For the USB_DIAGNOSTICDEVICE_INTERFACE_CLASS
    //
    USB_REPROGRAMMABLEDIAGNOSTIC_SUB_CLASS = 0x01,

    // For the USB_WIRELESSCONTROLLER_INTERFACE_CLASS
    //
    USB_RF_CONTROLLER_SUB_CLASS = 0x01,

    // For the USB_MISCELLANEOUS_CLASS
    //
    USB_COMMON_CLASS_SUB_CLASS = 0x02,

    // For the USB_VIDEO_INTERFACE_CLASS
    //
    USB_VIDEO_CONTROL_SUB_CLASS = 0x01,
    USB_VIDEO_STREAMING_SUB_CLASS = 0x02,
    USB_VIDEO_INTERFACE_COLLECTION_SUB_CLASS = 0x03
};

#endif
