#include "messages/PlaybackStateMessages.h"
#include "messages/Messages.h"
#include "playback/NowPlayingFields.h"
#include <Message.h>

namespace PlaybackStateMessages {

PlaybackMessageData
ReadUpdate(const BMessage& message)
{
	PlaybackMessageData update;
	update.isPlaying = message.GetBool("is_playing", false);
	update.progressMs = message.GetInt32("progress_ms", 0);
	update.durationMs = message.GetInt32("duration_ms", 0);
	update.volumePct = message.GetInt32("volume_percent", -1);
	update.optimistic = message.GetBool("optimistic", false);
	update.preserveCurrentArtwork = message.GetBool(
		"preserve_current_artwork", false);
	update.volumeAuthoritative = message.GetBool("volume_authoritative",
		true);
	bool hasItem = true;
	update.knownItemState = message.FindBool("has_item", &hasItem) == B_OK;
	// FindBool may overwrite its output on failure; keep the unknown default.
	if (update.knownItemState)
		update.hasItem = hasItem;
	update.trackUri = message.GetString("track_uri", "");
	update.repeatState = message.GetString("repeat_state", "off");
	update.shuffleState = message.GetBool("shuffle_state", false);
	update.metadata.title.value = message.GetString(MessageFields::Title, "");
	update.metadata.artist.value = message.GetString(MessageFields::Artist, "");
	update.metadata.albumId.value = message.GetString("album_id", "");
	update.metadata.artistId.value = message.GetString("artist_id", "");
	update.metadata.itemKind.value = message.GetString(kNowPlayingItemKindField, "");
	update.metadata.openUri.value = message.GetString(
		kNowPlayingPrimaryOpenUriField, "");
	update.metadata.parentUri.value = message.GetString(
		kNowPlayingParentUriField, "");
	update.metadata.parentKind.value = message.GetString(
		kNowPlayingParentKindField, "");
	update.metadata.showId.value = message.GetString(kNowPlayingShowIdField, "");
	update.metadata.audiobookId.value = message.GetString(
		kNowPlayingAudiobookIdField, "");
	update.metadata.artworkUrl.value = message.GetString("artwork_url", "");
	update.deviceId = message.GetString(MessageFields::DeviceId, "");
	update.deviceName = message.GetString("device_name", "");
	update.deviceType = message.GetString("device_type", "");
	return update;
}


BMessage
MakeReplicantState(const ReplicantPlaybackState& state)
{
	BMessage stateMsg(MSG_REPLICANT_STATE);
	stateMsg.AddBool("is_playing",    state.isPlaying);
	stateMsg.AddInt32("progress_ms",  state.progressMs);
	stateMsg.AddInt32("duration_ms",  state.durationMs);
	stateMsg.AddString("title",        state.metadata.title.value.c_str());
	stateMsg.AddString("artist",       state.metadata.artist.value.c_str());
	stateMsg.AddString("album_id",     state.metadata.albumId.value.c_str());
	stateMsg.AddString("artist_id",    state.metadata.artistId.value.c_str());
	stateMsg.AddString("track_uri",    state.trackUri.c_str());
	stateMsg.AddString(kNowPlayingItemKindField, state.metadata.itemKind.value.c_str());
	stateMsg.AddString(kNowPlayingPrimaryOpenUriField,
		state.metadata.openUri.value.c_str());
	stateMsg.AddString(kNowPlayingParentUriField, state.metadata.parentUri.value.c_str());
	stateMsg.AddString(kNowPlayingParentKindField, state.metadata.parentKind.value.c_str());
	stateMsg.AddString(kNowPlayingShowIdField, state.metadata.showId.value.c_str());
	stateMsg.AddString(kNowPlayingAudiobookIdField,
		state.metadata.audiobookId.value.c_str());
	stateMsg.AddString("repeat_state", state.repeatState.c_str());
	stateMsg.AddBool("shuffle_state",  state.shuffleState);
	if (state.volumePct >= 0)
		stateMsg.AddInt32("volume_percent", state.volumePct);
	stateMsg.AddString("artwork_url", state.metadata.artworkUrl.value.c_str());
	return stateMsg;
}


ReplicantPlaybackState
ReadReplicantState(const BMessage& message)
{
	PlaybackMessageData update = ReadUpdate(message);
	ReplicantPlaybackState state{update.metadata, update.trackUri,
		update.isPlaying, update.progressMs, update.durationMs, update.volumePct,
		update.repeatState, update.shuffleState};
	if (state.metadata.openUri.value.empty()) {
		state.metadata.openUri = {state.trackUri,
			PlaybackMetadataSource::ItemUriFallback};
	}
	return state;
}

}
