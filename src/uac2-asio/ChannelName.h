// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================
// ASIO is a trademark and software of Steinberg Media Technologies GmbH

#pragma once

#include <windows.h>

#include "UAC_User.h"

void CopyAsioChannelName(
    const UAC_CHANNEL_INFO * channelInfo,
    ULONG                    numChannels,
    LONG                     channel,
    bool                     isInput,
    char (&name)[UAC_MAX_CHANNEL_NAME_LENGTH]
);
