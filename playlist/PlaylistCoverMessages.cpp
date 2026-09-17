#include "PlaylistCoverMessages.h"
#include "Messages.h"
#include <utility>

static bool
ValidCoverOutcome(int32 error, const char* url)
{
	return error >= 0 && error <= static_cast<int32>(PlaylistCoverError::RefreshFailed)
		&& url && (error != 0 || url[0]);
}

BMessage
MakePlaylistCoverResultMessage(const PlaylistCoverResult& result)
{
	BMessage message(MSG_PLAYLIST_COVER_RESULT);
	message.AddInt64("request_id", result.command.requestId);
	message.AddString("playlist_id", result.command.playlistId.c_str());
	message.AddInt32("error", static_cast<int32>(result.error));
	message.AddInt32("status", result.status);
	message.AddString("cover_url", result.error == PlaylistCoverError::None ? result.coverUrl.c_str() : "");
	return message;
}

bool
ReadPlaylistCoverResultMessage(const BMessage& message, PlaylistCoverResult& result)
{
	if (message.what != MSG_PLAYLIST_COVER_RESULT)
		return false;
	int64 request = 0;
	int32 error = 0;
	int32 status = -1;
	const char* id = nullptr;
	const char* url = nullptr;
	if (message.FindInt64("request_id", &request) != B_OK || request <= 0
			|| message.FindString("playlist_id", &id) != B_OK || !id || !id[0]
			|| message.FindInt32("error", &error) != B_OK
			|| message.FindInt32("status", &status) != B_OK
			|| message.FindString("cover_url", &url) != B_OK
			|| !ValidCoverOutcome(error, url))
		return false;
	PlaylistCoverResult parsed;
	parsed.command = {request, id};
	parsed.error = static_cast<PlaylistCoverError>(error);
	parsed.status = status;
	if (parsed.error == PlaylistCoverError::None) parsed.coverUrl = url;
	result = std::move(parsed);
	return true;
}
