#include "SpotifyNavigationMessages.h"
#include "messages/MessageContracts.h"

#include <utility>

namespace {
bool
SingleField(const BMessage& message, const char* field, type_code expected)
{
	type_code type = 0;
	int32 count = 0;
	return message.GetInfo(field, &type, &count) == B_OK && type == expected && count == 1;
}

bool
ResolutionFieldsValid(const BMessage& message)
{
	const struct { const char* name = nullptr; type_code type = 0; } fields[] = {
		{"request", B_MESSAGE_TYPE}, {"resolution_kind", B_INT32_TYPE},
		{"resolved_uri", B_STRING_TYPE}, {MessageFields::Status, B_INT32_TYPE},
		{"retry_after", B_INT32_TYPE}
	};
	for (const auto& field : fields) {
		if (!SingleField(message, field.name, field.type))
			return false;
	}
	return true;
}
}

BMessage
MakeSpotifyOpenMessage(const SpotifyOpenRequest& request)
{
	BMessage message(MSG_OPEN_SPOTIFY_URI);
	message.AddString(MessageFields::Uri, request.uri.c_str());
	message.AddString(MessageFields::Title, request.title.c_str());
	message.AddString("coverUrl", request.coverUrl.c_str());
	message.AddBool("skip_audiobook_resolution", request.skipAudiobookResolution);
	return message;
}

bool
ReadSpotifyOpenMessage(const BMessage& message, SpotifyOpenRequest& request)
{
	SpotifyOpenRequest parsed;
	std::string coverAlias;
	if (message.what != MSG_OPEN_SPOTIFY_URI
			|| !MessageContracts::ReadString(message, MessageFields::Uri, parsed.uri)
			|| parsed.uri.empty()
			|| !MessageContracts::ReadString(message, MessageFields::Title, parsed.title)
			|| !MessageContracts::ReadString(message, "coverUrl", parsed.coverUrl)
			|| !MessageContracts::ReadString(message, "cover_url", coverAlias)
			|| !MessageContracts::OptionalField(message, "skip_audiobook_resolution", B_BOOL_TYPE))
		return false;
	if (parsed.coverUrl.empty())
		parsed.coverUrl = coverAlias;
	parsed.skipAudiobookResolution = message.GetBool("skip_audiobook_resolution", false);
	request = std::move(parsed);
	return true;
}

BMessage
MakeSpotifyShowResolutionMessage(const SpotifyShowResolution& result)
{
	BMessage message(MSG_SPOTIFY_SHOW_RESOLVED);
	BMessage request = MakeSpotifyOpenMessage(result.request);
	message.AddMessage("request", &request);
	message.AddInt32("resolution_kind", static_cast<int32>(result.kind));
	message.AddString("resolved_uri", result.resolvedUri.c_str());
	message.AddInt32(MessageFields::Status, result.status);
	message.AddInt32("retry_after", result.retryAfter);
	return message;
}

bool
ReadSpotifyShowResolutionMessage(const BMessage& message, SpotifyShowResolution& result)
{
	if (message.what != MSG_SPOTIFY_SHOW_RESOLVED || !ResolutionFieldsValid(message))
		return false;
	SpotifyShowResolution parsed;
	BMessage request;
	if (message.FindMessage("request", &request) != B_OK
			|| !ReadSpotifyOpenMessage(request, parsed.request))
		return false;
	parsed.kind = static_cast<SpotifyShowResolutionKind>(message.GetInt32("resolution_kind", -1));
	parsed.resolvedUri = message.GetString("resolved_uri", "");
	parsed.status = message.GetInt32(MessageFields::Status, -1);
	parsed.retryAfter = message.GetInt32("retry_after", -1);
	if (!ValidSpotifyShowResolution(parsed))
		return false;
	result = std::move(parsed);
	return true;
}
