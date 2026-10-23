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
    _In_reads_opt_(numChannels) const UAC_CHANNEL_INFO * channelInfo,
    _In_ ULONG                                         numChannels,
    _In_ LONG                                          channel,
    _In_ bool                                          isInput,
    _Out_writes_z_(UAC_MAX_CHANNEL_NAME_LENGTH) char (&name)[UAC_MAX_CHANNEL_NAME_LENGTH]
);
