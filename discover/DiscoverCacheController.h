#pragma once

#include "DiscoverTabPolicy.h"

#include <optional>

struct DiscoverTabCacheState {
	bool loaded = false;
	int64_t loadTime = 0;
	bool pageLoading = false;
	bool pageHasMore = false;
	bool cacheBacked = false;
	bool freshSnapshot = false;
	int32_t pageOffset = 0;
	int32_t loadGeneration = 0;
	std::string pageCursor;
	int32_t cacheGeneration = 0;
	bool cachePending = false;
	bool invalidated = false;
};

struct DiscoverCacheReadRequest {
	std::string accountId;
	int32_t tab = TAB_NONE;
	int32_t generation = 0;
};

struct DiscoverOffsetPage {
	int32_t nextOffset;
	bool hasMore;
};

struct DiscoverPageResult {
	int32_t tab = TAB_NONE;
	int32_t loadGeneration = 0;
	bool hasMore = false;
	std::optional<int32_t> nextOffset;
	std::string nextCursor;
};

inline DiscoverOffsetPage
AdvanceDiscoverPage(int32_t offset, int32_t sourceItems, int32_t total)
{
	return {offset + sourceItems, sourceItems > 0 && offset + sourceItems < total};
}

// Owned by one DiscoverWindow, accessed only on its message loop. Workers own
// request copies. Reset advances generations, including across account changes.
// Time is supplied by the caller in monotonic microseconds, never wall-clock time.
class DiscoverCacheController {
public:
	static constexpr int64_t Expiry = 5LL * 60 * 1000000;

	const DiscoverTabCacheState& State(int32_t tab) const { return fTabs.at(tab); }
	const std::string& AccountId() const { return fAccountId; }
	void SetAccountIfEmpty(const std::string& account)
	{
		if (fAccountId.empty())
			fAccountId = account;
	}

	void Reset(const std::string& account)
	{
		fAccountId = account;
		for (auto& state : fTabs) {
			int32_t load = state.loadGeneration + 1;
			int32_t cache = state.cacheGeneration + 1;
			state = {};
			state.loadGeneration = load;
			state.cacheGeneration = cache;
		}
	}

	bool Expired(int32_t tab, int64_t now) const
	{
		const auto& state = State(tab);
		return state.loaded && (state.invalidated || state.cacheBacked
			|| now - state.loadTime > Expiry);
	}

	// Keep the visible rows while invalidating both in-flight sources. A fresh
	// snapshot is required before this tab can be persisted as authoritative.
	void Invalidate(int32_t tab)
	{
		auto& state = fTabs.at(tab);
		state.loadGeneration++;
		state.cacheGeneration++;
		state.cachePending = false;
		state.pageLoading = false;
		state.pageHasMore = false;
		state.pageOffset = 0;
		state.pageCursor.clear();
		state.invalidated = true;
	}

	bool CanRead(int32_t tab) const
	{
		const auto& state = State(tab);
		return !fAccountId.empty() && !state.invalidated && !state.cachePending && !state.cacheBacked
			&& !state.freshSnapshot;
	}

	DiscoverCacheReadRequest BeginRead(int32_t tab)
	{
		auto& state = fTabs.at(tab);
		state.cachePending = true;
		return {fAccountId, tab, ++state.cacheGeneration};
	}

	bool AcceptCacheBatch(const DiscoverCacheReadRequest& request, bool available,
		bool last, int32_t selectedTab)
	{
		if (request.tab < 0 || request.tab >= TAB_COUNT || request.accountId != fAccountId)
			return false;
		auto& state = fTabs[request.tab];
		if (!state.cachePending || request.generation != state.cacheGeneration)
			return false;
		if (last)
			state.cachePending = false;
		return available && !state.invalidated && !state.freshSnapshot && request.tab == selectedTab;
	}

	bool AcceptFreshRows(int32_t tab, std::optional<int32_t> generation) const
	{
		return tab >= 0 && tab < TAB_COUNT && generation
			&& *generation == State(tab).loadGeneration;
	}

	void CompleteRows(int32_t tab, bool fromCache, bool snapshot)
	{
		auto& state = fTabs.at(tab);
		if (fromCache) {
			state.loaded = true;
			state.cacheBacked = true;
			state.loadTime = 0;
		} else if (snapshot) {
			state.freshSnapshot = true;
			state.cacheBacked = false;
			state.invalidated = false;
		}
	}

	void PrepareLoad(int32_t tab, bool nextPage, int64_t now)
	{
		if (nextPage)
			return;
		auto& state = fTabs.at(tab);
		state.loadGeneration++;
		state.loaded = true;
		state.loadTime = now;
		state.pageLoading = false;
		state.pageHasMore = true;
		state.pageOffset = 0;
		state.pageCursor.clear();
	}

	bool FinishPage(int32_t tab, int32_t generation, bool hasMore,
		int32_t nextOffset, const std::string& cursor)
	{
		if (tab < 0 || tab >= TAB_COUNT || generation != State(tab).loadGeneration)
			return false;
		auto& state = fTabs[tab];
		state.pageLoading = false;
		state.pageHasMore = hasMore;
		state.pageOffset = nextOffset;
		state.pageCursor = cursor;
		return true;
	}

	bool CanLoadNext(int32_t tab) const
	{
		const auto& state = State(tab);
		return (tab == TAB_FOLLOWED_ARTISTS || tab == TAB_SAVED_EPISODES
			|| tab == TAB_AUDIOBOOKS) && !state.pageLoading && state.pageHasMore;
	}

	void BeginPage(int32_t tab) { fTabs.at(tab).pageLoading = true; }
	void MarkUnloaded(int32_t tab) { fTabs.at(tab).loaded = false; }
	void Touch(int32_t tab, int64_t now) { fTabs.at(tab).loadTime = now; }
	bool ShouldPersist(int32_t tab) const
	{
		return !State(tab).invalidated && (State(tab).freshSnapshot || State(tab).cacheBacked);
	}

private:
	std::array<DiscoverTabCacheState, TAB_COUNT> fTabs{};
	std::string fAccountId;
};
