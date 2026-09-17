#include "PlaylistMetadataMessages.h"
#include "Messages.h"

#include <utility>

namespace {
bool
ReadString(const BMessage& message, const char* field, std::string& result)
{
	const char* value = nullptr;
	if (message.FindString(field, &value) != B_OK || !value)
		return false;
	result = value;
	return true;
}

bool
ReadContext(const BMessage& message, PlaylistMetadataResult& result)
{
	int32 kind = 0;
	if (message.what != MSG_PLAYLIST_METADATA_RESULT
			|| message.FindInt32("metadata_kind", &kind) != B_OK
			|| kind < static_cast<int32>(PlaylistMetadataKind::Playlist)
			|| kind > static_cast<int32>(PlaylistMetadataKind::CurrentUser)) {
		return false;
	}
	result.request.kind = static_cast<PlaylistMetadataKind>(kind);
	return ReadString(message, "id", result.request.id)
		&& message.FindBool("ok", &result.ok) == B_OK
		&& message.FindBool("response_valid", &result.responseValid) == B_OK
		&& message.FindInt32("status", &result.status) == B_OK
		&& message.FindInt32("retry_after", &result.retryAfter) == B_OK;
}

bool
ReadData(const BMessage& message, PlaylistMetadataResult& result)
{
	return ReadString(message, "title", result.title)
		&& ReadString(message, "cover_url", result.coverUrl)
		&& ReadString(message, "snapshot_id", result.snapshotId)
		&& ReadString(message, "description", result.description)
		&& ReadString(message, "owner_id", result.ownerId)
		&& message.FindBool("public", &result.isPublic) == B_OK
		&& message.FindInt32("total", &result.total) == B_OK
		&& ReadString(message, "user_id", result.userId)
		&& ReadString(message, "legacy_user_id", result.legacyUserId);
}
}

BMessage
MakePlaylistMetadataMessage(const PlaylistMetadataResult& result)
{
	BMessage message(MSG_PLAYLIST_METADATA_RESULT);
	message.AddInt32("metadata_kind", static_cast<int32>(result.request.kind));
	message.AddString("id", result.request.id.c_str());
	message.AddBool("ok", result.ok);
	message.AddBool("response_valid", result.responseValid);
	message.AddInt32("status", result.status);
	message.AddInt32("retry_after", result.retryAfter);
	if (!result.ok || !result.responseValid)
		return message;
	message.AddString("title", result.title.c_str());
	message.AddString("cover_url", result.coverUrl.c_str());
	message.AddString("snapshot_id", result.snapshotId.c_str());
	message.AddString("description", result.description.c_str());
	message.AddString("owner_id", result.ownerId.c_str());
	message.AddBool("public", result.isPublic);
	message.AddInt32("total", result.total);
	message.AddString("user_id", result.userId.c_str());
	message.AddString("legacy_user_id", result.legacyUserId.c_str());
	return message;
}

bool
ReadPlaylistMetadataMessage(const BMessage& message, PlaylistMetadataResult& result)
{
	PlaylistMetadataResult parsed;
	if (!ReadContext(message, parsed))
		return false;
	if (parsed.ok && (!parsed.responseValid || !ReadData(message, parsed)))
		return false;
	result = std::move(parsed);
	return true;
}

BMessage
MakePlaylistTitleMessage(const std::string& title)
{
	BMessage message(MSG_PLAYLIST_TITLE_UPDATE);
	message.AddString("title", title.c_str());
	return message;
}

BMessage
MakePlaylistSnapshotMessage(const PlaylistMetadataResult& result)
{
	BMessage message = MakePlaylistMetadataMessage(result);
	message.what = MSG_PLAYLIST_SNAPSHOT_RESULT;
	return message;
}

bool
ReadPlaylistSnapshotMessage(const BMessage& message, PlaylistMetadataResult& result)
{
	if (message.what != MSG_PLAYLIST_SNAPSHOT_RESULT)
		return false;
	BMessage metadata(message);
	metadata.what = MSG_PLAYLIST_METADATA_RESULT;
	PlaylistMetadataResult parsed;
	if (!ReadPlaylistMetadataMessage(metadata, parsed)
			|| parsed.request.kind != PlaylistMetadataKind::Playlist || parsed.request.id.empty())
		return false;
	result = std::move(parsed);
	return true;
}

BMessage
MakePlaylistCoverMessage(const std::string& url)
{
	BMessage message(MSG_PLAYLIST_COVER_UPDATE);
	message.AddString("url", url.c_str());
	return message;
}
