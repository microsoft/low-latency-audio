// Copyright (c) Yamaha Corporation.
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================
// ASIO is a trademark and software of Steinberg Media Technologies GmbH

/*++

Module Name:

    AudioIsochronousEngine.h

Abstract:

    Define a class to manage audio streaming.

Environment:

    Kernel-mode Driver Framework

--*/

#ifndef _AUDIOISOCHRONOUSENGINE_H_
#define _AUDIOISOCHRONOUSENGINE_H_

#include <windef.h>
#include <ks.h>

#include "public.h"
#include "UAC_User.h"
#include "USBAudio.h"

class USBAudioDataFormatManager;
class USBAudioStreamInterfaceGroup;
enum class TraversalDirection;
enum class AudioNodeKind;

class AudioIsochronousEngine
{
  public:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    AudioIsochronousEngine(
        _In_ PDEVICE_CONTEXT                deviceContext,
        _In_ USBAudioStreamInterfaceGroup * usbAudioStreamInterfaceGroup
    );

    virtual __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ~AudioIsochronousEngine();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    Initialize();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    ActivateAudioInterface(
        _In_ ULONG desiredSampleRate,
        _In_ ULONG desiredFormatType,
        _In_ ULONG desiredFormat,
        _In_ ULONG desiredBytesPerSampleIn,
        _In_ ULONG desiredValidBitsPerSampleIn,
        _In_ ULONG desiredBytesPerSampleOut,
        _In_ ULONG desiredValidBitsPerSampleOut,
        _In_ bool  forceSetSampleRate = false
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS CalculateUsbLatency(
        _Out_ PUAC_USB_LATENCY usbLatency
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    SelectAlternateInterface(
        _In_ IsoDirection direction,
        _In_ UCHAR        interfaceNumber,
        _In_ UCHAR        alternateSetting
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    SetPipeInformation();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    StartIsoStream();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    StopIsoStream();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void
    SetTerminateStream();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void
    SetAccessible(
        _In_ bool accessible
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    VOID
    IsoRequestCompletionRoutine(
        _In_ PWDF_REQUEST_COMPLETION_PARAMS completionParams,
        _In_ StreamObject *                 streamObject,
        _In_ TransferObject *               transferObject
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void
    SetMeasuredSampleRate(
        _In_ bool  isInput,
        _In_ ULONG measuredSampleRate
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void
    SetAsioBufferPeriod(
        _In_ ULONG bufferPeriod
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void
    SetAsioDriverVersion(
        _In_ ULONG asioDriverVersion
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
    GetStreamDevicesAdjusted(
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
    USBAudioDataFormatManager *
    GetUSBAudioDataFormatManager(
        _In_ bool isInput
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    GetCurrentDataFormat(
        _In_ bool             isInput,
        _Out_ ACXDATAFORMAT & dataFormat
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    bool
    HasInputIsochronousInterface() const;

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    bool
    HasOutputIsochronousInterface() const;

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    bool
    HasInputAndOutputIsochronousInterfaces() const;

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    _Success_(NT_SUCCESS(return))
    GetStreamChannelInfo(
        _In_ bool      isInput,
        _Out_ UCHAR &  numOfChannels,
        _Out_ USHORT & terminalType,
        _Out_ UCHAR &  terminalID,
        _Out_ UCHAR &  volumeUnitID,
        _Out_ UCHAR &  muteUnitID
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    _Success_(NT_SUCCESS(return))
    GetStreamChannelInfoAdjusted(
        _In_ bool      isInput,
        _Out_ UCHAR &  numOfChannels,
        _Out_ USHORT & terminalType,
        _Out_ UCHAR &  terminalID,
        _Out_ UCHAR &  volumeUnitID,
        _Out_ UCHAR &  muteUnitID
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    _Success_(NT_SUCCESS(return))
    GetInformationForHostPin(
        _In_ UCHAR    unitID,
        _Out_ UCHAR & numOfChannels
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    _Success_(NT_SUCCESS(return))
    GetInformationForBridgePin(
        _In_ UCHAR     unitID,
        _Out_ UCHAR &  numOfChannels,
        _Out_ USHORT & terminalType,
        _Out_ UCHAR &  channelNames,
        _Out_ NS_USBAudio::AUDIO_CHANNEL_CLUSTER_DESCRIPTOR & connectorState
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    _Success_(NT_SUCCESS(return))
    GetInformationForVolumeElement(
        _In_ UCHAR    unitID,
        _Out_ UCHAR & numOfChannels,
        _Out_ LONG &  minimum,
        _Out_ LONG &  maximum,
        _Out_ ULONG & steppingDelta
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    _Success_(NT_SUCCESS(return))
    GetInformationForMuteElement(
        _In_ UCHAR    unitID,
        _Out_ UCHAR & numOfChannels
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    _Success_(NT_SUCCESS(return))
    GetInformationForSuperMixElement(
        _In_ UCHAR    unitID,
        _Out_ UCHAR & numOfInputChannels,
        _Out_ UCHAR & numOfOutputChannels
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    _Success_(NT_SUCCESS(return))
    GetInformationForMuxElement(
        _In_ UCHAR    unitID,
        _Out_ UCHAR & numOfChannels,
        _Out_ UCHAR & numOfInputPins
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    _Success_(NT_SUCCESS(return))
    GetInformationForAgcElement(
        _In_ UCHAR    unitID,
        _Out_ UCHAR & numOfChannels
    );

    // Render Circuit
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    AddStaticRender(
        _In_ WDFDEVICE              device,
        _In_ const GUID *           componentGuid,
        _In_ const UNICODE_STRING * circuitName
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    AddRenderCircuit(
        _In_ WDFDEVICE device
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    RemoveRenderCircuit(
        _In_ WDFDEVICE device
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    SetRenderCircuit(
        _In_ ACXCIRCUIT renderCircuit
    );

    // Capture Circuit
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    AddStaticCapture(
        _In_ WDFDEVICE              device,
        _In_ const GUID *           componentGuid,
        _In_ const GUID *           micCustomName,
        _In_ const UNICODE_STRING * circuitName
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    AddCaptureCircuit(
        _In_ WDFDEVICE device
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    RemoveCaptureCircuit(
        _In_ WDFDEVICE device
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    SetCaptureCircuit(
        _In_ ACXCIRCUIT captureCircuit
    );

    // Circuit Commoon
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    VolumeChangeLevelNotification(
        _In_ UCHAR entityID
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    MuteChangeStateNotification(
        _In_ UCHAR entityID
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    ConnectorChangeStateNotification(
        _In_ UCHAR entityID
    );

    // Power management
    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void
    D0Entry();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void
    D0Exit();

    // Stream
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    StreamPrepareHardware(
        _In_ bool            isInput,
        _In_ ULONG           deviceIndex,
        _In_ CStreamEngine * streamEngine
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    StreamReleaseHardware(
        _In_ bool  isInput,
        _In_ ULONG deviceIndex
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    StreamSetDataFormat(
        _In_ bool          isInput,
        _In_ ULONG         deviceIndex,
        _In_ ACXDATAFORMAT dataFormat
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    StreamSetRtPackets(
        _In_ bool                                                               isInput,
        _In_ ULONG                                                              deviceIndex,
        _Inout_updates_(packetsCount) _Inout_updates_bytes_(packetSize) PVOID * packets,
        _In_ ULONG                                                              packetsCount,
        _In_ ULONG                                                              packetSize,
        _In_ ULONG                                                              channel,
        _In_ ULONG                                                              numOfChannelsPerDevice
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void StreamUnsetRtPackets(
        _In_ bool  isInput,
        _In_ ULONG deviceIndex
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    StreamRun(
        _In_ bool  isInput,
        _In_ ULONG deviceIndex
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    StreamPause(
        _In_ bool  isInput,
        _In_ ULONG deviceIndex
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    StreamGetCurrentPacket(
        _In_ bool    isInput,
        _In_ ULONG   deviceIndex,
        _Out_ PULONG currentPacket
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    StreamResetCurrentPacket(
        _In_ bool  isInput,
        _In_ ULONG deviceIndex
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    StreamResetInternal(
        _In_ bool  isInput,
        _In_ ULONG deviceIndex
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    StreamGetCapturePacket(
        _In_ ULONG       deviceIndex,
        _Out_ PULONG     lastCapturePacket,
        _Out_ PULONGLONG qpcPacketStart
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    StreamGetPresentationPosition(
        _In_ bool        isInput,
        _In_ ULONG       deviceIndex,
        _Out_ PULONGLONG positionInBlocks,
        _Out_ PULONGLONG qpcPosition
    );

    // ASIO user-mode communication
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    bool HasAsioOwnership();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(NT_SUCCESS(return))
    NTSTATUS
    GetAudioProperty(
        _Out_ UAC_AUDIO_PROPERTY & audioProperty
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(NT_SUCCESS(return))
    NTSTATUS
    GetChannelInfo(
        _Inout_ PUAC_GET_CHANNEL_INFO_CONTEXT channelInfo,
        _In_ ULONG                            contextSize,
        _Out_ ULONG &                         minValueSize
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(NT_SUCCESS(return))
    NTSTATUS
    GetClockInfo(
        _Inout_ PUAC_GET_CLOCK_INFO_CONTEXT clockInfo,
        _In_ ULONG                          contextSize,
        _Out_ ULONG &                       minValueSize
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(NT_SUCCESS(return))
    NTSTATUS
    SetClockSource(
        _In_ PUAC_SET_CLOCK_SOURCE_CONTEXT clockSource,
        _In_ ULONG                         contextSize,
        _Out_ ULONG &                      minValueSize
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(NT_SUCCESS(return))
    NTSTATUS
    SetSampleFormat(
        _In_ UACSampleFormat sampleFormat
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(NT_SUCCESS(return))
    NTSTATUS
    ChangeSampleRate(
        _In_ ULONG desiredSampleRate
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(NT_SUCCESS(return))
    NTSTATUS
    GetAsioOwnership(
        _In_ WDFFILEOBJECT fileObject
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(NT_SUCCESS(return))
    NTSTATUS
    StartAsioStream();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(NT_SUCCESS(return))
    NTSTATUS
    StopAsioStream();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(NT_SUCCESS(return))
    NTSTATUS
    SetAsioBuffer(
        _In_ ULONG    recBufferLength,
        _Inout_ PBYTE recBuffer,
        _In_ ULONG    recBufferOffset,
        _In_ ULONG    playBufferLength,
        _In_ PBYTE    playBuffer,
        _In_ ULONG    playBufferOffset
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(NT_SUCCESS(return))
    NTSTATUS
    UnsetAsioBuffer();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(NT_SUCCESS(return))
    NTSTATUS
    ReleaseAsioOwnership(
        _In_ WDFFILEOBJECT fileObject
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(NT_SUCCESS(return))
    NTSTATUS
    GetBufferPeriod(
        _Out_ ULONG & bufferPeriod
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(NT_SUCCESS(return))
    NTSTATUS
    SetBufferPeriod(
        _In_ ULONG bufferPeriod
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(NT_SUCCESS(return))
    NTSTATUS
    GetInputLatency(
        _Out_ LONG & inputLatency
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(NT_SUCCESS(return))
    NTSTATUS
    GetOutputLatency(
        _Out_ LONG & outputLatency
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(NT_SUCCESS(return))
    NTSTATUS
    SetAsioDevice(
        _In_ const WDFSTRING asioDeviceString
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(NT_SUCCESS(return))
    NTSTATUS
    GetAsioDevice(
        _Inout_ WDFSTRING & asioDeviceString
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    FileCleanup(
        _In_ WDFFILEOBJECT fileObject
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void
    AcquireAsioWaitLock();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void
    ReleaseAsioWaitLock();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void
    AcquireStreamWaitLock();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void
    ReleaseStreamWaitLock();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void
    AcquireStreamEngineWaitLock();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    void
    ReleaseStreamEngineWaitLock();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    const AUDIO_STREAM_PROPERTY_SET & GetAudioStreamPropertySet();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    const SELECTED_INTERFACE_AND_PIPE & GetInputInterfaceAndPipe();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    const SELECTED_INTERFACE_AND_PIPE & GetOutputInterfaceAndPipe();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    const SELECTED_INTERFACE_AND_PIPE & GetFeedbackInterfaceAndPipe();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    const UAC_USB_LATENCY & GetUsbLatency();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    ContiguousMemory * GetContiguousMemory() const noexcept;

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    AsioBufferObject * GetAsioBufferObject() const noexcept;

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    RtPacketObject * GetRtPacketObject() const noexcept;

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    StreamObject * GetStreamObject() const noexcept;

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    CStreamEngine * GetRenderStreamEngine(
        _In_range_(0, m_numOfOutputDevices - 1) ULONG deviceIndex
    ) const noexcept;

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    CStreamEngine * GetCaptureStreamEngine(
        _In_range_(0, m_numOfInputDevices - 1) ULONG deviceIndex
    ) const noexcept;

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    const ULONG GetNumOfInputDevices();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    const ULONG GetNumOfOutputDevices();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS WalkNextUnit(
        _In_ bool                    isInput,
        _Inout_updates_(4) ULONGLONG idMap[4],
        _Inout_updates_(4) ULONGLONG unvisitedUnitMap[4],
        _Out_ AudioNodeKind &        audioNodeKind,
        _Inout_ UCHAR &              unitID,
        _Inout_ ULONG &              controlBitmap,
        _Inout_ UCHAR &              nextUnitID,
        _Inout_ TraversalDirection & traversalDirection,
        _Inout_ bool &               hasMoreData
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void ReportInternalParameters();

    static __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    AudioIsochronousEngine * Create(
        _In_ PDEVICE_CONTEXT                deviceContext,
        _In_ USBAudioStreamInterfaceGroup * usbAudioStreamInterfaceGroup
    );

  protected:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    void BuildChannelMap();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS StartTransfer(
        _In_ StreamObject * streamObject,
        _In_ ULONG          index,
        _In_ IsoDirection   direction
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    NTSTATUS InitializeIsoUrbIn(
        _In_ StreamObject *   streamObject,
        _In_ TransferObject * transferObject,
        _In_ ULONG            numPackets
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    NTSTATUS InitializeIsoUrbOut(
        _In_ StreamObject *   streamObject,
        _In_ TransferObject * transferObject,
        _In_ ULONG            numPackets
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    NTSTATUS InitializeIsoUrbFeedback(
        _In_ StreamObject *   streamObject,
        _In_ TransferObject * transferObject,
        _In_ ULONG            numPackets
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    NTSTATUS ProcessTransferIn(
        _In_ StreamObject *   streamObject,
        _In_ TransferObject * transferObject
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    NTSTATUS ProcessTransferOut(
        _In_ StreamObject *   streamObject,
        _In_ TransferObject * transferObject
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    NTSTATUS ProcessTransferFeedback(
        _In_ StreamObject *   streamObject,
        _In_ TransferObject * transferObject
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    AbortPipes(
        _In_ IsoDirection direction
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    InitializePipeContextForSuperSpeedDevice(
        _In_ WDFUSBINTERFACE interface,
        _In_ UCHAR           selectedAlternateSetting,
        _In_ WDFUSBPIPE      pipe
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    InitializePipeContextForSuperSpeedIsochPipe(
        _In_ UCHAR      interfaceNumber,
        _In_ UCHAR      selectedAlternateSetting,
        _In_ WDFUSBPIPE pipe
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    InitializePipeContextForHighSpeedDevice(
        _In_ WDFUSBPIPE pipe
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    InitializePipeContextForFullSpeedDevice(
        _In_ WDFUSBPIPE pipe
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    PUSB_ENDPOINT_DESCRIPTOR
    GetEndpointDescriptorForEndpointAddress(
        _In_ UCHAR                                            interfaceNumber,
        _In_ UCHAR                                            selectedAlternateSetting,
        _In_ UCHAR                                            endpointAddress,
        _Out_ PUSB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR * endpointCompanionDescriptor
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS NotifyAllPinsDataFormatChange(
        _In_ bool          isInput,
        _In_ ACXDATAFORMAT dataFormatBeforeChange,
        _In_ ACXDATAFORMAT dataFormatAfterChange
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    bool IsValidInternalParameters(
        _In_ const INTERNAL_PARAMETERS & internalParameters
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS UpdateFramePerIrp(
        _In_ ULONG bufferPeriod
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS UpdateBufferOperationOffset(
        _In_ ULONG bufferPeriod
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS LoadInternalParametersFromDeviceRegistry();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS SaveInternalParametersToDeviceRegistry();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS SaveAsioDeviceToRegistry(
        _In_ const WDFSTRING asioDeviceString
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(NT_SUCCESS(return))
    NTSTATUS LoadAsioDeviceFromRegistry(
        _Inout_ WDFSTRING & asioDeviceString
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS SaveSampleRateToRegistry(
        _In_ WDFDEVICE device,
        _In_ ULONG     sampleRate
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(NT_SUCCESS(return))
    NTSTATUS LoadSampleRateFromRegistry(
        _In_ WDFDEVICE device,
        _Out_ ULONG &  sampleRate
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(NT_SUCCESS(return))
    NTSTATUS OpenSubRegistryKey(
        _In_ WDFKEY    registryKey,
        _Out_ WDFKEY & subRegistryKey
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    _Success_(NT_SUCCESS(return))
    NTSTATUS MakeRegistoryIndexKey(
        _In_ ULONG              index,
        _Out_ UNICODE_STRING *  keyName,
        _Out_writes_(4) WCHAR * stringBuffer
    );

    PDEVICE_CONTEXT                m_deviceContext{nullptr};
    USBAudioStreamInterfaceGroup * m_usbAudioStreamInterfaceGroup{nullptr};
    AsioBufferObject *             m_asioBufferObject{nullptr};
    ContiguousMemory *             m_contiguousMemory{nullptr};
    RtPacketObject *               m_rtPacketObject{nullptr};
    StreamObject *                 m_streamObject{nullptr};
    AUDIO_STREAM_PROPERTY_SET      m_audioStreamPropertySet{};
    SELECTED_INTERFACE_AND_PIPE    m_inputInterfaceAndPipe{};
    SELECTED_INTERFACE_AND_PIPE    m_outputInterfaceAndPipe{};
    SELECTED_INTERFACE_AND_PIPE    m_feedbackInterfaceAndPipe{};
    NTSTATUS                       m_lastActivationStatus{STATUS_UNSUCCESSFUL};
    UAC_USB_LATENCY                m_usbLatency{};
    CStreamEngine **               m_renderStreamEngine{nullptr};
    CStreamEngine **               m_captureStreamEngine{nullptr};
    WDFMEMORY                      m_renderStreamEngineMemory{};
    WDFMEMORY                      m_captureStreamEngineMemory{};
    ULONG                          m_numOfInputDevices{0};
    ULONG                          m_numOfOutputDevices{0};
    WDFWAITLOCK                    m_asioWaitLock{nullptr};
    WDFWAITLOCK                    m_streamWaitLock{nullptr};
    WDFWAITLOCK                    m_streamEngineWaitLock{nullptr};
    WDFFILEOBJECT                  m_asioBufferOwner{};
    WDFFILEOBJECT                  m_asioOwner{};
    WDFFILEOBJECT                  m_resetRequestOwner{};
    LONG                           m_startCounterAsio{0};
    LONG                           m_startCounterWdmAudio{0};
    LONG                           m_startCounterIsoStream{0};
    WCHAR                          m_inputAsioChannelName[UAC_MAX_ASIO_CHANNEL][UAC_MAX_CHANNEL_NAME_LENGTH]{};
    WCHAR                          m_outputAsioChannelName[UAC_MAX_ASIO_CHANNEL][UAC_MAX_CHANNEL_NAME_LENGTH]{};
    ACXCIRCUIT                     m_renderCircuit{nullptr};
    ACXCIRCUIT                     m_captureCircuit{nullptr};
};

#endif
