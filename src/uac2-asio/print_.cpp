// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

/*++

Module Name:

    print_.cpp

Abstract:

    TBD

Environment:

    TBD

--*/

#include "framework.h"
#include "print_.h"

#if defined(_MANAGED)
#pragma unmanaged
#endif //_MANAGED

extern "C" void message_print_(const TCHAR * pTextForPrint, ...)
{
    va_list args;
    va_start(args, pTextForPrint);

#if defined(_CONSOLE)
    vwprintf_s(pTextForPrint, args);
#else
    static TCHAR pMessage[MAX_DEBUG_MESSAGE_LENGTH];

    _vstprintf_s(pMessage, MAX_DEBUG_MESSAGE_LENGTH, pTextForPrint, args);
    OutputDebugString(pMessage);
#endif

    va_end(args);
}

extern "C" void get_formatted_error_message_(TCHAR * buffer, ULONG length)
{
    TCHAR * message;

    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        GetLastError(),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&message,
        0,
        nullptr
    );

    error_print_("obtained error message: %s\n", message);
    _tcscpy_s(buffer, length, message);

    LocalFree(message);
}
