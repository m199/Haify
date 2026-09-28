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
	// An own command or a local librespot event: playback is about to change.
	// Ends the long-idle backoff and allows two quick follow-up polls while
	// Spotify does not report playback yet (e.g. during a device transfer).
	void NoteActivity();
	// Also accepts a failed report when no API client exists and no request began.
	PlaybackPollDecision Complete(const PlaybackPollReport& report, int64_t nowUs);

private:
	int64_t _FailureDelay(int32_t retryAfterSeconds);
	// Playing: 15 s. Paused/empty: 3 s for the follow-ups after an activity,
	// otherwise 15 s, and 60 s after two idle minutes.
	int64_t _SuccessDelay(bool isPlaying, int64_t nowUs);
	bool fRequestPending = false;
	int64_t fIdleSinceUs = 0;
	int32_t fFollowUpPolls = 0;
	bool fHasPlaybackState = false;
	int64_t fStartupEmptyRetryUntilUs = 0;
	int32_t fFailures = 0;
};
