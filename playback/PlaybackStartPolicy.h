#pragma once

#include "PlaybackCommand.h"

#include <algorithm>

inline bool
PlaybackTargetsAudiobookQueue(const PlaybackCommand& command)
{
	// Context alone never identified chapters in the existing start workflow.
	return command.parentKind == "audiobook"
		|| SpotifyItemKindForUri(command.primaryOpenUri) == kSpotifyItemAudiobook;
}

inline bool
PlaybackStartCyclesShuffle(const PlaybackCommand& command, bool shuffleOn)
{
	return shuffleOn && !PlaybackTargetsAudiobookQueue(command)
		&& SpotifyItemKindForUri(command.uri) == kSpotifyItemTrack
		&& SpotifyItemKindForUri(command.contextUri) == kSpotifyItemPlaylist;
}

inline std::vector<std::string>
PlaybackStartUriBatch(const PlaybackCommand& command)
{
	// Preserve order and duplicates; the selected item counts towards the cap.
	const size_t following = std::min(command.nextQueueUris.size(), size_t(99));
	std::vector<std::string> uris;
	uris.reserve(following + 1);
	uris.push_back(command.uri);
	uris.insert(uris.end(), command.nextQueueUris.begin(),
		command.nextQueueUris.begin() + following);
	return uris;
}
