#include "playback/PlaybackPollState.h"

#include <cstdlib>
#include <cstdio>
#include <initializer_list>
#include <limits>

#ifdef NDEBUG
#error Playback poll tests require assertions; compile without NDEBUG.
#endif

using Action = PlaybackPollAction;

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
CheckPendingAndCadence()
{
	PlaybackPollState state;
	state.Start(1000000);
	Check(!state.RequestPending());
	Check(state.BeginRequest());
	Check(state.RequestPending() && !state.BeginRequest());
	auto result = state.Complete({true, true, true}, 2000000);
	Check(!state.RequestPending());
	Check(result.action == Action::ApplyPlayback && result.delayUs == 5000000);
	Check(state.BeginRequest());
	result = state.Complete({true, true, false}, 3000000);
	Check(result.action == Action::ApplyPlayback && result.delayUs == 15000000);
	// Once an item was seen, empty state is accepted even inside startup time.
	result = state.Complete({true, false, false}, 4000000);
	Check(result.action == Action::ApplyEmpty && result.delayUs == 15000000);
}


static void
CheckStartupEmptyDeadline()
{
	PlaybackPollState state;
	state.Start(1000000);
	state.BeginRequest();
	auto result = state.Complete({true, false, false}, 30999999);
	Check(!state.RequestPending());
	Check(result.action == Action::RetryOnly && result.delayUs == 1000000);
	result = state.Complete({true, false, false}, 31000000);
	Check(result.action == Action::ApplyEmpty && result.delayUs == 15000000);
	result = state.Complete({true, false, true}, 32000000);
	Check(result.action == Action::ApplyEmpty && result.delayUs == 5000000);
	// Successful empty state does not raise the startup error-backoff cap.
	for (int i = 0; i < 10; ++i)
		result = state.Complete({}, 33000000);
	Check(result.action == Action::RetryOnly && result.delayUs == 5000000);
}


static void
CheckBackoffAndRecovery()
{
	PlaybackPollState state;
	state.Start(1000000);
	for (int64_t expected : {1000000LL, 2000000LL, 4000000LL, 5000000LL, 5000000LL}) {
		state.BeginRequest();
		auto result = state.Complete({}, 2000000);
		Check(!state.RequestPending());
		Check(result.action == Action::RetryOnly && result.delayUs == expected);
	}
	// Even a deferred successful empty report resets failure backoff.
	state.Complete({true, false, false}, 3000000);
	Check(state.Complete({}, 4000000).delayUs == 1000000);
	state.Complete({true, true, true}, 5000000);
	for (int64_t expected : {1000000LL, 2000000LL, 4000000LL, 8000000LL,
			16000000LL, 30000000LL, 30000000LL}) {
		Check(state.Complete({}, 6000000).delayUs == expected);
	}
	state.Complete({true, false, false}, 7000000);
	Check(state.Complete({}, 8000000).delayUs == 1000000);
}


static void
CheckRetryAfter()
{
	PlaybackPollState state;
	state.Start(1000000);
	auto result = state.Complete({false, false, false, 90}, 2000000);
	Check(result.action == Action::RetryOnly && result.delayUs == 90000000);
	Check(state.Complete({false, false, false, 0}, 3000000).delayUs == 2000000);
	Check(state.Complete({false, false, false, -1}, 4000000).delayUs == 4000000);
	int32_t maxRetry = std::numeric_limits<int32_t>::max();
	Check(state.Complete({false, false, false, maxRetry}, 5000000).delayUs
		== int64_t(maxRetry) * 1000000LL);
	// Success ignores a retry hint and resets the next failure to one second.
	Check(state.Complete({true, true, true, 90}, 6000000).delayUs == 5000000);
	Check(state.Complete({}, 7000000).delayUs == 1000000);
}


int
main()
{
	CheckPendingAndCadence();
	CheckStartupEmptyDeadline();
	CheckBackoffAndRecovery();
	CheckRetryAfter();
	std::puts("Playback poll state tests passed");
	return 0;
}
