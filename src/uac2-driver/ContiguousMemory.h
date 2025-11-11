// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

/*++

Module Name:

    ContiguousMemory.h

Abstract:

    Defines a class that manages ContiguousMemory.

Environment:

    Kernel-mode Driver Framework

--*/

#ifndef _CONTIGUOUSMEMORY_H_
#define _CONTIGUOUSMEMORY_H_

class ContiguousMemory
{
  public:
    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ContiguousMemory();
    virtual __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ~ContiguousMemory();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    Allocate(
        _In_ USBAudioConfiguration * usbAudioConfiguration,
        _In_ ULONG                   maxBurstOverride,
        _In_ ULONG                   maxClassicFramesPerIrp,
        _In_ ULONG                   framesPerMs
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    Free();

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    NTSTATUS
    Clear();

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    bool
    IsValid(
        _In_ LONG         index,
        _In_ IsoDirection direction
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    NONPAGED_CODE_SEG
    PUCHAR
    GetDataBuffer(
        _In_ LONG         index,
        _In_ IsoDirection direction
    );

    __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ULONG
    GetSize(
        _In_ IsoDirection direction
    );

    __drv_maxIRQL(DISPATCH_LEVEL)
    NONPAGED_CODE_SEG
    ULONG
    GetTotalSize(
        _In_ IsoDirection direction
    );

    static __drv_maxIRQL(DISPATCH_LEVEL)
    PAGED_CODE_SEG
    ContiguousMemory * Create();

  private:
    static __drv_maxIRQL(PASSIVE_LEVEL)
    PAGED_CODE_SEG
    ULONG
    GetMaxPacketSize(
        _In_ USBAudioConfiguration * usbAudioConfiguration,
        _In_ IsoDirection            direction
    );

    ULONG  m_contiguousMemorySize[toInt(IsoDirection::NumOfIsoDirection)]{0};
    PUCHAR m_contiguousMemory[toInt(IsoDirection::NumOfIsoDirection)][UAC_MAX_IRP_NUMBER]{};
};

#endif
