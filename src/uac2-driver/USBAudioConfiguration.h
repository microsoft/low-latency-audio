// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

/*++

Module Name:

    USBAudioConfiguration.h

Abstract:

    Definition of classes that parses and manages the USB device descriptor.

Environment:

    Kernel-mode Driver Framework

--*/

#ifndef _USB_AUDIO_CONFIGURATION_H_
#define _USB_AUDIO_CONFIGURATION_H_

#include <acx.h>
#include "UAC_User.h"
#include "USBAudio.h"
#include "USBAudioDataFormat.h"

typedef struct _CURRENT_SETTINGS
{
    UCHAR DeviceClass{0};
    UCHAR InterfaceNumber{0};
    UCHAR AlternateSetting{0};
    UCHAR EndpointAddress{0};
    UCHAR InterfaceClass{0};
    UCHAR InterfaceProtocol{0};
    UCHAR FeedbackInterfaceNumber{0};
    UCHAR FeedbackAlternateSetting{0};
    UCHAR FeedbackEndpointAddress{0};
    UCHAR FeedbackInterval{0};
    UCHAR Channels{0};
    UCHAR ChannelNames{0};
    ULONG BytesPerSample{0};
    ULONG ValidBitsPerSample{0};
    ULONG LockDelay{0};
    ULONG MaxFramesPerPacket{0};
    ULONG MaxPacketSize{0};
    // ULONG AltSupportedSampleRate{0};
    UCHAR TerminalLink{0};
    // UCHAR SamplePerFrame{0};
    // UCHAR ActiveAlternateSetting{0};
    // ULONG ValidAlternateSettingMap{0};
    bool IsDeviceAdaptive;
    bool IsDeviceSynchronous;
} CURRENT_SETTINGS, *PCURRENT_SETTINGS;

template <class T, ULONG I>
class VariableArray
{
  public:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    VariableArray();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual ~VariableArray();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS Set(
        _In_ WDFOBJECT parentObject,
        _In_ ULONG     index,
        _In_ T         data
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS Get(
        _In_ ULONG index,
        _Out_ T &  data
    ) const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS Append(
        _In_ WDFOBJECT parentObject,
        _In_ T         data
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ULONG GetNumOfArray() const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void Report() const;

  protected:
    WDFMEMORY m_memory{nullptr};
    T *       m_array{nullptr};
    ULONG     m_sizeOfArray{0};
    ULONG     m_numOfArray{0};

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS Allocate(
        _In_ WDFOBJECT parentObject,
        _In_ ULONG     sizeOfArray
    );
};

class USBAudioEndpoint
{
  public:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    USBAudioEndpoint(
        _In_ WDFOBJECT                parentObject,
        _In_ PUSB_ENDPOINT_DESCRIPTOR endpoint
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual ~USBAudioEndpoint();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    UCHAR GetEndpointAddress() const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    UCHAR GetEndpointAttribute() const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    IsoDirection GetDirection() const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    USHORT GetMaxPacketSize() const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    UCHAR GetInterval() const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    UCHAR GetAttributes() const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    static USBAudioEndpoint * Create(
        _In_ WDFOBJECT                      parentObject,
        _In_ const PUSB_ENDPOINT_DESCRIPTOR descriptor
    );

  protected:
    WDFOBJECT                m_parentObject{nullptr};
    PUSB_ENDPOINT_DESCRIPTOR m_endpointDescriptor{nullptr};
};

class USBAudioEndpointCompanion
{
  public:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    USBAudioEndpointCompanion(
        _In_ WDFOBJECT                                     parentObject,
        _In_ PUSB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR endpoint
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual ~USBAudioEndpointCompanion();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    UCHAR GetMaxBurst() const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    USHORT GetBytesPerInterval() const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    static USBAudioEndpointCompanion * Create(
        _In_ WDFOBJECT                                           parentObject,
        _In_ const PUSB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR descriptor
    );

  protected:
    WDFOBJECT                                     m_parentObject{nullptr};
    PUSB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR m_endpointCompanionDescriptor{nullptr};
};

class USBAudioInterface
{
  public:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    USBAudioInterface(
        _In_ WDFOBJECT                 parentObject,
        _In_ PUSB_INTERFACE_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual ~USBAudioInterface();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetEndpoint(
        _In_ const PUSB_ENDPOINT_DESCRIPTOR endpoint
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual UCHAR GetLength() const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual UCHAR GetDescriptorType() const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual UCHAR GetInterfaceNumber() const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual UCHAR GetAlternateSetting() const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual UCHAR GetNumEndpoints() const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual UCHAR GetInterfaceClass() const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual UCHAR GetInterfaceSubClass() const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual UCHAR GetInterfaceProtocol() const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual UCHAR GetInterface() const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(return == true)
    virtual bool GetEndpointAddress(
        _In_ ULONG    index,
        _Out_ UCHAR & endpointAddress
    ) const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual UCHAR GetEndpointAddress();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(return == true)
    virtual bool GetEndpointAttribute(
        _In_ ULONG    index,
        _Out_ UCHAR & endpointAttribute
    ) const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual UCHAR GetEndpointAttribute();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(return == true)
    virtual bool GetMaxPacketSize(
        _In_ IsoDirection direction,
        _Out_ USHORT &    maxPacketSize
    ) const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(return == true)
    virtual bool GetMaxPacketSize(
        _In_ ULONG     index,
        _Out_ USHORT & maxPacketSize
    ) const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(return == true)
    virtual bool GetInterval(
        _In_ ULONG    index,
        _Out_ UCHAR & bInterval
    ) const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(return == true)
    virtual bool GetAttributes(
        _In_ ULONG    index,
        _Out_ UCHAR & bmAttributes
    ) const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetEndpointCompanion(
        _In_ const PUSB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR endpoint
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(return == true)
    virtual bool GetBytesPerInterval(
        _In_ ULONG     index,
        _Out_ USHORT & wBytesPerInterval
    ) const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual bool IsEndpointTypeSupported(
        _In_ UCHAR endpointType
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual bool IsEndpointTypeIsochronousSynchronizationSupported(
        _In_ UCHAR synchronizationType
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual bool IsSupportDirection(
        _In_ bool isInput
    );

    virtual NTSTATUS QueryCurrentAttributeAll(
        _In_ PDEVICE_CONTEXT deviceContext
    ) = 0;

    virtual bool IsStreamInterface() = 0;

    virtual bool IsControlInterface() = 0;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual PUSB_INTERFACE_DESCRIPTOR & GetInterfaceDescriptor();

  protected:
    WDFOBJECT                    m_parentObject{nullptr};
    PUSB_INTERFACE_DESCRIPTOR    m_interfaceDescriptor{nullptr};
    USBAudioEndpoint **          m_usbAudioEndpoints{nullptr};
    WDFMEMORY                    m_usbAudioEndpointsMemory{nullptr};
    USBAudioEndpointCompanion ** m_usbAudioEndpointCompanions{nullptr};
    WDFMEMORY                    m_usbAudioEndpointCompanionsMemory{nullptr};
    ULONG                        m_numOfEndpoint{0};
    ULONG                        m_numOfEndpointCompanion{0};
};

class USBAudioControlInterface : public USBAudioInterface
{
  public:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    USBAudioControlInterface(
        _In_ WDFOBJECT                 parentObject,
        _In_ PUSB_INTERFACE_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual ~USBAudioControlInterface();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual bool IsStreamInterface();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual bool IsControlInterface();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetGenericAudioDescriptor(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    );

    virtual NTSTATUS SetClockSource(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    ) = 0;

    virtual NTSTATUS SetInputTerminal(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    ) = 0;

    virtual NTSTATUS SetOutputTerminal(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    ) = 0;

    virtual NTSTATUS SetMixerUnit(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    ) = 0;

    virtual NTSTATUS SetSelectorUnit(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    ) = 0;

    virtual NTSTATUS SetFeatureUnit(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    ) = 0;

    virtual NTSTATUS SetProcesingUnit(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    ) = 0;

    virtual NTSTATUS SetExtensionUnit(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    ) = 0;

    virtual NTSTATUS SetClockSelector(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    ) = 0;

    virtual NTSTATUS SetClockMultiplier(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    ) = 0;

    virtual NTSTATUS SetSampleRateConverter(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    ) = 0;

    virtual NTSTATUS QueryRangeAttributeAll(
        _In_ PDEVICE_CONTEXT deviceContext
    ) = 0;

    virtual NTSTATUS SearchOutputTerminalFromInputTerminal(
        _In_ UCHAR     terminalLink,
        _Out_ UCHAR &  numOfChannels,
        _Out_ USHORT & terminalType,
        _Out_ UCHAR &  volumeUnitID,
        _Out_ UCHAR &  muteUnitID
    ) = 0;

    virtual NTSTATUS SearchInputTerminalFromOutputTerminal(
        _In_ UCHAR     terminalLink,
        _Out_ UCHAR &  numOfChannels,
        _Out_ USHORT & terminalType,
        _Out_ UCHAR &  volumeUnitID,
        _Out_ UCHAR &  muteUnitID
    ) = 0;

    virtual NTSTATUS SetCurrentSampleFrequency(
        _In_ PDEVICE_CONTEXT deviceContext,
        _In_ ULONG           desiredSampleRate
    ) = 0;

    virtual NTSTATUS GetCurrentSampleFrequency(
        _In_ PDEVICE_CONTEXT deviceContext,
        _Out_ ULONG &        sampleRate
    ) = 0;

    virtual NTSTATUS GetCurrentSupportedSampleFrequency(
        _In_ PDEVICE_CONTEXT deviceContext,
        _In_ ULONG &         supportedSampleRate
    ) = 0;

  protected:
    enum
    {
        MAX_AUDIO_DESCRIPTOR = 30
    };

    ULONG                                                                          m_inputCurrentSampleRate{0};
    ULONG                                                                          m_inputSupportedSampleRate{0};
    ULONG                                                                          m_outputCurrentSampleRate{0};
    ULONG                                                                          m_outputSupportedSampleRate{0};
    VariableArray<NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR, MAX_AUDIO_DESCRIPTOR> m_genericAudioDescriptorInfo;
};

class USBAudioStreamInterface : public USBAudioInterface
{
  public:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    USBAudioStreamInterface(
        _In_ WDFOBJECT                 parentObject,
        _In_ PUSB_INTERFACE_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual ~USBAudioStreamInterface();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual bool IsStreamInterface();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual bool IsControlInterface();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual bool IsInterfaceSupportingFormats() = 0;

    virtual NTSTATUS CheckInterfaceConfiguration(
        _In_ PDEVICE_CONTEXT deviceContext
    ) = 0;

    virtual NTSTATUS SetFormatType(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    ) = 0;

    virtual NTSTATUS SetGeneral(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    ) = 0;

    virtual NTSTATUS SetIsochronousAudioDataEndpoint(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    ) = 0;

    virtual UCHAR GetCurrentTerminalLink() = 0;

    virtual UCHAR GetCurrentBmControls() = 0;

    virtual UCHAR GetCurrentChannels() = 0;

    virtual UCHAR GetCurrentChannelNames() = 0;

    virtual ULONG GetMaxSupportedBytesPerSample() = 0;

    virtual ULONG GetMaxSupportedValidBitsPerSample() = 0;

    virtual UCHAR GetCurrentActiveAlternateSetting() = 0;

    virtual ULONG GetCurrentValidAlternateSettingMap() = 0;

    virtual UCHAR GetValidBitsPerSample() = 0;

    virtual UCHAR GetBytesPerSample() = 0;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual ULONG GetLockDelay();

    virtual bool HasInputIsochronousEndpoint() = 0;

    virtual bool HasOutputIsochronousEndpoint() = 0;

    virtual bool HasFeedbackEndpoint() = 0;

    virtual UCHAR GetFeedbackEndpointAddress() = 0;

    virtual UCHAR GetFeedbackInterval() = 0;

    virtual bool IsValidAudioDataFormat(
        _In_ ULONG formatType,
        _In_ ULONG audioDataFormat
    ) = 0;

    virtual NTSTATUS RegisterUSBAudioDataFormatManager(
        _In_ USBAudioDataFormatManager & usbAudioDataFormatManagerIn,
        _In_ USBAudioDataFormatManager & usbAudioDataFormatManagerOut
    ) = 0;

  protected:
    ULONG m_lockDelay{0};
};

class USBAudio1ControlInterface : public USBAudioControlInterface
{
  public:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    USBAudio1ControlInterface(
        _In_ WDFOBJECT                 parentObject,
        _In_ PUSB_INTERFACE_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual ~USBAudio1ControlInterface();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetClockSource(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetInputTerminal(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetOutputTerminal(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetMixerUnit(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetSelectorUnit(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetFeatureUnit(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetProcesingUnit(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetExtensionUnit(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetClockSelector(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetClockMultiplier(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetSampleRateConverter(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS QueryCurrentAttributeAll(
        _In_ PDEVICE_CONTEXT deviceContext
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS QueryRangeAttributeAll(
        _In_ PDEVICE_CONTEXT deviceContext
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SearchOutputTerminalFromInputTerminal(
        _In_ UCHAR     terminalLink,
        _Out_ UCHAR &  numOfChannels,
        _Out_ USHORT & terminalType,
        _Out_ UCHAR &  volumeUnitID,
        _Out_ UCHAR &  muteUnitID
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SearchInputTerminalFromOutputTerminal(
        _In_ UCHAR     terminalLink,
        _Out_ UCHAR &  numOfChannels,
        _Out_ USHORT & terminalType,
        _Out_ UCHAR &  volumeUnitID,
        _Out_ UCHAR &  muteUnitID
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetCurrentSampleFrequency(
        _In_ PDEVICE_CONTEXT deviceContext,
        _In_ ULONG           desiredSampleRate
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS GetCurrentSampleFrequency(
        _In_ PDEVICE_CONTEXT deviceContext,
        _Out_ ULONG &        sampleRate
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS GetCurrentSupportedSampleFrequency(
        _In_ PDEVICE_CONTEXT deviceContext,
        _In_ ULONG &         supportedSampleRate
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    static USBAudio1ControlInterface * Create(
        _In_ WDFOBJECT                       parentObject,
        _In_ const PUSB_INTERFACE_DESCRIPTOR descriptor
    );

  protected:
};

class USBAudio1StreamInterface : public USBAudioStreamInterface
{
  public:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    USBAudio1StreamInterface(
        _In_ WDFOBJECT                 parentObject,
        _In_ PUSB_INTERFACE_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual ~USBAudio1StreamInterface();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS QueryCurrentAttributeAll(
        _In_ PDEVICE_CONTEXT deviceContext
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual bool IsInterfaceSupportingFormats();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS CheckInterfaceConfiguration(
        _In_ PDEVICE_CONTEXT deviceContext
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetFormatType(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetGeneral(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    );

    virtual NTSTATUS SetIsochronousAudioDataEndpoint(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual UCHAR GetCurrentTerminalLink();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual UCHAR GetCurrentBmControls();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual UCHAR GetCurrentChannels();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual UCHAR GetCurrentChannelNames();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual ULONG GetMaxSupportedBytesPerSample();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual ULONG GetMaxSupportedValidBitsPerSample();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual UCHAR GetCurrentActiveAlternateSetting();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual ULONG GetCurrentValidAlternateSettingMap();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual UCHAR GetValidBitsPerSample();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual UCHAR GetBytesPerSample();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual bool HasInputIsochronousEndpoint();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual bool HasOutputIsochronousEndpoint();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual bool HasFeedbackEndpoint();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual UCHAR GetFeedbackEndpointAddress();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual UCHAR GetFeedbackInterval();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual bool IsValidAudioDataFormat(
        _In_ ULONG formatType,
        _In_ ULONG audioDataFormat
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS RegisterUSBAudioDataFormatManager(
        _In_ USBAudioDataFormatManager & usbAudioDataFormatManagerIn,
        _In_ USBAudioDataFormatManager & usbAudioDataFormatManagerOut
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    static USBAudio1StreamInterface * Create(
        _In_ WDFOBJECT                       parentObject,
        _In_ const PUSB_INTERFACE_DESCRIPTOR descriptor
    );

  protected:
    NS_USBAudio0100::PCS_AS_INTERFACE_DESCRIPTOR                       m_csAsInterfaceDescriptor{nullptr};
    NS_USBAudio0100::PCS_AS_ISOCHRONOUS_AUDIO_DATA_ENDPOINT_DESCRIPTOR m_isochronousAudioDataEndpointDescriptor{nullptr};
};

class USBAudio2ControlInterface : public USBAudioControlInterface
{
  public:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    USBAudio2ControlInterface(
        _In_ WDFOBJECT                 parentObject,
        _In_ PUSB_INTERFACE_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual ~USBAudio2ControlInterface();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetClockSource(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetInputTerminal(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetOutputTerminal(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetMixerUnit(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetSelectorUnit(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetFeatureUnit(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetProcesingUnit(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetExtensionUnit(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetClockSelector(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetClockMultiplier(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetSampleRateConverter(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS QueryCurrentAttributeAll(
        _In_ PDEVICE_CONTEXT deviceContext
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS QueryRangeAttributeAll(
        _In_ PDEVICE_CONTEXT deviceContext
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SearchOutputTerminalFromInputTerminal(
        _In_ UCHAR     terminalLink,
        _Out_ UCHAR &  numOfChannels,
        _Out_ USHORT & terminalType,
        _Out_ UCHAR &  volumeUnitID,
        _Out_ UCHAR &  muteUnitID
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SearchInputTerminalFromOutputTerminal(
        _In_ UCHAR     terminalLink,
        _Out_ UCHAR &  numOfChannels,
        _Out_ USHORT & terminalType,
        _Out_ UCHAR &  volumeUnitID,
        _Out_ UCHAR &  muteUnitID
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetCurrentSampleFrequency(
        _In_ PDEVICE_CONTEXT deviceContext,
        _In_ ULONG           desiredSampleRate
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS GetCurrentSampleFrequency(
        _In_ PDEVICE_CONTEXT deviceContext,
        _Out_ ULONG &        sampleRate
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS GetCurrentSupportedSampleFrequency(
        _In_ PDEVICE_CONTEXT deviceContext,
        _In_ ULONG &         supportedSampleRate
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    static USBAudio2ControlInterface * Create(
        _In_ WDFOBJECT                       parentObject,
        _In_ const PUSB_INTERFACE_DESCRIPTOR descriptor
    );

  protected:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS QueryCurrentSampleFrequency(
        _In_ PDEVICE_CONTEXT deviceContext
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS GetCurrentSupportedSampleFrequency(
        _In_ PDEVICE_CONTEXT deviceContext,
        _In_ UCHAR           clockSourceID,
        _In_ ULONG &         supportedSampleRate
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS GetCurrentFeatureUnit(
        _In_ PDEVICE_CONTEXT deviceContext
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS GetRangeSampleFrequency(
        _In_ PDEVICE_CONTEXT deviceContext,
        _In_ UCHAR           clockSourceID
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS GetRangeSampleFrequency(
        _In_ PDEVICE_CONTEXT deviceContext
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS GetRangeFeatureUnit(
        _In_ PDEVICE_CONTEXT deviceContext
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS GetCurrentClockSourceID(
        _In_ PDEVICE_CONTEXT deviceContext,
        _Out_ UCHAR &        clockID
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS GetCurrentClockSourceID(
        _In_ PDEVICE_CONTEXT deviceContext,
        _In_ bool            isInput,
        _Out_ UCHAR &        clockID
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS GetCurrentClockSourceID(
        _In_ PDEVICE_CONTEXT deviceContext,
        _Out_ UCHAR &        inputClockID,
        _Out_ UCHAR &        outputClockID
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS SetCurrentClockSourceInternal(
        _In_ PDEVICE_CONTEXT deviceContext
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS USBAudio2ControlInterface::SearchOutputTerminal(
        _Inout_ UCHAR &  sourceID,
        _Inout_ UCHAR &  numOfChannels,
        _Inout_ USHORT & terminalType,
        _Inout_ UCHAR &  volumeUnitID,
        _Inout_ UCHAR &  muteUnitID,
        _In_ SCHAR       recursionCount
    );

    enum
    {
        MAX_TERMINAL = 10,
        MAX_FEATURE_UNIT = 10
    };

    // NS_USBAudio0200::PCS_AC_INTERFACE_HEADER_DESCRIPTOR m_interfaceDescriptor{nullptr};
    NS_USBAudio0200::PCS_AC_CLOCK_SELECTOR_DESCRIPTOR                                    m_clockSelectorDescriptor{nullptr};
    VariableArray<NS_USBAudio0200::PCS_AC_CLOCK_SOURCE_DESCRIPTOR, UAC_MAX_CLOCK_SOURCE> m_acClockSourceInfo;
    VariableArray<NS_USBAudio0200::PCS_AC_OUTPUT_TERMINAL_DESCRIPTOR, MAX_TERMINAL>      m_acOutputTerminalInfo;
    VariableArray<NS_USBAudio0200::PCS_AC_INPUT_TERMINAL_DESCRIPTOR, MAX_TERMINAL>       m_acInputTerminalInfo;
    VariableArray<NS_USBAudio0200::PCS_AC_FEATURE_UNIT_DESCRIPTOR, MAX_FEATURE_UNIT>     m_acFeatureUnitInfo;
};

class USBAudio2StreamInterface : public USBAudioStreamInterface
{
  public:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    USBAudio2StreamInterface(
        _In_ WDFOBJECT                 parentObject,
        _In_ PUSB_INTERFACE_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual ~USBAudio2StreamInterface();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS QueryCurrentAttributeAll(
        _In_ PDEVICE_CONTEXT deviceContext
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual bool IsInterfaceSupportingFormats();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS CheckInterfaceConfiguration(
        _In_ PDEVICE_CONTEXT deviceContext
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetFormatType(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetGeneral(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SetIsochronousAudioDataEndpoint(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual UCHAR GetCurrentTerminalLink();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual UCHAR GetCurrentBmControls();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual UCHAR GetCurrentChannels();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual UCHAR GetCurrentChannelNames();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual ULONG GetMaxSupportedBytesPerSample();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual ULONG GetMaxSupportedValidBitsPerSample();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual UCHAR GetCurrentActiveAlternateSetting();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual ULONG GetCurrentValidAlternateSettingMap();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual UCHAR GetValidBitsPerSample();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual UCHAR GetBytesPerSample();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual bool HasInputIsochronousEndpoint();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual bool HasOutputIsochronousEndpoint();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual bool HasFeedbackEndpoint();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual UCHAR GetFeedbackEndpointAddress();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual UCHAR GetFeedbackInterval();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual bool IsValidAudioDataFormat(
        _In_ ULONG formatType,
        _In_ ULONG audioDataFormat
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS UpdateCurrentACTValAltSettingsControl(
        _In_ PDEVICE_CONTEXT deviceContext
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS UpdateCurrentACTAltSettingsControl(
        _In_ PDEVICE_CONTEXT deviceContext
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS UpdateCurrentAudioDataFormat(
        _In_ PDEVICE_CONTEXT deviceContext
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS RegisterUSBAudioDataFormatManager(
        _In_ USBAudioDataFormatManager & usbAudioDataFormatManagerIn,
        _In_ USBAudioDataFormatManager & usbAudioDataFormatManagerOut
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    static USBAudio2StreamInterface * Create(
        _In_ WDFOBJECT                       parentObject,
        _In_ const PUSB_INTERFACE_DESCRIPTOR descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    static bool IsValidAlternateSetting(
        _In_ ULONG validAlternateSettingMap,
        _In_ UCHAR alternateSetting
    );

  protected:
    UCHAR                                                              m_activeAlternateSetting{0};                          // valid only when alternate interface is 0
    ULONG                                                              m_validAlternateSettingMap{0};                        // valid only when alternate interface is 0
    ULONG                                                              m_formatType{NS_USBAudio0200::FORMAT_TYPE_UNDEFINED}; // valid only when alternate interface is not 0
    ULONG                                                              m_audioDataFormat{NS_USBAudio0200::PCM};              // valid only when alternate interface is not 0
    bool                                                               m_enableGetFormatType{false};
    USBAudioDataFormat *                                               m_usbAudioDataFormat{nullptr};
    NS_USBAudio0200::PCS_AS_TYPE_I_FORMAT_TYPE_DESCRIPTOR              m_formatITypeDescriptor{nullptr};
    NS_USBAudio0200::PCS_AS_TYPE_III_FORMAT_TYPE_DESCRIPTOR            m_formatIIITypeDescriptor{nullptr};
    NS_USBAudio0200::PCS_AS_INTERFACE_DESCRIPTOR                       m_csAsInterfaceDescriptor{nullptr};
    NS_USBAudio0200::PCS_AS_ISOCHRONOUS_AUDIO_DATA_ENDPOINT_DESCRIPTOR m_isochronousAudioDataEndpointDescriptor{nullptr};
};

class USBAudioInterfaceInfo
{
    enum
    {
        DEFAULT_SIZE_OF_ALTERNATE_INTERFACES = 3
    };

  public:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    USBAudioInterfaceInfo(
        _In_ WDFOBJECT parentObject
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual ~USBAudioInterfaceInfo();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS StoreInterface(
        _In_ USBAudioInterface * interface
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS GetInterfaceNumber(
        _Out_ ULONG & interfaceNumber
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    bool IsStreamInterface();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    bool IsControlInterface();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS QueryCurrentAttributeAll(
        _In_ PDEVICE_CONTEXT deviceContext
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS QueryRangeAttributeAll(
        _In_ PDEVICE_CONTEXT deviceContext
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS CheckInterfaceConfiguration(
        _In_ PDEVICE_CONTEXT deviceContext
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(return == true)
    bool GetMaxPacketSize(
        _In_ IsoDirection direction,
        _Out_ ULONG &     maxPacketSize
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    GetMaxSupportedValidBitsPerSample(
        _In_ bool     isInput,
        _In_ ULONG    desiredFormatType,
        _In_ ULONG    desiredFormat,
        _Out_ ULONG & maxSupportedBytesPerSample,
        _Out_ ULONG & maxSupportedValidBitsPerSample
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    GetNearestSupportedValidBitsPerSamples(
        _In_ bool       isInput,
        _In_ ULONG      desiredFormatType,
        _In_ ULONG      desiredFormat,
        _Inout_ ULONG & nearestSupportedBytesPerSample,
        _Inout_ ULONG & nearestSupportedValidBitsPerSample
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    bool IsSupportDirection(
        _In_ bool isInput
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(return == true)
    bool GetTerminalLink(
        _Out_ UCHAR & terminalLink
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS SetCurrentSampleFrequency(
        _In_ PDEVICE_CONTEXT deviceContext,
        _In_ ULONG           desiredSampleRate
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS GetCurrentSampleFrequency(
        _In_ PDEVICE_CONTEXT deviceContext,
        _Out_ ULONG &        sampleRate
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS SelectAlternateInterface(
        _In_ PDEVICE_CONTEXT       deviceContext,
        _In_ bool                  isInput,
        _In_ ULONG                 desiredFormatType,
        _In_ ULONG                 desiredFormat,
        _In_ ULONG                 desiredBytesPerSample,
        _In_ ULONG                 desiredValidBitsPerSample,
        _Inout_ CURRENT_SETTINGS & currentSettings
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SearchOutputTerminalFromInputTerminal(
        _In_ UCHAR     terminalLink,
        _Out_ UCHAR &  numOfChannels,
        _Out_ USHORT & terminalType,
        _Out_ UCHAR &  volumeUnitID,
        _Out_ UCHAR &  muteUnitID
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SearchInputTerminalFromOutputTerminal(
        _In_ UCHAR     terminalLink,
        _Out_ UCHAR &  numOfChannels,
        _Out_ USHORT & terminalType,
        _Out_ UCHAR &  volumeUnitID,
        _Out_ UCHAR &  muteUnitID
    );

  protected:
    WDFOBJECT                                                                m_parentObject{nullptr};
    VariableArray<USBAudioInterface *, DEFAULT_SIZE_OF_ALTERNATE_INTERFACES> m_usbAudioAlternateInterfaces;
};

class USBAudioConfiguration
{
  public:
    enum
    {
        InvalidID = 0xff,
        InvalidString = 0x00
    };

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    USBAudioConfiguration(
        _In_ PDEVICE_CONTEXT        deviceContext,
        _In_ PUSB_DEVICE_DESCRIPTOR usbDeviceDescriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual ~USBAudioConfiguration();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS ParseDescriptors(
        _In_ PUSB_CONFIGURATION_DESCRIPTOR usbConfigurationDescriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS QueryDeviceFeatures();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS CheckInterfaceConfiguration();

    // __drv_maxIRQL(PASSIVE_LEVEL)
    // PAGED_CODE_SEG
    // NTSTATUS ResolveDangling();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS ActivateAudioInterface(
        _In_ ULONG desiredSampleRate,
        _In_ ULONG desiredFormatType,
        _In_ ULONG desiredFormat,
        _In_ ULONG inputDesiredBytesPerSample,
        _In_ ULONG inputDesiredValidBitsPerSample,
        _In_ ULONG outputDesiredBytesPerSample,
        _In_ ULONG outputDesiredValidBitsPerSample,
        _In_ bool  forceSetSampleRate
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    GetStreamChannelInfo(
        _In_ bool      isInput,
        _Out_ UCHAR &  numOfChannels,
        _Out_ USHORT & terminalType,
        _Out_ UCHAR &  volumeUnitID,
        _Out_ UCHAR &  muteUnitID
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    GetStreamDevices(
        _In_ bool     isInput,
        _Out_ ULONG & numOfDevices
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    GetStreamChannels(
        _In_ bool     isInput,
        _Out_ UCHAR & numOfChannels
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS GetChannelName(
        _In_ bool         isInput,
        _In_ ULONG        channel,
        _Out_ WDFMEMORY & memory,
        _Out_ PWSTR &     channelName
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS GetStereoChannelName(
        _In_ bool         isInput,
        _In_ ULONG        channel,
        _Out_ WDFMEMORY & memory,
        _Out_ PWSTR &     channelName
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ULONG
    GetMaxPacketSize(
        _In_ IsoDirection direction
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    GetMaxSupportedValidBitsPerSample(
        _In_ bool     isInput,
        _In_ ULONG    desiredFormatType,
        _In_ ULONG    desiredFormat,
        _Out_ ULONG & maxSupportedBytesPerSample,
        _Out_ ULONG & maxSupportedValidBitsPerSample
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    GetNearestSupportedValidBitsPerSamples(
        _In_ bool       isInput,
        _In_ ULONG      desiredFormatType,
        _In_ ULONG      desiredFormat,
        _Inout_ ULONG & nearestSupportedBytesPerSample,
        _Inout_ ULONG & nearestSupportedValidBitsPerSample
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    GetNearestSupportedSampleRate(
        _Inout_ ULONG & sampleRate
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    USBAudioDataFormatManager *
    GetUSBAudioDataFormatManager(
        _In_ bool isInput
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    bool isInterfaceProtocolUSBAudio2(
        _In_ UCHAR interfaceProtocol
    ) const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    bool isUSBAudio2() const;

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    bool hasInputIsochronousInterface() const;

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    bool hasOutputIsochronousInterface() const;

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    bool hasInputAndOutputIsochronousInterfaces() const;

    static __drv_maxIRQL(DISPATCH_LEVEL)
    PAGED_CODE_SEG
    USBAudioConfiguration * Create(
        _In_ PDEVICE_CONTEXT        deviceContext,
        _In_ PUSB_DEVICE_DESCRIPTOR usbDeviceDescriptor
    );

  protected:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS CreateInterface(
        _In_ const PUSB_INTERFACE_DESCRIPTOR descriptor,
        _Out_ USBAudioInterface *&           usbAudioInterface
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS ParseInterfaceDescriptor(
        _In_ PUSB_INTERFACE_DESCRIPTOR descriptor,
        _Inout_ USBAudioInterface *&   lastInterface,
        _In_ bool &                    hasTargetInterface
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS ParseEndpointDescriptor(
        _In_ PUSB_ENDPOINT_DESCRIPTOR descriptor,
        _Inout_ USBAudioInterface *&  lastInterface
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS ParseEndpointCompanionDescriptor(
        _In_ PUSB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR descriptor,
        _Inout_ USBAudioInterface *&                       lastInterface
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS ParseCSInterface(
        _In_ const NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor,
        _Inout_ USBAudioInterface *&                         lastInterface
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS ParseCSEndpoint(
        _In_ NS_USBAudio::PCS_GENERIC_AUDIO_DESCRIPTOR descriptor,
        _Inout_ USBAudioInterface *&                   lastInterface
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS SetCurrentSampleFrequency(
        _In_ PDEVICE_CONTEXT deviceContext,
        _In_ ULONG           desiredSampleRate
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS GetCurrentSampleFrequency(
        _In_ PDEVICE_CONTEXT deviceContext,
        _Out_ ULONG &        sampleRate
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS SelectAlternateInterface(
        _In_ PDEVICE_CONTEXT deviceContext,
        _In_ bool            isInput,
        _In_ ULONG           desiredFormatType,
        _In_ ULONG           desiredFormat,
        _In_ ULONG           desiredBytesPerSample,
        _In_ ULONG           desiredValidBitsPerSample
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    static NTSTATUS
    GetDescriptor(
        _In_ WDFUSBDEVICE usbDevice,
        _In_ UCHAR        urbDescriptorType,
        _In_ UCHAR        index,
        _In_ USHORT       languageId,
        _Out_ WDFMEMORY & memory,
        _Out_ PVOID &     descriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    static NTSTATUS
    GetStringDescriptor(
        _In_ WDFUSBDEVICE usbDevice,
        _In_ UCHAR        index,
        _In_ USHORT       languageId,
        _Out_ WDFMEMORY & memory,
        _Out_ PWSTR &     string
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    static NTSTATUS
    GetDefaultProductName(
        _In_ WDFOBJECT    parentObject,
        _Out_ WDFMEMORY & memory,
        _Out_ PWSTR &     string
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SearchOutputTerminalFromInputTerminal(
        _In_ UCHAR     terminalLink,
        _Out_ UCHAR &  numOfChannels,
        _Out_ USHORT & terminalType,
        _Out_ UCHAR &  volumeUnitID,
        _Out_ UCHAR &  muteUnitID
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    virtual NTSTATUS SearchInputTerminalFromOutputTerminal(
        _In_ UCHAR     terminalLink,
        _Out_ UCHAR &  numOfChannels,
        _Out_ USHORT & terminalType,
        _Out_ UCHAR &  volumeUnitID,
        _Out_ UCHAR &  muteUnitID
    );

    PDEVICE_CONTEXT               m_deviceContext{nullptr};
    PUSB_DEVICE_DESCRIPTOR        m_usbDeviceDescriptor{nullptr};
    PUSB_CONFIGURATION_DESCRIPTOR m_usbConfigurationDescriptor{nullptr};
    USBAudioInterfaceInfo **      m_usbAudioInterfaceInfoes{nullptr};
    WDFMEMORY                     m_usbAudioInterfaceInfoesMemory{nullptr};
    bool                          m_isUSBAudio2{false};
    bool                          m_isInputIsochronousInterfaceExists{false};
    bool                          m_isOutputIsochronousInterfaceExists{false};
    USBAudioDataFormatManager     m_inputUsbAudioDataFormatManager;
    USBAudioDataFormatManager     m_outputUsbAudioDataFormatManager;
};

#endif
