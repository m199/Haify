#include "SpotifySessionTestSupport.h"
#include "spotify/session/SpotifySessionMessages.h"
#include "messages/MessageContracts.h"
#include "spotify/SpotifyCapabilities.h"

#include <iostream>

static SpotifyAccountResult Profile()
{
	return {7, true, true, 200, -1, "legacy", "provider"};
}

static void Reject(const BMessage& message)
{
	auto result = Profile();
	assert(!ReadSpotifyAccountResultMessage(message, result));
	assert(result.id == "legacy" && result.requestId == 7);
}

static void TestProfileWire()
{
	BMessage valid = MakeSpotifyAccountResultMessage(Profile());
	SpotifyAccountResult result;
	assert(ReadSpotifyAccountResultMessage(valid, result));
	assert(result.id == "legacy" && result.providerId == "provider" && result.status == 200);
	for (const char* field : {"request_id", "ok", "response_valid", "status", "retry_after",
			"account_id", "provider_account_id"}) {
		BMessage message(valid);
		message.RemoveName(field);
		Reject(message);
		message.AddFloat(field, 1.0f);
		Reject(message);
		message = valid;
		type_code type = 0;
		int32 count = 0;
		valid.GetInfo(field, &type, &count);
		if (type == B_BOOL_TYPE) message.AddBool(field, false);
		else if (type == B_INT32_TYPE) message.AddInt32(field, 1);
		else if (type == B_INT64_TYPE) message.AddInt64(field, 1);
		else message.AddString(field, "duplicate");
		Reject(message);
	}
	valid.ReplaceBool("ok", false);
	Reject(valid);
	Reject(BMessage(MSG_SPOTIFY_ACCOUNT_RESULT)); // Old bare profile is not a pending request.
	SpotifyAccountResult failed{8, false, false, 429, 10, "", ""};
	assert(ReadSpotifyAccountResultMessage(MakeSpotifyAccountResultMessage(failed), result));
	assert(!result.ok && result.status == 429 && result.retryAfter == 10);
	auto invalid = Profile();
	invalid.requestId = 0;
	Reject(MakeSpotifyAccountResultMessage(invalid));
	invalid = Profile();
	invalid.status = -2;
	Reject(MakeSpotifyAccountResultMessage(invalid));
}

static void TestScopedPlaylistNotification()
{
	assert(MakeSpotifyAccountPlaylistsMessage({"account", false, 429, 10}).what != MSG_PLAYLISTS_CHANGED);
	BMessage message = MakeSpotifyAccountPlaylistsMessage({"account", true, 200, -1});
	assert(message.what == MSG_PLAYLISTS_CHANGED);
	assert(MessageContracts::MatchesAccount(message, "account"));
	assert(!MessageContracts::MatchesAccount(message, "other"));
	assert(!MessageContracts::MatchesAccount(message, ""));
}

static void TestTokenWire()
{
	TokenResult token;
	token.success = true;
	token.httpStatus = 200;
	token.accessToken = "access";
	token.refreshToken = "refresh";
	token.scopes = "scope";
	token.expiresIn = 3600;
	BMessage message(MSG_AUTH_COMPLETE);
	AddSpotifyTokenResult(message, token);
	TokenResult parsed;
	assert(ReadSpotifyTokenResult(message, parsed));
	assert(parsed.success && parsed.httpStatus == 200 && parsed.accessToken == token.accessToken);
	assert(parsed.refreshToken == token.refreshToken && parsed.scopes == "scope" && parsed.expiresIn == 3600);
	message.AddString("access_token", "duplicate");
	parsed.accessToken = "untouched";
	assert(!ReadSpotifyTokenResult(message, parsed) && parsed.accessToken == "untouched");
	message = BMessage(MSG_AUTH_COMPLETE);
	message.AddString("expires_in", "3600");
	assert(!ReadSpotifyTokenResult(message, parsed));
	message = BMessage(MSG_AUTH_COMPLETE);
	assert(ReadSpotifyTokenResult(message, parsed) && !parsed.success && parsed.expiresIn == 3600);
}

static void TestCapabilityWire()
{
	SpotifyCapabilities capabilities;
	capabilities.SetAudiobookMode(kAudiobookEnabled);
	BMessage snapshot = MakeSpotifyCapabilitiesSnapshot(capabilities);
	assert(snapshot.what == MSG_SPOTIFY_CAPABILITIES_CHANGED);
	assert(snapshot.GetBool("audiobooks_enabled", false));
	assert(snapshot.GetInt32("audiobook_mode", -1) == kAudiobookEnabled);
	assert(snapshot.GetInt32("audiobook_state", -1) == kAudiobookUnknown);
	BMessage done = MakeSpotifyCapabilityProbeResult();
	assert(done.what == MSG_SPOTIFY_CAPABILITIES_CHANGED && done.GetBool("probe_result", false));
}

int main()
{
	TestProfileWire();
	TestScopedPlaylistNotification();
	TestTokenWire();
	TestCapabilityWire();
	std::cout << "Spotify session message tests passed.\n";
}
