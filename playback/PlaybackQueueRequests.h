#pragma once

#include <Messenger.h>

#include <string>

class PlaybackApi;

// Adds one playable item to the Spotify queue. On success it posts
// MSG_PLAYBACK_QUEUE_CHANGED to `application`, so an open queue window can
// reload at once instead of polling the queue on a short timer.
void	AddToPlaybackQueue(PlaybackApi& api, const std::string& uri,
			BMessenger application);
