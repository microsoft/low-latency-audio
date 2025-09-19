// Copyright (c) Microsoft Corporation.
// Copyright (c) Yamaha Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================


/*++

Module Name:

    Common.h

Abstract:

    Header file of common macros used within kernel.

Environment:

    Kernel mode

--*/

#ifndef _COMMON_H_
#define _COMMON_H_

//=============================================================================
// Macros
//=============================================================================

#ifndef UNREFERENCED_VAR
#define UNREFERENCED_VAR(status) \
    status = status
#endif

//-------------------------------------------------------------------------
// Description:
//
// jump to the given label.
//
// Parameters:
//
//      label - [in] label to jump if condition is met
//
#ifndef JUMP
#define JUMP(label) \
    goto label;
#endif

//-------------------------------------------------------------------------
// Description:
//
// If the condition evaluates to TRUE, jump to the given label.
//
// Parameters:
//
//      condition - [in] Code that fits in if statement
//      label - [in] label to jump if condition is met
//
#ifndef IF_TRUE_JUMP
#define IF_TRUE_JUMP(condition, label) \
    if (condition)                     \
    {                                  \
        goto label;                    \
    }
#endif

//-------------------------------------------------------------------------
// Description:
//
// If the condition evaluates to TRUE, perform the given statement
// then jump to the given label.
//
// Parameters:
//
//      condition - [in] Code that fits in if statement
//      action - [in] action to perform in body of if statement
//      label - [in] label to jump if condition is met
//
#ifndef IF_TRUE_ACTION_JUMP
#define IF_TRUE_ACTION_JUMP(condition, action, label) \
    if (condition)                                    \
    {                                                 \
        action;                                       \
        goto label;                                   \
    }
#endif

//-------------------------------------------------------------------------
// Description:
//
// If the ntStatus is not NT_SUCCESS, perform the given statement then jump to
// the given label.
//
// Parameters:
//
//      ntStatus - [in] Value to check
//      action - [in] action to perform in body of if statement
//      label - [in] label to jump if condition is met
//
#ifndef IF_FAILED_ACTION_JUMP
#define IF_FAILED_ACTION_JUMP(ntStatus, action, label) \
    if (!NT_SUCCESS(ntStatus))                         \
    {                                                  \
        action;                                        \
        goto label;                                    \
    }
#endif

//-------------------------------------------------------------------------
// Description:
//
// If the ntStatus passed is not NT_SUCCESS, jump to the given label.
//
// Parameters:
//
//      ntStatus - [in] Value to check
//      label - [in] label to jump if condition is met
//
#ifndef IF_FAILED_JUMP
#define IF_FAILED_JUMP(ntStatus, label) \
    if (!NT_SUCCESS(ntStatus))          \
    {                                   \
        goto label;                     \
    }
#endif

//-------------------------------------------------------------------------
// Description:
//
// If the condition evaluates to TRUE, perform the given statement
// then return with status
//
// Parameters:
//
//      condition - [in] Code that fits in if statement
//      action - [in] action to perform in body of if statement
//      status - [in] return code
//
#ifndef RETURN_NTSTATUS_IF_TRUE_ACTION
#define RETURN_NTSTATUS_IF_TRUE_ACTION(condition, action, status) \
    if (condition)                                                \
    {                                                             \
        action;                                                   \
        return status;                                            \
    }
#endif

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p)     \
    {                       \
        if (p)              \
        {                   \
            (p)->Release(); \
            (p) = nullptr;  \
        }                   \
    }
#endif

#ifndef SAFE_DELETE_PTR_WITH_TAG
#define SAFE_DELETE_PTR_WITH_TAG(ptr, tag) \
    if (ptr)                               \
    {                                      \
        ExFreePoolWithTag((ptr), tag);     \
        (ptr) = nullptr;                   \
    }
#endif

class WaitLocker
{
  public:
    FORCEINLINE
    WaitLocker(_In_ WDFWAITLOCK & waitLock, _In_opt_ PLONGLONG timeout)
        : m_WaitLock(waitLock)
    {
        WdfWaitLockAcquire(m_WaitLock, timeout);
    }

    FORCEINLINE
    virtual ~WaitLocker()
    {
        WdfWaitLockRelease(m_WaitLock);
    }

  private:
    WDFWAITLOCK & m_WaitLock;
};

#endif
