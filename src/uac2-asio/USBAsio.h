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

    USBAsio.h

Abstract:

    TBD

Environment:

    TBD

--*/

#pragma once

#include "asiosys.h"
#include "rpc.h"
#include "rpcndr.h"
#ifndef COM_NO_WINDOWS_H
#include <windows.h>
#include "ole2.h"
#endif
#include <wil/resource.h>

#include "combase.h"
#include "iasiodrv.h"
#include "UAC_User.h"

#define ASIO_THREAD_STATISTICS

#define ERROR_MESSAGE_LENGTH      128
#define DRIVER_NAME_LENGTH        (32 - 1)
#define CLOCK_SOURCE_NAME_LENGTH  32
#define CHANNEL_INFO_NAME_LENGTH  32
#define SUPPORTED_INPUT_CHANNELS  2
#define SUPPORTED_OUTPUT_CHANNELS 2
#define NOTIFICATION_TIMEOUT      3000
#define ASIO_RESET_TIMEOUT        1000

enum
{
    BLOCKFRAMES = UAC_DEFAULT_ASIO_BUFFER_SIZE,
    NUMOFINPUTS = 64,
    NUMOFOUTPUTS = 64
};

class CUSBAsio : public IASIO, public CUnknown
{
  public:
    CUSBAsio(
        _In_ LPUNKNOWN unknown,
        _In_ HRESULT * result
    );

    virtual ~CUSBAsio();

    DECLARE_IUNKNOWN

    // Factory method
    static CUnknown * CreateInstance(
        _In_ LPUNKNOWN unknown,
        _In_ HRESULT * result
    );

    // IUnknown
    virtual HRESULT STDMETHODCALLTYPE NonDelegatingQueryInterface(
        _In_ REFIID   riid,
        _Out_ void ** object
    );

    // IASIO
    ASIOBool init(
        _In_ void * sysRef
    );
    void getDriverName(
        _Out_writes_z_(32) char * name
    ); // max 32 bytes incl. terminating zero

    long getDriverVersion();
    void getErrorMessage(
        _Out_writes_bytes_(128) char * errorMessage
    ); // max 128 bytes incl.

    ASIOError start();
    ASIOError stop();

    ASIOError getChannels(
        _Out_ long * numInputChannels,
        _Out_ long * numOutpuChannels
    );
    ASIOError getLatencies(
        _Out_ long * inputLatency,
        _Out_ long * outputLatency
    );
    ASIOError getBufferSize(
        _Out_ long * minSize,
        _Out_ long * maxSize,
        _Out_ long * rreferredSize,
        _Out_ long * granularity
    );
    ASIOError canSampleRate(
        _In_ ASIOSampleRate sampleRate
    );
    ASIOError getSampleRate(
        _Out_ ASIOSampleRate * sampleRate
    );
    ASIOError setSampleRate(
        _In_ ASIOSampleRate sampleRate
    );
    ASIOError getClockSources(
        _Out_writes_(*numSources) ASIOClockSource * clocks,
        _Out_ long *                                numSources
    );
    ASIOError setClockSource(
        _In_ long index
    );
    ASIOError getSamplePosition(
        _Out_ ASIOSamples *   samplePosition,
        _Out_ ASIOTimeStamp * timeStamp
    );
    ASIOError getChannelInfo(
        _Inout_ ASIOChannelInfo * info
    );
    ASIOError createBuffers(
        _Inout_updates_(numChannels) ASIOBufferInfo * bufferInfos,
        _In_ long                                     numChannels,
        _In_ long                                     bufferSize,
        _In_ ASIOCallbacks *                          callbacks
    );
    ASIOError disposeBuffers();
    ASIOError controlPanel();
    ASIOError future(
        _In_ long      selector,
        _Inout_ void * option
    );
    ASIOError outputReady();

    void BufferSwitch();

  private:
    bool         InputOpen();
    void         ThreadStart();
    void         ThreadStop();
    void         BufferSwitchX();
    static ULONG GetSupportedSampleFormats();

    double                        m_samplePosition{0};
    double                        m_sampleRate{UAC_DEFAULT_SAMPLE_RATE};
    ASIOCallbacks *               m_callbacks{nullptr};
    ASIOTime                      m_asioTime{0};
    ASIOTimeStamp                 m_theSystemTime{0};
    volatile UCHAR *              m_inputBuffers[NUMOFINPUTS * 2]{};
    UCHAR *                       m_outputBuffers[NUMOFOUTPUTS * 2]{};
    DWORD                         m_initialSystemTime{0};
    DWORD                         m_calculatedSystemTime{0};
    ULONGLONG                     m_initialKernelTime{0};
    TCHAR *                       m_desiredPath{nullptr};
    long                          m_inMap[NUMOFINPUTS]{};
    long                          m_outMap[NUMOFOUTPUTS]{};
    long                          m_blockFrames{UAC_DEFAULT_ASIO_BUFFER_SIZE};
    long                          m_inputLatency{0};
    long                          m_outputLatency{0};
    ULONG                         m_activeInputs{0};
    ULONG                         m_activeOutputs{0};
    long                          m_toggle{0};
    bool                          m_isActive{false};
    bool                          m_isStarted{false};
    bool                          m_isTimeInfoMode{false};
    bool                          m_isTcRead{false};
    TCHAR                         m_errorMessage[ERROR_MESSAGE_LENGTH]{};
    bool                          m_requireSampleRateChange{0.};
    ASIOSampleRate                m_nextSampleRate{0.};
    bool                          m_isRequireAsioReset{false};
    bool                          m_isDropoutDetectionSetting{true};
    bool                          m_isSupportDropoutDetection{false};
    bool                          m_isRequireReportDropout{false};
    bool                          m_isRequireLatencyChange{false};
    LONG                          m_outputReadyBlock{0};
    HANDLE                        m_usbDeviceHandle{INVALID_HANDLE_VALUE};
    UAC_AUDIO_PROPERTY            m_audioProperty{0};
    UAC_SET_FLAGS_CONTEXT         m_driverFlags{0};
    ULONG                         m_fixedSamplingRate{0};
    ASIOIoFormatType              m_requestedSampleFormat{0};
    ULONG                         m_inAvailableChannels{0};
    ULONG                         m_outAvailableChannels{0};
    PUAC_GET_CHANNEL_INFO_CONTEXT m_channelInfo{nullptr};
    PUAC_GET_CLOCK_INFO_CONTEXT   m_clockInfo{nullptr};
    wil::critical_section         m_deviceInfoCS;
    wil::critical_section         m_clientInfoCS;
    wil::critical_section         m_recBufferCS;
    UCHAR *                       m_driverPlayBufferWithKsProperty{nullptr};
    UCHAR *                       m_driverPlayBuffer{nullptr};
    volatile UCHAR *              m_driverRecBuffer{nullptr};
    LONGLONG                      m_playReadyPosition{0};
    HANDLE                        m_notificationEvent{nullptr};
    HANDLE                        m_outputReadyEvent{nullptr};
    HANDLE                        m_deviceReadyEvent{nullptr};
    HANDLE                        m_stopEvent{nullptr};
    HANDLE                        m_workerThread{nullptr};
    LONG                          m_threadPriority{-2};
    HANDLE                        m_asioResetEvent{nullptr};
    HANDLE                        m_terminateAsioResetEvent{nullptr};
    HANDLE                        m_asioResetThread{nullptr};
    HANDLE                        m_outputReadyBlockEvent{nullptr};

    static unsigned int __stdcall WorkerThread(
        _In_ void * Param
    );
    static unsigned int __stdcall AsioResetThread(
        _In_ void * Param
    );

    bool  MeasureLatency();
    bool  ApplySettings();
    bool  ExecuteControlPanel();
    ULONG CalcInputLatency(
        _In_ ULONG SamplingRate,
        _In_ ULONG PeriodFrames,
        _In_ ULONG ClassicFramesPerIrp,
        _In_ LONG  OutputFrameDelay,
        _In_ LONG  LatencyOffset,
        _In_ ULONG BufferOperationThread,
        _In_ ULONG InBufferOperationOffset,
        _In_ ULONG OutBufferOperationOffset,
        _In_ ULONG PacketsPerMs
    );
    ULONG calcOutputLatency(
        _In_ ULONG SamplingRate,
        _In_ ULONG PeriodFrames,
        _In_ ULONG ClassicFramesPerIrp,
        _In_ LONG  OutputFrameDelay,
        _In_ LONG  LatencyOffset,
        _In_ ULONG BufferOperationThread,
        _In_ ULONG InBufferOperationOffset,
        _In_ ULONG OutBufferOperationOffset,
        _In_ ULONG PacketsPerMs
    );
    LONG m_InstanceIndex;

    bool RequestClockInfoChange();

    bool GetDesiredPath();
    bool ObtainDeviceParameter();
};
