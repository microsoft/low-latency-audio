// Copyright (c) Yamaha Corporation.
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

/*++

Module Name:

    USBAudioDataFormat.h

Abstract:

    Definitions of classes managing Audio Data Format Types I and III for USB Audio 2.0.

Environment:

    Kernel-mode Driver Framework

--*/

#ifndef _USB_AUDIO_FORMAT_H_
#define _USB_AUDIO_FORMAT_H_

#include <acx.h>
#include "UAC_User.h"
#include "USBAudio.h"

class USBAudioDataFormat
{
  public:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    USBAudioDataFormat();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    USBAudioDataFormat(
        _In_ UCHAR          formatType,
        _In_reads_(4) UCHAR formats[4],
        _In_ UCHAR          subslotSize,
        _In_ UCHAR          bitResolution
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual ~USBAudioDataFormat();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    UCHAR GetBytesPerSample();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    UCHAR GetValidBits();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ULONG GetFormatType();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ULONG GetFormat();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    bool IsEqualFormat(
        _In_ UCHAR          formatType,
        _In_reads_(4) UCHAR formats[4],
        _In_ UCHAR          subslotSize,
        _In_ UCHAR          bitResolution
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    bool operator==(_In_ USBAudioDataFormat const & format) const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    USBAudioDataFormat * GetNext();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    USBAudioDataFormat * Append(
        _In_ USBAudioDataFormat * nextUSBAudioDataFormat
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    static USBAudioDataFormat * Create(
        _In_ UCHAR          formatType,
        _In_reads_(4) UCHAR formats[4],
        _In_ UCHAR          subslotSize,
        _In_ UCHAR          bitResolution
    );

    static __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ULONG ConverBmFormats(
        _In_reads_(4) UCHAR formats[4]
    );

    static __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    bool IsSupportedFormat(
        _In_ ULONG formatType,
        _In_ ULONG format
    );

    static __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    UACSampleFormat ConvertFormatToSampleFormat(
        _In_ ULONG formatType,
        _In_ ULONG format
    );

    static __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS ConvertFormatToSampleFormat(
        _In_ UACSampleFormat sampleFormat,
        _Out_ ULONG &        formatType,
        _Out_ ULONG &        format
    );

    static __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    UACSampleType ConverSampleFormatToSampleType(
        _In_ UACSampleFormat sampleFormat,
        _In_ ULONG           bytesPerSample,
        _In_ ULONG           validBitsPerSample
    );

    static __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ULONG ConverSampleTypeToBytesPerSample(
        _In_ UACSampleType sampleType
    );

    static __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ULONG GetSampleFormatsTypeI();

    static __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ULONG GetSampleFormatsTypeIII();

    static __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS BuildWaveFormatExtensible(
        _In_ WDFOBJECT                               parentObject,
        _In_ ULONG                                   sampleRate,
        _In_ UCHAR                                   channels,
        _In_ UCHAR                                   bytesPerSample,
        _In_ UCHAR                                   validBits,
        _In_ ULONG                                   formatType,
        _In_ ULONG                                   format,
        _Inout_ PKSDATAFORMAT_WAVEFORMATEXTENSIBLE & ksDataFormatWaveFormatExtensible,
        _Inout_ WDFMEMORY &                          ksDataFormatWaveFormatExtensibleMemory
    );

  protected:
    UCHAR                m_formatType{0};
    ULONG                m_format{0};        // bmFormats[4]
    UCHAR                m_subslotSize{0};   // bSubslotSize
    UCHAR                m_bitResolution{0}; // bBitResolution
    USBAudioDataFormat * m_nextUSBAudioDataFormat{nullptr};
    // UCHAR m_channels;		   // bNrChannels
};

class USBAudioDataFormatManager
{
  public:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    USBAudioDataFormatManager();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual ~USBAudioDataFormatManager();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    SetUSBAudioDataFormat(
        _In_ UCHAR                  formatType,
        _In_reads_(4) UCHAR         formats[4],
        _In_ UCHAR                  subslotSize,
        _In_ UCHAR                  bitResolution,
        _Out_ USBAudioDataFormat *& usbAudioDataFormat
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    USBAudioDataFormat * GetUSBAudioDataFormat(
        _In_ ULONG index
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ULONG GetNumOfUSBAudioDataFormats();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ULONG GetSupportedSampleFormats();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    UCHAR GetBytesPerSample(
        _In_ ULONG index
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    UCHAR GetValidBits(
        _In_ ULONG index
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ULONG GetFormatType(
        _In_ ULONG index
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ULONG GetFormat(
        _In_ ULONG index
    );

  protected:
    USBAudioDataFormat * m_usbAudioDataFormat{nullptr};
    ULONG                m_numOfFormats{0};
};

#endif
