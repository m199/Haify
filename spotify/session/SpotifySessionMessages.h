#pragma once

#include "SpotifyAccountSession.h"
#include "spotify/auth/SpotifyAuth.h"
#include <Message.h>

class SpotifyCapabilities;

BMessage MakeSpotifyAccountResultMessage(const SpotifyAccountResult& result);
bool ReadSpotifyAccountResultMessage(const BMessage& message, SpotifyAccountResult& result);
BMessage MakeSpotifyAccountPlaylistsMessage(const SpotifyAccountPlaylistsResult& result);
BMessage MakeSpotifyCapabilityProbeResult();
BMessage MakeSpotifyCapabilitiesSnapshot(const SpotifyCapabilities& capabilities);
void AddSpotifyTokenResult(BMessage& message, const TokenResult& result);
bool ReadSpotifyTokenResult(const BMessage& message, TokenResult& result);
