#pragma once

#include <cstdint>
#include <optional>

struct PlaybackPosition {
	bool isPlaying = false;
	int32_t progressMs = 0;
	int32_t durationMs = 0;
};

struct PlaybackSeekReport {
	int32_t progressMs = 0;
	bool verifyNeeded = false;
};

// Owned by the player looper; all times are supplied in monotonic microseconds.
// No clock, messages, timers or transport. Filter reports before Store; local
// position events and optimistic metadata retain their existing bypass paths.
class PlaybackTimeline {
public:
	bool IsPlaying() const { return fPosition.isPlaying; }
	int32_t ProgressMs() const { return fPosition.progressMs; }
	int32_t DurationMs() const { return fPosition.durationMs; }
	int64_t ElapsedSinceSyncMs(int64_t nowUs) const;
	int32_t EstimatedProgressMs(int64_t nowUs) const;
	std::optional<int32_t> RemainingMs(int64_t nowUs) const;
	void Store(const PlaybackPosition& position, int64_t nowUs);
	// Preserve the stored position when toggling; do not accrue elapsed time.
	void SetPlaying(bool playing, int64_t nowUs);
	void SeekOptimistic(int32_t progressMs, int64_t nowUs);
	PlaybackSeekReport FilterReport(const PlaybackPosition& report,
		bool optimistic, bool trackChanged, int64_t nowUs);

private:
	PlaybackPosition fPosition;
	int64_t fLastSyncUs = 0;
	int32_t fSeekTargetMs = 0;
	int64_t fSeekGuardUntilUs = 0;
};
