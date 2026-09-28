#include "playback/PlaybackVolumeState.h"

#include <algorithm>

namespace {

constexpr int64_t kVolumeGuardUs = 5000000;
constexpr int kDefaultRestoreVolume = 50;

} // namespace

PlaybackVolumeCommand
PlaybackVolumeState::SetOptimistic(int percent, int64_t nowUs)
{
	Store(std::clamp(percent, 0, 100));
	fTarget = fPercent;
	fGuardUntilUs = nowUs + kVolumeGuardUs;
	fMutedByHaify = fPercent == 0;
	return {fPercent, {}};
}

std::optional<PlaybackVolumeCommand>
PlaybackVolumeState::RestoreMuted(int64_t nowUs)
{
	if (!fMutedByHaify || fPercent != 0 || !fHasLastNonZero)
		return std::nullopt;
	int target = fLastNonZero <= 0 ? kDefaultRestoreVolume : fLastNonZero;
	auto command = SetOptimistic(target, nowUs);
	command.deviceId = fMuteDeviceId;
	return command;
}

std::optional<PlaybackVolumeCommand>
PlaybackVolumeState::ToggleMute(const PlaybackVolumeDevice& device, int64_t nowUs)
{
	if (!device.supportsVolume || device.restricted || device.percent < 0)
		return std::nullopt;
	fMuteDeviceId = device.id;
	int target = 0;
	if (device.percent > 0) {
		fLastNonZero = std::min(device.percent, 100);
		fHasLastNonZero = true;
	} else {
		target = fHasLastNonZero ? fLastNonZero : kDefaultRestoreVolume;
	}
	auto command = SetOptimistic(target, nowUs);
	command.deviceId = fMuteDeviceId;
	return command;
}

PlaybackVolumeReportDecision
PlaybackVolumeState::FilterReport(int percent, int64_t nowUs)
{
	if (fGuardUntilUs <= 0)
		return PlaybackVolumeReportDecision::Accept;
	if (nowUs >= fGuardUntilUs) {
		fGuardUntilUs = 0;
		fTarget = -1;
		return PlaybackVolumeReportDecision::Accept;
	}
	int64_t difference = static_cast<int64_t>(percent) - fTarget;
	if (difference < 0)
		difference = -difference;
	// A matching report does not end the guard: older polls can still arrive.
	return difference <= 1 ? PlaybackVolumeReportDecision::Accept
		: PlaybackVolumeReportDecision::Defer;
}

void
PlaybackVolumeState::Store(int percent)
{
	if (percent >= 0)
		fPercent = percent;
	if (fPercent > 0) {
		fLastNonZero = fPercent;
		fHasLastNonZero = true;
		fMutedByHaify = false;
	}
}
