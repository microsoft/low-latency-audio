// Copyright (c) Microsoft Corporation.
// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

/*++

Module Name:

    Driver.cpp

Abstract:

    This file contains the driver entry points and callbacks.

Environment:

    Kernel-mode Driver Framework

--*/

#include "Driver.h"
#include "Public.h"
#include "Device.h"
#ifndef __INTELLISENSE__
#include "Driver.tmh"
#endif

//
//  Global variables
//

//
//  Local function prototypes
//
EXTERN_C_START
EVT_WDF_OBJECT_CONTEXT_CLEANUP USBAudioAcxDriverEvtDriverContextCleanup; // IRQL <= DISPATCH_LEVEL, Conditionally IRQL = PASSIVE_LEVEL
EVT_WDF_DRIVER_UNLOAD          USBAudioAcxDriverEvtDriverUnload;         // PASSIVE_LEVEL
EXTERN_C_END

//
//  Assign text sections for each routine.
//

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, USBAudioAcxDriverEvtDriverContextCleanup)
#pragma alloc_text(PAGE, USBAudioAcxDriverEvtDriverUnload)
#endif

INIT_CODE_SEG
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  driverObject,
    _In_ PUNICODE_STRING registryPath
)
/*++

Routine Description:
    DriverEntry initializes the driver and is the first routine called by the
    system after the driver is loaded. DriverEntry specifies the other entry
    points in the function driver, such as EvtDevice and DriverUnload.

Parameters Description:

    driverObject - represents the instance of the function driver that is loaded
    into memory. DriverEntry must initialize members of driverObject before it
    returns to the caller. driverObject is allocated by the system before the
    driver is loaded, and it is released by the system after the system unloads
    the function driver from memory.

    registryPath - represents the driver specific path in the Registry.
    The function driver can use the path to store driver related data between
    reboots. The path does not store hardware instance specific data.

Return Value:

    STATUS_SUCCESS if successful,
    STATUS_UNSUCCESSFUL otherwise.

--*/
{
    WDF_DRIVER_CONFIG     config;
    ACX_DRIVER_CONFIG     acxConfig;
    WDFDRIVER             driver;
    NTSTATUS              status = STATUS_SUCCESS;
    WDF_OBJECT_ATTRIBUTES attributes;

    PAGED_CODE();

    //
    // Initialize WPP Tracing
    //
    WPP_INIT_TRACING(driverObject, registryPath);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    auto exit = wil::scope_exit([&]() {
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, "%!FUNC! failed %!STATUS!", status);
            WPP_CLEANUP(driverObject);

            if (g_RegistryPath.Buffer != nullptr)
            {
                ExFreePool(g_RegistryPath.Buffer);
                RtlZeroMemory(&g_RegistryPath, sizeof(g_RegistryPath));
            }
        }
    });

    RETURN_NTSTATUS_IF_FAILED(CopyRegistrySettingsPath(registryPath));

    //
    // Register a cleanup callback so that we can call WPP_CLEANUP when
    // the framework driver object is deleted during driver unload.
    //
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = USBAudioAcxDriverEvtDriverContextCleanup;

    WDF_DRIVER_CONFIG_INIT(&config, USBAudioAcxDriverEvtDeviceAdd);
    config.EvtDriverUnload = USBAudioAcxDriverEvtDriverUnload;

    //
    // Create a framework driver object to represent our driver.
    //
    RETURN_NTSTATUS_IF_FAILED(WdfDriverCreate(driverObject, registryPath, &attributes, &config, &driver));

    //
    // Initializing the ACX driver configuration struct which contains size and flags
    // elements.
    //
    ACX_DRIVER_CONFIG_INIT(&acxConfig);

    //
    // The driver calls this DDI in its DriverEntry callback after creating the WDF driver
    // object. ACX uses this call to apply any post driver settings.
    //
    RETURN_NTSTATUS_IF_FAILED(AcxDriverInitialize(driver, &acxConfig));

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");

    return status;
}

PAGED_CODE_SEG
VOID USBAudioAcxDriverEvtDriverContextCleanup(
    _In_ WDFOBJECT driverObject
)
/*++
Routine Description:

    Free all the resources allocated in DriverEntry.

Arguments:

    driverObject - handle to a WDF Driver object.

Return Value:

    VOID.

--*/
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    //
    // Stop WPP Tracing
    //
    WPP_CLEANUP(WdfDriverWdmGetDriverObject((WDFDRIVER)driverObject));

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
}

PAGED_CODE_SEG
_Use_decl_annotations_
VOID USBAudioAcxDriverEvtDriverUnload(
    _In_ WDFDRIVER driver
)
{
    PAGED_CODE();

    if (!driver)
    {
        ASSERT(FALSE);
        return;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    WPP_CLEANUP(WdfDriverWdmGetDriverObject(driver));

    if (g_RegistryPath.Buffer != nullptr)
    {
        ExFreePool(g_RegistryPath.Buffer);
        RtlZeroMemory(&g_RegistryPath, sizeof(g_RegistryPath));
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");

    return;
}
