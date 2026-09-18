#include "SpotifyAccountSession.h"
#include "SpotifySessionPolicy.h"
#include "spotify/SpotifyCapabilities.h"
#include "spotify/api/SpotifyApi.h"
#include "spotify/api/SpotifyResponse.h"

namespace {
bool
MapIdentity(const nlohmann::json& profile, SpotifyAccountResult& result)
{
	if (!profile.is_object())
		return false;
	auto provider = profile.find("account_id");
	if (provider != profile.end()) {
		if (!provider->is_string()) return false;
		result.providerId = provider->get<std::string>();
	}
	auto id = profile.find("id");
	if (id != profile.end()) {
		if (!id->is_string()) return false;
		result.id = id->get<std::string>();
	} else
		result.id = result.providerId;
	return !result.id.empty();
}
}

void
RequestSpotifyAccount(ProfileApi& api, int64_t requestId,
	std::function<void(const SpotifyAccountResult&)> complete)
{
	if (requestId <= 0 || !complete) return;
	api.RefreshCurrentUserProfile([requestId, complete](bool ok, const nlohmann::json& data) {
		SpotifyAccountResult result;
		result.requestId = requestId;
		result.ok = ok;
		result.status = SpotifyResponseStatus(data);
		result.retryAfter = SpotifyResponseRetryAfter(data);
		result.valid = ok && MapIdentity(data, result);
		if (!result.valid) {
			result.id.clear();
			result.providerId.clear();
		}
		complete(result);
	});
}

SpotifyAccountUpdate
ApplySpotifyAccount(SpotifyApi& api, SpotifyCapabilities& capabilities,
	const SpotifyAccountResult& result)
{
	SpotifyAccountUpdate update;
	if (result.requestId <= 0 || !result.ok || !result.valid || result.id.empty()) return update;
	HaifySettings settings = SettingsController::Load();
	bool changed = SpotifyAccountChanged(settings.spotifyAccountId, result.id, result.providerId);
	update.storageStatus = SettingsController::Update([&](HaifySettings& value) {
		value.spotifyAccountId = result.id;
	});
	if (update.storageStatus != B_OK) return update;
	if (changed)
		api.ClearSession();
	api.SetAccountId(result.id);
	if (changed) {
		api.SetAccessToken(settings.accessToken);
		capabilities.Reset();
	}
	update.applied = true;
	update.changed = changed;
	return update;
}

void
RefreshSpotifyAccountPlaylists(SpotifyApi& api,
	std::function<void(const SpotifyAccountPlaylistsResult&)> complete)
{
	std::string account = api.AccountId();
	// Rebuild writable rows after the profile identifies the account. Keep the
	// existing PlaylistApi cache policy; this does not force an extra network GET.
	api.Playlists().GetPlaylists([account, complete](bool ok, const nlohmann::json& data) {
		if (complete)
			complete({account, ok, SpotifyResponseStatus(data), SpotifyResponseRetryAfter(data)});
	});
}
