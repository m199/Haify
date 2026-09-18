#pragma once

#include "spotify/SpotifyUri.h"

#include <cstdint>
#include <string>
#include <vector>

// The resolved device and audiobook provenance travel with the command.
// Display metadata stays in the original UI message.
struct PlaybackCommand {
	std::string uri;
	std::string contextUri = "";
	std::string deviceId = "";
	int32_t startPositionMs = 0;
	std::vector<std::string> nextQueueUris = {};
	std::string parentKind = "";
	std::string primaryOpenUri = "";
};

inline bool
ValidPlaybackUri(const std::string& uri)
{
	return !SpotifyItemIdForUri(uri).empty()
		|| uri == "spotify:collection" || uri == "spotify:saved-episodes";
}

inline bool
ValidPlaybackCommand(const PlaybackCommand& command)
{
	if (command.startPositionMs < 0 || !ValidPlaybackUri(command.uri))
		return false;
	for (const std::string& uri : command.nextQueueUris) {
		if (!SpotifyItemIsPlayable(SpotifyItemKindForUri(uri))
				|| SpotifyItemIdForUri(uri).empty())
			return false;
	}
	return true;
}
