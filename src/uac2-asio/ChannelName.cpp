// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================
// ASIO is a trademark and software of Steinberg Media Technologies GmbH

#include "ChannelName.h"

#include <cstdio>
#include <cstring>

namespace
{
void CopyDescriptorName(
    const UAC_CHANNEL_INFO & channelInfo,
    char (&name)[UAC_MAX_CHANNEL_NAME_LENGTH]
)
{
    _snprintf_s(name, _TRUNCATE, "%S", channelInfo.Name);
}

bool HasUsableChannelNames(
    const UAC_CHANNEL_INFO * channelInfo,
    ULONG                   numChannels,
    bool                    isInput
)
{
    if (channelInfo == nullptr)
    {
        return false;
    }

    bool foundChannel = false;
    for (ULONG channelIndex = 0; channelIndex < numChannels; ++channelIndex)
    {
        const auto & channel = channelInfo[channelIndex];
        if ((channel.IsInput != FALSE) != isInput)
        {
            continue;
        }

        foundChannel = true;
        char channelName[UAC_MAX_CHANNEL_NAME_LENGTH]{};
        CopyDescriptorName(channel, channelName);
        if (channelName[0] == '\0')
        {
            return false;
        }

        for (ULONG candidateIndex = channelIndex + 1; candidateIndex < numChannels; ++candidateIndex)
        {
            const auto & candidate = channelInfo[candidateIndex];
            if ((candidate.IsInput != FALSE) != isInput)
            {
                continue;
            }

            char candidateName[UAC_MAX_CHANNEL_NAME_LENGTH]{};
            CopyDescriptorName(candidate, candidateName);
            if (std::strcmp(channelName, candidateName) == 0)
            {
                return false;
            }
        }
    }

    return foundChannel;
}
} // namespace

void CopyAsioChannelName(
    const UAC_CHANNEL_INFO * channelInfo,
    ULONG                    numChannels,
    LONG                     channel,
    bool                     isInput,
    char (&name)[UAC_MAX_CHANNEL_NAME_LENGTH]
)
{
    ULONG channelInfoIndex = 0;
    if (channelInfo != nullptr)
    {
        for (; channelInfoIndex < numChannels; ++channelInfoIndex)
        {
            const auto & candidate = channelInfo[channelInfoIndex];
            if (candidate.Index == channel && (candidate.IsInput != FALSE) == isInput)
            {
                break;
            }
        }
    }

    if (channelInfo != nullptr &&
        channelInfoIndex < numChannels &&
        HasUsableChannelNames(channelInfo, numChannels, isInput))
    {
        CopyDescriptorName(channelInfo[channelInfoIndex], name);
        return;
    }

    _snprintf_s(name, _TRUNCATE, "%s %ld", isInput ? "Input" : "Output", channel + 1);
}
