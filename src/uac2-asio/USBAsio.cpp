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

    USBAsio.cpp

Abstract:

    TBD

Environment:

    TBD

--*/

#include "framework.h"
#include <initguid.h> // Guid definition
#include <ks.h>
#include <ksmedia.h>
#include <timeapi.h>
#include <math.h>
#include <process.h>
#include "USBAsio.h"
#include "USBDevice.h"
#include "print_.h"
#include "resource.h"

#pragma data_seg(".interprocess")
static LONG g_Instance = 0;
static LONG g_AsioResetThread = 0;
static LONG g_WorkerThread = 0;
#pragma data_seg()

static constexpr double c_TwoRaisedTo32 = 4294967296.;
static constexpr double c_TwoRaisedTo32Reciprocal = 1. / c_TwoRaisedTo32;

#define ASIODRV_NAME            _T("USBAsio.dll")
#define CONTROLPANELPROGRAMNAME _T("USBAsioControlPanel.exe")
#if defined(_M_ARM64EC) || defined(_M_ARM64)
#define DRIVER_NAME_8b "USB ASIO (ARM64X)"
#else
#define DRIVER_NAME_8b "USB ASIO"
#endif
#define DRIVER_NAME _T(DRIVER_NAME_8b)

//
// Parameters are currently passed using the HKEY_CURRENT_USER registry.
// This implementation is not ideal, so we plan to switch to using
// DeviceIoControl via the ACX driver in the near future.
//
static const TCHAR * c_RegistryKeyName = _T("Software\\Microsoft\\Windows USB ASIO"); // ZANTEI, tentative
static const TCHAR * c_FixedSamplingRateValueName = _T("FixedSamplingRate");
static const TCHAR * c_PeriodFramesValueName = _T("PeriodFrames");
static const TCHAR * c_ClassicFramesPerIrpValueName = _T("ClassicFramesPerIrp");
static const TCHAR * c_ClassicFramesPerIrp2ValueName = _T("ClassicFramesPerIrp2");
static const TCHAR * c_MaxIrpNumberValueName = _T("MaxIrpNumber");
static const TCHAR * c_FirstPacketLatencyValueName = _T("FirstPacketLatency");
static const TCHAR * c_PreSendFramesValueName = _T("PreSendFrames");
static const TCHAR * c_OutputFrameDelayValueName = _T("OutputFrameDelay");
static const TCHAR * c_DelayedOutputBufferSwitchName = _T("DelayedOutputBufferSwitch");
static const TCHAR * c_AsioDeviceValueName = _T("AsioDevice");
static const TCHAR * c_OutputBufferOperationOffsetName = _T("OutBufferOperationOffset");
static const TCHAR * c_OutputHubOffsetName = _T("OutHubOffset");
static const TCHAR * c_InputBufferOperationOffsetName = _T("InBufferOperationOffset");
static const TCHAR * c_InputHubOffsetName = _T("InHubOffset");
static const TCHAR * c_BufferThreadPriorityName = _T("BufferThreadPriority");
static const TCHAR * c_DropoutDetectionName = _T("DropoutDetection");
static const TCHAR * c_OutBulkOperationOffset = _T("OutBulkOperationOffset");
static const TCHAR * c_ServiceName = _T("USBAudio2-ACX");
static const TCHAR * c_ReferenceName = _T("RenderDevice0");

#define DSD_ZERO_BYTE 0x96
#define DSD_ZERO_WORD 0x9696

extern HRESULT UnregisterAsioDriver(
    _In_ CLSID   clsId,
    _In_ LPCTSTR dllName,
    _In_ LPCTSTR regName
);

extern HRESULT RegisterAsioDriver(
    _In_ CLSID   clsId,
    _In_ LPCTSTR dllName,
    _In_ LPCTSTR regName,
    _In_ LPCTSTR asioDescriptor,
    _In_ LPCTSTR threadModel
);

static void getNanoSeconds(ASIOTimeStamp * timeStamp)
{
    double nanoSeconds = (double)((unsigned long)timeGetTime()) * 1000000.;
    timeStamp->hi = (unsigned long)(nanoSeconds / c_TwoRaisedTo32);
    timeStamp->lo = (unsigned long)(nanoSeconds - (timeStamp->hi * c_TwoRaisedTo32));
}

CUnknown * CreateInstance(LPUNKNOWN, HRESULT *)
{
    return (CUnknown *)nullptr;
};

// {327468A4-1351-4930-BB6B-0FEB69BF5D70}
CLSID IID_ASIO_DRIVER = {0x327468a4, 0x1351, 0x4930, {0xbb, 0x6b, 0xf, 0xeb, 0x69, 0xbf, 0x5d, 0x70}};

CFactoryTemplate g_Templates[] = {
    {L"YSUSB_ASIO", &IID_ASIO_DRIVER, CUSBAsio::CreateInstance}
};

int g_NumOfTemplates = sizeof(g_Templates) / sizeof(g_Templates[0]);

static const ULONG c_FrameRateList[] = {
    11025, 22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000, 705600, 768000
};
static const ULONG c_FrameRateListNumber = sizeof(c_FrameRateList) / sizeof(c_FrameRateList[0]);

HRESULT RegisterAsioDriver()
{
    HRESULT result = RegisterAsioDriver(IID_ASIO_DRIVER, ASIODRV_NAME, DRIVER_NAME, DRIVER_NAME, TEXT("Apartment"));

    if (!SUCCEEDED(result))
    {
        TCHAR message[ERROR_MESSAGE_LENGTH] = {0};
        _stprintf_s(message, ERROR_MESSAGE_LENGTH, TEXT("Register Server failed ! (%d)"), result);
        MessageBox(nullptr, message, DRIVER_NAME, MB_OK);
    }

    return result;
}

HRESULT UnregisterAsioDriver()
{
    HRESULT result = UnregisterAsioDriver(IID_ASIO_DRIVER, ASIODRV_NAME, DRIVER_NAME);

    if (!SUCCEEDED(result))
    {
        TCHAR message[ERROR_MESSAGE_LENGTH] = {0};
        _stprintf_s(message, ERROR_MESSAGE_LENGTH, TEXT("Unregister Server failed ! (%d)"), result);
        MessageBox(nullptr, message, DRIVER_NAME, MB_OK);
    }

    return result;
}

_Use_decl_annotations_
CUnknown *
CUSBAsio::CreateInstance(
    LPUNKNOWN unknown,
    HRESULT * result
)
{
    return (CUnknown *)new CUSBAsio(unknown, result);
}

_Use_decl_annotations_
HRESULT STDMETHODCALLTYPE
CUSBAsio::NonDelegatingQueryInterface(
    REFIID  riid,
    void ** object
)
{
    if (riid == IID_ASIO_DRIVER)
    {
        return GetInterface(this, object);
    }
    return CUnknown::NonDelegatingQueryInterface(riid, object);
}

_Use_decl_annotations_
CUSBAsio::CUSBAsio(
    LPUNKNOWN unknown,
    HRESULT * result
)
    : CUnknown((TCHAR *)_T("CUSBAsio"), unknown, result)
{
#ifdef _DEBUG
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
    long i;

    bool isSuccess = true;

    m_InstanceIndex = InterlockedIncrement(&g_Instance);

    info_print_(_T("USB ASIO created, instance %d.\n"), InterlockedCompareExchange(&g_Instance, 0, 0));

    RtlZeroMemory(&m_audioProperty, sizeof(UAC_AUDIO_PROPERTY));

    GetDesiredPath();

    m_usbDeviceHandle = OpenUsbDevice((const LPGUID)&KSCATEGORY_AUDIO, c_ServiceName, c_ReferenceName, m_desiredPath);
    if (m_usbDeviceHandle == INVALID_HANDLE_VALUE)
    {
        TCHAR messageString[ERROR_MESSAGE_LENGTH] = {0};
        LoadString(GetModuleHandle(nullptr), IDS_ERRMSG_CONSTRUCT, messageString, sizeof(messageString) / sizeof(messageString[0]));
        _tcscpy_s(m_errorMessage, ERROR_MESSAGE_LENGTH, messageString);
        return;
    }

    const ULONG maxRetry = 6;
    for (ULONG retry = 0; retry < maxRetry; ++retry)
    {
        isSuccess = GetAsioOwnership(m_usbDeviceHandle);
        if (isSuccess)
        {
            break;
        }
        if (retry < maxRetry - 1)
        {
            Sleep(500);
        }
    }

    if (!isSuccess)
    {
        TCHAR messageString[ERROR_MESSAGE_LENGTH] = {0};
        LoadString(GetModuleHandle(nullptr), IDS_ERRMSG_CONSTRUCT, messageString, sizeof(messageString) / sizeof(messageString[0]));
        _tcscpy_s(m_errorMessage, ERROR_MESSAGE_LENGTH, messageString);
        CloseHandle(m_usbDeviceHandle);
        m_usbDeviceHandle = INVALID_HANDLE_VALUE;
    }

    ApplySettings();

    m_callbacks = 0;

    isSuccess = ObtainDeviceParameter();
    if (!isSuccess)
    {
        return;
    }

    switch (m_audioProperty.CurrentSampleFormat)
    {
    case UACSampleFormat::UAC_SAMPLE_FORMAT_PCM:
    case UACSampleFormat::UAC_SAMPLE_FORMAT_IEEE_FLOAT:
        m_requestedSampleFormat = kASIOPCMFormat;
        break;
    default:
    case UACSampleFormat::UAC_SAMPLE_FORMAT_PCM8:
        m_requestedSampleFormat = kASIOFormatInvalid;
        break;
    }

    GetClockInfo(m_usbDeviceHandle, &m_clockInfo);

    m_stopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    m_asioResetEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    m_terminateAsioResetEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    m_outputReadyBlockEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    auto beginThreadResult = _beginthreadex(nullptr, 0, AsioResetThread, this, 0, nullptr);
    ;
    if (beginThreadResult <= 0)
    {
        return;
    }
    m_asioResetThread = (HANDLE)beginThreadResult;
    SetThreadPriority(m_asioResetThread, THREAD_PRIORITY_ABOVE_NORMAL);

    m_samplePosition = 0;
    m_isActive = false;
    m_isStarted = false;
    m_isTimeInfoMode = false;
    m_isTcRead = false;
    for (i = 0; i < NUMOFINPUTS; i++)
    {
        m_inputBuffers[i] = 0;
        m_inMap[i] = 0;
    }
    for (i = 0; i < NUMOFOUTPUTS; i++)
    {
        m_outputBuffers[i] = 0;
        m_outMap[i] = 0;
    }
    m_toggle = 0;
}

CUSBAsio::~CUSBAsio()
{
    DWORD result = 0;

    disposeBuffers();

    if (m_channelInfo != nullptr)
    {
        delete[] ((UCHAR *)m_channelInfo);
    }
    if (m_clockInfo != nullptr)
    {
        delete[] ((UCHAR *)m_clockInfo);
    }
    if (m_desiredPath != nullptr)
    {
        delete[] m_desiredPath;
    }
    if (m_usbDeviceHandle != INVALID_HANDLE_VALUE)
    {
        ReleaseAsioOwnership(m_usbDeviceHandle);
        CloseHandle(m_usbDeviceHandle);
        m_usbDeviceHandle = INVALID_HANDLE_VALUE;
    }
    if (m_terminateAsioResetEvent != nullptr)
    {
        if (m_asioResetThread != nullptr)
        {
            DWORD timeout = (DWORD)(NOTIFICATION_TIMEOUT) * 2;
            SetEvent(m_terminateAsioResetEvent);
            result = WaitForSingleObject(m_asioResetThread, timeout);
            if (result == WAIT_OBJECT_0)
            {
                CloseHandle(m_asioResetThread);
            }
        }
        CloseHandle(m_terminateAsioResetEvent);
    }
    if (m_asioResetEvent != nullptr)
    {
        CloseHandle(m_asioResetEvent);
    }
    if (m_stopEvent != nullptr)
    {
        CloseHandle(m_stopEvent);
    }
    if (m_outputReadyBlockEvent != nullptr)
    {
        CloseHandle(m_outputReadyBlockEvent);
    }

    InterlockedDecrement(&g_Instance);

    info_print_(_T("USB ASIO destructed, instance %d.\n"), InterlockedCompareExchange(&g_Instance, 0, 0));
}

_Use_decl_annotations_
void CUSBAsio::getDriverName(char * name)
{
	// 
	// name uses multi-byte character sets,
	// so sprintf_s is used.
	// 
    strcpy_s(name, DRIVER_NAME_LENGTH, DRIVER_NAME_8b);
}

long CUSBAsio::getDriverVersion()
{
    info_print_(_T("getDriverVersion\n"));
    return 0x00010000L;
}

_Use_decl_annotations_
void CUSBAsio::getErrorMessage(char * errorMessage)
{
    info_print_(_T("getErrorMessage\n"));
    // >>comment-001<<
    size_t size = _tcslen(m_errorMessage) + 1;
    size = min(size, ERROR_MESSAGE_LENGTH);

#ifdef _UNICODE
    // TCHAR is wchar_t → convert wide char to multi-byte
    WideCharToMulti-Byte(CP_ACP, 0, m_errorMessage, -1, errorMessage, ERROR_MESSAGE_LENGTH, NULL, NULL);
#else
    // TCHAR is char → direct copy
    strcpy_s(errorMessage, ERROR_MESSAGE_LENGTH, m_errorMessage);
#endif
}

_Use_decl_annotations_
ASIOBool CUSBAsio::init(void * /* sysRef */)
{
    info_print_(_T("init\n"));
    // Due to a change in the error handling policy, it has been changed to return an error if an error occurs in CUSBAsio::CUSBAsio ().
    if (m_usbDeviceHandle == INVALID_HANDLE_VALUE || m_inputLatency == 0 || m_outputLatency == 0)
    {
        // Error in CUSBAsio::CUSBAsio ()
        return ASIOFalse;
    }
    return ASIOTrue;
}

ASIOError CUSBAsio::start()
{
    info_print_(_T("start\n"));
    if (m_usbDeviceHandle == INVALID_HANDLE_VALUE || m_inputLatency == 0 || m_outputLatency == 0)
    {
        return ASE_NotPresent;
    }

    if (!m_isActive)
    {
        return ASE_NotPresent;
    }

    if (m_callbacks)
    {
        m_samplePosition = 0;
        m_theSystemTime.lo = m_theSystemTime.hi = 0;
        m_toggle = 0;

        m_initialSystemTime = 0;
        m_calculatedSystemTime = 0;
        m_initialKernelTime = 0;

        m_isStarted = true;

        ThreadStart(); // activate 'hardware'

        return ASE_OK;
    }
    return ASE_NotPresent;
}

ASIOError CUSBAsio::stop()
{
    info_print_(_T("stop\n"));

    if (m_usbDeviceHandle == INVALID_HANDLE_VALUE || m_inputLatency == 0 || m_outputLatency == 0)
    {
        return ASE_NotPresent;
    }

    if (!m_isStarted)
    {
        return ASE_OK;
    }
    m_isStarted = false;
    ThreadStop(); // de-activate 'hardware'
    StopAsioStream(m_usbDeviceHandle);

    return ASE_OK;
}

_Use_decl_annotations_
ASIOError CUSBAsio::getChannels(long * numInputChannels, long * numOutputChannels)
{
    info_print_(_T("getChannels\n"));
    if (m_usbDeviceHandle == INVALID_HANDLE_VALUE || m_inputLatency == 0 || m_outputLatency == 0)
    {
        *numInputChannels = 0;
        *numOutputChannels = 0;
        // No error is returned even if the hardware is unusable. -> Error handling policy changed: ASE_NotPresent is returned.
        return ASE_NotPresent;
    }
    if (numInputChannels == nullptr || numOutputChannels == nullptr)
    {
        return ASE_InvalidParameter;
    }
    {
        auto lockDevice = m_deviceInfoCS.lock();
        *numInputChannels = m_inAvailableChannels;
        *numOutputChannels = m_outAvailableChannels;
    }
    info_print_(_T("Channels: IN %d, OUT %d.\n"), *numInputChannels, *numOutputChannels);
    return ASE_OK;
}

_Use_decl_annotations_
ASIOError CUSBAsio::getLatencies(long * inputLatency, long * outputLatency)
{
    info_print_(_T("getLatencies\n"));

    *inputLatency = 0;
    *outputLatency = 0;

    if (m_usbDeviceHandle == INVALID_HANDLE_VALUE || m_inputLatency == 0 || m_outputLatency == 0)
    {
        // No error is returned even if the hardware is unusable. -> Error handling policy changed: ASE_NotPresent is returned.
        return ASE_NotPresent;
    }
    if (inputLatency == nullptr || outputLatency == nullptr)
    {
        return ASE_InvalidParameter;
    }

    if (GetAudioProperty(m_usbDeviceHandle, &m_audioProperty))
    {
        info_print_(_T("Obtained latency offset in-%d out-%d\n"), m_audioProperty.InputLatencyOffset, m_audioProperty.OutputLatencyOffset);
    }

    *inputLatency = m_blockFrames + m_audioProperty.InputLatencyOffset;
    *outputLatency = m_blockFrames + m_audioProperty.OutputLatencyOffset;

    // >>comment-002<<
    return ASE_OK;
}

_Use_decl_annotations_
ASIOError CUSBAsio::getBufferSize(long * minSize, long * maxSize, long * preferredSize, long * granularity)
{
    info_print_(_T("getBufferSize\n"));

    *minSize = 0;
    *maxSize = 0;
    *preferredSize = 0;
    *granularity = 0;

    if (m_usbDeviceHandle == INVALID_HANDLE_VALUE || m_inputLatency == 0 || m_outputLatency == 0)
    {
        // No error is returned even if the hardware is unusable. -> Error handling policy changed: ASE_NotPresent is returned.
        return ASE_NotPresent;
    }
    if (minSize == nullptr || maxSize == nullptr || preferredSize == nullptr || granularity == nullptr)
    {
        return ASE_InvalidParameter;
    }
    *minSize = *maxSize = *preferredSize = m_blockFrames; // allow this size only
    *granularity = 0;
    // No error is returned even if the hardware is unusable.
    // Some DAWs will crash if 0 is returned, so the initial value of m_blockFrames is 1024.
    return ASE_OK;
}

_Use_decl_annotations_
ASIOError CUSBAsio::canSampleRate(ASIOSampleRate sampleRate)
{
    info_print_(_T("canSampleRate\n"));
    if (m_usbDeviceHandle == INVALID_HANDLE_VALUE || m_inputLatency == 0 || m_outputLatency == 0)
    {
        // No error is returned even if the hardware is unusable. -> Error handling policy changed: ASE_NotPresent is returned.
        return ASE_NotPresent;
    }
    info_print_(_T("requested %lf Hz\n"), sampleRate);
    if (m_fixedSamplingRate != 0)
    {
        if ((ULONG)sampleRate == m_fixedSamplingRate)
        {
            return ASE_OK;
        }
        else
        {
            return ASE_NoClock;
        }
    }

    {
        auto        lockDevice = m_deviceInfoCS.lock();
        const ULONG requiredFrameRate = (ULONG)sampleRate;

        if ((m_requestedSampleFormat == kASIOPCMFormat) && ((m_audioProperty.SupportedSampleFormats & GetSupportedSampleFormats()) != 0))
        {
            for (ULONG index = 0; index < c_FrameRateListNumber; ++index)
            {
                if ((requiredFrameRate == c_FrameRateList[index]) && ((m_audioProperty.SupportedSampleRate & (1 << index)) != 0))
                {
                    info_print_(_T("This device works at requested sample rate.\n"));
                    return ASE_OK;
                }
            }
        }
    }
    info_print_(_T("This device does not work at requested sample rate.\n"));
    return ASE_NoClock;
}

_Use_decl_annotations_
ASIOError CUSBAsio::getSampleRate(ASIOSampleRate * sampleRate)
{
    verbose_print_(_T("getSampleRate\n"));

    if (sampleRate == nullptr)
    {
        return ASE_InvalidParameter;
    }

    *sampleRate = {};

    if (m_usbDeviceHandle == INVALID_HANDLE_VALUE || m_inputLatency == 0 || m_outputLatency == 0)
    {
        // No error is returned even if the hardware is unusable. -> Error handling policy changed: ASE_NotPresent is returned.
        return ASE_NotPresent;
    }
    // Do not return 0 because some DAWs will crash due to division by 0.
    // (The initial value of m_sampleRate is 44100)
    {
        auto lockDevice = m_deviceInfoCS.lock();
        *sampleRate = m_sampleRate;
    }
    // info_print_(_T("getSampleRate\n"));
    // info_print_(_T("current %lf Hz, device current %u Hz\n"),this->m_sampleRate,m_audioProperty.SampleRate);
    return ASE_OK;
}

_Use_decl_annotations_
ASIOError CUSBAsio::setSampleRate(ASIOSampleRate sampleRate)
{
    if (m_usbDeviceHandle == INVALID_HANDLE_VALUE || m_inputLatency == 0 || m_outputLatency == 0)
    {
        // No error is returned even if the hardware is unusable. -> Error handling policy changed: ASE_NotPresent is returned.
        return ASE_NotPresent;
    }
    info_print_(_T("setSampleRate\n"));
    info_print_(_T("current %lf Hz, device current %u Hz, request %lf Hz\n"), this->m_sampleRate, m_audioProperty.SampleRate, sampleRate);
    if (canSampleRate(sampleRate) != ASE_OK)
    {
        return ASE_NoClock;
    }
    {
        auto lockClient = m_clientInfoCS.lock();
        auto lockDevice = m_deviceInfoCS.lock();

        if (sampleRate != this->m_sampleRate)
        {
            BOOL  result = FALSE;
            ULONG sampleFormat;
            if (m_requestedSampleFormat != kASIOPCMFormat)
            {
                return ASE_NoClock;
            }
            if ((m_audioProperty.CurrentSampleFormat != UACSampleFormat::UAC_SAMPLE_FORMAT_PCM) && (m_audioProperty.CurrentSampleFormat == UACSampleFormat::UAC_SAMPLE_FORMAT_IEEE_FLOAT))
            {
                if (m_audioProperty.SupportedSampleFormats & (1 << toULong(UACSampleFormat::UAC_SAMPLE_FORMAT_IEEE_FLOAT)))
                {
                    sampleFormat = toULong(UACSampleFormat::UAC_SAMPLE_FORMAT_IEEE_FLOAT);
                }
                else if (m_audioProperty.SupportedSampleFormats & (1 << toULong(UACSampleFormat::UAC_SAMPLE_FORMAT_PCM)))
                {
                    sampleFormat = toULong(UACSampleFormat::UAC_SAMPLE_FORMAT_PCM);
                }
                else
                {
                    return ASE_NoClock;
                }
            }
            else
            {
                sampleFormat = toULong(m_audioProperty.CurrentSampleFormat);
            }

            result = SetSampleFormat(m_usbDeviceHandle, sampleFormat);
            ULONG frameRate = (ULONG)sampleRate;

            for (ULONG i = 0; i < c_FrameRateListNumber; ++i)
            {
                if (frameRate == c_FrameRateList[i] &&
                    ((m_audioProperty.SupportedSampleRate & (1 << i)) != 0))
                {
                    info_print_(_T("This device works at requested sample rate.\n"));
                    result = ChangeSampleRate(m_usbDeviceHandle, frameRate);
                    break;
                }
            }

            if (!result)
            {
                return ASE_InvalidMode;
            }
            this->m_sampleRate = sampleRate;

            if (!RequestClockInfoChange())
            {
                return ASE_NotPresent;
            }
        }
    }
    return ASE_OK;
}

_Use_decl_annotations_
ASIOError CUSBAsio::getClockSources(ASIOClockSource * clocks, long * numSources)
{
    verbose_print_(_T("getClockSources\n"));

    if (clocks == nullptr)
    {
        return ASE_InvalidParameter;
    }
    *clocks = {};

    if (numSources == nullptr)
    {
        return ASE_InvalidParameter;
    }
    *numSources = 0;

    if (m_usbDeviceHandle == INVALID_HANDLE_VALUE || m_inputLatency == 0 || m_outputLatency == 0)
    {
        // No error is returned even if the hardware is unusable. -> Error handling policy changed: ASE_NotPresent is returned.
        return ASE_NotPresent;
    }
    if (*numSources < (long)m_audioProperty.ClockSources)
    {
        info_print_(_T("too small buffers. *NumSources %d, m_audioProperty.ClockSources %d.\n"), *numSources, m_audioProperty.ClockSources);
        // return ASE_InvalidParameter;
    }

    // Device information is obtained only when the instance is initialized.
    {
        auto lockDevice = m_deviceInfoCS.lock();
        if (m_clockInfo == nullptr)
        {
            return ASE_HWMalfunction;
        }
        UAC_GET_CLOCK_INFO_CONTEXT * clockInfo = m_clockInfo;

        long numDeviceClocks = clockInfo->NumClockSource;
        if (*numSources < (long)clockInfo->NumClockSource)
        {
            numDeviceClocks = *numSources;
        }

        for (long i = 0; i < numDeviceClocks; ++i)
        {
            clocks[i].index = i;
            clocks[i].associatedChannel = -1;
            clocks[i].associatedGroup = -1;
            clocks[i].isCurrentSource = clockInfo->ClockSource[i].IsCurrentSource ? ASIOTrue : ASIOFalse;

			// 
			// ASIOClockSource::name uses multi-byte character sets,
			// so sprintf_s is used.
			// 
            sprintf_s(clocks[i].name, CLOCK_SOURCE_NAME_LENGTH, "%S", clockInfo->ClockSource[i].Name);
        }
        *numSources = clockInfo->NumClockSource;
    }
    return ASE_OK;
}

_Use_decl_annotations_
ASIOError CUSBAsio::setClockSource(long index)
{
    info_print_(_T("setClockSource\n"));
    BOOL result = FALSE;
    if (m_usbDeviceHandle == INVALID_HANDLE_VALUE || m_inputLatency == 0 || m_outputLatency == 0)
    {
        // No error is returned even if the hardware is unusable. -> Error handling policy changed: ASE_NotPresent is returned.
        return ASE_NotPresent;
    }
    {
        auto lockClient = m_clientInfoCS.lock();
        auto lockDevice = m_deviceInfoCS.lock();
        if (index < (long)m_audioProperty.ClockSources)
        {
            UAC_SET_CLOCK_SOURCE_CONTEXT context;
            context.Index = index;
            if (SetClockSource(m_usbDeviceHandle, index))
            {
                if (m_clockInfo != nullptr)
                {
                    delete[] ((UCHAR *)m_clockInfo);
                    m_clockInfo = nullptr;
                }
                GetClockInfo(m_usbDeviceHandle, &m_clockInfo);
                result = RequestClockInfoChange();
                if (!result)
                {
                    return ASE_NotPresent;
                }
                return ASE_OK;
            }
        }
    }
    return ASE_InvalidMode;
}

_Use_decl_annotations_
ASIOError CUSBAsio::getSamplePosition(ASIOSamples * samplePosition, ASIOTimeStamp * timeStamp)
{
    // info_print_(_T("getSamplePosition\n"));

    *samplePosition = {};
    *timeStamp = {};

    if (m_usbDeviceHandle == INVALID_HANDLE_VALUE || m_inputLatency == 0 || m_outputLatency == 0)
    {
        // No error is returned even if the hardware is unusable. -> Error handling policy changed: ASE_NotPresent is returned.
        return ASE_NotPresent;
    }
    if (samplePosition == nullptr || timeStamp == nullptr)
    {
        return ASE_InvalidParameter;
    }
    timeStamp->lo = m_theSystemTime.lo;
    timeStamp->hi = m_theSystemTime.hi;
    if (m_samplePosition >= c_TwoRaisedTo32)
    {
        samplePosition->hi = (unsigned long)(m_samplePosition * c_TwoRaisedTo32Reciprocal);
        samplePosition->lo = (unsigned long)(m_samplePosition - (samplePosition->hi * c_TwoRaisedTo32));
    }
    else
    {
        samplePosition->hi = 0;
        samplePosition->lo = (unsigned long)m_samplePosition;
    }
    // info_print_(_T("getSamplePosition SamplePosition %u, TimeStamp %u\n"), samplePosition->lo, timeStamp->lo);
    return ASE_OK;
}

_Use_decl_annotations_
ASIOError CUSBAsio::getChannelInfo(ASIOChannelInfo * info)
{
    verbose_print_(_T("getChannelInfo\n"));

    if (info == nullptr)
    {
        return ASE_InvalidParameter;
    }

    if (m_usbDeviceHandle == INVALID_HANDLE_VALUE || m_inputLatency == 0 || m_outputLatency == 0)
    {
        // No error is returned even if the hardware is unusable. -> Error handling policy changed: ASE_NotPresent is returned.
        return ASE_NotPresent;
    }
    {
        auto lockDevice = m_deviceInfoCS.lock();
        if (info->channel < 0 || (info->isInput ? (ULONG)info->channel >= m_inAvailableChannels : (ULONG)info->channel >= m_outAvailableChannels))
        {
            return ASE_InvalidParameter;
        }
        switch (m_requestedSampleFormat)
        {
        case kASIODSDFormat:
            info->type = ASIOSTDSDInt8MSB1;
            break;
        default:
            info->type = toInt(m_audioProperty.SampleType);
            break;
        }
        info->channelGroup = 0;
        info->isActive = ASIOFalse;
        ULONG i;
        if (info->isInput)
        {
            for (i = 0; i < m_activeInputs; i++)
            {
                if (m_inMap[i] == info->channel)
                {
                    info->isActive = ASIOTrue;
                    break;
                }
            }
        }
        else
        {
            for (i = 0; i < m_activeOutputs; i++)
            {
                if (m_outMap[i] == info->channel)
                {
                    info->isActive = ASIOTrue;
                    break;
                }
            }
        }
        ULONG ch = 0;
        for (; ch < m_channelInfo->NumChannels; ++ch)
        {
            if (m_channelInfo->Channel[ch].Index == info->channel && m_channelInfo->Channel[ch].IsInput == info->isInput)
            {
                break;
            }
        }

		// 
		// ASIOChannelInfo::name uses multi-byte character sets,
		// so sprintf_s is used.
		// 
        if (ch == m_channelInfo->NumChannels)
        {
            sprintf_s(info->name, DRIVER_NAME_LENGTH, "channel %u", info->channel);
        }
        else
        {
            sprintf_s(info->name, DRIVER_NAME_LENGTH, "%S", m_channelInfo->Channel[ch].Name);
        }
    }
#ifdef _UNICODE
    info_print_(_T("getChannelInfo(): channel %d, isInput %d, isActive %d, channelGroup %d, type %d, name %S\n"), info->channel, info->isInput, info->isActive, info->channelGroup, info->type, info->name);
#else
    info_print_(_T("getChannelInfo(): channel %d, isInput %d, isActive %d, channelGroup %d, type %d, name %s\n"), info->channel, info->isInput, info->isActive, info->channelGroup, info->type, info->name);
#endif

    return ASE_OK;
}

_Use_decl_annotations_
ASIOError CUSBAsio::createBuffers(ASIOBufferInfo * bufferInfos, long numChannels, long bufferSize, ASIOCallbacks * callbacks)
{
    info_print_(_T("createBuffers\n"));
    ASIOBufferInfo * info = bufferInfos;
    long             i;
    BOOL             result = FALSE;

    if (m_usbDeviceHandle == INVALID_HANDLE_VALUE || m_inputLatency == 0 || m_outputLatency == 0)
    {
        // No error is returned even if the hardware is unusable. -> Error handling policy changed: ASE_NotPresent is returned.
        info_print_(_T("createBuffers : device not ready.\n"));
        return ASE_NotPresent;
    }
    if (bufferInfos == nullptr || callbacks == nullptr)
    {
        return ASE_InvalidParameter;
    }

    if (m_requestedSampleFormat == kASIOPCMFormat && (ULONG)m_sampleRate != m_audioProperty.SampleRate)
    {
        info_print_(_T("createBuffers : invalid format, format req %u, cur %u, fs req %lf, cur %u.\n"), m_requestedSampleFormat, m_audioProperty.CurrentSampleFormat, m_sampleRate, m_audioProperty.SampleRate);
        return ASE_InvalidMode;
    }

    ASIOError error = ASE_OK;
    bool      callDisposeBuffers = false;
    auto      createBuffersScope = wil::scope_exit([&]() {
        if ((error != ASE_OK) && callDisposeBuffers)
        {
            disposeBuffers();
        }
    });

    {
        auto lockClient = m_clientInfoCS.lock();

        if (m_isActive)
        {
            info_print_(_T("createBuffers : already initialized.\n"));
            error = ASE_OK;
            return error;
        }

        m_isActive = true;

        {
            auto lockDevice = m_deviceInfoCS.lock();

            // >>comment-003<<
            result = StopAsioStream(m_usbDeviceHandle);

            result = UnsetAsioBuffer(m_usbDeviceHandle);

            m_activeInputs = 0;
            m_activeOutputs = 0;
            ULONGLONG recChannelsMap = {0};
            ULONGLONG playChannelsMap = {0};

            for (i = 0; i < numChannels; ++i, ++info)
            {
                if (info->isInput)
                {
                    if (info->channelNum < 0)
                    {
                        info_print_(_T("createBuffers : invalid parameter.\n"));
                        error = ASE_InvalidParameter;
                        return error;
                    }
                    if ((ULONG)info->channelNum >= m_inAvailableChannels)
                    {
                        info_print_(_T("createBuffers : over channel.\n"));
                        error = ASE_InvalidMode;
                        return error;
                    }
                    m_inMap[m_activeInputs] = info->channelNum;
                    ++m_activeInputs;
                    recChannelsMap |= 1ULL << info->channelNum;
                    if (m_activeInputs > m_inAvailableChannels)
                    {
                        info_print_(_T("createBuffers : over channel.\n"));
                        error = ASE_InvalidMode;
                        return error;
                    }
                }
                else
                {
                    if (info->channelNum < 0)
                    {
                        info_print_(_T("createBuffers : invalid parameter.\n"));
                        error = ASE_InvalidParameter;
                        return error;
                    }
                    if ((ULONG)info->channelNum >= m_outAvailableChannels)
                    {
                        info_print_(_T("createBuffers : over channel.\n"));
                        error = ASE_InvalidMode;
                        return error;
                    }
                    m_outMap[m_activeOutputs] = info->channelNum;
                    ++m_activeOutputs;
                    playChannelsMap |= 1ULL << info->channelNum;
                    if (m_activeOutputs > m_outAvailableChannels)
                    {
                        info_print_(_T("createBuffers : over channel.\n"));
                        error = ASE_InvalidMode;
                        return error;
                    }
                }
            }

            if (bufferSize != m_blockFrames)
            {
                info_print_(_T("createBuffers : requested buffer size %u differs from preferred %u.\n"), bufferSize, m_blockFrames);
                m_blockFrames = bufferSize;
                m_isRequireAsioReset = true;
                SetEvent(m_asioResetEvent);
            }

            ULONG bytesPerSample = 0;
            switch (m_audioProperty.SampleType)
            {
            case UACSampleType::UACSTInt16LSB:
                bytesPerSample = 2;
                break;
            case UACSampleType::UACSTInt24LSB:
                bytesPerSample = 3;
                break;
            case UACSampleType::UACSTInt32LSB16:
            case UACSampleType::UACSTInt32LSB20:
            case UACSampleType::UACSTInt32LSB24:
            case UACSampleType::UACSTInt32LSB:
            case UACSampleType::UACSTFloat32LSB:
                bytesPerSample = 4;
                break;
            default:
                bytesPerSample = 2;
                break;
            }
            ULONG bufferSizeBytes = m_blockFrames;
            bufferSizeBytes *= bytesPerSample;

            ULONG playSize = sizeof(UAC_ASIO_PLAY_BUFFER_HEADER) + m_outAvailableChannels * bufferSizeBytes * 2;
            ULONG recSize = sizeof(UAC_ASIO_REC_BUFFER_HEADER) + m_inAvailableChannels * bufferSizeBytes * 2;

            m_driverPlayBufferWithKsProperty = new UCHAR[sizeof(KSPROPERTY) + playSize];
            m_driverPlayBuffer = (m_driverPlayBufferWithKsProperty != nullptr) ? &(m_driverPlayBufferWithKsProperty[sizeof(KSPROPERTY)]) : nullptr;
            {
                auto lockRecBuffer = m_recBufferCS.lock();
                callDisposeBuffers = true;
                m_driverRecBuffer = new UCHAR[recSize];
                if (m_driverPlayBuffer == nullptr || m_driverRecBuffer == nullptr)
                {
                    info_print_(_T("createBuffers : insufficient resources.\n"));
                    error = ASE_NoMemory;
                    return error;
                }

                info_print_(_T("play buffer at %p, %u bytes, rec buffer at %p, %u bytes, period %d samples.\n"), m_driverPlayBuffer, playSize, m_driverRecBuffer, recSize, m_blockFrames);

                ZeroMemory((void *)m_driverPlayBuffer, playSize);
                ZeroMemory((void *)m_driverRecBuffer, recSize);

                if (m_audioProperty.CurrentSampleFormat != UACSampleFormat::UAC_SAMPLE_FORMAT_PCM)
                {
                    FillMemory((void *)(m_driverPlayBuffer + sizeof(UAC_ASIO_PLAY_BUFFER_HEADER)), m_outAvailableChannels * bufferSizeBytes * 2, DSD_ZERO_BYTE);
                }

                m_playReadyPosition = 0LL;

                m_activeInputs = 0;
                m_activeOutputs = 0;
                info = bufferInfos;
                for (i = 0; i < numChannels; i++, info++)
                {
                    if (info->isInput)
                    {
                        m_inputBuffers[m_activeInputs] = m_driverRecBuffer + sizeof(UAC_ASIO_REC_BUFFER_HEADER) + bufferSizeBytes * 2 * info->channelNum;
                        info->buffers[0] = (void *)(m_inputBuffers[m_activeInputs]);
                        info->buffers[1] = (void *)(m_inputBuffers[m_activeInputs] + bufferSizeBytes);
                        m_inMap[m_activeInputs] = info->channelNum;
                        ++m_activeInputs;
                    }
                    else // output
                    {
                        m_outputBuffers[m_activeOutputs] = m_driverPlayBuffer + sizeof(UAC_ASIO_PLAY_BUFFER_HEADER) + bufferSizeBytes * 2 * info->channelNum;
                        info->buffers[0] = (void *)(m_outputBuffers[m_activeOutputs]);
                        info->buffers[1] = (void *)(m_outputBuffers[m_activeOutputs] + bufferSizeBytes);
                        m_outMap[m_activeOutputs] = info->channelNum;
                        ++m_activeOutputs;
                    }
                }

                m_notificationEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
                if (m_notificationEvent == nullptr)
                {
                    info_print_(_T("createBuffers : insufficient resources.\n"));
                    error = ASE_NoMemory;
                    return error;
                }

                m_outputReadyEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
                if (m_outputReadyEvent == nullptr)
                {
                    info_print_(_T("createBuffers : insufficient resources.\n"));
                    error = ASE_NoMemory;
                    return error;
                }

                m_deviceReadyEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
                if (m_deviceReadyEvent == nullptr)
                {
                    info_print_(_T("createBuffers : insufficient resources.\n"));
                    error = ASE_NoMemory;
                    return error;
                }

                UAC_ASIO_PLAY_BUFFER_HEADER *         playHdr = (UAC_ASIO_PLAY_BUFFER_HEADER *)m_driverPlayBuffer;
                volatile UAC_ASIO_REC_BUFFER_HEADER * recHdr = (volatile UAC_ASIO_REC_BUFFER_HEADER *)m_driverRecBuffer;

                playHdr->AsioDriverVersion = UAC_ASIO_DRIVER_VERSION;
                playHdr->HeaderLength = sizeof(UAC_ASIO_PLAY_BUFFER_HEADER);
                playHdr->PeriodSamples = m_blockFrames;
                playHdr->PlayChannels = m_outAvailableChannels; // m_activeOutputs;
                playHdr->RecChannels = m_inAvailableChannels;   // m_activeInputs;
                playHdr->PlayChannelsMap = playChannelsMap;
                playHdr->RecChannelsMap = recChannelsMap;
                recHdr->HeaderLength = sizeof(UAC_ASIO_REC_BUFFER_HEADER);
#ifdef _WIN64
                playHdr->NotificationEvent.p64 = m_notificationEvent;
                playHdr->OutputReadyEvent.p64 = m_outputReadyEvent;
                playHdr->DeviceReadyEvent.p64 = m_deviceReadyEvent;
#else // _WIN64
                playHdr->NotificationEvent = m_notificationEvent;
                playHdr->OutputReadyEvent = m_outputReadyEvent;
                playHdr->DeviceReadyEvent = m_deviceReadyEvent;
#endif
                playHdr->Training = 0;
                result = SetAsioBuffer(m_usbDeviceHandle, m_driverPlayBufferWithKsProperty, sizeof(KSPROPERTY) + playSize, (UCHAR *)m_driverRecBuffer, recSize);

                if (!result)
                {
                    DWORD lastError = GetLastError();
                    if (lastError == ERROR_REVISION_MISMATCH)
                    {
                        TCHAR messageString[ERROR_MESSAGE_LENGTH] = {0};
                        LoadString(GetModuleHandle(nullptr), IDS_ERRMSG_VERSION_MISMATCH, messageString, sizeof(messageString) / sizeof(messageString[0]));
                        _tcscpy_s(m_errorMessage, ERROR_MESSAGE_LENGTH, messageString);
                        info_print_(_T("createBuffers : driver version mismatch.\n"));
                    }
                    else
                    {
                        info_print_(_T("createBuffers : physical driver reports error.\n"));
                    }
                    error = ASE_NotPresent;

                    return error;
                }

                this->m_callbacks = callbacks;
                if (callbacks->asioMessage(kAsioSupportsTimeInfo, 0, 0, 0))
                {
                    info_print_(_T("time info mode.\n"));
                    m_isTimeInfoMode = true;
                    m_asioTime.timeInfo.speed = 1.;
                    m_asioTime.timeInfo.systemTime.hi = m_asioTime.timeInfo.systemTime.lo = 0;
                    m_asioTime.timeInfo.samplePosition.hi = m_asioTime.timeInfo.samplePosition.lo = 0;
                    m_asioTime.timeInfo.sampleRate = m_sampleRate;
                    m_asioTime.timeInfo.flags = kSystemTimeValid | kSamplePositionValid | kSampleRateValid;
                    m_asioTime.timeCode.flags = 0;
                }
                else
                {
                    info_print_(_T("NOT time info mode.\n"));
                    m_isTimeInfoMode = false;
                }
                for (i = 0; i < numChannels; i++)
                {
                    info_print_(_T("buffer %2u: isInput %d, channelNum %d, buffer0 %p, buffer1 %p.\n"), i, bufferInfos[i].isInput, bufferInfos[i].channelNum, bufferInfos[i].buffers[0], bufferInfos[i].buffers[1]);
                }
            }
        }
    }
    info_print_(_T("createBuffers : completed.\n"));
    error = ASE_OK;
    return error;
}

ASIOError CUSBAsio::disposeBuffers()
{
    info_print_(_T("disposeBuffers\n"));
    BOOL result;

    if (m_usbDeviceHandle == INVALID_HANDLE_VALUE || m_inputLatency == 0 || m_outputLatency == 0)
    {
        // No error is returned even if the hardware is unusable. -> Error handling policy changed: ASE_NotPresent is returned.
        info_print_(_T("disposeBuffers : device not ready.\n"));
        return ASE_NotPresent;
    }

    {
        auto lockClient = m_clientInfoCS.lock();

        if (!m_isActive)
        {
            return ASE_InvalidMode;
        }
        m_isActive = false;

        {
            auto lockDevice = m_deviceInfoCS.lock();

            m_callbacks = nullptr;
            stop();
            result = UnsetAsioBuffer(m_usbDeviceHandle);
            m_activeInputs = 0;
            m_activeOutputs = 0;
            if (m_driverPlayBufferWithKsProperty != nullptr)
            {
                delete[] m_driverPlayBufferWithKsProperty;
                m_driverPlayBufferWithKsProperty = nullptr;
                m_driverPlayBuffer = nullptr;
            }
            {
                auto lockRecBuffer = m_recBufferCS.lock();
                if (m_driverRecBuffer != nullptr)
                {
                    delete[] m_driverRecBuffer;
                    m_driverRecBuffer = nullptr;
                }
            }
            if (m_deviceReadyEvent != nullptr)
            {
                result = CloseHandle(m_deviceReadyEvent);
                m_deviceReadyEvent = nullptr;
            }
            if (m_outputReadyEvent != nullptr)
            {
                result = CloseHandle(m_outputReadyEvent);
                m_outputReadyEvent = nullptr;
            }
            if (m_notificationEvent != nullptr)
            {
                result = CloseHandle(m_notificationEvent);
                m_notificationEvent = nullptr;
            }
        }
    }
    return ASE_OK;
}

ASIOError CUSBAsio::controlPanel()
{
    info_print_(_T("controlPanel\n"));

    BOOL result = ExecuteControlPanel();
    if (!result)
    {
        // If the Cancel button is pressed.
        return ASE_OK;
    }

    return ASE_OK;
}

_Use_decl_annotations_
ASIOError CUSBAsio::future(long selector, void * option) // !!! check properties
{
    switch (selector)
    {
    case kAsioEnableTimeCodeRead:
        return ASE_NotPresent;
    case kAsioDisableTimeCodeRead:
        return ASE_NotPresent;
    case kAsioSetInputMonitor:
        return ASE_NotPresent;
    case kAsioTransport:
        return ASE_NotPresent;
    case kAsioSetInputGain:
        return ASE_NotPresent;
    case kAsioGetInputMeter:
        return ASE_NotPresent;
    case kAsioSetOutputGain:
        return ASE_NotPresent;
    case kAsioGetOutputMeter:
        return ASE_NotPresent;
    case kAsioCanInputMonitor:
        return ASE_NotPresent;
    case kAsioCanTimeInfo:
        return ASE_SUCCESS;
    case kAsioCanTimeCode:
        return ASE_NotPresent;
    case kAsioCanTransport:
        return ASE_NotPresent;
    case kAsioCanInputGain:
        return ASE_NotPresent;
    case kAsioCanInputMeter:
        return ASE_NotPresent;
    case kAsioCanOutputGain:
        return ASE_NotPresent;
    case kAsioCanOutputMeter:
        return ASE_NotPresent;
    case kAsioOptionalOne:
        return ASE_NotPresent;
    case kAsioSetIoFormat: {
        if (option == nullptr)
        {
            return ASE_NotPresent;
        }
        ASIOIoFormat * requestedFormat = (ASIOIoFormat *)option;
        info_print_(_T("kAsioSetIoFormat request. Device supported 0x%x, current %u, requested %u.\n"), m_audioProperty.SupportedSampleFormats, m_audioProperty.CurrentSampleFormat, requestedFormat->FormatType);
        auto lockClient = m_clientInfoCS.lock();

        if ((requestedFormat->FormatType == kASIOPCMFormat) && ((m_audioProperty.SupportedSampleFormats & GetSupportedSampleFormats()) != 0))
        {
            m_requestedSampleFormat = requestedFormat->FormatType;
            return ASE_SUCCESS;
        }
        else
        {
            return ASE_NotPresent;
        }
    }
    case kAsioGetIoFormat: {
        if (option == nullptr)
        {
            return ASE_NotPresent;
        }
        ASIOIoFormat * requestedFormat = (ASIOIoFormat *)option;
        info_print_(_T("kAsioGetIoFormat request. Device supported 0x%x, current %u.\n"), m_audioProperty.SupportedSampleFormats, m_audioProperty.CurrentSampleFormat);
        if ((m_audioProperty.SupportedSampleFormats & GetSupportedSampleFormats()) != 0)
        {
            requestedFormat->FormatType = m_requestedSampleFormat;
            return ASE_SUCCESS;
        }
        else
        {
            return ASE_NotPresent;
        }
    }
    case kAsioCanDoIoFormat: {
        if (option == nullptr)
        {
            return ASE_NotPresent;
        }
        ASIOIoFormat * requestedFormat = (ASIOIoFormat *)option;
        info_print_(_T("kAsioCanDoIoFormat. Device supported %u, current 0x%x, requested %u.\n"), m_audioProperty.SupportedSampleFormats, m_audioProperty.CurrentSampleFormat, requestedFormat->FormatType);
        if ((requestedFormat->FormatType == kASIOPCMFormat) && ((m_audioProperty.SupportedSampleFormats & GetSupportedSampleFormats()) != 0))
        {
            return ASE_SUCCESS;
        }
        else
        {
            return ASE_NotPresent;
        }
    }
    case kAsioCanReportOverload: {
        if (m_isDropoutDetectionSetting)
        {
            m_isSupportDropoutDetection = true;
            info_print_(_T("kAsioCanReportOverload request.\n"));
            return ASE_SUCCESS;
        }
        else
        {
            m_isSupportDropoutDetection = false;
            return ASE_NotPresent;
        }
    }
    case kAsioGetInternalBufferSamples: {
        if (option == nullptr)
        {
            return ASE_InvalidParameter;
        }
        ASIOInternalBufferInfo * internalBufferInfo = (ASIOInternalBufferInfo *)option;
        internalBufferInfo->inputSamples = m_audioProperty.InputDriverBuffer;
        internalBufferInfo->outputSamples = m_audioProperty.OutputDriverBuffer;
        info_print_(_T("kAsioGetInternalBufferSamples request. in %u samples, out %u samples.\n"), internalBufferInfo->inputSamples, internalBufferInfo->outputSamples);
        return ASE_SUCCESS;
    }
    }
    return ASE_InvalidParameter;
}

//--------------------------------------------------------------------------------------------------------
// private methods
//--------------------------------------------------------------------------------------------------------
void CUSBAsio::BufferSwitch()
{
    if (m_isStarted && m_callbacks)
    {
        getNanoSeconds(&m_theSystemTime); // latch system time
        m_samplePosition += m_blockFrames;
        if (m_isTimeInfoMode)
        {
            BufferSwitchX();
        }
        else
        {
            m_callbacks->bufferSwitch(m_toggle, ASIOTrue);
        }
        m_toggle = m_toggle ? 0 : 1;
    }
}

void CUSBAsio::BufferSwitchX()
{
    getSamplePosition(&m_asioTime.timeInfo.samplePosition, &m_asioTime.timeInfo.systemTime);
    m_callbacks->bufferSwitchTimeInfo(&m_asioTime, m_toggle, ASIOTrue);
    m_asioTime.timeInfo.flags &= ~(kSampleRateChanged | kClockSourceChanged);
}

ULONG CUSBAsio::GetSupportedSampleFormats()
{
    return ((1 << toULong(UACSampleFormat::UAC_SAMPLE_FORMAT_PCM)) | (1 << toULong(UACSampleFormat::UAC_SAMPLE_FORMAT_IEEE_FLOAT)));
}

ASIOError CUSBAsio::outputReady()
{
    if (!m_isActive)
    {
        return ASE_OK;
    }
    auto                                  lockRecBuffer = m_recBufferCS.lock();
    volatile UAC_ASIO_REC_BUFFER_HEADER * recHdr = (volatile UAC_ASIO_REC_BUFFER_HEADER *)m_driverRecBuffer;
    if (recHdr != nullptr)
    {
        InterlockedOr((LONG *)&recHdr->OutputReady, toInt(UserThreadStatuses::OutputReady));
        recHdr->PlayReadyPosition = m_playReadyPosition;
    }
    SetEvent(m_outputReadyEvent);
    SetEvent(m_outputReadyBlockEvent);
    InterlockedOr(&m_outputReadyBlock, 1);
    return ASE_OK;
}

_Use_decl_annotations_
ULONG CUSBAsio::CalcInputLatency(
    ULONG samplingRate,
    ULONG periodFrames,
    ULONG classicFramesPerIrp,
    LONG /* outputFrameDelay */,
    LONG  latencyOffset,
    ULONG bufferOperationThread,
    ULONG inBufferOperationOffset,
    ULONG /* outBufferOperationOffset */,
    ULONG packetsPerMs
)
{
    double latency;

    if (bufferOperationThread)
    {
        latency = periodFrames + ((double)samplingRate * (double)((classicFramesPerIrp * packetsPerMs) + inBufferOperationOffset)) / (packetsPerMs * 1000) + latencyOffset;
    }
    else
    {
        latency = periodFrames + ((double)samplingRate * (double)classicFramesPerIrp) / 2000 + latencyOffset;
    }

    return (ULONG)latency;
}

_Use_decl_annotations_
ULONG CUSBAsio::calcOutputLatency(
    ULONG samplingRate,
    ULONG periodFrames,
    ULONG classicFramesPerIrp,
    LONG  outputFrameDelay,
    LONG  latencyOffset,
    ULONG bufferOperationThread,
    ULONG /* inBufferOperationOffset */,
    ULONG outBufferOperationOffset,
    ULONG packetsPerMs
)
{
    double latency;

    if (bufferOperationThread)
    {
        latency = periodFrames + ((double)samplingRate * (double)outBufferOperationOffset) / (packetsPerMs * 1000) + latencyOffset;
    }
    else
    {
        if (outputFrameDelay == 0)
        {
            latency = periodFrames + ((double)samplingRate * (double)classicFramesPerIrp * 3) / 2000 + latencyOffset;
        }
        else
        {
            latency = periodFrames + ((double)samplingRate * ((double)classicFramesPerIrp + 2 * outputFrameDelay)) / 2000 + latencyOffset;
        }
    }

    return (ULONG)latency;
}

//---------------------------------------------------------------------------------------------
bool CUSBAsio::MeasureLatency()
{
    if (m_activeInputs != 0 || m_activeOutputs != 0)
    {
        return true;
    }

#if defined(INFO_PRINT_)
    ULONG classicFramesPerIrp = (m_audioProperty.PacketsPerSec == 1000 ? m_driverFlags.ClassicFramesPerIrp : m_driverFlags.ClassicFramesPerIrp2);
#endif

    m_inputLatency = m_blockFrames + m_audioProperty.InputLatencyOffset;

    m_outputLatency = m_blockFrames + m_audioProperty.OutputLatencyOffset;

    info_print_(_T(" SampleRate = %d, m_blockFrames = %d, ClassicFramesPerIrp = %d, OutFrameDelay = %d, InputLatencyOffset = %d, OutputLatencyOffset = %d\n"), m_audioProperty.SampleRate, m_blockFrames, classicFramesPerIrp, m_driverFlags.OutputFrameDelay, m_audioProperty.InputLatencyOffset, m_audioProperty.OutputLatencyOffset);
    info_print_(_T("calculated latency is in:%d, out:%d samples.\n"), m_inputLatency, m_outputLatency);

    if (m_inputLatency == 0 || m_outputLatency == 0)
    {
        return false;
    }
    else
    {
        return true;
    }
}

bool CUSBAsio::ApplySettings()
{
    LONG  result;
    HKEY  hKey;
    ULONG temp;
    DWORD size;

    // default values
    m_fixedSamplingRate = 0;
    m_blockFrames = UAC_DEFAULT_ASIO_BUFFER_SIZE;
    m_driverFlags.FirstPacketLatency = UAC_DEFAULT_FIRST_PACKET_LATENCY;
    m_driverFlags.ClassicFramesPerIrp = UAC_DEFAULT_CLASSIC_FRAMES_PER_IRP;
    m_driverFlags.MaxIrpNumber = UAC_DEFAULT_MAX_IRP_NUMBER;
    m_driverFlags.PreSendFrames = UAC_DEFAULT_PRE_SEND_FRAMES;
    m_driverFlags.OutputFrameDelay = UAC_DEFAULT_OUTPUT_FRAME_DELAY;
    m_driverFlags.DelayedOutputBufferSwitch = UAC_DEFAULT_DELAYED_OUTPUT_BUFFER_SWITCH;
    m_driverFlags.InputBufferOperationOffset = UAC_DEFAULT_IN_BUFFER_OPERATION_OFFSET;
    m_driverFlags.InputHubOffset = UAC_DEFAULT_IN_HUB_OFFSET;
    m_driverFlags.OutputBufferOperationOffset = UAC_DEFAULT_OUT_BUFFER_OPERATION_OFFSET;
    m_driverFlags.OutputHubOffset = UAC_DEFAULT_OUT_HUB_OFFSET;
    m_driverFlags.BufferThreadPriority = UAC_DEFAULT_BUFFER_THREAD_PRIORITY;
    m_driverFlags.ClassicFramesPerIrp2 = UAC_DEFAULT_CLASSIC_FRAMES_PER_IRP;
    m_driverFlags.SuggestedBufferPeriod = UAC_DEFAULT_ASIO_BUFFER_SIZE;
    m_threadPriority = 2;
    m_isDropoutDetectionSetting = UAC_DEFAULT_DROPOUT_DETECTION;

    result = RegOpenKeyEx(HKEY_CURRENT_USER, c_RegistryKeyName, 0, KEY_READ, &hKey);

    if (result == ERROR_SUCCESS)
    {
        size = sizeof(ULONG);
        result = RegQueryValueEx(hKey, c_FixedSamplingRateValueName, 0, nullptr, (PBYTE)&temp, &size);
        if (result == ERROR_SUCCESS)
        {
            m_fixedSamplingRate = temp;
        }

        size = sizeof(ULONG);
        result = RegQueryValueEx(hKey, c_PeriodFramesValueName, 0, nullptr, (PBYTE)&temp, &size);
        if (result == ERROR_SUCCESS)
        {
            m_blockFrames = temp;
        }

        size = sizeof(ULONG);
        result = RegQueryValueEx(hKey, c_FirstPacketLatencyValueName, 0, nullptr, (PBYTE)&temp, &size);
        if (result == ERROR_SUCCESS)
        {
            m_driverFlags.FirstPacketLatency = temp;
        }
        size = sizeof(ULONG);
        result = RegQueryValueEx(hKey, c_ClassicFramesPerIrpValueName, 0, nullptr, (PBYTE)&temp, &size);
        if (result == ERROR_SUCCESS)
        {
            m_driverFlags.ClassicFramesPerIrp = temp;
        }
        size = sizeof(ULONG);
        result = RegQueryValueEx(hKey, c_ClassicFramesPerIrp2ValueName, 0, nullptr, (PBYTE)&temp, &size);
        if (result == ERROR_SUCCESS)
        {
            m_driverFlags.ClassicFramesPerIrp2 = temp;
        }

        size = sizeof(ULONG);
        result = RegQueryValueEx(hKey, c_MaxIrpNumberValueName, 0, nullptr, (PBYTE)&temp, &size);
        if (result == ERROR_SUCCESS)
        {
            m_driverFlags.MaxIrpNumber = temp;
        }

        size = sizeof(ULONG);
        result = RegQueryValueEx(hKey, c_PreSendFramesValueName, 0, nullptr, (PBYTE)&temp, &size);
        if (result == ERROR_SUCCESS)
        {
            m_driverFlags.PreSendFrames = temp;
        }

        size = sizeof(ULONG);
        result = RegQueryValueEx(hKey, c_OutputFrameDelayValueName, 0, nullptr, (PBYTE)&temp, &size);
        if (result == ERROR_SUCCESS)
        {
            m_driverFlags.OutputFrameDelay = temp;
        }

        size = sizeof(ULONG);
        result = RegQueryValueEx(hKey, c_DelayedOutputBufferSwitchName, 0, nullptr, (PBYTE)&temp, &size);
        if (result == ERROR_SUCCESS)
        {
            m_driverFlags.DelayedOutputBufferSwitch = temp;
        }

        size = sizeof(ULONG);
        result = RegQueryValueEx(hKey, c_InputBufferOperationOffsetName, 0, nullptr, (PBYTE)&temp, &size);
        if (result == ERROR_SUCCESS)
        {
            m_driverFlags.InputBufferOperationOffset = temp;
        }

        size = sizeof(ULONG);
        result = RegQueryValueEx(hKey, c_InputHubOffsetName, 0, nullptr, (PBYTE)&temp, &size);
        if (result == ERROR_SUCCESS)
        {
            m_driverFlags.InputHubOffset = temp;
        }

        size = sizeof(ULONG);
        result = RegQueryValueEx(hKey, c_OutputBufferOperationOffsetName, 0, nullptr, (PBYTE)&temp, &size);
        if (result == ERROR_SUCCESS)
        {
            m_driverFlags.OutputBufferOperationOffset = temp;
        }

        size = sizeof(ULONG);
        result = RegQueryValueEx(hKey, c_OutputHubOffsetName, 0, nullptr, (PBYTE)&temp, &size);
        if (result == ERROR_SUCCESS)
        {
            m_driverFlags.OutputHubOffset = temp;
        }

        size = sizeof(ULONG);
        result = RegQueryValueEx(hKey, c_BufferThreadPriorityName, 0, nullptr, (PBYTE)&temp, &size);
        if (result == ERROR_SUCCESS)
        {
            m_driverFlags.BufferThreadPriority = temp;
        }

        size = sizeof(ULONG);
        result = RegQueryValueEx(hKey, c_DropoutDetectionName, 0, nullptr, (PBYTE)&temp, &size);
        if (result == ERROR_SUCCESS)
        {
            m_isDropoutDetectionSetting = temp != 0;
        }

        m_driverFlags.SuggestedBufferPeriod = m_blockFrames;

        RegCloseKey(hKey);
    }

    if (!SetFlags(m_usbDeviceHandle, m_driverFlags))
    {
        info_print_(_T("set flags failed.\n"));
        return false;
    }
    return true;
}

bool CUSBAsio::ExecuteControlPanel()
{
    TCHAR path[MAX_PATH] = {0};
    TCHAR drive[_MAX_DRIVE] = {0};
    TCHAR dir[_MAX_DIR] = {0};

    GetModuleFileNameEx(GetCurrentProcess(), GetModuleHandle(ASIODRV_NAME), path, MAX_PATH);
    _tsplitpath_s(path, drive, _MAX_DRIVE, dir, _MAX_DIR, nullptr, 0, nullptr, 0);
    _stprintf_s(path, MAX_PATH, TEXT("%s%s%s"), drive, dir, CONTROLPANELPROGRAMNAME);

    STARTUPINFO startupInfo;
    ZeroMemory(&startupInfo, sizeof(STARTUPINFO));
    startupInfo.cb = sizeof(STARTUPINFO);

    PROCESS_INFORMATION processInfo{};

    CreateProcess(
        path,
        nullptr,
        nullptr,
        nullptr,
        FALSE,
        NORMAL_PRIORITY_CLASS,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo
    );

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
}

bool CUSBAsio::GetDesiredPath()
{
    LONG  result;
    DWORD size;
    HKEY  hKey;

    if (m_desiredPath != nullptr)
    {
        delete[] m_desiredPath;
        m_desiredPath = nullptr;
    }

    result = RegOpenKeyEx(HKEY_CURRENT_USER, c_RegistryKeyName, 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS)
    {
        return false;
    }

    result = RegQueryValueEx(hKey, c_AsioDeviceValueName, 0, nullptr, nullptr, &size);
    if (result != ERROR_SUCCESS || size == 0)
    {
        RegCloseKey(hKey);
        return false;
    }

    m_desiredPath = new TCHAR[size / sizeof(TCHAR)];
    result = RegQueryValueEx(hKey, c_AsioDeviceValueName, 0, nullptr, (LPBYTE)m_desiredPath, &size);
    if (result != ERROR_SUCCESS)
    {
        RegCloseKey(hKey);
        delete[] m_desiredPath;
        m_desiredPath = nullptr;
        return false;
    }

    RegCloseKey(hKey);

    info_print_(_T("ASIO device path : %s\n"), m_desiredPath);

    return true;
}

bool CUSBAsio::ObtainDeviceParameter()
{
    int  bufferCoefficient = 1;
    bool isLatencyMeasured = true;
    BOOL result = TRUE;

    auto lockDevice = m_deviceInfoCS.lock();

    ULONG maxRetry = 6;
    for (ULONG retry = 0; retry < maxRetry; ++retry)
    {
        if (m_channelInfo != nullptr)
        {
            delete[] ((UCHAR *)m_channelInfo);
            m_channelInfo = nullptr;
        }
        m_inputLatency = 0;
        m_outputLatency = 0;

        if (m_audioProperty.SampleRate > 50000 && m_audioProperty.SampleRate < 99999)
        {
            bufferCoefficient = 2;
        }
        if (m_audioProperty.SampleRate > 100000 && m_audioProperty.SampleRate < 199999)
        {
            bufferCoefficient = 4;
        }
        if (m_audioProperty.SampleRate > 200000 && m_audioProperty.SampleRate < 399999)
        {
            bufferCoefficient = 8;
        }
        if (m_audioProperty.SampleRate > 400000)
        {
            bufferCoefficient = 16;
        }
        m_blockFrames /= bufferCoefficient;

        result = GetAudioProperty(m_usbDeviceHandle, &m_audioProperty);
        if (!result || !m_audioProperty.IsAccessible)
        {
            info_print_(_T("failed to obtain device property\n"));
            TCHAR messageString[ERROR_MESSAGE_LENGTH] = {0};
            LoadString(GetModuleHandle(nullptr), IDS_ERRMSG_CONSTRUCT, messageString, sizeof(messageString) / sizeof(messageString[0]));
            _tcscpy_s(m_errorMessage, ERROR_MESSAGE_LENGTH, messageString);
            ReleaseAsioOwnership(m_usbDeviceHandle);
            CloseHandle(m_usbDeviceHandle);
            m_usbDeviceHandle = INVALID_HANDLE_VALUE;
            return false;
        }
        if ((m_audioProperty.InputAsioChannels < 1) && (m_audioProperty.OutputAsioChannels < 1))
        {
            TCHAR messageString[ERROR_MESSAGE_LENGTH] = {0};
            LoadString(GetModuleHandle(nullptr), IDS_ERRMSG_CONSTRUCT, messageString, sizeof(messageString) / sizeof(messageString[0]));
            _tcscpy_s(m_errorMessage, ERROR_MESSAGE_LENGTH, messageString);
            ReleaseAsioOwnership(m_usbDeviceHandle);
            CloseHandle(m_usbDeviceHandle);
            m_usbDeviceHandle = INVALID_HANDLE_VALUE;
            return false;
        }
        UCHAR * channelInfoBuffer = nullptr;
        result = GetChannelInfo(m_usbDeviceHandle, (PUAC_GET_CHANNEL_INFO_CONTEXT *)&channelInfoBuffer);
        if (result)
        {
            m_channelInfo = (PUAC_GET_CHANNEL_INFO_CONTEXT)channelInfoBuffer;
        }
        else
        {
            TCHAR messageString[ERROR_MESSAGE_LENGTH] = {0};
            LoadString(GetModuleHandle(nullptr), IDS_ERRMSG_CONSTRUCT, messageString, sizeof(messageString) / sizeof(messageString[0]));
            _tcscpy_s(m_errorMessage, ERROR_MESSAGE_LENGTH, messageString);
            ReleaseAsioOwnership(m_usbDeviceHandle);
            CloseHandle(m_usbDeviceHandle);
            m_usbDeviceHandle = INVALID_HANDLE_VALUE;
            return false;
        }

        bufferCoefficient = 1;
        if (m_audioProperty.SampleRate > 50000 && m_audioProperty.SampleRate < 99999)
        {
            bufferCoefficient = 2;
        }
        if (m_audioProperty.SampleRate > 100000 && m_audioProperty.SampleRate < 199999)
        {
            bufferCoefficient = 4;
        }
        if (m_audioProperty.SampleRate > 200000 && m_audioProperty.SampleRate < 399999)
        {
            bufferCoefficient = 8;
        }
        if (m_audioProperty.SampleRate > 400000)
        {
            bufferCoefficient = 16;
        }
        m_blockFrames *= bufferCoefficient;

        m_inAvailableChannels = m_audioProperty.InputAsioChannels;
        m_outAvailableChannels = m_audioProperty.OutputAsioChannels;
        m_sampleRate = (double)m_audioProperty.SampleRate;

        isLatencyMeasured = MeasureLatency();
        if (isLatencyMeasured)
        {
            break;
        }
        if (retry < maxRetry - 1)
        {
            Sleep(500);
            continue;
        }
    }
    if (!isLatencyMeasured)
    {
        ReleaseAsioOwnership(m_usbDeviceHandle);
        CloseHandle(m_usbDeviceHandle);
        m_usbDeviceHandle = INVALID_HANDLE_VALUE;
        // >>comment-004<<
        TCHAR messageString[ERROR_MESSAGE_LENGTH] = {0};
        LoadString(GetModuleHandle(nullptr), IDS_ERRMSG_LATENCY, messageString, sizeof(messageString) / sizeof(messageString[0]));
        _tcscpy_s(m_errorMessage, ERROR_MESSAGE_LENGTH, messageString);
        return false;
    }
    if (m_inAvailableChannels > NUMOFINPUTS)
    {
        m_inAvailableChannels = NUMOFINPUTS;
    }
    if (m_outAvailableChannels > NUMOFOUTPUTS)
    {
        m_outAvailableChannels = NUMOFOUTPUTS;
    }
    return true;
}

bool CUSBAsio::RequestClockInfoChange()
{
    info_print_(_T("RequestClockInfoChange\n"));

    auto lockClient = m_clientInfoCS.lock();
    if (m_isActive)
    {
        auto lockRecBuffer = m_recBufferCS.lock();
        // Issues a callback when a buffer is allocated but stopped.
        volatile UAC_ASIO_REC_BUFFER_HEADER * recHdr = (volatile UAC_ASIO_REC_BUFFER_HEADER *)m_driverRecBuffer;
        if (recHdr != nullptr && (recHdr->DeviceStatus & toInt(DeviceStatuses::SampleRateChanged)) != 0 && recHdr->CurrentSampleRate != 0)
        {
            m_requireSampleRateChange = true;
            m_nextSampleRate = (ASIOSampleRate)(recHdr->CurrentSampleRate);
            SetEvent(m_asioResetEvent);
            recHdr->DeviceStatus &= ~((ULONG)toInt(DeviceStatuses::SampleRateChanged));
        }
        if (recHdr != nullptr && (recHdr->DeviceStatus & toInt(DeviceStatuses::ResetRequired)) != 0)
        {
            m_isRequireAsioReset = true;
            SetEvent(m_asioResetEvent);
            recHdr->DeviceStatus &= ~((ULONG)toInt(DeviceStatuses::ResetRequired));
        }
    }
    else
    {
        // If the fs/clock source is changed before the buffer is acquired, the device information is acquired again and the latency is calculated again.
        bool result = ObtainDeviceParameter();
        info_print_(_T("ObtainDeviceParameter() completed, result %u, current rate %u, format %u\n"), result, m_audioProperty.SampleRate, m_audioProperty.CurrentSampleFormat);
        if (!result)
        {
            return false;
        }
    }
    return true;
}

_Use_decl_annotations_
unsigned int __stdcall CUSBAsio::AsioResetThread(void * param)
{
    CUSBAsio *     self = (CUSBAsio *)param;
    bool           done = false;
    DWORD          status = 0;
    ASIOSampleRate oldSampleRate = 0;
    ULONG          resetQueue = 0;
    ULONG          resetExecuted = 0;

    InterlockedIncrement(&g_AsioResetThread);

    info_print_(_T("entering ASIO reset thread instance %d.\n"), InterlockedCompareExchange(&g_AsioResetThread, 0, 0));

    HANDLE handlesForWait[] = {self->m_terminateAsioResetEvent, self->m_asioResetEvent};

    do
    {
        if (resetQueue > 0)
        {
            auto lockClient = self->m_clientInfoCS.lock();
            if (self->m_callbacks != nullptr && self->m_callbacks->asioMessage != nullptr)
            {
                info_print_(_T("AsioResetThread: ASIO reset callback try %u, thread ID %u.\n"), resetExecuted, GetCurrentThreadId());
                self->m_callbacks->asioMessage(kAsioResetRequest, 0, nullptr, nullptr);
                --resetQueue;
                ++resetExecuted;
            }
        }
        status = WaitForMultipleObjects(sizeof(handlesForWait) / sizeof(handlesForWait[0]), handlesForWait, FALSE, ASIO_RESET_TIMEOUT);
        switch (status)
        {
        case WAIT_OBJECT_0:
            done = true;
            break;
        case WAIT_OBJECT_0 + 1:
            if (self->m_requireSampleRateChange)
            {
                self->m_requireSampleRateChange = false;
                auto lockClient = self->m_clientInfoCS.lock();
                if (self->m_callbacks != nullptr && self->m_callbacks->sampleRateDidChange != nullptr && oldSampleRate != self->m_nextSampleRate)
                {
                    info_print_(_T("AsioResetThread: sample rate change callback, new %lf.\n"), self->m_nextSampleRate);
                    self->m_callbacks->sampleRateDidChange(self->m_nextSampleRate);
                    oldSampleRate = self->m_nextSampleRate;
                }
            }
            if (self->m_isSupportDropoutDetection && self->m_isRequireReportDropout)
            {
                self->m_isRequireReportDropout = false;
                if (self->m_callbacks != nullptr && self->m_callbacks->asioMessage != nullptr)
                {
                    info_print_(_T("AsioResetThread: dropout detect callback.\n"));
                    self->m_callbacks->asioMessage(kAsioOverload, 0, nullptr, nullptr);
                }
            }
            if (self->m_isRequireLatencyChange)
            {
                self->m_isRequireLatencyChange = false;
                if (self->m_callbacks != nullptr && self->m_callbacks->asioMessage != nullptr)
                {
                    info_print_(_T("AsioResetThread: latency change callback.\n"));
                    self->m_callbacks->asioMessage(kAsioLatenciesChanged, 0, nullptr, nullptr);
                }
            }
            if (self->m_isRequireAsioReset)
            {
                self->m_isRequireAsioReset = false;
                ++resetQueue;
            }
            break;
        case WAIT_TIMEOUT:
            break;
        default:
            done = true;
            break;
        }
    } while (!done);

    info_print_(_T("exiting ASIO reset thread %d.\n"), InterlockedCompareExchange(&g_AsioResetThread, 0, 0));
    InterlockedDecrement(&g_AsioResetThread);

    return 0;
}

void CUSBAsio::ThreadStart()
{
    ResetEvent(m_stopEvent);
    auto beginThreadResult = _beginthreadex(nullptr, 0, WorkerThread, this, 0, nullptr);
    m_workerThread = (HANDLE)beginThreadResult;
    if ((beginThreadResult > 0) && (m_threadPriority == -2))
    {
        SetThreadPriority(m_workerThread, THREAD_PRIORITY_TIME_CRITICAL);
        info_print_(_T("call SetThreadPriority %d.\n"), THREAD_PRIORITY_TIME_CRITICAL);
    }
}

void CUSBAsio::ThreadStop()
{
    DWORD status;
    SetEvent(m_stopEvent);
    if (m_workerThread != nullptr)
    {
        DWORD timeout = (DWORD)(NOTIFICATION_TIMEOUT) * 2;
        status = WaitForSingleObject(m_workerThread, timeout);
        if (status == WAIT_OBJECT_0)
        {
            CloseHandle(m_workerThread);
        }
        else if (status == WAIT_TIMEOUT)
        {
            error_print_("wait timouut. force terminating worker thread.");
            // Understanding that there is an issue, preventing proper cleanup, call TerminateThread.
            // TerminateThread(m_workerThread, 0);

            // Implemented without using TerminateThread.
            // If the thread does not exit within the timeout, it will be forcibly terminated by the OS when the application exits.

            CloseHandle(m_workerThread);
            InterlockedDecrement(&g_WorkerThread);
        }
        else
        {
            error_print_(_T("wait timeout.\n"));
        }
    }
    m_workerThread = nullptr;
}

_Use_decl_annotations_
unsigned int __stdcall CUSBAsio::WorkerThread(void * param)
{
    CUSBAsio *                            self = (CUSBAsio *)param;
    volatile UAC_ASIO_REC_BUFFER_HEADER * recHdr = (volatile UAC_ASIO_REC_BUFFER_HEADER *)self->m_driverRecBuffer;
    ULONG                                 wakeup = 0;
    DWORD                                 status = 0;
    bool                                  done = false;

#ifdef ASIO_THREAD_STATISTICS
    static const ULONG statsSize = 120000;
    ULONG              statsPos = 0;

    typedef struct ASIO_STATISTICS_
    {
        double dueTime;
    } ASIO_STATISTICS, *PASIO_STATISTICS;

    PASIO_STATISTICS stats = new ASIO_STATISTICS[statsSize];

    LARGE_INTEGER performanceFreq = {0};
    QueryPerformanceFrequency(&performanceFreq);

    ULONGLONG lastAsioCallbackPC = {0};

#if defined(INFO_PRINT_)
    double idealPeriod = (double)(self->m_blockFrames * 1000000) / self->m_sampleRate;
#endif
#endif

    UAC_ASIO_REC_BUFFER_HEADER curHdr = {0};
    UAC_ASIO_REC_BUFFER_HEADER prevHdr = {0};
    prevHdr.RecBufferPosition = 0 - self->m_blockFrames;

    InterlockedIncrement(&g_WorkerThread);

    info_print_(_T("entering worker thread instance %d.\n"), InterlockedCompareExchange(&g_WorkerThread, 0, 0));

    HANDLE handlesForWait[] = {self->m_stopEvent, self->m_notificationEvent};

    DWORD taskIndex = 0;
    if (self->m_threadPriority != -2)
    {
        HANDLE hTask = AvSetMmThreadCharacteristics(TEXT("Pro Audio"), &taskIndex);
        AvSetMmThreadPriority(hTask, (AVRT_PRIORITY)self->m_threadPriority);
        info_print_(_T("call AvSetMmThreadPriority %d.\n"), self->m_threadPriority);
    }

    self->BufferSwitch();

    Sleep(self->m_blockFrames / 1000 / self->m_audioProperty.SampleRate);

    self->BufferSwitch();

    StartAsioStream(self->m_usbDeviceHandle);

    DWORD timeout = NOTIFICATION_TIMEOUT;

    recHdr->CallbackRemain = 0;

    do
    {
        bool setAsioResetEvent = false;
        status = WaitForMultipleObjects(sizeof(handlesForWait) / sizeof(handlesForWait[0]), handlesForWait, FALSE, timeout);
        switch (status)
        {
        case WAIT_OBJECT_0:
            done = true;
            break;
        case WAIT_OBJECT_0 + 1:
            memcpy(&curHdr, (void *)recHdr, sizeof(curHdr)); // Explicit copy
            if ((curHdr.DeviceStatus & toInt(DeviceStatuses::ClockSourceChanged)) != 0)
            {
                info_print_(_T("clock source change detected, new %u.\n"), curHdr.CurrentClockSource);
                self->m_asioTime.timeInfo.flags |= kClockSourceChanged;
                recHdr->DeviceStatus &= ~((ULONG)toInt(DeviceStatuses::ClockSourceChanged));
            }
            if (((curHdr.DeviceStatus & toInt(DeviceStatuses::SampleRateChanged)) != 0 && curHdr.CurrentSampleRate != 0) ||
                (curHdr.CurrentSampleRate != (ULONG)self->m_sampleRate))
            {
                info_print_(_T("sample rate change detected, old %u, new %u.\n"), self->m_audioProperty.SampleRate, curHdr.CurrentSampleRate);
                self->m_asioTime.timeInfo.flags |= kSampleRateChanged;
                self->m_requireSampleRateChange = true;
                self->m_nextSampleRate = (ASIOSampleRate)curHdr.CurrentSampleRate;
                setAsioResetEvent = true;
                recHdr->DeviceStatus &= ~((ULONG)toInt(DeviceStatuses::SampleRateChanged));
            }
            if ((curHdr.DeviceStatus & toInt(DeviceStatuses::OverloadDetected)) != 0)
            {
                info_print_(_T("overload detected.\n"));
                self->m_isRequireReportDropout = true;
                setAsioResetEvent = true;
                recHdr->DeviceStatus &= ~((ULONG)toInt(DeviceStatuses::OverloadDetected));
            }
            if ((curHdr.DeviceStatus & toInt(DeviceStatuses::LatencyChanged)) != 0)
            {
                info_print_(_T("latency change detected.\n"));
                self->m_isRequireLatencyChange = true;
                setAsioResetEvent = true;
                recHdr->DeviceStatus &= ~((ULONG)toInt(DeviceStatuses::LatencyChanged));
            }
            if ((curHdr.DeviceStatus & toInt(DeviceStatuses::ResetRequired)) != 0 ||
                (curHdr.CurrentSampleRate != (ULONG)self->m_sampleRate))
            {
                info_print_(_T("reset request detected.\n"));
                self->m_isRequireAsioReset = true;
                setAsioResetEvent = true;
                // To prevent "Ableton Live" from hanging, callbacks will be processed even after a reset request.
                recHdr->DeviceStatus &= ~((ULONG)toInt(DeviceStatuses::ResetRequired));
            }
            if (self->m_outputReadyBlock)
            {
                if (WaitForSingleObject(&self->m_outputReadyBlockEvent, NOTIFICATION_TIMEOUT) == WAIT_TIMEOUT)
                {
                    done = true;
                    break;
                }
            }
            {
                LONG positionDiff = (LONG)(curHdr.RecBufferPosition - prevHdr.RecBufferPosition);
                LONG iteration = positionDiff / self->m_blockFrames;
                if (iteration > 3)
                {
                    SetEvent(self->m_asioResetEvent);
                    iteration %= 2;
                }
                while (iteration > 0)
                {
                    self->m_playReadyPosition = recHdr->RecBufferPosition;
                    recHdr->PlayReadyPosition = self->m_playReadyPosition;
#ifdef ASIO_THREAD_STATISTICS
                    if (performanceFreq.QuadPart != 0)
                    {
                        LARGE_INTEGER currentPC = {0};
                        QueryPerformanceCounter(&currentPC);

                        double measuredPeriod = (double)((currentPC.QuadPart - lastAsioCallbackPC) * 1000000) / (double)(performanceFreq.QuadPart);

                        if (lastAsioCallbackPC != 0 && statsPos < statsSize)
                        {
                            stats[statsPos].dueTime = measuredPeriod;
                            ++statsPos;
                        }

                        lastAsioCallbackPC = currentPC.QuadPart;
                    }
#endif
                    ULONG outputReady = 0;
                    LONG  readyBuffers = 0;
                    {
                        auto lockRecBuffer = self->m_recBufferCS.lock();
                        outputReady = InterlockedExchange((LONG *)&recHdr->OutputReady, toInt(UserThreadStatuses::BufferStart));
                        readyBuffers = InterlockedIncrement(&recHdr->ReadyBuffers);
                    }
                    if (self->m_initialSystemTime == 0)
                    {
                        self->m_initialSystemTime = timeGetTime();
                        self->m_initialKernelTime = recHdr->NotifySystemTime;
                    }
                    else
                    {
                        self->m_calculatedSystemTime = self->m_initialSystemTime +
                                                       (DWORD)((recHdr->NotifySystemTime - self->m_initialKernelTime) / 1000);
                    }
                    InterlockedIncrement(&recHdr->AsioProcessStart);
                    self->BufferSwitch();
                    InterlockedIncrement(&recHdr->AsioProcessComplete);
                    {
                        auto lockRecBuffer = self->m_recBufferCS.lock();
                        outputReady = InterlockedExchange(&recHdr->OutputReady, toInt(UserThreadStatuses::BufferStart) | toInt(UserThreadStatuses::BufferEnd) | toInt(UserThreadStatuses::OutputReady));
                        if (self->m_outputReadyBlock && (!(outputReady & toInt(UserThreadStatuses::OutputReady))) && (outputReady & toInt(UserThreadStatuses::BufferStart)))
                        {
                            InterlockedOr(&recHdr->OutputReady, toInt(UserThreadStatuses::OutputReadyDelay));
                            SetEvent(self->m_outputReadyEvent);
                        }
                    }
                    --iteration;
                    if (iteration == 0)
                    {
                        break;
                    }
                    error_print_(_T("out of sync, ASIO callback iteration %u, sleep %u(ms).\n"), iteration, self->m_blockFrames * 500 / (LONG)(self->m_sampleRate));
                    error_print_(_T("prev hdr PC%7u PB%7u RC%7u RB%7u\n"), prevHdr.PlayCurrentPosition, prevHdr.PlayBufferPosition, prevHdr.RecCurrentPosition, prevHdr.RecBufferPosition);
                    error_print_(_T("cur  hdr PC%7u PB%7u RC%7u RB%7u REB%7u\n"), curHdr.PlayCurrentPosition, curHdr.PlayBufferPosition, curHdr.RecCurrentPosition, curHdr.RecBufferPosition, readyBuffers);
                    status = WaitForSingleObject(self->m_stopEvent, 0);
                    if (status == WAIT_TIMEOUT)
                    {
                        continue;
                    }
                    else
                    {
                        done = true;
                        break;
                    }
                }
                prevHdr = curHdr;
                ++wakeup;
            }
            break;
        default:
            // If no notification is received from the kernel driver after waiting for a certain period of time,
            // it is assumed that an error has occurred, the thread is terminated, and the application is prompted to reset.
            if (timeout == NOTIFICATION_TIMEOUT)
            {
                error_print_(_T("wait timeout. requesting reset.\n"));
                error_print_(_T("cur  hdr PC%7u PB%7u RC%7u RB%7u\n"), recHdr->PlayCurrentPosition, recHdr->PlayBufferPosition, recHdr->RecCurrentPosition, recHdr->RecBufferPosition);
                self->m_isRequireAsioReset = true;
                setAsioResetEvent = true;
                timeout = self->m_blockFrames / 1000 / self->m_audioProperty.SampleRate;
            }
            self->BufferSwitch();
            break;
        }
        if (setAsioResetEvent)
        {
            SetEvent(self->m_asioResetEvent);
        }
    } while (!done);
    info_print_(_T("exiting worker thread...\n"));
#ifdef ASIO_THREAD_STATISTICS
    if (statsPos != 0)
    {
        double dueTimeTotal = 0;
        for (ULONG i = 0; i < statsPos; ++i)
        {
            dueTimeTotal += stats[i].dueTime;
        }
        double dueTimeAvg = dueTimeTotal / (double)statsPos;
        double dueTimeVar = 0;
        double dueTimeMax = 0;
        double dueTimeMin = 60000000;
        for (ULONG i = 0; i < statsPos; ++i)
        {
            dueTimeVar += pow(stats[i].dueTime - dueTimeAvg, 2);
            if (dueTimeMax < stats[i].dueTime)
            {
                dueTimeMax = stats[i].dueTime;
            }
            if (dueTimeMin > stats[i].dueTime)
            {
                dueTimeMin = stats[i].dueTime;
            }
        }
        dueTimeVar /= (double)(statsPos);
#if defined(INFO_PRINT_)
        double dueTimeStddev = sqrt(dueTimeVar);
#endif
        info_print_(_T("- ASIO Callback %5u(times), DueTime Calc %5d(us), Avg %5d(us), Stddev %5d(us), Max %5d(us), Min %5d(us)\n"), statsPos, (LONG)idealPeriod, (LONG)dueTimeAvg, (LONG)dueTimeStddev, (LONG)dueTimeMax, (LONG)dueTimeMin);
    }
    delete[] stats;
#endif
    InterlockedDecrement(&g_WorkerThread);
    return 0;
}
