#pragma once

#include <cstdint>
#include <optional>
#include <string>

// An optimistic intent, not confirmation that the remote device accepted it.
// Empty deviceId keeps the existing active-device routing for slider changes.
struct PlaybackVolumeCommand {
	int percent = 0;
	std::string deviceId;
};

struct PlaybackVolumeDevice {
	int percent = -1;
	bool supportsVolume = true;
	bool restricted = false;
	std::string id;
};

enum class PlaybackVolumeReportDecision {
	Accept,
	Defer
};

// Owned by the player looper. Time is supplied in monotonic microseconds;
// this state owns no UI, timers, messages, transport or persistent settings.
class PlaybackVolumeState {
public:
	int Percent() const { return fPercent; }
	PlaybackVolumeCommand SetOptimistic(int percent, int64_t nowUs);
	std::optional<PlaybackVolumeCommand> RestoreMuted(int64_t nowUs);
	std::optional<PlaybackVolumeCommand> ToggleMute(
		const PlaybackVolumeDevice& device, int64_t nowUs);
	PlaybackVolumeReportDecision FilterReport(int percent, int64_t nowUs);
	// Store only after filtering authoritative reports. Negative means absent;
	// optimistic/non-authoritative metadata continues to bypass the guard.
	void Store(int percent);

private:
	int fPercent = -1;
	int fLastNonZero = 50;
	bool fHasLastNonZero = false;
	bool fMutedByHaify = false;
	int fTarget = -1;
	int64_t fGuardUntilUs = 0;
	std::string fMuteDeviceId;
};
