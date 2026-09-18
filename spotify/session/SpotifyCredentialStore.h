#pragma once

#include "spotify/auth/SpotifyAuth.h"
#include <SupportDefs.h>

class SpotifyApi;

struct SpotifyCredentialResult {
	bool stored = false;
	int expiresIn = 0;
	status_t storageStatus = B_OK;
	std::string error = "";
	std::string description = "";
};

// Called by the App looper. A failed validation/write never updates the client.
SpotifyCredentialResult StoreSpotifyCredentials(SpotifyApi& api, const TokenResult& token);
// Clears in-memory API state even if persistence fails; failure remains visible.
status_t ClearSpotifyCredentials(SpotifyApi& api);
status_t ClearStoredSpotifyCredentials();
