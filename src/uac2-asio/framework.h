// Copyright (c) Yamaha Corporation.
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

/*++

Module Name:

    framework.h

Abstract:

    TBD

Environment:

    TBD

--*/

#pragma once

#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif


#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// Windows Header Files
#include <windows.h>
#include <tchar.h>
#include <psapi.h>

// #define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS
// #include <atlbase.h>
// #include <atlstr.h>
// #include <intrin.h>
#include <avrt.h>
