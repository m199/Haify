#include "PlaylistWriteMessages.h"
#include "messages/Messages.h"
#include <utility>

BMessage
MakePlaylistWriteMessage(const PlaylistWriteResult& result)
{
	BMessage message(result.kind == PlaylistWriteKind::Clear
		? MSG_PLAYLIST_CLEAR_RESULT : MSG_PLAYLIST_ADD_RESULT);
	message.AddInt64("request_id", result.requestId);
	message.AddString("playlist_id", result.playlistId.c_str());
	message.AddBool("ok", result.ok);
	message.AddInt32("status", result.status);
	message.AddString("snapshot_id", result.ok ? result.snapshotId.c_str() : "");
	return message;
}

bool
ReadPlaylistWriteMessage(const BMessage& message, PlaylistWriteResult& result)
{
	if (message.what != MSG_PLAYLIST_CLEAR_RESULT && message.what != MSG_PLAYLIST_ADD_RESULT)
		return false;
	PlaylistWriteResult parsed;
	parsed.kind = message.what == MSG_PLAYLIST_CLEAR_RESULT
		? PlaylistWriteKind::Clear : PlaylistWriteKind::Add;
	int64 request = 0;
	int32 status = -1;
	const char* id = nullptr;
	const char* snapshot = nullptr;
	if (message.FindInt64("request_id", &request) != B_OK || request <= 0
			|| message.FindString("playlist_id", &id) != B_OK || !id || !id[0]
			|| message.FindBool("ok", &parsed.ok) != B_OK
			|| message.FindInt32("status", &status) != B_OK
			|| message.FindString("snapshot_id", &snapshot) != B_OK || !snapshot)
		return false;
	parsed.requestId = request;
	parsed.playlistId = id;
	parsed.status = status;
	if (parsed.ok) parsed.snapshotId = snapshot;
	result = std::move(parsed);
	return true;
}
