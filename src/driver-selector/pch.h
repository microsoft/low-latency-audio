// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================

#pragma once

#include <windows.h>
#include <unknwn.h>
#include <restrictederrorinfo.h>
#include <hstring.h>
#include <shellapi.h>

#include <winmeta.h>
#include <TraceLoggingProvider.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

// Must come before the other WIL headers: it teaches WIL about C++/WinRT exceptions. Without it,
// CATCH_LOG or LOG_CAUGHT_EXCEPTION on a winrt::hresult_error fail-fasts the process instead of
// logging.
#include <wil/cppwinrt.h>
#include <wil/com.h>
#include <wil/resource.h>
#include <wil/result_macros.h>
#include <wil/tracelogging.h>

#undef GetCurrentTime

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.h>

#include <winrt/Microsoft.UI.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Interop.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Microsoft.UI.Xaml.Navigation.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Microsoft.Windows.ApplicationModel.Resources.h>

#include <microsoft.ui.xaml.window.h>

namespace foundation = ::winrt::Windows::Foundation;
namespace collections = ::winrt::Windows::Foundation::Collections;
namespace xaml = ::winrt::Microsoft::UI::Xaml;
namespace controls = ::winrt::Microsoft::UI::Xaml::Controls;
namespace media = ::winrt::Microsoft::UI::Xaml::Media;

#include "Telemetry.h"

// The row type is used from a data template but has no .xaml file of its own, so the generated
// XAML type information can only reach it through here.
#include "AudioDeviceItem.h"
