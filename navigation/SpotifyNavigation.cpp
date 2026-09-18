#include "SpotifyNavigation.h"
#include "spotify/SpotifyUri.h"
#include "spotify/api/ContentApi.h"

#include <limits>

namespace {
bool
CanResolveShow(const SpotifyOpenRequest& request)
{
	return SpotifyItemKindForUri(request.uri) == kSpotifyItemShow
		&& !SpotifyItemIdForUri(request.uri).empty()
		&& !request.skipAudiobookResolution;
}

int32_t
ResponseInteger(const nlohmann::json& data, const char* field)
{
	if (!data.is_object())
		return -1;
	auto value = data.find(field);
	if (value == data.end() || !value->is_number_integer())
		return -1;
	if (value->is_number_unsigned()) {
		auto number = value->get<uint64_t>();
		return number <= uint64_t(std::numeric_limits<int32_t>::max())
			? static_cast<int32_t>(number) : -1;
	}
	auto number = value->get<int64_t>();
	return number >= 0 && number <= std::numeric_limits<int32_t>::max()
		? static_cast<int32_t>(number) : -1;
}

SpotifyShowResolution
MapResolution(const SpotifyOpenRequest& request, bool ok, const nlohmann::json& data)
{
	SpotifyShowResolution result;
	result.request = request;
	result.resolvedUri = request.uri;
	result.status = ResponseInteger(data, "status");
	result.retryAfter = ResponseInteger(data, "retry_after");
	if (!ok)
		return result;
	if (!data.is_object()) {
		result.kind = SpotifyShowResolutionKind::LegacyShowAfterMalformedResponse;
		return result;
	}
	std::string uri;
	auto value = data.find("uri");
	if (value != data.end() && value->is_string())
		uri = value->get<std::string>();
	if (SpotifyItemKindForUri(uri) == kSpotifyItemAudiobook) {
		result.kind = SpotifyShowResolutionKind::ApiAudiobook;
		result.resolvedUri = uri;
	} else {
		// Preserve the old successful-object rule, including an empty object.
		// This URI is a compatibility substitute, not supplied by Spotify.
		result.kind = SpotifyShowResolutionKind::SynthesizedAudiobookUri;
		result.resolvedUri = SpotifyUriForItemKind(kSpotifyItemAudiobook,
			SpotifyItemIdForUri(request.uri));
	}
	return result;
}
}

SpotifyNavigationAction
SelectSpotifyNavigation(const SpotifyOpenRequest& request,
	bool audiobooksEnabled, bool contentApiAvailable)
{
	if (request.uri.empty())
		return SpotifyNavigationAction::None;
	switch (SpotifyItemKindForUri(request.uri)) {
		case kSpotifyItemArtist: return SpotifyNavigationAction::Artist;
		case kSpotifyItemEpisode: return SpotifyNavigationAction::Episode;
		case kSpotifyItemAudiobook:
			return audiobooksEnabled ? SpotifyNavigationAction::Audiobook
				: SpotifyNavigationAction::AudiobooksUnavailable;
		case kSpotifyItemTrack: return SpotifyNavigationAction::PlayTrack;
		case kSpotifyItemShow:
			return audiobooksEnabled && contentApiAvailable && CanResolveShow(request)
				? SpotifyNavigationAction::ResolveShow : SpotifyNavigationAction::Collection;
		case kSpotifyItemAlbum:
		case kSpotifyItemPlaylist:
			return SpotifyNavigationAction::Collection;
		default:
			return request.uri == "spotify:collection" ? SpotifyNavigationAction::Collection
				: SpotifyNavigationAction::Unsupported;
	}
}

bool
ValidSpotifyShowResolution(const SpotifyShowResolution& result)
{
	if (!CanResolveShow(result.request) || result.status < -1 || result.retryAfter < -1)
		return false;
	switch (result.kind) {
		case SpotifyShowResolutionKind::ApiAudiobook:
			return SpotifyItemKindForUri(result.resolvedUri) == kSpotifyItemAudiobook;
		case SpotifyShowResolutionKind::SynthesizedAudiobookUri:
			return result.resolvedUri == SpotifyUriForItemKind(kSpotifyItemAudiobook,
				SpotifyItemIdForUri(result.request.uri));
		case SpotifyShowResolutionKind::LegacyShowAfterApiFailure:
		case SpotifyShowResolutionKind::LegacyShowAfterMalformedResponse:
			return result.resolvedUri == result.request.uri;
		default:
			return false;
	}
}

SpotifyOpenRequest
ResolvedSpotifyOpenRequest(const SpotifyShowResolution& result)
{
	if (!ValidSpotifyShowResolution(result))
		return {};
	SpotifyOpenRequest request = result.request;
	request.uri = result.resolvedUri;
	request.skipAudiobookResolution = true;
	return request;
}

bool
ResolveSpotifyShow(ContentApi& api, const SpotifyOpenRequest& request,
	std::function<void(const SpotifyShowResolution&)> complete)
{
	if (!CanResolveShow(request) || !complete)
		return false;
	api.GetAudiobook(SpotifyItemIdForUri(request.uri),
		[request, complete](bool ok, const nlohmann::json& data) {
		complete(MapResolution(request, ok, data));
	});
	return true;
}
