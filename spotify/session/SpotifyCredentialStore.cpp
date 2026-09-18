#include "SpotifyCredentialStore.h"
#include "SpotifySessionPolicy.h"
#include "spotify/api/SpotifyApi.h"
#include "Config.h"

SpotifyCredentialResult
StoreSpotifyCredentials(SpotifyApi& api, const TokenResult& token)
{
	SpotifyCredentialResult result;
	HaifySettings previous = SettingsController::Load();
	std::string scopes = token.scopes.empty() ? previous.grantedScopes : token.scopes;
	if (!SpotifyHasRequiredScopes(scopes, SPOTIFY_REQUIRED_SCOPES)) {
		result.error = "insufficient_scope";
		result.description = "Spotify did not grant all required permissions.";
		return result;
	}
	// Validate before persistence: a nominal success without a token must not
	// overwrite the saved expiry or rotate the refresh token.
	if (!token.success || token.accessToken.empty()) {
		result.error = token.error.empty() ? "missing_access_token" : token.error;
		return result;
	}
	result.storageStatus = SettingsController::Update([&](HaifySettings& settings) {
		settings.accessToken = token.accessToken;
		if (!token.refreshToken.empty())
			settings.refreshToken = token.refreshToken;
		settings.grantedScopes = scopes;
		settings.accessTokenExpiresAt = time(nullptr) + token.expiresIn;
		settings.authScopeVersion = HAIFY_AUTH_SCOPE_VERSION;
	});
	if (result.storageStatus != B_OK) {
		result.error = "settings_write_failed";
		result.description = "Could not save Spotify credentials.";
		return result;
	}
	api.SetAccessToken(token.accessToken);
	result.stored = true;
	result.expiresIn = token.expiresIn;
	return result;
}

status_t
ClearStoredSpotifyCredentials()
{
	return SettingsController::Update([](HaifySettings& settings) {
		settings.accessToken.clear();
		settings.refreshToken.clear();
		settings.grantedScopes.clear();
		settings.accessTokenExpiresAt = 0;
	});
}

status_t
ClearSpotifyCredentials(SpotifyApi& api)
{
	status_t status = ClearStoredSpotifyCredentials();
	api.ClearSession();
	return status;
}
