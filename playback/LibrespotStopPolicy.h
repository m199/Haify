#pragma once

#include <signal.h>

#include <cstdint>

// Escalation for stopping librespot. SIGINT lets it leave Spotify Connect
// cleanly; SIGTERM and finally SIGKILL follow if the process does not exit.
namespace LibrespotStopPolicy {

const int64_t kCheckIntervalUs = 100000;
const int64_t kTermAfterUs = 2000000;
const int64_t kKillAfterUs = 4000000;

// Signal to send now, or 0 while the last one is still in its grace period.
// The caller has already sent SIGINT at elapsed time 0.
inline int
NextSignal(int64_t elapsedUs, int lastSignal)
{
	if (elapsedUs >= kKillAfterUs && lastSignal != SIGKILL)
		return SIGKILL;
	if (elapsedUs >= kTermAfterUs && lastSignal == SIGINT)
		return SIGTERM;
	return 0;
}

}
