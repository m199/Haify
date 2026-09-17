#include "PlaylistReorderMessages.h"
#include "Messages.h"

#include <utility>

BMessage
MakePlaylistReorderMessage(const PlaylistReorderResult& result)
{
	BMessage message(MSG_PLAYLIST_REORDER_RESULT);
	message.AddInt64("request_id", result.requestId);
	message.AddString("playlist_id", result.playlistId.c_str());
	message.AddBool("ok", result.ok);
	message.AddInt32("status", result.status);
	message.AddString("snapshot_id", result.ok ? result.snapshotId.c_str() : "");
	return message;
}

bool
ReadPlaylistReorderMessage(const BMessage& message, PlaylistReorderResult& result)
{
	PlaylistReorderResult parsed;
	int64 requestId = 0;
	int32 status = -1;
	const char* playlistId = nullptr;
	const char* snapshotId = nullptr;
	if (message.what != MSG_PLAYLIST_REORDER_RESULT
			|| message.FindInt64("request_id", &requestId) != B_OK || requestId <= 0
			|| message.FindString("playlist_id", &playlistId) != B_OK
			|| !playlistId || !playlistId[0]
			|| message.FindBool("ok", &parsed.ok) != B_OK
			|| message.FindInt32("status", &status) != B_OK
			|| message.FindString("snapshot_id", &snapshotId) != B_OK || !snapshotId)
		return false;
	parsed.requestId = requestId;
	parsed.playlistId = playlistId;
	parsed.status = status;
	if (parsed.ok)
		parsed.snapshotId = snapshotId;
	result = std::move(parsed);
	return true;
}
