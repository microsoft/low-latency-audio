// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================
// ASIO is a trademark and software of Steinberg Media Technologies GmbH

/*++

Module Name:

    USBAudioDataFormat.cpp

Abstract:

    Implement classes that manage USB Audio 2.0 Audio Data Format Types I and III.

Environment:

    Kernel-mode Driver Framework

--*/

#include "Driver.h"
#include "Device.h"
#include "Public.h"
#include "Private.h"
#include "Common.h"
#include "DeviceControl.h"
#include "ErrorStatistics.h"
#include "USBAudioDataFormat.h"
#include "CircuitHelper.h"

#ifndef __INTELLISENSE__
#include "USBAudioDataFormat.tmh"
#endif

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioDataFormat::USBAudioDataFormat()
{
    PAGED_CODE();
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioDataFormat::USBAudioDataFormat(
    UCHAR formatType,
    UCHAR formats[4],
    UCHAR subslotSize,
    UCHAR bitResolution
)
    : m_formatType(formatType),
      m_format(ConverBmFormats(formats)),
      m_subslotSize(subslotSize),
      m_bitResolution(bitResolution)
{
    PAGED_CODE();
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioDataFormat::~USBAudioDataFormat()
{
    PAGED_CODE();
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR
USBAudioDataFormat::GetBytesPerSample()
{
    PAGED_CODE();

    return m_subslotSize;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR
USBAudioDataFormat::GetValidBits()
{
    PAGED_CODE();

    return m_bitResolution;
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG
USBAudioDataFormat::GetFormatType()
{
    PAGED_CODE();

    return m_formatType;
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG
USBAudioDataFormat::GetFormat()
{
    PAGED_CODE();

    return m_format;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioDataFormat::IsEqualFormat(
    UCHAR formatType,
    UCHAR formats[4],
    UCHAR subslotSize,
    UCHAR bitResolution
)
{
    USBAudioDataFormat format(formatType, formats, subslotSize, bitResolution);

    PAGED_CODE();

    return *this == format;
}

_Use_decl_annotations_
PAGED_CODE_SEG
bool USBAudioDataFormat::operator==(_In_ USBAudioDataFormat const & format) const
{
    PAGED_CODE();

    return (m_formatType == format.m_formatType) && (m_format == format.m_format) && (m_subslotSize == format.m_subslotSize) && (m_bitResolution == format.m_bitResolution);
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioDataFormat *
USBAudioDataFormat::GetNext()
{
    PAGED_CODE();

    return (m_nextUSBAudioDataFormat);
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioDataFormat *
USBAudioDataFormat::Append(USBAudioDataFormat * nextUSBAudioDataFormat)
{
    PAGED_CODE();

    ASSERT(nextUSBAudioDataFormat != nullptr);
    ASSERT(m_nextUSBAudioDataFormat == nullptr);

    m_nextUSBAudioDataFormat = nextUSBAudioDataFormat;
    return nextUSBAudioDataFormat;
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioDataFormat *
USBAudioDataFormat::Create(
    UCHAR formatType,
    UCHAR formats[4],
    UCHAR subslotSize,
    UCHAR bitResolution
)
{
    PAGED_CODE();

    USBAudioDataFormat * format = new (POOL_FLAG_NON_PAGED, DRIVER_TAG) USBAudioDataFormat(formatType, formats, subslotSize, bitResolution);

    return format;
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG
USBAudioDataFormat::ConverBmFormats(
    UCHAR formats[4]
)
{
    PAGED_CODE();
    return (((ULONG)formats[3]) << 24) | (((ULONG)formats[2]) << 16) | (((ULONG)formats[1]) << 8) | (ULONG)formats[0];
}

PAGED_CODE_SEG
_Use_decl_annotations_
bool USBAudioDataFormat::IsSupportedFormat(
    _In_ ULONG formatType,
    _In_ ULONG format
)
{
    bool isSupportedFormat = false;

    PAGED_CODE();

    switch (formatType)
    {
    case NS_USBAudio0200::FORMAT_TYPE_I:
        switch (format)
        {
        case NS_USBAudio0200::PCM:
            isSupportedFormat = true;
            break;
        case NS_USBAudio0200::PCM8:
            // TBD
            break;
        case NS_USBAudio0200::IEEE_FLOAT:
            isSupportedFormat = true;
            break;
        default:
            break;
        }
        break;
    case NS_USBAudio0200::FORMAT_TYPE_III:
        switch (format)
        {
        case NS_USBAudio0200::IEC61937_AC_3:
        case NS_USBAudio0200::IEC61937_MPEG_2_AAC_ADTS:
        case NS_USBAudio0200::IEC61937_DTS_I:
        case NS_USBAudio0200::IEC61937_DTS_II:
        case NS_USBAudio0200::IEC61937_DTS_III:
        case NS_USBAudio0200::TYPE_III_WMA:
            isSupportedFormat = false;
            break;
        default:
            break;
        }
    }

    return isSupportedFormat;
}

PAGED_CODE_SEG
_Use_decl_annotations_
UACSampleFormat USBAudioDataFormat::ConvertFormatToSampleFormat(
    _In_ ULONG formatType,
    _In_ ULONG format
)
{
    PAGED_CODE();

    UACSampleFormat sampleFormat = UACSampleFormat::UAC_SAMPLE_FORMAT_PCM;

    switch (formatType)
    {
    case NS_USBAudio0200::FORMAT_TYPE_I:
        switch (format)
        {
        case NS_USBAudio0200::PCM:
            sampleFormat = UACSampleFormat::UAC_SAMPLE_FORMAT_PCM;
            break;
        case NS_USBAudio0200::PCM8:
            sampleFormat = UACSampleFormat::UAC_SAMPLE_FORMAT_PCM8;
            break;
        case NS_USBAudio0200::IEEE_FLOAT:
            sampleFormat = UACSampleFormat::UAC_SAMPLE_FORMAT_IEEE_FLOAT;
            break;
        default:
            break;
        }
        break;
    case NS_USBAudio0200::FORMAT_TYPE_III:
        switch (format)
        {
        case NS_USBAudio0200::IEC61937_AC_3:
            sampleFormat = UACSampleFormat::UAC_SAMPLE_FORMAT_IEC61937_AC_3;
            break;
        case NS_USBAudio0200::IEC61937_MPEG_2_AAC_ADTS:
            sampleFormat = UACSampleFormat::UAC_SAMPLE_FORMAT_IEC61937_MPEG_2_AAC_ADTS;
            break;
        case NS_USBAudio0200::IEC61937_DTS_I:
            sampleFormat = UACSampleFormat::UAC_SAMPLE_FORMAT_IEC61937_DTS_I;
            break;
        case NS_USBAudio0200::IEC61937_DTS_II:
            sampleFormat = UACSampleFormat::UAC_SAMPLE_FORMAT_IEC61937_DTS_II;
            break;
        case NS_USBAudio0200::IEC61937_DTS_III:
            sampleFormat = UACSampleFormat::UAC_SAMPLE_FORMAT_IEC61937_DTS_III;
            break;
        case NS_USBAudio0200::TYPE_III_WMA:
            sampleFormat = UACSampleFormat::UAC_SAMPLE_FORMAT_TYPE_III_WMA;
            break;
        default:
            break;
        }
    }

    return sampleFormat;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudioDataFormat::ConvertFormatToSampleFormat(
    UACSampleFormat sampleFormat,
    ULONG &         formatType,
    ULONG &         format
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    switch (sampleFormat)
    {
    case UACSampleFormat::UAC_SAMPLE_FORMAT_PCM:
        formatType = NS_USBAudio0200::FORMAT_TYPE_I;
        format = NS_USBAudio0200::PCM;
        break;
    case UACSampleFormat::UAC_SAMPLE_FORMAT_PCM8:
        formatType = NS_USBAudio0200::FORMAT_TYPE_I;
        format = NS_USBAudio0200::PCM8;
        break;
    case UACSampleFormat::UAC_SAMPLE_FORMAT_IEEE_FLOAT:
        formatType = NS_USBAudio0200::FORMAT_TYPE_I;
        format = NS_USBAudio0200::IEEE_FLOAT;
        break;
    case UACSampleFormat::UAC_SAMPLE_FORMAT_IEC61937_AC_3:
        formatType = NS_USBAudio0200::FORMAT_TYPE_III;
        format = NS_USBAudio0200::IEC61937_AC_3;
        break;
    case UACSampleFormat::UAC_SAMPLE_FORMAT_IEC61937_MPEG_2_AAC_ADTS:
        formatType = NS_USBAudio0200::FORMAT_TYPE_III;
        format = NS_USBAudio0200::IEC61937_MPEG_2_AAC_ADTS;
        break;
    case UACSampleFormat::UAC_SAMPLE_FORMAT_IEC61937_DTS_I:
        formatType = NS_USBAudio0200::FORMAT_TYPE_III;
        format = NS_USBAudio0200::IEC61937_DTS_I;
        break;
    case UACSampleFormat::UAC_SAMPLE_FORMAT_IEC61937_DTS_II:
        formatType = NS_USBAudio0200::FORMAT_TYPE_III;
        format = NS_USBAudio0200::IEC61937_DTS_II;
        break;
    case UACSampleFormat::UAC_SAMPLE_FORMAT_IEC61937_DTS_III:
        formatType = NS_USBAudio0200::FORMAT_TYPE_III;
        format = NS_USBAudio0200::IEC61937_DTS_III;
        break;
    case UACSampleFormat::UAC_SAMPLE_FORMAT_TYPE_III_WMA:
        formatType = NS_USBAudio0200::FORMAT_TYPE_III;
        format = NS_USBAudio0200::TYPE_III_WMA;
        break;
    default:
    case UACSampleFormat::UAC_SAMPLE_FORMAT_DSD_SINGLE:
    case UACSampleFormat::UAC_SAMPLE_FORMAT_DSD_DOUBLE:
    case UACSampleFormat::UAC_SAMPLE_FORMAT_DSD_NATIVE:
        status = STATUS_INVALID_PARAMETER;
        break;
    }

    return status;
}

PAGED_CODE_SEG
_Use_decl_annotations_
UACSampleType
USBAudioDataFormat::ConverSampleFormatToSampleType(
    UACSampleFormat sampleFormat,
    ULONG           bytesPerSample,
    ULONG           validBitsPerSample
)
{
    UACSampleType sampleType = UACSampleType::UACSTLastEntry;

    PAGED_CODE();

    switch (sampleFormat)
    {
    case UACSampleFormat::UAC_SAMPLE_FORMAT_PCM:
        switch (bytesPerSample)
        {
        case 1:
            switch (validBitsPerSample)
            {
            case 8:
                sampleType = UACSampleType::UACSTInt16LSB;
                break;
            default:
                break;
            }
            break;
        case 2:
            switch (validBitsPerSample)
            {
            case 16:
                sampleType = UACSampleType::UACSTInt16LSB;
                break;
            default:
                break;
            }
            break;
        case 3:
            switch (validBitsPerSample)
            {
            case 24:
                sampleType = UACSampleType::UACSTInt24LSB;
                break;
            default:
                break;
            }
            break;
        case 4:
            //
            // The sample data is left-justified, so
            // UACSampleType::UACSTInt32LSB16, UACSampleType::UACSTInt32LSB20,
            // UACSampleType::UACSTInt32LSB20, UACSampleType::UACSTInt32LSB24
            // are not used.
            //
            sampleType = UACSampleType::UACSTInt32LSB;
            break;
        default:
            break;
        }
        break;
    case UACSampleFormat::UAC_SAMPLE_FORMAT_IEEE_FLOAT:
        if ((bytesPerSample == 4) && (validBitsPerSample == 32))
        {
            sampleType = UACSampleType::UACSTFloat32LSB;
        }
        break;
    case UACSampleFormat::UAC_SAMPLE_FORMAT_IEC61937_AC_3:
    case UACSampleFormat::UAC_SAMPLE_FORMAT_IEC61937_MPEG_2_AAC_ADTS:
    case UACSampleFormat::UAC_SAMPLE_FORMAT_IEC61937_DTS_I:
    case UACSampleFormat::UAC_SAMPLE_FORMAT_IEC61937_DTS_II:
    case UACSampleFormat::UAC_SAMPLE_FORMAT_IEC61937_DTS_III:
    case UACSampleFormat::UAC_SAMPLE_FORMAT_TYPE_III_WMA:
        // ASIO does not support these sample formats.
    default:
        sampleType = UACSampleType::UACSTLastEntry;
        break;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DESCRIPTOR, " - sampleFormat %u, bytesPerSample %u, validBitsPerSample %u, sampleType %d", toULong(sampleFormat), bytesPerSample, validBitsPerSample, toInt(sampleType));

    return sampleType;
}

PAGED_CODE_SEG
_Use_decl_annotations_
ULONG
USBAudioDataFormat::ConverSampleTypeToBytesPerSample(
    UACSampleType sampleType
)
{
    ULONG bytesPerSample = 4;

    PAGED_CODE();

    switch (sampleType)
    {
    case UACSampleType::UACSTInt16LSB:
        bytesPerSample = 2;
        break;
    case UACSampleType::UACSTInt24LSB:
        bytesPerSample = 3;
        break;
    default:
    case UACSampleType::UACSTInt32LSB16:
    case UACSampleType::UACSTInt32LSB20:
    case UACSampleType::UACSTInt32LSB24:
    case UACSampleType::UACSTInt32LSB:
    case UACSampleType::UACSTFloat32LSB:
    case UACSampleType::UACSTLastEntry:
        bytesPerSample = 4;
        break;
    }

    return bytesPerSample;
}

PAGED_CODE_SEG
_Use_decl_annotations_
NTSTATUS USBAudioDataFormat::BuildWaveFormatExtensible(
    ULONG                               sampleRate,
    UCHAR                               channels,
    UCHAR                               bytesPerSample,
    UCHAR                               validBits,
    ULONG                               formatType,
    ULONG                               format,
    KSDATAFORMAT_WAVEFORMATEXTENSIBLE & pcmWaveFormatExtensible
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    // TBD
    // NS_USBAudio0200::FORMAT_TYPE_III, WAVEFORMATEXTENSIBLE_IEC61937
    // https://learn.microsoft.com/en-us/windows/win32/coreaudio/representing-formats-for-iec-61937-transmissions
    const GUID * ksDataFormatSubType = ConvertAudioDataFormat(formatType, format);
    if (ksDataFormatSubType != nullptr)
    {
        pcmWaveFormatExtensible.DataFormat.FormatSize = sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE);
        pcmWaveFormatExtensible.DataFormat.MajorFormat = KSDATAFORMAT_TYPE_AUDIO;
        pcmWaveFormatExtensible.DataFormat.SubFormat = *ksDataFormatSubType;
        pcmWaveFormatExtensible.DataFormat.Specifier = KSDATAFORMAT_SPECIFIER_WAVEFORMATEX;

        pcmWaveFormatExtensible.WaveFormatExt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        pcmWaveFormatExtensible.WaveFormatExt.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        pcmWaveFormatExtensible.WaveFormatExt.dwChannelMask = (channels == 1 ? KSAUDIO_SPEAKER_MONO : KSAUDIO_SPEAKER_STEREO);
        pcmWaveFormatExtensible.WaveFormatExt.SubFormat = *ksDataFormatSubType;

        pcmWaveFormatExtensible.DataFormat.SampleSize = channels * bytesPerSample;
        pcmWaveFormatExtensible.WaveFormatExt.Format.nChannels = static_cast<WORD>(channels);
        pcmWaveFormatExtensible.WaveFormatExt.Format.nSamplesPerSec = sampleRate;
        pcmWaveFormatExtensible.WaveFormatExt.Format.nAvgBytesPerSec = channels * bytesPerSample * sampleRate;
        pcmWaveFormatExtensible.WaveFormatExt.Format.nBlockAlign = static_cast<WORD>(channels * bytesPerSample);
        pcmWaveFormatExtensible.WaveFormatExt.Format.wBitsPerSample = static_cast<WORD>(bytesPerSample * 8);
        pcmWaveFormatExtensible.WaveFormatExt.Samples.wValidBitsPerSample = validBits;
    }
    else
    {
        status = STATUS_INVALID_PARAMETER;
    }
    return status;
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioDataFormatManager::USBAudioDataFormatManager()
{
    PAGED_CODE();
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioDataFormatManager::~USBAudioDataFormatManager()
{
    USBAudioDataFormat * usbAudioDataFormat = m_usbAudioDataFormat;

    PAGED_CODE();

    while (usbAudioDataFormat != nullptr)
    {
        USBAudioDataFormat * current = usbAudioDataFormat;
        usbAudioDataFormat = usbAudioDataFormat->GetNext();
        delete current;
    }
    m_usbAudioDataFormat = nullptr;
    m_numOfFormats = 0;
}

_Use_decl_annotations_
PAGED_CODE_SEG
NTSTATUS
USBAudioDataFormatManager::SetUSBAudioDataFormat(
    UCHAR                 formatType,
    UCHAR                 formats[4],
    UCHAR                 subslotSize,
    UCHAR                 bitResolution,
    USBAudioDataFormat *& usbAudioDataFormat
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Entry");

    usbAudioDataFormat = m_usbAudioDataFormat;

    while (usbAudioDataFormat != nullptr)
    {
        if (!USBAudioDataFormat::IsSupportedFormat(formatType, USBAudioDataFormat::ConverBmFormats(formats)))
        {
            return STATUS_SUCCESS;
        }
        if (usbAudioDataFormat->IsEqualFormat(formatType, formats, subslotSize, bitResolution))
        {
            return STATUS_SUCCESS;
        }
        usbAudioDataFormat = usbAudioDataFormat->GetNext();
    }
    usbAudioDataFormat = USBAudioDataFormat::Create(formatType, formats, subslotSize, bitResolution);
    RETURN_NTSTATUS_IF_TRUE(usbAudioDataFormat == nullptr, STATUS_INSUFFICIENT_RESOURCES);

    if (m_usbAudioDataFormat != nullptr)
    {
        usbAudioDataFormat->Append(m_usbAudioDataFormat);
    }
    m_usbAudioDataFormat = usbAudioDataFormat;
    m_numOfFormats++;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DESCRIPTOR, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
PAGED_CODE_SEG
USBAudioDataFormat *
USBAudioDataFormatManager::GetUSBAudioDataFormat(ULONG index)
{
    USBAudioDataFormat * usbAudioDataFormat = m_usbAudioDataFormat;

    PAGED_CODE();

    ASSERT(index < m_numOfFormats);
    if (index >= m_numOfFormats)
    {
        return nullptr;
    }

    for (ULONG i = 0; (i < index) && (usbAudioDataFormat != nullptr); i++)
    {
        usbAudioDataFormat = usbAudioDataFormat->GetNext();
    }

    return usbAudioDataFormat;
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG
USBAudioDataFormatManager::GetNumOfUSBAudioDataFormats()
{
    PAGED_CODE();

    return m_numOfFormats;
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG
USBAudioDataFormatManager::GetSupportedSampleFormats()
{
    ULONG                supportedSampleFormats = 0;
    USBAudioDataFormat * usbAudioDataFormat = m_usbAudioDataFormat;

    PAGED_CODE();

    while (usbAudioDataFormat != nullptr)
    {

        UACSampleFormat sampleFormat = USBAudioDataFormat::ConvertFormatToSampleFormat(usbAudioDataFormat->GetFormatType(), usbAudioDataFormat->GetFormat());

        supportedSampleFormats |= (1 << toULong(sampleFormat));

        usbAudioDataFormat = usbAudioDataFormat->GetNext();
    }

    return supportedSampleFormats;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR
USBAudioDataFormatManager::GetBytesPerSample(ULONG index)
{
    USBAudioDataFormat * usbAudioDataFormat = GetUSBAudioDataFormat(index);

    PAGED_CODE();

    ASSERT(usbAudioDataFormat != nullptr);

    if (usbAudioDataFormat != nullptr)
    {
        return usbAudioDataFormat->GetBytesPerSample();
    }
    return 0;
}

_Use_decl_annotations_
PAGED_CODE_SEG
UCHAR
USBAudioDataFormatManager::GetValidBits(ULONG index)
{
    USBAudioDataFormat * usbAudioDataFormat = GetUSBAudioDataFormat(index);

    PAGED_CODE();

    ASSERT(usbAudioDataFormat != nullptr);
    if (usbAudioDataFormat != nullptr)
    {
        return usbAudioDataFormat->GetValidBits();
    }
    return 0;
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG
USBAudioDataFormatManager::GetFormatType(ULONG index)
{
    USBAudioDataFormat * usbAudioDataFormat = GetUSBAudioDataFormat(index);

    PAGED_CODE();

    ASSERT(usbAudioDataFormat != nullptr);
    if (usbAudioDataFormat != nullptr)
    {
        return usbAudioDataFormat->GetFormatType();
    }
    return 0;
}

_Use_decl_annotations_
PAGED_CODE_SEG
ULONG
USBAudioDataFormatManager::GetFormat(ULONG index)
{
    USBAudioDataFormat * usbAudioDataFormat = GetUSBAudioDataFormat(index);

    PAGED_CODE();

    ASSERT(usbAudioDataFormat != nullptr);
    if (usbAudioDataFormat != nullptr)
    {
        return usbAudioDataFormat->GetFormat();
    }
    return 0;
}
