#include "PlaylistRemovalMessages.h"
#include "Messages.h"

BMessage
MakePlaylistRemovalMessage(const PlaylistRemovalResult& result)
{
	BMessage message(MSG_PLAYLIST_REMOVAL_RESULT);
	message.AddInt64("request_id", result.requestId);
	message.AddString("playlist_id", result.playlistId.c_str());
	message.AddBool("ok", result.ok);
	message.AddInt32("status", result.status);
	message.AddBool("partial_update", result.partialUpdate);
	return message;
}

bool
ReadPlaylistRemovalMessage(const BMessage& message, PlaylistRemovalResult& result)
{
	PlaylistRemovalResult parsed;
	int64 requestId = 0;
	int32 status = -1;
	const char* playlistId = nullptr;
	if (message.what != MSG_PLAYLIST_REMOVAL_RESULT
			|| message.FindInt64("request_id", &requestId) != B_OK || requestId <= 0
			|| message.FindString("playlist_id", &playlistId) != B_OK
			|| !playlistId || !playlistId[0]
			|| message.FindBool("ok", &parsed.ok) != B_OK
			|| message.FindInt32("status", &status) != B_OK
			|| message.FindBool("partial_update", &parsed.partialUpdate) != B_OK) {
		return false;
	}
	parsed.requestId = requestId;
	parsed.playlistId = playlistId;
	parsed.status = status;
	result = std::move(parsed);
	return true;
}
