#pragma once

#include "DiscoverCacheDocument.h"
#include <Messenger.h>

namespace DiscoverCacheRepository {
// Workers own copied request/snapshot values, never window pointers. Cache
// replies retain account + generation. Write failures are reported to stderr;
// failed/stale temporary files never replace the previous readable cache.
void LoadAsync(const BMessenger& target, const DiscoverCacheReadRequest& request);
void WriteAsync(const DiscoverCacheSnapshot& snapshot);
}
