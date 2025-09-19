// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

/*++

Module Name:

    print_.h

Abstract:

    TBD

Environment:

    TBD

--*/

#pragma once

#include <stdio.h>

#ifdef UNICODE
#define WIDEN2(x)     L##x
#define WIDEN(x)      WIDEN2(x)
#define __TFUNCTION__ WIDEN(__FUNCTION__)
#else
#define __TFUNCTION__ __FUNCTION__
#endif

// #define VERBOSE_PRINT_
#ifdef _DEBUG
#define INFO_PRINT_
#define ERROR_PRINT_
#endif

#define MAX_DEBUG_MESSAGE_LENGTH 256

#if defined(__cplusplus)
extern "C"
{
#endif //_cplusplus

    void message_print_(const TCHAR * pTextForPrint, ...);
    void get_formatted_error_message_(TCHAR * buffer, ULONG length);

#if defined(__cplusplus)
}
#endif //_cplusplus

#if defined(VERBOSE_PRINT_)
#define verbose_print_(textForPrint, ...) message_print_(textForPrint, __VA_ARGS__)
#else
#define verbose_print_(textForPrint, ...)
#endif

#if defined(INFO_PRINT_)
#define info_print_(textForPrint, ...) message_print_(textForPrint, __VA_ARGS__)
#else
#define info_print_(textForPrint, ...)
#endif

#if defined(ERROR_PRINT_)
#define error_print_(textForPrint, ...) message_print_(__TFUNCTION__ _TEXT(":") textForPrint, __VA_ARGS__)
#else
#define error_print_(textForPrint, ...)
#endif
