#include "playback/PlaybackTimeline.h"

#include <algorithm>

namespace {
constexpr int64_t kSeekGuardUs = 5000000LL;
constexpr int32_t kSeekToleranceMs = 2000;
}

int64_t
PlaybackTimeline::ElapsedSinceSyncMs(int64_t nowUs) const
{
	return fLastSyncUs > 0 ? (nowUs - fLastSyncUs) / 1000LL : 0;
}


int32_t
PlaybackTimeline::EstimatedProgressMs(int64_t nowUs) const
{
	int64_t progress = fPosition.progressMs;
	if (fPosition.isPlaying && fLastSyncUs > 0) {
		progress += std::max(ElapsedSinceSyncMs(nowUs), int64_t(0));
		if (fPosition.durationMs > 0)
			progress = std::min(progress, int64_t(fPosition.durationMs));
	}
	return static_cast<int32_t>(progress);
}


std::optional<int32_t>
PlaybackTimeline::RemainingMs(int64_t nowUs) const
{
	if (!fPosition.isPlaying || fPosition.durationMs <= 0 || fLastSyncUs <= 0)
		return std::nullopt;
	int64_t estimated = int64_t(fPosition.progressMs) + ElapsedSinceSyncMs(nowUs);
	estimated = std::min(estimated, int64_t(fPosition.durationMs));
	return fPosition.durationMs - static_cast<int32_t>(estimated);
}


void
PlaybackTimeline::Store(const PlaybackPosition& position, int64_t nowUs)
{
	fPosition = position;
	fLastSyncUs = nowUs;
}


void
PlaybackTimeline::SetPlaying(bool playing, int64_t nowUs)
{
	fPosition.isPlaying = playing;
	fLastSyncUs = nowUs;
}


void
PlaybackTimeline::SeekOptimistic(int32_t progressMs, int64_t nowUs)
{
	fPosition.progressMs = progressMs;
	fSeekTargetMs = progressMs;
	fSeekGuardUntilUs = nowUs + kSeekGuardUs;
	fLastSyncUs = nowUs;
}


PlaybackSeekReport
PlaybackTimeline::FilterReport(const PlaybackPosition& report,
	bool optimistic, bool trackChanged, int64_t nowUs)
{
	if (trackChanged) {
		fSeekGuardUntilUs = 0;
		fSeekTargetMs = 0;
		return {report.progressMs, false};
	}
	if (optimistic || fSeekGuardUntilUs <= 0)
		return {report.progressMs, false};
	if (nowUs >= fSeekGuardUntilUs) {
		fSeekGuardUntilUs = 0;
		return {report.progressMs, false};
	}
	int64_t estimated = fSeekTargetMs;
	if (report.isPlaying)
		estimated += ElapsedSinceSyncMs(nowUs);
	if (report.durationMs > 0)
		estimated = std::min(estimated, int64_t(report.durationMs));
	estimated = std::max(estimated, int64_t(0));
	int64_t delta = int64_t(report.progressMs) - estimated;
	if (delta < 0)
		delta = -delta;
	if (delta <= kSeekToleranceMs) {
		fSeekGuardUntilUs = 0;
		return {report.progressMs, false};
	}
	fSeekTargetMs = static_cast<int32_t>(estimated);
	return {fSeekTargetMs, true};
}
