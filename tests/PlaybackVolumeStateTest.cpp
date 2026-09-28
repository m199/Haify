#include "playback/PlaybackVolumeState.h"

#include <cstdio>
#include <cstdlib>
#include <limits>

#ifdef NDEBUG
#error Playback volume tests require assertions; compile without NDEBUG.
#endif

static void
CheckCondition(bool condition, const char* expression, const char* function, int line)
{
	if (condition)
		return;
	std::fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, line, function, expression);
	std::abort();
}

#define Check(condition) CheckCondition((condition), #condition, __func__, __LINE__)

using Decision = PlaybackVolumeReportDecision;

static void
CheckInitialStateAndClamping()
{
	PlaybackVolumeState state;
	Check(state.Percent() == -1);
	Check(!state.RestoreMuted(100));
	state.Store(-1);
	Check(state.Percent() == -1);
	Check(state.FilterReport(67, 100) == Decision::Accept);
	state.Store(67);
	state.Store(-1);
	Check(state.Percent() == 67);
	auto command = state.SetOptimistic(std::numeric_limits<int>::max(), 200);
	Check(command.percent == 100 && command.deviceId.empty());
	Check(state.Percent() == 100);
	command = state.SetOptimistic(std::numeric_limits<int>::min(), 300);
	Check(command.percent == 0 && state.Percent() == 0);
	auto restore = state.RestoreMuted(400);
	Check(restore && restore->percent == 100 && restore->deviceId.empty());
	Check(state.Percent() == 100);
}

static void
CheckGuardOrderingAndExpiry()
{
	PlaybackVolumeState state;
	state.SetOptimistic(70, 1000000);
	Check(state.FilterReport(20, 1000001) == Decision::Defer);
	Check(state.Percent() == 70);
	Check(state.FilterReport(69, 2000000) == Decision::Accept);
	state.Store(69);
	Check(state.FilterReport(71, 2000001) == Decision::Accept);
	Check(state.FilterReport(72, 2000002) == Decision::Defer);
	Check(state.FilterReport(68, 2000003) == Decision::Defer);
	// A near-target confirmation does not permit a later stale poll through.
	Check(state.FilterReport(20, 5999999) == Decision::Defer);
	Check(state.FilterReport(20, 6000000) == Decision::Accept);
	state.Store(20);
	Check(state.Percent() == 20);
	Check(state.FilterReport(40, 6000001) == Decision::Accept);
}

static void
CheckNewCommandReplacesGuard()
{
	PlaybackVolumeState state;
	state.SetOptimistic(80, 1000000);
	state.SetOptimistic(30, 4000000);
	Check(state.FilterReport(80, 4000001) == Decision::Defer);
	Check(state.FilterReport(30, 6000000) == Decision::Accept);
	Check(state.FilterReport(80, 8999999) == Decision::Defer);
	Check(state.FilterReport(80, 9000000) == Decision::Accept);
	state.SetOptimistic(0, 10000000);
	Check(state.FilterReport(1, 10000001) == Decision::Accept);
	Check(state.FilterReport(2, 10000002) == Decision::Defer);
	state.SetOptimistic(100, 11000000);
	Check(state.FilterReport(99, 11000001) == Decision::Accept);
	Check(state.FilterReport(98, 11000002) == Decision::Defer);
}

static void
CheckLocalMuteAndDeviceRouting()
{
	PlaybackVolumeState state;
	PlaybackVolumeDevice device{37, true, false, "remote-device"};
	auto mute = state.ToggleMute(device, 1000000);
	Check(mute && mute->percent == 0 && mute->deviceId == device.id);
	Check(state.Percent() == 0);
	Check(state.FilterReport(37, 1000001) == Decision::Defer);
	Check(state.FilterReport(0, 1000002) == Decision::Accept);
	state.Store(0);
	auto restore = state.RestoreMuted(2000000);
	Check(restore && restore->percent == 37 && restore->deviceId == device.id);
	Check(state.Percent() == 37 && !state.RestoreMuted(2000001));
	Check(state.FilterReport(0, 2000002) == Decision::Defer);
	// Slider commands still target the active device, even after a mute lookup.
	auto slider = state.SetOptimistic(22, 3000000);
	Check(slider.percent == 22 && slider.deviceId.empty());
	state.SetOptimistic(0, 4000000);
	restore = state.RestoreMuted(5000000);
	Check(restore && restore->percent == 22 && restore->deviceId == device.id);
}

static void
CheckExternalMuteAndDefaultRestore()
{
	PlaybackVolumeState state;
	state.SetOptimistic(0, 100);
	Check(!state.RestoreMuted(200));
	auto restore = state.ToggleMute({0, true, false, "external"}, 300);
	Check(restore && restore->percent == 50 && restore->deviceId == "external");
	state.Store(64);
	state.Store(0);
	Check(!state.RestoreMuted(400));
	restore = state.ToggleMute({0, true, false, "another-device"}, 500);
	Check(restore && restore->percent == 64);
	Check(restore->deviceId == "another-device");
	state.SetOptimistic(0, 1000000);
	Check(state.FilterReport(28, 6000000) == Decision::Accept);
	state.Store(28);
	Check(!state.RestoreMuted(6000001));
}

static void
CheckRejectedDeviceKeepsState()
{
	for (const PlaybackVolumeDevice& invalid : {
			PlaybackVolumeDevice{90, false, false, "unsupported"},
			PlaybackVolumeDevice{90, true, true, "restricted"},
			PlaybackVolumeDevice{-1, true, false, "missing-volume"}}) {
		PlaybackVolumeState state;
		Check(state.ToggleMute({42, true, false, "original"}, 1000000).has_value());
		Check(!state.ToggleMute(invalid, 2000000));
		Check(state.Percent() == 0);
		// Rejection must neither extend the old guard nor overwrite its device.
		Check(state.FilterReport(90, 6000000) == Decision::Accept);
		auto restore = state.RestoreMuted(7000000);
		Check(restore && restore->percent == 42 && restore->deviceId == "original");
	}
	PlaybackVolumeState state;
	Check(state.ToggleMute({140, true, false, "loud"}, 100).has_value());
	auto restore = state.RestoreMuted(200);
	Check(restore && restore->percent == 100);
}

int
main()
{
	CheckInitialStateAndClamping();
	CheckGuardOrderingAndExpiry();
	CheckNewCommandReplacesGuard();
	CheckLocalMuteAndDeviceRouting();
	CheckExternalMuteAndDefaultRestore();
	CheckRejectedDeviceKeepsState();
	std::puts("Playback volume state tests passed");
	return 0;
}
