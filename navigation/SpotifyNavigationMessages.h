#pragma once

#include "SpotifyNavigation.h"
#include <Message.h>

// Existing 'open' command and private asynchronous result. Readers reject
// malformed singleton fields without changing output. See message-contracts.md.
BMessage MakeSpotifyOpenMessage(const SpotifyOpenRequest& request);
bool ReadSpotifyOpenMessage(const BMessage& message, SpotifyOpenRequest& request);
BMessage MakeSpotifyShowResolutionMessage(const SpotifyShowResolution& result);
bool ReadSpotifyShowResolutionMessage(const BMessage& message, SpotifyShowResolution& result);
