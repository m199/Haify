#include "playback/PlaybackTimeline.h"

#include <cstdlib>
#include <cstdio>
#include <initializer_list>

#ifdef NDEBUG
#error Playback timeline tests require assertions; compile without NDEBUG.
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

static void
CheckPositionAndToggle()
{
	PlaybackTimeline state;
	Check(!state.IsPlaying() && state.ProgressMs() == 0 && state.DurationMs() == 0);
	Check(!state.RemainingMs(1000000));
	Check(state.ElapsedSinceSyncMs(1000000) == 0);
	state.Store({true, 10000, 60000}, 1000000);
	Check(state.EstimatedProgressMs(3000000) == 12000);
	Check(state.RemainingMs(3000000) == 48000);
	Check(state.EstimatedProgressMs(90000000) == 60000);
	Check(state.RemainingMs(90000000) == 0);
	// Existing toggle semantics restart the clock at the stored position.
	state.SetPlaying(false, 4000000);
	Check(state.ProgressMs() == 10000);
	Check(state.EstimatedProgressMs(6000000) == 10000);
	Check(!state.RemainingMs(6000000));
	state.SetPlaying(true, 7000000);
	Check(state.EstimatedProgressMs(8000000) == 11000);
	state.Store({true, 1000, 0}, 1000000);
	Check(state.EstimatedProgressMs(2000000) == 2000);
	Check(!state.RemainingMs(2000000));
	state.Store({true, 1000, 60000}, 0);
	Check(state.EstimatedProgressMs(2000000) == 1000);
	Check(!state.RemainingMs(2000000));
}


static void
CheckRepeatedStaleReports()
{
	PlaybackTimeline state;
	state.Store({true, 10000, 60000}, 1000000);
	state.SeekOptimistic(30000, 2000000);
	auto result = state.FilterReport({true, 11000, 60000}, false, false, 3000000);
	Check(result.verifyNeeded && result.progressMs == 31000);
	state.Store({true, result.progressMs, 60000}, 3000000);
	result = state.FilterReport({true, 12000, 60000}, false, false, 4000000);
	Check(result.verifyNeeded && result.progressMs == 32000);
	state.Store({true, result.progressMs, 60000}, 4000000);
	result = state.FilterReport({true, 14000, 60000}, false, false, 6999999);
	Check(result.verifyNeeded && result.progressMs == 34999);
	// The guard expires at the original seek deadline, even after stale reports.
	result = state.FilterReport({true, 15000, 60000}, false, false, 7000000);
	Check(!result.verifyNeeded && result.progressMs == 15000);
}


static void
CheckToleranceAndConfirmation()
{
	for (int delta : {-2001, -2000, 2000, 2001}) {
		PlaybackTimeline state;
		state.Store({true, 0, 60000}, 1000000);
		state.SeekOptimistic(30000, 2000000);
		auto result = state.FilterReport({true, 31000 + delta, 60000},
			false, false, 3000000);
		bool outside = delta == -2001 || delta == 2001;
		Check(result.verifyNeeded == outside);
		Check(result.progressMs == (outside ? 31000 : 31000 + delta));
		if (!outside) {
			// Unlike the volume guard, a matching position ends the seek guard.
			result = state.FilterReport({true, 0, 60000}, false, false, 3000001);
			Check(!result.verifyNeeded && result.progressMs == 0);
		}
	}
}


static void
CheckTrackChangeAndOptimisticBypass()
{
	PlaybackTimeline state;
	state.SeekOptimistic(30000, 1000000);
	auto result = state.FilterReport({true, 1000, 60000}, true, false, 2000000);
	Check(!result.verifyNeeded && result.progressMs == 1000);
	result = state.FilterReport({true, 1000, 60000}, false, false, 2000000);
	Check(result.verifyNeeded && result.progressMs == 31000);
	result = state.FilterReport({true, 500, 60000}, true, true, 2000000);
	Check(!result.verifyNeeded && result.progressMs == 500);
	result = state.FilterReport({true, 600, 60000}, false, false, 3000000);
	Check(!result.verifyNeeded && result.progressMs == 600);
}


static void
CheckReplacementSeekAndBounds()
{
	PlaybackTimeline state;
	state.SeekOptimistic(30000, 1000000);
	state.SeekOptimistic(50000, 5000000);
	auto result = state.FilterReport({false, 0, 60000}, false, false, 7000000);
	Check(result.verifyNeeded && result.progressMs == 50000);
	state.SeekOptimistic(59000, 8000000);
	result = state.FilterReport({true, 0, 60000}, false, false, 10000000);
	Check(result.verifyNeeded && result.progressMs == 60000);
	state.SeekOptimistic(-1000, 11000000);
	result = state.FilterReport({false, 3000, 60000}, false, false, 12000000);
	Check(result.verifyNeeded && result.progressMs == 0);
	// Local position events Store directly, retaining the outstanding seek guard.
	state.SeekOptimistic(30000, 13000000);
	state.Store({true, 5000, 60000}, 14000000);
	result = state.FilterReport({true, 5000, 60000}, false, false, 15000000);
	Check(result.verifyNeeded && result.progressMs == 31000);
}


int
main()
{
	CheckPositionAndToggle();
	CheckRepeatedStaleReports();
	CheckToleranceAndConfirmation();
	CheckTrackChangeAndOptimisticBypass();
	CheckReplacementSeekAndBounds();
	std::puts("Playback timeline tests passed");
	return 0;
}
