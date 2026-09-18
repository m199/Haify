#pragma once

#include <cstdint>
#include <functional>
#include <string>

class ProfileApi;
class SpotifyApi;
class SpotifyCapabilities;

struct SpotifyAccountResult {
	int64_t requestId = 0;
	bool ok = false;
	bool valid = false;
	int32_t status = -1;
	int32_t retryAfter = -1;
	std::string id = "";
	std::string providerId = "";
};

// App-looper-owned request gate, independent from token refresh generations.
class SpotifyAccountRequestState {
public:
	int64_t Begin() { fPending = ++fSequence; return fPending; }
	void Cancel() { fPending = 0; }
	bool Accept(int64_t requestId)
	{
		if (requestId <= 0 || requestId != fPending) return false;
		fPending = 0;
		return true;
	}
private:
	int64_t fSequence = 0;
	int64_t fPending = 0;
};

struct SpotifyAccountUpdate {
	bool applied = false;
	bool changed = false;
	int32_t storageStatus = 0;
};

struct SpotifyAccountPlaylistsResult {
	std::string account = "";
	bool ok = false;
	int32_t status = -1;
	int32_t retryAfter = -1;
};

void RequestSpotifyAccount(ProfileApi& api, int64_t requestId,
	std::function<void(const SpotifyAccountResult&)> complete);
SpotifyAccountUpdate ApplySpotifyAccount(SpotifyApi& api, SpotifyCapabilities& capabilities,
	const SpotifyAccountResult& result);
void RefreshSpotifyAccountPlaylists(SpotifyApi& api,
	std::function<void(const SpotifyAccountPlaylistsResult&)> complete);
