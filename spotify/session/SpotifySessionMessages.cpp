#include "SpotifySessionMessages.h"
#include "messages/MessageContracts.h"
#include "spotify/SpotifyCapabilities.h"

#include <utility>

namespace {
bool AccountFieldsValid(const BMessage& message)
{
	const struct { const char* field = nullptr; type_code type = 0; } fields[] = {
		{MessageFields::RequestId, B_INT64_TYPE}, {MessageFields::Ok, B_BOOL_TYPE},
		{MessageFields::ResponseValid, B_BOOL_TYPE}, {MessageFields::Status, B_INT32_TYPE},
		{"retry_after", B_INT32_TYPE}, {MessageFields::AccountId, B_STRING_TYPE},
		{"provider_account_id", B_STRING_TYPE}
	};
	for (const auto& field : fields) {
		type_code type = 0;
		int32 count = 0;
		if (message.GetInfo(field.field, &type, &count) != B_OK || type != field.type || count != 1)
			return false;
	}
	return true;
}
}

BMessage MakeSpotifyAccountResultMessage(const SpotifyAccountResult& result)
{
	BMessage message(MSG_SPOTIFY_ACCOUNT_RESULT);
	message.AddInt64(MessageFields::RequestId, result.requestId);
	message.AddBool(MessageFields::Ok, result.ok);
	message.AddBool(MessageFields::ResponseValid, result.valid);
	message.AddInt32(MessageFields::Status, result.status);
	message.AddInt32("retry_after", result.retryAfter);
	message.AddString(MessageFields::AccountId, result.id.c_str());
	message.AddString("provider_account_id", result.providerId.c_str());
	return message;
}

bool ReadSpotifyAccountResultMessage(const BMessage& message, SpotifyAccountResult& result)
{
	if (message.what != MSG_SPOTIFY_ACCOUNT_RESULT || !AccountFieldsValid(message)) return false;
	SpotifyAccountResult parsed;
	parsed.requestId = message.GetInt64(MessageFields::RequestId, 0);
	parsed.ok = message.GetBool(MessageFields::Ok, false);
	parsed.valid = message.GetBool(MessageFields::ResponseValid, false);
	parsed.status = message.GetInt32(MessageFields::Status, -1);
	parsed.retryAfter = message.GetInt32("retry_after", -1);
	parsed.id = message.GetString(MessageFields::AccountId, "");
	parsed.providerId = message.GetString("provider_account_id", "");
	if (parsed.requestId <= 0 || parsed.status < -1 || parsed.retryAfter < -1) return false;
	if (parsed.valid && (!parsed.ok || parsed.id.empty())) return false;
	if (!parsed.valid && (!parsed.id.empty() || !parsed.providerId.empty())) return false;
	result = std::move(parsed);
	return true;
}

BMessage MakeSpotifyAccountPlaylistsMessage(const SpotifyAccountPlaylistsResult& result)
{
	if (!result.ok || result.account.empty()) return BMessage();
	BMessage message(MSG_PLAYLISTS_CHANGED);
	message.AddString(MessageFields::AccountId, result.account.c_str());
	return message;
}

BMessage MakeSpotifyCapabilityProbeResult()
{
	BMessage message(MSG_SPOTIFY_CAPABILITIES_CHANGED);
	message.AddBool("probe_result", true);
	return message;
}

BMessage MakeSpotifyCapabilitiesSnapshot(const SpotifyCapabilities& capabilities)
{
	BMessage message(MSG_SPOTIFY_CAPABILITIES_CHANGED);
	message.AddInt32("audiobook_state", static_cast<int32>(capabilities.AudiobookState()));
	message.AddInt32("audiobook_mode", static_cast<int32>(capabilities.AudiobookModeSetting()));
	message.AddBool("audiobooks_enabled", capabilities.AudiobooksEnabled());
	return message;
}

void AddSpotifyTokenResult(BMessage& message, const TokenResult& result)
{
	message.AddBool("ok", result.success);
	message.AddInt32("http_status", result.httpStatus);
	message.AddInt32("expires_in", result.expiresIn);
	message.AddString("access_token", result.accessToken.c_str());
	message.AddString("refresh_token", result.refreshToken.c_str());
	message.AddString("scopes", result.scopes.c_str());
	message.AddString("error", result.error.c_str());
	message.AddString("error_description", result.errorDescription.c_str());
}

bool ReadSpotifyTokenResult(const BMessage& message, TokenResult& result)
{
	const struct { const char* field = nullptr; type_code type = 0; } fields[] = {
		{"ok", B_BOOL_TYPE}, {"http_status", B_INT32_TYPE}, {"expires_in", B_INT32_TYPE},
		{"access_token", B_STRING_TYPE}, {"refresh_token", B_STRING_TYPE},
		{"scopes", B_STRING_TYPE}, {"error", B_STRING_TYPE}, {"error_description", B_STRING_TYPE}
	};
	if (message.what != MSG_AUTH_COMPLETE) return false;
	for (const auto& field : fields) {
		if (!MessageContracts::OptionalField(message, field.field, field.type)) return false;
	}
	TokenResult parsed;
	parsed.success = message.GetBool("ok", false);
	parsed.httpStatus = message.GetInt32("http_status", -1);
	parsed.expiresIn = message.GetInt32("expires_in", 3600);
	parsed.accessToken = message.GetString("access_token", "");
	parsed.refreshToken = message.GetString("refresh_token", "");
	parsed.scopes = message.GetString("scopes", "");
	parsed.error = message.GetString("error", "");
	parsed.errorDescription = message.GetString("error_description", "");
	result = std::move(parsed);
	return true;
}
