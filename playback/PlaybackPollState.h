#pragma once

#include <cstdint>

// Scheduling projection of the existing API result. It does not classify or
// replace transport errors; 404 fallback selection stays with the API adapter.
struct PlaybackPollReport {
	bool ok = false;
	bool hasItem = false;
	bool isPlaying = false;
	int32_t retryAfterSeconds = -1;
};

enum class PlaybackPollAction {
	RetryOnly,
	ApplyEmpty,
	ApplyPlayback
};

struct PlaybackPollDecision {
	PlaybackPollAction action = PlaybackPollAction::RetryOnly;
	int64_t delayUs = 0;
};

// One player-looper owner. The window dispatches requests, delivers each final
// response once, applies accepted messages, then schedules the returned delay.
// No timers, callbacks, messages, transport or persistence are owned here.
class PlaybackPollState {
public:
	// Called once during player setup, before the first poll.
	void Start(int64_t nowUs);
	bool RequestPending() const { return fRequestPending; }
	bool BeginRequest();
	// Also accepts a failed report when no API client exists and no request began.
	PlaybackPollDecision Complete(const PlaybackPollReport& report, int64_t nowUs);

private:
	int64_t _FailureDelay(int32_t retryAfterSeconds);
	bool fRequestPending = false;
	bool fHasPlaybackState = false;
	int64_t fStartupEmptyRetryUntilUs = 0;
	int32_t fFailures = 0;
};
