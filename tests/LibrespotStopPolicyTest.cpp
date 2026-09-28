#include "playback/LibrespotStopPolicy.h"

#include <cstdio>
#include <cstdlib>

static void
CheckCondition(bool condition, const char* expression, const char* function, int line)
{
	if (condition)
		return;
	std::fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, line, function, expression);
	std::abort();
}

#define Check(condition) CheckCondition((condition), #condition, __func__, __LINE__)

using LibrespotStopPolicy::NextSignal;

static void
CheckEscalationMatchesTheFormerBlockingLoop()
{
	Check(NextSignal(0, SIGINT) == 0);
	Check(NextSignal(1999999, SIGINT) == 0);
	Check(NextSignal(2000000, SIGINT) == SIGTERM);
	Check(NextSignal(3000000, SIGTERM) == 0);
	Check(NextSignal(4000000, SIGTERM) == SIGKILL);
	Check(NextSignal(9000000, SIGKILL) == 0);
}

// A late timer tick must not skip the kill after a missed SIGTERM window.
static void
CheckLateTickStillKills()
{
	Check(NextSignal(5000000, SIGINT) == SIGKILL);
}

int
main()
{
	CheckEscalationMatchesTheFormerBlockingLoop();
	CheckLateTickStillKills();
	std::puts("LibrespotStopPolicyTest passed");
	return 0;
}
