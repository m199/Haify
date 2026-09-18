#pragma once

#include <cstdint>
#include <functional>
#include <string>

class ContentApi;

struct SpotifyOpenRequest {
	std::string uri = "";
	std::string title = "";
	std::string coverUrl = "";
	bool skipAudiobookResolution = false;
};

enum class SpotifyNavigationAction {
	None, Artist, Episode, Audiobook, PlayTrack, ResolveShow, Collection,
	AudiobooksUnavailable, Unsupported
};

SpotifyNavigationAction SelectSpotifyNavigation(const SpotifyOpenRequest& request,
	bool audiobooksEnabled, bool contentApiAvailable);

enum class SpotifyShowResolutionKind {
	ApiAudiobook, SynthesizedAudiobookUri,
	LegacyShowAfterApiFailure, LegacyShowAfterMalformedResponse
};

struct SpotifyShowResolution {
	SpotifyOpenRequest request;
	SpotifyShowResolutionKind kind = SpotifyShowResolutionKind::LegacyShowAfterApiFailure;
	std::string resolvedUri = "";
	int32_t status = -1;
	int32_t retryAfter = -1;
};

bool ValidSpotifyShowResolution(const SpotifyShowResolution& result);
SpotifyOpenRequest ResolvedSpotifyOpenRequest(const SpotifyShowResolution& result);

// One GET, no retries or UI access. Callback owns request/result copies; the
// caller keeps ContentApi alive. Invalid requests return false without callback.
// Legacy fallbacks are explicit compatibility exceptions, not confirmed shows.
bool ResolveSpotifyShow(ContentApi& api, const SpotifyOpenRequest& request,
	std::function<void(const SpotifyShowResolution&)> complete);
