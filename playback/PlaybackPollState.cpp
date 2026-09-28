#include "playback/PlaybackPollState.h"

#include <algorithm>

namespace {
constexpr int64_t kStartupIntervalUs = 1000000LL;
constexpr int64_t kStartupErrorLimitUs = 5000000LL;
constexpr int64_t kStartupEmptyRetryLimitUs = 30000000LL;
constexpr int64_t kActiveIntervalUs = 5000000LL;
constexpr int64_t kIdleIntervalUs = 15000000LL;
constexpr int64_t kErrorLimitUs = 30000000LL;
}

void
PlaybackPollState::Start(int64_t nowUs)
{
	fStartupEmptyRetryUntilUs = nowUs + kStartupEmptyRetryLimitUs;
}


bool
PlaybackPollState::BeginRequest()
{
	if (fRequestPending)
		return false;
	fRequestPending = true;
	return true;
}


int64_t
PlaybackPollState::_FailureDelay(int32_t retryAfterSeconds)
{
	// Only six backoff levels exist. Saturation avoids overflow on long outages.
	fFailures = std::min(fFailures + 1, int32_t(6));
	if (retryAfterSeconds > 0)
		return int64_t(retryAfterSeconds) * 1000000LL;
	int64_t delay = kStartupIntervalUs << (fFailures - 1);
	int64_t limit = fHasPlaybackState ? kErrorLimitUs : kStartupErrorLimitUs;
	return std::min(delay, limit);
}


PlaybackPollDecision
PlaybackPollState::Complete(const PlaybackPollReport& report, int64_t nowUs)
{
	fRequestPending = false;
	if (!report.ok)
		return {PlaybackPollAction::RetryOnly, _FailureDelay(report.retryAfterSeconds)};
	fFailures = 0;
	if (!report.hasItem && !fHasPlaybackState && nowUs < fStartupEmptyRetryUntilUs)
		return {PlaybackPollAction::RetryOnly, kStartupIntervalUs};
	if (report.hasItem) {
		fHasPlaybackState = true;
		fStartupEmptyRetryUntilUs = 0;
	}
	auto action = report.hasItem
		? PlaybackPollAction::ApplyPlayback : PlaybackPollAction::ApplyEmpty;
	return {action, report.isPlaying ? kActiveIntervalUs : kIdleIntervalUs};
}
