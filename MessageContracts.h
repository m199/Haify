#pragma once

#include "Messages.h"
#include "DragItem.h"
#include "spotify/SpotifyUri.h"

#include <Message.h>
#include <string>
#include <vector>

// Serialization only. Callers retain the original BMessage when forwarding so
// metadata and Haiku's drop coordinates are not lost. See docs/message-contracts.md.
namespace MessageContracts {

inline bool
OptionalField(const BMessage& message, const char* field, type_code expected)
{
	type_code type = 0;
	int32 count = 0;
	status_t status = message.GetInfo(field, &type, &count);
	return status == B_NAME_NOT_FOUND
		|| (status == B_OK && type == expected && count == 1);
}

inline bool
ReadString(const BMessage& message, const char* field, std::string& value)
{
	if (!OptionalField(message, field, B_STRING_TYPE))
		return false;
	value = message.GetString(field, "");
	return true;
}

// Legacy producers may omit account_id. Scoped notifications must match the
// receiving session; malformed or repeated identity fields are rejected.
inline bool
MatchesAccount(const BMessage& message, const std::string& account)
{
	if (!OptionalField(message, MessageFields::AccountId, B_STRING_TYPE))
		return false;
	const char* source;
	return message.FindString(MessageFields::AccountId, &source) != B_OK || account == source;
}

inline bool
ReadInt32(const BMessage& message, const char* field, int32& value)
{
	if (!OptionalField(message, field, B_INT32_TYPE))
		return false;
	value = message.GetInt32(field, value);
	return true;
}

inline bool
ReadItemUri(const BMessage& message, std::string& uri, bool allowAlbum = false)
{
	std::string track, album;
	if (!ReadString(message, MessageFields::Uri, uri)
			|| !ReadString(message, MessageFields::TrackUri, track))
		return false;
	if (allowAlbum && !ReadString(message, MessageFields::AlbumUri, album))
		return false;
	if (uri.empty()) uri = track;
	if (uri.empty()) uri = album;
	return !uri.empty() && (track.empty() || track == uri)
		&& (album.empty() || album == uri);
}

inline bool
ValidDragSource(const DragItem& item, int32 intent)
{
	return item.sourceIndex >= -1
		&& (item.sourceIndex < 0 || !item.sourcePlaylist.empty())
		&& intent >= 0 && intent <= 2;
}

inline int32
DragSelectionCount(const BMessage& message)
{
	type_code indexType = 0, uriType = 0;
	int32 indexCount = 0, uriCount = 0;
	status_t indices = message.GetInfo(MessageFields::SourceIndices, &indexType, &indexCount);
	status_t uris = message.GetInfo(MessageFields::SourceUris, &uriType, &uriCount);
	if (indices == B_NAME_NOT_FOUND && uris == B_NAME_NOT_FOUND)
		return 0;
	const char* snapshot = nullptr;
	if (indices != B_OK || uris != B_OK || indexType != B_INT32_TYPE
			|| uriType != B_STRING_TYPE || indexCount <= 0 || indexCount != uriCount
			|| message.FindString(MessageFields::SourceSnapshot, &snapshot) != B_OK)
		return -1;
	return indexCount;
}

inline bool
ReadDragSelection(const BMessage& message, DragItem& item)
{
	int32 count = DragSelectionCount(message);
	if (count == 0)
		return true;
	if (count < 0 || item.intent != DropIntent::Reorder || item.sourcePlaylist.empty())
		return false;
	int32 previous = -1;
	bool foundAnchor = false;
	for (int32 i = 0; i < count; i++) {
		int32 index = -1;
		const char* uri = nullptr;
		if (message.FindInt32(MessageFields::SourceIndices, i, &index) != B_OK
				|| message.FindString(MessageFields::SourceUris, i, &uri) != B_OK
				|| !uri || index <= previous || SpotifyItemIdForUri(uri).empty()
				|| !SpotifyItemCanAddToPlaylist(SpotifyItemKindForUri(uri)))
			return false;
		if (index == item.sourceIndex)
			foundAnchor = item.uri == uri;
		item.sourceIndices.push_back(index);
		item.sourceUris.emplace_back(uri);
		previous = index;
	}
	return foundAnchor;
}

inline bool
ReadDragFields(const BMessage& message, DragItem& item, std::string& type, int32& intent)
{
	return ReadItemUri(message, item.uri, true)
		&& ReadString(message, MessageFields::ItemType, type)
		&& ReadString(message, MessageFields::SourcePlaylist, item.sourcePlaylist)
		&& ReadString(message, MessageFields::SourceSnapshot, item.sourceSnapshot)
		&& ReadInt32(message, MessageFields::SourceIndex, item.sourceIndex)
		&& ReadInt32(message, MessageFields::DropIntent, intent);
}

inline bool
ReadDragItem(const BMessage& message, DragItem& result)
{
	if (message.what != MSG_DRAG_ITEM && message.what != MSG_PLAYLIST_DROP
			&& message.what != MSG_DISCOVER_DROP
			&& message.what != MSG_DISCOVER_DRAG_HOVER)
		return false;
	DragItem item;
	std::string type;
	int32 intent = 0;
	if (!ReadDragFields(message, item, type, intent))
		return false;
	item.kind = SpotifyItemKindForUri(item.uri);
	if (SpotifyItemIdForUri(item.uri).empty()
			|| (!type.empty() && type != SpotifyItemTypeName(item.kind)))
		return false;
	if (!ValidDragSource(item, intent))
		return false;
	item.intent = static_cast<DropIntent>(intent);
	if (!ReadDragSelection(message, item))
		return false;
	result = item;
	return true;
}

inline BMessage
MakeDragItem(const DragItem& item)
{
	BMessage message(MSG_DRAG_ITEM);
	message.AddString(MessageFields::Uri, item.uri.c_str());
	message.AddString(MessageFields::ItemType, SpotifyItemTypeName(item.kind));
	if (SpotifyItemCanAddToPlaylist(item.kind))
		message.AddString(MessageFields::TrackUri, item.uri.c_str());
	if (item.kind == kSpotifyItemAlbum)
		message.AddString(MessageFields::AlbumUri, item.uri.c_str());
	if (!item.sourcePlaylist.empty())
		message.AddString(MessageFields::SourcePlaylist, item.sourcePlaylist.c_str());
	if (item.sourceIndex >= 0)
		message.AddInt32(MessageFields::SourceIndex, item.sourceIndex);
	message.AddInt32(MessageFields::DropIntent, static_cast<int32>(item.intent));
	for (int32_t index : item.sourceIndices)
		message.AddInt32(MessageFields::SourceIndices, index);
	for (const std::string& uri : item.sourceUris)
		message.AddString(MessageFields::SourceUris, uri.c_str());
	if (!item.sourceIndices.empty())
		message.AddString(MessageFields::SourceSnapshot, item.sourceSnapshot.c_str());
	return message;
}

struct DiscoverDropTarget {
	int32 tab = -1;
	std::string uri;
	bool writable = false;
};

inline bool
ReadDiscoverDropTarget(const BMessage& message, DiscoverDropTarget& result)
{
	DiscoverDropTarget target;
	if (message.what != MSG_DISCOVER_DROP
			|| !ReadInt32(message, MessageFields::Tab, target.tab)
			|| target.tab < 0
			|| !ReadString(message, MessageFields::TargetUri, target.uri)
			|| !OptionalField(message, MessageFields::TargetWritable, B_BOOL_TYPE))
		return false;
	target.writable = message.GetBool(MessageFields::TargetWritable, false);
	result = target;
	return true;
}

struct PlayCommand {
	std::string uri;
	std::string contextUri = "";
	std::string deviceId = "";
	int32 startPositionMs = 0;
	std::vector<std::string> nextQueueUris = {};
};

inline bool
ValidPlaybackUri(const std::string& uri)
{
	return !SpotifyItemIdForUri(uri).empty()
		|| uri == "spotify:collection" || uri == "spotify:saved-episodes";
}

inline bool
ReadQueueUris(const BMessage& message, std::vector<std::string>& uris)
{
	type_code type = 0;
	int32 count = 0;
	status_t status = message.GetInfo(MessageFields::NextQueueUri, &type, &count);
	if (status == B_NAME_NOT_FOUND)
		return true;
	if (status != B_OK || type != B_STRING_TYPE)
		return false;
	for (int32 index = 0; index < count; index++) {
		const char* uri = nullptr;
		if (message.FindString(MessageFields::NextQueueUri, index, &uri) != B_OK
				|| !SpotifyItemIsPlayable(SpotifyItemKindForUri(uri))
				|| SpotifyItemIdForUri(uri).empty())
			return false;
		uris.emplace_back(uri);
	}
	return true;
}

inline bool
ReadPlayCommand(const BMessage& message, PlayCommand& result)
{
	if (message.what != MSG_PLAY_URI)
		return false;
	PlayCommand command;
	if (!ReadItemUri(message, command.uri)
			|| !ReadString(message, MessageFields::ContextUri, command.contextUri)
			|| !ReadString(message, MessageFields::DeviceId, command.deviceId)
			|| !ReadInt32(message, MessageFields::StartPositionMs, command.startPositionMs)
			|| !ReadQueueUris(message, command.nextQueueUris))
		return false;
	if (command.startPositionMs < 0 || !ValidPlaybackUri(command.uri))
		return false;
	result = command;
	return true;
}

inline BMessage
MakePlayCommand(const PlayCommand& command)
{
	BMessage message(MSG_PLAY_URI);
	message.AddString(MessageFields::Uri, command.uri.c_str());
	if (!command.contextUri.empty())
		message.AddString(MessageFields::ContextUri, command.contextUri.c_str());
	if (!command.deviceId.empty())
		message.AddString(MessageFields::DeviceId, command.deviceId.c_str());
	if (command.startPositionMs != 0)
		message.AddInt32(MessageFields::StartPositionMs, command.startPositionMs);
	for (const std::string& uri : command.nextQueueUris)
		message.AddString(MessageFields::NextQueueUri, uri.c_str());
	return message;
}

// Play and resume share device selection. Preserve the full command and replace
// the singleton, including an explicitly empty ID, instead of appending a value.
inline bool
SetPlaybackDevice(BMessage& message, const std::string& deviceId)
{
	if (deviceId.empty())
		return false;
	BMessage updated(message);
	updated.RemoveName(MessageFields::DeviceId);
	if (updated.AddString(MessageFields::DeviceId, deviceId.c_str()) != B_OK)
		return false;
	message = updated;
	return true;
}

struct QueueCommand { std::string uri; };

inline BMessage
MakeQueueCommand(const QueueCommand& command, uint32 what = MSG_QUEUE_ITEM)
{
	BMessage message(what);
	message.AddString(what == MSG_SEARCH_QUEUE_ITEM
		? MessageFields::Uri : MessageFields::TrackUri, command.uri.c_str());
	return message;
}

inline bool
ReadQueueCommand(const BMessage& message, QueueCommand& result)
{
	QueueCommand command;
	if ((message.what != MSG_QUEUE_ITEM && message.what != MSG_SEARCH_QUEUE_ITEM)
			|| !ReadItemUri(message, command.uri)
			|| !SpotifyItemIsPlayable(SpotifyItemKindForUri(command.uri))
			|| SpotifyItemIdForUri(command.uri).empty())
		return false;
	result = command;
	return true;
}

struct CurrentTrackUpdate { std::string uri; };

inline BMessage
MakeCurrentTrackUpdate(const CurrentTrackUpdate& update)
{
	BMessage message(MSG_CURRENT_TRACK_UPDATE);
	message.AddString(MessageFields::TrackUri, update.uri.c_str());
	return message;
}

inline bool
ReadCurrentTrackUpdate(const BMessage& message, CurrentTrackUpdate& result)
{
	CurrentTrackUpdate update;
	if (message.what != MSG_CURRENT_TRACK_UPDATE
			|| !message.HasString(MessageFields::TrackUri)
			|| !ReadString(message, MessageFields::TrackUri, update.uri))
		return false;
	result = update;
	return true;
}

enum class DevicePromptAction { Selected, StartLocal, Closed };

struct DevicePromptResult {
	DevicePromptAction action = DevicePromptAction::Closed;
	std::string deviceId = "";
	bool cancelled = false;
};

inline BMessage
MakeDevicePromptResult(const DevicePromptResult& result)
{
	if (result.action == DevicePromptAction::StartLocal)
		return BMessage(MSG_PLAYBACK_DEVICE_START_LOCAL);
	if (result.action == DevicePromptAction::Selected) {
		BMessage message(MSG_PLAYBACK_DEVICE_SELECTED);
		message.AddString(MessageFields::DeviceId, result.deviceId.c_str());
		return message;
	}
	BMessage message(MSG_PLAYBACK_DEVICE_PROMPT_CLOSED);
	message.AddBool(MessageFields::Cancelled, result.cancelled);
	return message;
}

inline bool
ReadDevicePromptResult(const BMessage& message, DevicePromptResult& result)
{
	DevicePromptResult value;
	switch (message.what) {
		case MSG_PLAYBACK_DEVICE_SELECTED:
			value.action = DevicePromptAction::Selected;
			if (!ReadString(message, MessageFields::DeviceId, value.deviceId)
					|| value.deviceId.empty())
				return false;
			break;
		case MSG_PLAYBACK_DEVICE_START_LOCAL:
			value.action = DevicePromptAction::StartLocal;
			break;
		case MSG_PLAYBACK_DEVICE_PROMPT_CLOSED:
			if (!OptionalField(message, MessageFields::Cancelled, B_BOOL_TYPE)
					|| message.FindBool(MessageFields::Cancelled, &value.cancelled) != B_OK)
				return false;
			break;
		default:
			return false;
	}
	result = value;
	return true;
}

} // namespace MessageContracts
