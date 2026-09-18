#pragma once

#include "PlaybackCommand.h"

#include <functional>

class PlaybackApi;

struct PlaybackStartStep {
	bool attempted = false;
	bool accepted = false;
	int status = -1;
	int retryAfter = -1;
};

struct PlaybackStartResult {
	std::string uri;
	std::string deviceId;
	bool usedSingleItemFallback = false;
	PlaybackStartStep disableShuffle;
	PlaybackStartStep play;
	PlaybackStartStep restoreShuffle;
};

using PlaybackStartCompletion = std::function<void(const PlaybackStartResult&)>;

// False means invalid command and no side effects/callback. Once dispatched,
// completion runs after play and any shuffle restoration, on the API callback
// thread. Accepted means the server accepted the request, not audible playback.
// Requests own command/result copies; api must outlive their callbacks.
bool DispatchPlaybackStart(PlaybackApi& api, const PlaybackCommand& command,
	bool shuffleOn, PlaybackStartCompletion complete);
