// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

/*++

Module Name:

    Driver.h

Abstract:

    This file contains the driver definitions.

Environment:

    Kernel-mode Driver Framework

--*/

#ifndef _DRIVER_H_
#define _DRIVER_H_

#include <ntddk.h>
#include <wdf.h>
#include <acx.h>

EXTERN_C_START
// If you do not include usbdlib.h with extern "C", a link error will occur for USBD_ParseConfigurationDescriptorEx etc.
#include "usbdi.h"   // for USBD_CONFIGURATION_HANDLE, PUSBD_INTERFACE_INFORMATION etc
#include "usbdlib.h" // for USBD_CONFIGURATION_HANDLE, PUSBD_INTERFACE_INFORMATION etc

#include <wdf.h>
#include <acx.h>
#include <wdfusb.h>
#include <initguid.h>

DRIVER_INITIALIZE DriverEntry;

EXTERN_C_END

#include "Device.h"
#include "Trace.h"
#include "Private.h"

//
// WDFDRIVER Events
//

#endif // #ifndef _DRIVER_H_
