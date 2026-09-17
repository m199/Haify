#include "MessageContracts.h"
#include "NowPlayingFields.h"
#include "UiLogic.h"

#include <cassert>
#include <cstdio>

#ifdef NDEBUG
#error Message contract tests require assertions; compile without NDEBUG.
#endif

using namespace MessageContracts;

static void
TestDragCompatibility()
{
	DragItem source{"spotify:track:one", kSpotifyItemTrack,
		"spotify:playlist:source", 4};
	BMessage message = MakeDragItem(source);
	DragItem result;
	assert(ReadDragItem(message, result));
	assert(result.uri == source.uri && result.kind == source.kind);
	assert(result.sourcePlaylist == source.sourcePlaylist && result.sourceIndex == 4);
	message.RemoveName(MessageFields::Uri);
	message.RemoveName(MessageFields::ItemType);
	message.RemoveName(MessageFields::DropIntent);
	assert(ReadDragItem(message, result));
	assert(result.uri == source.uri && result.intent == DropIntent::Automatic);
	BMessage album(MSG_DRAG_ITEM);
	album.AddString(MessageFields::AlbumUri, "spotify:album:one");
	assert(ReadDragItem(album, result) && result.kind == kSpotifyItemAlbum);
}

static void
TestForwardedPlaylistDrop()
{
	DragItem source{"spotify:track:one", kSpotifyItemTrack,
		"spotify:playlist:source", 4};
	BMessage drag = MakeDragItem(source);
	drag.AddString(MessageFields::Title, "Track title");
	// DropFilter copies the original payload but routes it under a local code.
	BMessage drop(drag);
	drop.what = MSG_PLAYLIST_DROP;
	assert(drop.what == 'drpT' && drag.what == MSG_DRAG_ITEM);
	DragItem result;
	assert(ReadDragItem(drop, result));
	assert(result.uri == source.uri && result.kind == source.kind);
	assert(result.sourcePlaylist == source.sourcePlaylist && result.sourceIndex == 4);
	assert(result.intent == DropIntent::Automatic);
	assert(std::string(drop.GetString(MessageFields::Title, "")) == "Track title");
	assert(ResolvePlaylistDropAction(source.sourcePlaylist, result, true, false)
		== kPlaylistDropReorder);
	auto plan = ResolvePlaylistReorderPlan(result.sourceIndex, 1, 1, 8);
	assert(plan.shouldMove && plan.targetIndex == 1);
	assert(ResolvePlaylistDropAction("spotify:playlist:other", result, true, false)
		== kPlaylistDropAddPlayableItem);
	assert(ResolvePlaylistDropAction(source.sourcePlaylist, result, false, false)
		== kPlaylistDropIgnore);
	assert(ResolvePlaylistDropAction(source.sourcePlaylist, result, true, true)
		== kPlaylistDropIgnore);
	drop.ReplaceInt32(MessageFields::DropIntent, static_cast<int32>(DropIntent::Reorder));
	assert(ReadDragItem(drop, result) && result.intent == DropIntent::Reorder);
	assert(ResolvePlaylistDropAction("spotify:playlist:other", result, true, false)
		== kPlaylistDropIgnore);
}

static void
TestForwardedDragValidation()
{
	DragItem source{"spotify:track:one", kSpotifyItemTrack,
		"spotify:playlist:source", 4};
	for (uint32 code : {MSG_DRAG_ITEM, MSG_PLAYLIST_DROP,
		MSG_DISCOVER_DROP, MSG_DISCOVER_DRAG_HOVER}) {
		BMessage message = MakeDragItem(source);
		message.what = code;
		DragItem result;
		assert(ReadDragItem(message, result));
		message.RemoveName(MessageFields::SourceIndex);
		message.AddString(MessageFields::SourceIndex, "4");
		result.uri = "unchanged";
		assert(!ReadDragItem(message, result) && result.uri == "unchanged");
	}
	BMessage unrelated = MakeDragItem(source);
	unrelated.what = MSG_PLAY_URI;
	DragItem result{"unchanged"};
	assert(!ReadDragItem(unrelated, result) && result.uri == "unchanged");
}

static void
TestGroupDrag()
{
	DragItem source{"spotify:track:one", kSpotifyItemTrack,
		"spotify:playlist:source", 2, DropIntent::Reorder};
	source.sourceIndices = {0, 2, 5};
	source.sourceUris = {"spotify:track:one", "spotify:track:one", "spotify:episode:two"};
	source.sourceSnapshot = "before";
	BMessage message = MakeDragItem(source);
	message.what = MSG_PLAYLIST_DROP;
	DragItem result;
	assert(ReadDragItem(message, result));
	assert(result.sourceIndices == source.sourceIndices && result.sourceUris == source.sourceUris);
	assert(result.sourceSnapshot == "before" && result.sourceIndex == 2);
	assert(ResolvePlaylistDropAction(source.sourcePlaylist, result, true, false)
		== kPlaylistDropReorder);
	assert(ResolvePlaylistDropAction("spotify:playlist:other", result, true, false)
		== kPlaylistDropIgnore);
	assert(!DragItemCanCopy(result));
	for (const char* field : {MessageFields::SourceIndices, MessageFields::SourceUris,
		MessageFields::SourceSnapshot}) {
		BMessage invalid(message);
		invalid.RemoveName(field);
		result.uri = "unchanged";
		assert(!ReadDragItem(invalid, result) && result.uri == "unchanged");
		invalid.AddBool(field, true);
		assert(!ReadDragItem(invalid, result));
	}
	for (const std::vector<int32_t>& indices : std::vector<std::vector<int32_t>>{
		{-1, 2, 5}, {0, 2, 2}, {2, 0, 5}, {0, 3, 5}, {0, 2}}) {
		auto invalid = source;
		invalid.sourceIndices = indices;
		assert(!ReadDragItem(MakeDragItem(invalid), result));
	}
	auto invalid = source;
	invalid.sourceUris[1] = "spotify:track:other";
	assert(!ReadDragItem(MakeDragItem(invalid), result));
	invalid = source;
	invalid.intent = DropIntent::Automatic;
	assert(!ReadDragItem(MakeDragItem(invalid), result));
}

static void
TestMalformedDrag()
{
	DragItem result{"unchanged"};
	BMessage empty(MSG_DRAG_ITEM);
	assert(!ReadDragItem(empty, result) && result.uri == "unchanged");
	BMessage message = MakeDragItem({"spotify:track:one", kSpotifyItemTrack});
	message.ReplaceString(MessageFields::ItemType, "album");
	assert(!ReadDragItem(message, result));
	message.ReplaceString(MessageFields::ItemType, "track");
	message.ReplaceString(MessageFields::TrackUri, "spotify:track:other");
	assert(!ReadDragItem(message, result));
	message.ReplaceString(MessageFields::TrackUri, "spotify:track:one");
	message.AddInt32(MessageFields::SourceIndex, 0);
	assert(!ReadDragItem(message, result));
	message.RemoveName(MessageFields::SourceIndex);
	message.AddString(MessageFields::SourceIndex, "0");
	assert(!ReadDragItem(message, result));
	message.RemoveName(MessageFields::SourceIndex);
	message.ReplaceInt32(MessageFields::DropIntent, 99);
	assert(!ReadDragItem(message, result));
}

static void
TestDropIntent()
{
	DragItem item{"spotify:track:one", kSpotifyItemTrack,
		"spotify:playlist:source", 4};
	assert(ResolvePlaylistDropAction(item.sourcePlaylist, item, true, false)
		== kPlaylistDropReorder);
	assert(ResolvePlaylistDropAction("spotify:playlist:other", item, true, false)
		== kPlaylistDropAddPlayableItem);
	assert(ResolvePlaylistDropAction(item.sourcePlaylist, item, true, true)
		== kPlaylistDropIgnore);
	item.intent = DropIntent::Reorder;
	assert(!DragItemCanCopy(item));
	assert(ResolvePlaylistDropAction("spotify:playlist:other", item, true, false)
		== kPlaylistDropIgnore);
	item.intent = DropIntent::Add;
	assert(ResolvePlaylistDropAction(item.sourcePlaylist, item, true, false)
		== kPlaylistDropIgnore);
}

static void
TestDiscoverTarget()
{
	BMessage message(MSG_DISCOVER_DROP);
	DiscoverDropTarget target;
	assert(!ReadDiscoverDropTarget(message, target));
	message.AddInt32(MessageFields::Tab, 0);
	assert(ReadDiscoverDropTarget(message, target));
	assert(target.uri.empty() && !target.writable);
	message.AddString(MessageFields::TargetWritable, "true");
	assert(!ReadDiscoverDropTarget(message, target));
}

static void
TestPlayCommand()
{
	PlayCommand source{"spotify:episode:one", "spotify:show:parent", "device", 1234,
		{"spotify:episode:two", "spotify:episode:three"}};
	BMessage message = MakePlayCommand(source);
	message.AddString(kNowPlayingParentKindField, "audiobook");
	BMessage forwarded(message);
	PlayCommand result;
	assert(ReadPlayCommand(forwarded, result));
	assert(result.uri == source.uri && result.contextUri == source.contextUri);
	assert(result.deviceId == source.deviceId && result.startPositionMs == 1234);
	assert(result.nextQueueUris == source.nextQueueUris);
	assert(std::string(forwarded.GetString(kNowPlayingParentKindField, "")) == "audiobook");
	message.ReplaceInt32(MessageFields::StartPositionMs, -1);
	assert(!ReadPlayCommand(message, result) && result.startPositionMs == 1234);
	message.ReplaceInt32(MessageFields::StartPositionMs, 0);
	message.AddString(MessageFields::NextQueueUri, "spotify:album:invalid");
	assert(!ReadPlayCommand(message, result));
}

static void
TestPlaybackDeviceReplacement()
{
	BMessage message = MakePlayCommand({"spotify:track:one"});
	message.AddString(MessageFields::DeviceId, "");
	message.AddString(MessageFields::Title, "Keep title");
	PlayCommand result;
	assert(ReadPlayCommand(message, result));
	assert(SetPlaybackDevice(message, "active-device"));
	assert(ReadPlayCommand(message, result));
	assert(result.deviceId == "active-device");
	assert(std::string(message.GetString(MessageFields::Title, "")) == "Keep title");
	assert(SetPlaybackDevice(message, "selected-device"));
	assert(ReadPlayCommand(message, result));
	assert(result.deviceId == "selected-device");
	assert(!SetPlaybackDevice(message, ""));
	assert(ReadPlayCommand(message, result) && result.deviceId == "selected-device");
	BMessage resume(MSG_PLAY_PAUSE);
	assert(SetPlaybackDevice(resume, "resume-device"));
	assert(resume.what == MSG_PLAY_PAUSE);
	assert(std::string(resume.GetString(MessageFields::DeviceId, "")) == "resume-device");
}

static void
TestLegacyPlayAndQueue()
{
	BMessage legacy(MSG_PLAY_URI);
	legacy.AddString(MessageFields::TrackUri, "spotify:track:one");
	PlayCommand play;
	assert(ReadPlayCommand(legacy, play));
	assert(play.contextUri.empty() && play.startPositionMs == 0);
	legacy.AddString(MessageFields::TrackUri, "spotify:track:two");
	assert(!ReadPlayCommand(legacy, play));
	BMessage queue = MakeQueueCommand({"spotify:episode:one"});
	QueueCommand result;
	assert(ReadQueueCommand(queue, result) && result.uri == "spotify:episode:one");
	queue.ReplaceString(MessageFields::TrackUri, "spotify:album:one");
	assert(!ReadQueueCommand(queue, result));
	queue = MakeQueueCommand({"spotify:track:one"}, MSG_SEARCH_QUEUE_ITEM);
	assert(ReadQueueCommand(queue, result) && result.uri == "spotify:track:one");
}

static void
TestCurrentTrack()
{
	CurrentTrackUpdate update;
	BMessage missing(MSG_CURRENT_TRACK_UPDATE);
	assert(!ReadCurrentTrackUpdate(missing, update));
	BMessage message = MakeCurrentTrackUpdate({"spotify:track:one"});
	assert(ReadCurrentTrackUpdate(message, update) && update.uri == "spotify:track:one");
	message.ReplaceString(MessageFields::TrackUri, "");
	assert(ReadCurrentTrackUpdate(message, update) && update.uri.empty());
	message.AddString(MessageFields::TrackUri, "duplicate");
	assert(!ReadCurrentTrackUpdate(message, update));
}

static void
TestDevicePrompt()
{
	DevicePromptResult result;
	BMessage selected = MakeDevicePromptResult({DevicePromptAction::Selected, "device"});
	assert(ReadDevicePromptResult(selected, result) && result.deviceId == "device");
	selected.ReplaceString(MessageFields::DeviceId, "");
	assert(!ReadDevicePromptResult(selected, result));
	BMessage local = MakeDevicePromptResult({DevicePromptAction::StartLocal});
	assert(ReadDevicePromptResult(local, result) && result.action == DevicePromptAction::StartLocal);
	BMessage closed = MakeDevicePromptResult({DevicePromptAction::Closed, "", true});
	assert(ReadDevicePromptResult(closed, result) && result.cancelled);
	closed.ReplaceBool(MessageFields::Cancelled, false);
	assert(ReadDevicePromptResult(closed, result) && !result.cancelled);
	closed.RemoveName(MessageFields::Cancelled);
	assert(!ReadDevicePromptResult(closed, result));
}

static void
TestAccountNotifications()
{
	BMessage legacy(MSG_LIBRARY_CHANGED);
	assert(MatchesAccount(legacy, "a"));
	legacy.AddString(MessageFields::AccountId, "a");
	assert(MatchesAccount(legacy, "a"));
	assert(!MatchesAccount(legacy, "b"));
	legacy.AddString(MessageFields::AccountId, "a");
	assert(!MatchesAccount(legacy, "a"));
	legacy.RemoveName(MessageFields::AccountId);
	legacy.AddInt32(MessageFields::AccountId, 1);
	assert(!MatchesAccount(legacy, "a"));
}

int
main()
{
	TestDragCompatibility();
	TestForwardedPlaylistDrop();
	TestForwardedDragValidation();
	TestGroupDrag();
	TestMalformedDrag();
	TestDropIntent();
	TestDiscoverTarget();
	TestPlayCommand();
	TestPlaybackDeviceReplacement();
	TestLegacyPlayAndQueue();
	TestCurrentTrack();
	TestDevicePrompt();
	TestAccountNotifications();
	std::puts("Message contract tests passed.");
	return 0;
}
