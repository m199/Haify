#pragma once

#include "settings/SettingsController.h"

#include <set>
#include <algorithm>
#include <sstream>
#include <string>
#include <ctime>
#include <limits>

inline bool
SpotifyHasRequiredScopes(const std::string& granted, const std::string& required)
{
	std::set<std::string> scopes;
	std::istringstream source(granted), expected(required);
	std::string scope;
	while (source >> scope)
		scopes.insert(scope);
	while (expected >> scope) {
		if (scopes.find(scope) == scopes.end())
			return false;
	}
	return true;
}

inline bool
SpotifyAccountChanged(const std::string& previous, const std::string& id,
	const std::string& providerId)
{
	return !previous.empty() && previous != id
		&& (providerId.empty() || previous != providerId);
}

enum class SpotifyAuthStartup { None, ClearObsoleteScopes, UseAccessToken, RefreshToken };

inline SpotifyAuthStartup
SelectSpotifyAuthStartup(const HaifySettings& settings, int scopeVersion, time_t now)
{
	if ((!settings.accessToken.empty() || !settings.refreshToken.empty())
			&& settings.authScopeVersion != scopeVersion)
		return SpotifyAuthStartup::ClearObsoleteScopes;
	if (!settings.accessToken.empty() && settings.accessTokenExpiresAt > now
			&& settings.accessTokenExpiresAt - now > 60)
		return SpotifyAuthStartup::UseAccessToken;
	return settings.refreshToken.empty() ? SpotifyAuthStartup::None
		: SpotifyAuthStartup::RefreshToken;
}

inline int
SpotifyTokenLifetime(time_t expiresAt, time_t now)
{
	if (expiresAt <= now)
		return 0;
	return static_cast<int>(std::min<time_t>(expiresAt - now, std::numeric_limits<int>::max()));
}

inline int
SpotifyTokenRefreshDelay(int expiresIn)
{
	return expiresIn > 120 ? expiresIn - 60 : 30;
}

struct SpotifyAuthFailureAction { bool clearSession = false; bool retryRefresh = false; };

inline SpotifyAuthFailureAction
SelectSpotifyAuthFailure(const std::string& error, bool refreshing)
{
	bool invalidGrant = error == "invalid_grant";
	return {invalidGrant, refreshing && !invalidGrant};
}
