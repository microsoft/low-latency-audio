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

Trace.h

--*/

#pragma once

#include <WppRecorder.h>
#include <evntrace.h> // For TRACE_LEVEL definitions

#define WPP_TOTAL_BUFFER_SIZE    (PAGE_SIZE)
#define WPP_ERROR_PARTITION_SIZE (WPP_TOTAL_BUFFER_SIZE / 4)

// {8AE4632D-511F-49C4-88F5-3A178D15EED7}
#define WPP_CONTROL_GUIDS                                                                                                                                     \
    WPP_DEFINE_CONTROL_GUID(USBAudioAcxDriverTraceGuid, (8AE4632D, 511F, 49C4, 88F5, 3A178D15EED7), WPP_DEFINE_BIT(FLAG_DEVICE_ALL) /* bit  0 = 0x00000001 */ \
                            WPP_DEFINE_BIT(FLAG_FUNCTION)                                                                           /* bit  1 = 0x00000002 */ \
                            WPP_DEFINE_BIT(FLAG_INFO)                                                                               /* bit  2 = 0x00000004 */ \
                            WPP_DEFINE_BIT(FLAG_PNP)                                                                                /* bit  3 = 0x00000008 */ \
                            WPP_DEFINE_BIT(FLAG_POWER)                                                                              /* bit  4 = 0x00000010 */ \
                            WPP_DEFINE_BIT(FLAG_STREAM)                                                                             /* bit  5 = 0x00000020 */ \
                            WPP_DEFINE_BIT(FLAG_INIT)                                                                               /* bit  6 = 0x00000040 */ \
                            WPP_DEFINE_BIT(FLAG_DDI)                                                                                /* bit  7 = 0x00000080 */ \
                            WPP_DEFINE_BIT(FLAG_GENERIC)                                                                            /* bit  8 = 0x00000100 */ \
                            WPP_DEFINE_BIT(TRACE_DRIVER)                                                                            /* bit  9 = 0x00000200 */ \
                            WPP_DEFINE_BIT(TRACE_DEVICE)                                                                            /* bit 10 = 0x00000400 */ \
                            WPP_DEFINE_BIT(TRACE_QUEUE)                                                                             /* bit 11 = 0x00000800 */ \
                            WPP_DEFINE_BIT(TRACE_CIRCUIT)                                                                           /* bit 12 = 0x00001000 */ \
                            WPP_DEFINE_BIT(TRACE_ASIO)                                                                              /* bit 13 = 0x00002000 */ \
                            WPP_DEFINE_BIT(TRACE_CTRLREQUEST)                                                                       /* bit 14 = 0x00004000 */ \
                            WPP_DEFINE_BIT(TRACE_MULTICLIENT)                                                                       /* bit 15 = 0x00008000 */ \
                            WPP_DEFINE_BIT(TRACE_DESCRIPTOR)                                                                        /* bit 16 = 0x00010000 */ \
    )

#include "trace_macros.h"
