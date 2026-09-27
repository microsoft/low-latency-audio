// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================
// ASIO is a trademark and software of Steinberg Media Technologies GmbH

#include <windows.h>

#include <cstddef>
#include <cstring>
#include <iostream>
#include <iterator>
#include <string>

#include "ChannelName.h"

namespace
{
template <size_t ChannelCount>
struct ChannelInfoFixture
{
    UAC_CHANNEL_INFO Channel[ChannelCount]{};
};

int g_failures = 0;

void SetChannel(UAC_CHANNEL_INFO & channel, LONG index, BOOL isInput, const wchar_t * name)
{
    channel.Index = index;
    channel.IsInput = isInput;
    wcscpy_s(channel.Name, name);
}

template <size_t ChannelCount>
void ExpectName(
    const char * expected,
    const UAC_CHANNEL_INFO (&channels)[ChannelCount],
    LONG channel,
    BOOL isInput,
    const char * testName
)
{
    char actual[UAC_MAX_CHANNEL_NAME_LENGTH]{};
    CopyAsioChannelName(channels, static_cast<ULONG>(ChannelCount), channel, isInput, actual);

    if (std::strcmp(actual, expected) != 0)
    {
        std::cerr << "FAIL: " << testName << ": expected '" << expected << "', got '" << actual << "'\n";
        ++g_failures;
    }
}

void TestUniqueDescriptorNamesArePreserved()
{
    ChannelInfoFixture<2> fixture;
    SetChannel(fixture.Channel[0], 0, TRUE, L"Mic 1");
    SetChannel(fixture.Channel[1], 1, TRUE, L"Mic 2");

    ExpectName("Mic 1", fixture.Channel, 0, TRUE, "unique input name");
    ExpectName("Mic 2", fixture.Channel, 1, TRUE, "second unique input name");
}

void TestDuplicateInputNamesUseNumberedFallbacks()
{
    ChannelInfoFixture<12> fixture;
    for (LONG channel = 0; channel < 12; ++channel)
    {
        SetChannel(fixture.Channel[channel], channel, TRUE, L"Model 12");
    }

    for (LONG channel = 0; channel < 12; ++channel)
    {
        const std::string expected = "Input " + std::to_string(channel + 1);
        ExpectName(expected.c_str(), fixture.Channel, channel, TRUE, "duplicate input name");
    }
}

void TestDuplicateOutputNamesUseNumberedFallbacks()
{
    ChannelInfoFixture<10> fixture;
    for (LONG channel = 0; channel < 10; ++channel)
    {
        SetChannel(fixture.Channel[channel], channel, FALSE, L"Model 12");
    }

    for (LONG channel = 0; channel < 10; ++channel)
    {
        const std::string expected = "Output " + std::to_string(channel + 1);
        ExpectName(expected.c_str(), fixture.Channel, channel, FALSE, "duplicate output name");
    }
}

void TestEmptyNamesUseNumberedFallbacks()
{
    ChannelInfoFixture<2> fixture;
    SetChannel(fixture.Channel[0], 0, TRUE, L"");
    SetChannel(fixture.Channel[1], 0, FALSE, L"");

    ExpectName("Input 1", fixture.Channel, 0, TRUE, "empty input name");
    ExpectName("Output 1", fixture.Channel, 0, FALSE, "empty output name");
}

void TestIncompleteDirectionUsesConsistentNumberedFallbacks()
{
    ChannelInfoFixture<2> fixture;
    SetChannel(fixture.Channel[0], 0, TRUE, L"Input 2");
    SetChannel(fixture.Channel[1], 1, TRUE, L"");

    ExpectName("Input 1", fixture.Channel, 0, TRUE, "incomplete input name set");
    ExpectName("Input 2", fixture.Channel, 1, TRUE, "empty name in input set");
}

void TestDuplicateDetectionIsScopedByDirection()
{
    ChannelInfoFixture<2> fixture;
    SetChannel(fixture.Channel[0], 0, TRUE, L"Channel 1");
    SetChannel(fixture.Channel[1], 0, FALSE, L"Channel 1");

    ExpectName("Channel 1", fixture.Channel, 0, TRUE, "input and output names are independent");
    ExpectName("Channel 1", fixture.Channel, 0, FALSE, "output and input names are independent");
}

void TestMaximumLengthNameIsNullTerminated()
{
    ChannelInfoFixture<1> fixture;
    constexpr wchar_t maximumLengthName[] = L"1234567890123456789012345678901";
    static_assert(std::size(maximumLengthName) == UAC_MAX_CHANNEL_NAME_LENGTH);
    SetChannel(fixture.Channel[0], 0, TRUE, maximumLengthName);

    ExpectName("1234567890123456789012345678901", fixture.Channel, 0, TRUE, "maximum length input name");
}
} // namespace

int main()
{
    TestUniqueDescriptorNamesArePreserved();
    TestDuplicateInputNamesUseNumberedFallbacks();
    TestDuplicateOutputNamesUseNumberedFallbacks();
    TestEmptyNamesUseNumberedFallbacks();
    TestIncompleteDirectionUsesConsistentNumberedFallbacks();
    TestDuplicateDetectionIsScopedByDirection();
    TestMaximumLengthNameIsNullTerminated();

    if (g_failures == 0)
    {
        std::cout << "All channel name tests passed.\n";
    }

    return g_failures == 0 ? 0 : 1;
}
