#pragma once

#include "playback/PlaybackMetadata.h"
#include "playback/PlaybackStartPolicy.h"

#include <optional>

// Owned by the player looper, independently of optimistic display state.
// A chapter URI alone (Spotify reports it as episode) cannot identify a book.
// Only an explicit audiobook command supplies this provenance and queue.
class AudiobookPlaybackContext {
public:
	void Begin(const PlaybackCommand& command)
	{
		Clear();
		if (!PlaybackTargetsAudiobookQueue(command))
			return;
		fChapterUri = command.uri;
		fBookUri = command.primaryOpenUri;
		fDeviceId = command.deviceId;
		fNextUris = command.nextQueueUris;
		fPending = true;
	}

	// Call after filtering stale reports and resolving generic metadata. Apply
	// explicit provenance last; waiting for confirmation must not lose the queue.
	void Apply(const std::string& itemUri, bool optimistic, PlaybackMetadata& metadata)
	{
		if (fChapterUri.empty())
			return;
		if (itemUri != fChapterUri) {
			if (!fPending && !optimistic)
				Clear();
			return;
		}
		if (!optimistic)
			fPending = false;
		using Source = PlaybackMetadataSource;
		metadata.itemKind = {"chapter", Source::PlaybackCommand};
		metadata.parentKind = {"audiobook", Source::PlaybackCommand};
		metadata.showId = {{}, Source::ClearedForItem};
		if (SpotifyItemKindForUri(fBookUri) == kSpotifyItemAudiobook) {
			metadata.openUri = {fBookUri, Source::PlaybackCommand};
			metadata.parentUri = metadata.openUri;
			metadata.audiobookId = {SpotifyItemIdForUri(fBookUri), Source::PlaybackCommand};
		}
	}

	std::optional<PlaybackCommand> TakeNext(const std::string& currentUri,
		const std::string& deviceId)
	{
		if (fChapterUri.empty() || currentUri != fChapterUri || fNextUris.empty())
			return std::nullopt;
		PlaybackCommand command{fNextUris.front()};
		fNextUris.erase(fNextUris.begin());
		command.parentKind = "audiobook";
		command.primaryOpenUri = fBookUri;
		command.deviceId = deviceId.empty() ? fDeviceId : deviceId;
		fChapterUri = command.uri;
		fPending = true;
		return command;
	}

private:
	void Clear()
	{
		fChapterUri.clear();
		fBookUri.clear();
		fDeviceId.clear();
		fNextUris.clear();
		fPending = false;
	}

	std::string fChapterUri;
	std::string fBookUri;
	std::string fDeviceId;
	std::vector<std::string> fNextUris;
	bool fPending = false;
};
