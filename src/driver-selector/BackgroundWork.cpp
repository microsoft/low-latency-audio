// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

#include "pch.h"
#include "BackgroundWork.h"

namespace driverselector
{
    namespace
    {
        // There is no resume_foreground overload for the Microsoft.UI dispatcher in this
        // projection, so the continuation is marshaled back with TryEnqueue. A queue that refuses
        // the work resumes in place, which is what a closing window looks like.
        struct ResumeOnDispatcher
        {
            winrt::Microsoft::UI::Dispatching::DispatcherQueue Queue{ nullptr };

            bool await_ready() const noexcept
            {
                return Queue == nullptr;
            }

            bool await_suspend(_In_ std::coroutine_handle<> handle) const noexcept
            {
                return Queue.TryEnqueue([handle]() { handle(); });
            }

            void await_resume() const noexcept
            {
            }
        };
    }

    _Use_decl_annotations_
    winrt::Windows::Foundation::IAsyncAction RunOnBackgroundAsync(std::function<void()> work) noexcept
    {
        // Captured before the switch: awaiting this action is not enough on its own to get the
        // caller back onto the UI thread.
        ResumeOnDispatcher const resumeOnCaller{
            winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread() };

        co_await winrt::resume_background();

        try
        {
            if (work)
            {
                work();
            }
        }
        DRIVER_SELECTOR_CATCH_AND_LOG(L"Background work failed.")

        co_await resumeOnCaller;
    }
}
