#include "SpotifySessionTestSupport.h"
#include "spotify/session/SpotifyAccountSession.h"
#include "spotify/session/SpotifySessionPolicy.h"
#include "spotify/session/SpotifyCredentialStore.h"
#include "spotify/SpotifyCapabilities.h"
#include "spotify/api/SpotifyResponse.h"
#include "Config.h"

#include <iostream>

using nlohmann::json;

static void TestProfileResultsAndRequestGate()
{
	const struct { json profile; bool valid = false; const char* id = nullptr; } cases[] = {
		{{{"id", "legacy"}, {"account_id", "provider"}}, true, "legacy"},
		{{{"account_id", "provider"}}, true, "provider"},
		{{{"id", ""}, {"account_id", "provider"}}, false, ""},
		{{{"id", nullptr}}, false, ""}, {{{"id", 7}}, false, ""},
		{{{"id", "legacy"}, {"account_id", false}}, false, ""},
		{json::array(), false, ""}, {json::object(), false, ""}
	};
	for (const auto& item : cases) {
		ProfileApi api([&item](const std::string& path, JsonCallback done) {
			assert(path == "/me");
			done(true, item.profile);
		});
		int calls = 0;
		RequestSpotifyAccount(api, 42, [&calls, &item](const SpotifyAccountResult& result) {
			++calls;
			assert(result.requestId == 42 && result.ok && result.valid == item.valid);
			assert(result.id == item.id);
		});
		assert(calls == 1);
	}
	SpotifyAccountRequestState state;
	auto old = state.Begin();
	auto current = state.Begin();
	assert(!state.Accept(old) && state.Accept(current) && !state.Accept(current));
	old = state.Begin();
	state.Cancel();
	assert(!state.Accept(old));
	current = state.Begin();
	assert(current != old && !state.Accept(old) && state.Accept(current));
	for (int status : {0, 401, 403, 404, 429, 500}) {
		ProfileApi api([status](const std::string&, JsonCallback done) {
			done(false, {{"status", status}, {"retry_after", 9}, {"id", "misleading"}});
		});
		RequestSpotifyAccount(api, 1, [status](const SpotifyAccountResult& result) {
			assert(!result.ok && !result.valid && result.id.empty());
			assert(result.status == status && result.retryAfter == 9);
		});
	}
}

static json PlaylistPage(const std::string& owner)
{
	return {{"items", json::array({{{"id", "p"}, {"name", "Owned"},
		{"owner", {{"id", owner}}}, {"collaborative", false}}})}, {"next", nullptr}};
}

static void TestIdentityRefreshSeparatesOldReaders()
{
	for (bool newerFirst : {false, true}) {
		SpotifyApi api("old token");
		SessionTestTransport transport;
		transport.Attach(api);
		SpotifyAccountRequestState state;
		std::string accepted;
		int calls = 0;
		auto complete = [&](const SpotifyAccountResult& result) {
			++calls;
			if (state.Accept(result.requestId)) accepted = result.id;
		};
		RequestSpotifyAccount(api.Profile(), state.Begin(), complete);
		api.SetAccessToken("new token");
		RequestSpotifyAccount(api.Profile(), state.Begin(), complete);
		assert(transport.requests.size() == 2); // Neither a cached nor coalesced old /me.
		if (newerFirst) transport.Reply(1, 200, {{"id", "new"}});
		transport.Reply(0, 200, {{"id", "old"}});
		if (!newerFirst) transport.Reply(1, 200, {{"id", "new"}});
		assert(calls == 2 && accepted == "new");
		bool cached = false;
		api.Profile().GetCurrentUserProfile([&cached](bool ok, const json& data) {
			cached = true;
			assert(ok && data["id"] == "new");
		});
		assert(cached && transport.requests.size() == 2); // Old result did not poison cache.
	}
}

static void TestAccountTransitionsAndCache()
{
	for (const std::string previous : {"", "legacy", "provider", "other"}) {
		ResetSessionTestSettings();
		sessionTestSettings.spotifyAccountId = previous;
		sessionTestSettings.accessToken = "token";
		SpotifyApi api("token");
		api.SetAccountId(previous);
		SpotifyCapabilities capabilities(&api);
		SessionTestTransport transport;
		transport.Attach(api);
		api.Playlists().GetPlaylists({});
		transport.Reply(0, 200, PlaylistPage(previous));
		assert(api.Playlists().GetCachedPlaylists().size() == (previous.empty() ? 0U : 1U));
		SpotifyAccountResult profile{1, true, true, -1, -1, "legacy", "provider"};
		auto update = ApplySpotifyAccount(api, capabilities, profile);
		assert(update.applied && update.changed == (previous == "other"));
		assert(api.AccountId() == "legacy" && sessionTestSettings.spotifyAccountId == "legacy");
		assert(api.Playlists().GetCachedPlaylists().empty() == (previous != "legacy"));
		bool notified = false;
		RefreshSpotifyAccountPlaylists(api, [&notified](const SpotifyAccountPlaylistsResult& result) {
			notified = true;
			assert(result.ok && result.account == "legacy");
		});
		if (previous == "legacy") {
			assert(notified && transport.requests.size() == 1); // Reuses current raw page.
		} else {
			assert(!notified && transport.requests.size() == 2);
			transport.Reply(1, 200, PlaylistPage("legacy"));
		}
		assert(notified && api.Playlists().GetCachedPlaylists().size() == 1);
	}
}

static void TestPersistenceFailureAndOldPlaylistCompletion()
{
	ResetSessionTestSettings();
	sessionTestSettings.spotifyAccountId = "old";
	SpotifyApi api("token");
	api.SetAccountId("old");
	SpotifyCapabilities capabilities(&api);
	sessionTestWriteStatus = B_ERROR;
	SpotifyAccountResult profile{1, true, true, 200, -1, "new", ""};
	auto update = ApplySpotifyAccount(api, capabilities, profile);
	assert(!update.applied && update.storageStatus == B_ERROR && !update.changed);
	assert(api.AccountId() == "old" && sessionTestSettings.spotifyAccountId == "old");
	SessionTestTransport transport;
	transport.Attach(api);
	bool notified = false;
	RefreshSpotifyAccountPlaylists(api, [&notified](const SpotifyAccountPlaylistsResult& result) {
		notified = true;
		assert(!result.ok && result.account == "old");
	});
	api.ClearSession();
	api.SetAccessToken("new token");
	api.SetAccountId("new");
	transport.Reply(0, 200, PlaylistPage("old"));
	assert(notified && api.Playlists().GetCachedPlaylists().empty());
}

static TokenResult Token()
{
	TokenResult token;
	token.success = true;
	token.accessToken = "new token";
	token.scopes = SPOTIFY_REQUIRED_SCOPES;
	token.expiresIn = 3600;
	return token;
}

static void TestCredentialPersistence()
{
	ResetSessionTestSettings();
	sessionTestSettings.refreshToken = "keep refresh";
	sessionTestSettings.grantedScopes = SPOTIFY_REQUIRED_SCOPES;
	SpotifyApi api("old token");
	auto token = Token();
	token.scopes.clear();
	auto result = StoreSpotifyCredentials(api, token);
	assert(result.stored && result.expiresIn == 3600);
	assert(sessionTestSettings.accessToken == "new token");
	assert(sessionTestSettings.refreshToken == "keep refresh");
	assert(sessionTestSettings.grantedScopes == SPOTIFY_REQUIRED_SCOPES);
	token.refreshToken = "rotated refresh";
	result = StoreSpotifyCredentials(api, token);
	assert(result.stored && sessionTestSettings.refreshToken == "rotated refresh");
	int writes = sessionTestWrites;
	token.accessToken.clear();
	token.refreshToken = "must not store";
	result = StoreSpotifyCredentials(api, token);
	assert(!result.stored && result.error == "missing_access_token" && sessionTestWrites == writes);
	assert(sessionTestSettings.refreshToken == "rotated refresh");
	token = Token();
	token.scopes = "insufficient";
	result = StoreSpotifyCredentials(api, token);
	assert(!result.stored && result.error == "insufficient_scope" && sessionTestWrites == writes);
	sessionTestWriteStatus = B_ERROR;
	result = StoreSpotifyCredentials(api, Token());
	assert(!result.stored && result.storageStatus == B_ERROR && result.error == "settings_write_failed");
	api.SetAccountId("account");
	status_t status = ClearSpotifyCredentials(api);
	assert(status == B_ERROR && api.AccountId().empty());
	assert(!sessionTestSettings.accessToken.empty()); // Disk failure is not claimed as success.
	sessionTestWriteStatus = B_OK;
	status = ClearSpotifyCredentials(api);
	assert(status == B_OK && sessionTestSettings.accessToken.empty() && sessionTestSettings.refreshToken.empty());
}

static void TestPolicyAndMalformedStatus()
{
	assert(SpotifyHasRequiredScopes("b a a\tc", "a b") && !SpotifyHasRequiredScopes("ab", "a b"));
	HaifySettings settings;
	settings.authScopeVersion = 3;
	assert(SelectSpotifyAuthStartup(settings, 3, 100) == SpotifyAuthStartup::None);
	settings.refreshToken = "refresh";
	assert(SelectSpotifyAuthStartup(settings, 4, 100) == SpotifyAuthStartup::ClearObsoleteScopes);
	assert(SelectSpotifyAuthStartup(settings, 3, 100) == SpotifyAuthStartup::RefreshToken);
	settings.accessToken = "access";
	settings.accessTokenExpiresAt = 160;
	assert(SelectSpotifyAuthStartup(settings, 3, 100) == SpotifyAuthStartup::RefreshToken);
	settings.accessTokenExpiresAt = 161;
	assert(SelectSpotifyAuthStartup(settings, 3, 100) == SpotifyAuthStartup::UseAccessToken);
	assert(SpotifyTokenLifetime(161, 100) == 61 && SpotifyTokenLifetime(99, 100) == 0);
	assert(SpotifyTokenRefreshDelay(120) == 30 && SpotifyTokenRefreshDelay(121) == 61);
	assert(SelectSpotifyAuthFailure("invalid_grant", true).clearSession);
	assert(!SelectSpotifyAuthFailure("invalid_grant", true).retryRefresh);
	assert(SelectSpotifyAuthFailure("server_error", true).retryRefresh);
	assert(!SelectSpotifyAuthFailure("server_error", false).retryRefresh);
	for (const json& value : {json(nullptr), json("429"), json(-2), json(1.5),
			json(std::numeric_limits<uint64_t>::max())}) {
		assert(SpotifyResponseStatus({{"status", value}}) == -1);
		assert(SpotifyResponseRetryAfter({{"retry_after", value}}) == -1);
	}
}

int main()
{
	TestProfileResultsAndRequestGate();
	TestIdentityRefreshSeparatesOldReaders();
	TestAccountTransitionsAndCache();
	TestPersistenceFailureAndOldPlaylistCompletion();
	TestCredentialPersistence();
	TestPolicyAndMalformedStatus();
	std::cout << "Spotify account/session tests passed.\n";
}
