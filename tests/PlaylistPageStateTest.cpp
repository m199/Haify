#include "playlist/PlaylistPageState.h"

#include <cassert>
#include <cstdio>

#ifdef NDEBUG
#error Playlist page state tests require assertions; compile without NDEBUG.
#endif

static PlaylistPageRequest
Begin(PlaylistPageState& state, bool podcast = true)
{
	auto request = MakePlaylistPageRequest(ResolvePlaylistContentTarget(
		podcast ? "spotify:show:show" : "spotify:playlist:list"), 999, 50);
	bool started = state.BeginPage(request);
	assert(started && state.Loading());
	assert(request.token.generation > 0 && request.token.requestId > 0);
	assert(request.offset == state.Position().offset);
	return request;
}

static PlaylistPageResult
Page(const PlaylistPageRequest& request, int32_t count = 50, int32_t total = 100)
{
	PlaylistPageResult result;
	result.request = request;
	result.ok = true;
	result.pageCount = count;
	result.total = total;
	result.nextOffset = request.offset + count;
	return result;
}

static PlaylistPageResult
Failure(const PlaylistPageRequest& request, int32_t status = 429)
{
	PlaylistPageResult result;
	result.request = request;
	result.status = status;
	result.retryAfter = 7;
	return result;
}

static void
TestPageProgressAndEnd()
{
	PlaylistPageState state;
	state.Reset("");
	assert(!state.ShouldLoad(false) && state.ShouldLoad(true));
	auto request = Begin(state, false);
	auto second = request;
	bool started = state.BeginPage(second);
	assert(!started);
	auto update = state.FinishPage(Page(request));
	assert(update.applied && state.Position().offset == 50 && state.Position().hasMore);
	request = Begin(state, false);
	update = state.FinishPage(Page(request));
	assert(update.applied && state.Position().offset == 100 && !state.CanLoad());
	state.Reset("");
	request = Begin(state, false);
	update = state.FinishPage(Page(request, 2, -1));
	assert(update.applied && state.Position().total == 0 && state.CanLoad());
	request = Begin(state, false);
	update = state.FinishPage(Page(request, 0, -1));
	assert(update.applied && state.Position().offset == 2 && !state.CanLoad());
}

static void
TestReloadAndDuplicateReplies()
{
	PlaylistPageState state;
	state.Reset("old");
	auto old = Begin(state);
	state.Reset("new");
	auto current = Begin(state);
	assert(current.token.generation != old.token.generation);
	for (const auto& result : {Page(old), Failure(old)}) {
		auto update = state.FinishPage(result);
		assert(!update.accepted && state.Loading() && state.Position().offset == 0);
		assert(state.Filter() == "new" && !state.WaitingRetry() && !state.SearchFailed());
	}
	state.DispatchFailed(old);
	assert(state.Loading());
	auto update = state.FinishPage(Page(current));
	assert(update.applied && state.Position().offset == 50);
	auto next = Begin(state);
	update = state.FinishPage(Page(current));
	assert(!update.accepted && state.Loading() && state.Position().offset == 50);
	update = state.FinishPage(Page(next));
	assert(update.applied && !state.Loading());
}

static void
TestWrongIdentityAndSameOffsetRetry()
{
	PlaylistPageState state;
	state.Reset("");
	auto first = Begin(state);
	auto update = state.FinishPage(Failure(first, 500));
	assert(update.accepted && !update.applied && !state.Loading());
	auto current = Begin(state);
	assert(first.offset == current.offset && first.token.requestId != current.token.requestId);
	update = state.FinishPage(Page(first));
	assert(!update.accepted && state.Loading());
	for (int field = 0; field < 4; field++) {
		auto wrong = Page(current);
		if (field == 0) wrong.request.id = "other";
		if (field == 1) wrong.request.offset++;
		if (field == 2) wrong.request.source = PlaylistPageSource::Playlist;
		if (field == 3) wrong.request.headRefresh = true;
		update = state.FinishPage(wrong);
		assert(!update.accepted && state.Loading());
	}
	update = state.FinishPage(Page(current));
	assert(update.applied);
}

static void
TestCacheRestoreAndMetadataRestart()
{
	PlaylistPageState state;
	state.Reset("");
	auto old = Begin(state, false);
	state.RestorePosition({50, 80, true});
	auto update = state.FinishPage(Page(old));
	assert(!update.accepted && state.Position().offset == 50);
	state.ReconcileMetadata(60, 70);
	assert(state.Position().total == 70 && state.Position().hasMore);
	old = Begin(state, false);
	state.Restart(25);
	assert(state.Position().offset == 0 && state.Position().total == 25);
	auto current = Begin(state, false);
	update = state.FinishPage(Page(old));
	assert(!update.accepted && state.Loading());
	update = state.FinishPage(Page(current, 25, 25));
	assert(update.applied && !state.CanLoad());
	state.RestorePosition({0, 0, false});
	assert(!state.CanLoad());
	state.RestorePosition({50, 80, true});
	assert(state.CanLoad() && state.Position().offset == 50);
}

static void
TestRapidSearchChangesReuseUnfilteredPage()
{
	PlaylistPageState state;
	state.Reset("first");
	auto request = Begin(state);
	int32_t skipped = state.ScheduleSearch();
	int32_t current = state.ScheduleSearch();
	bool applied = state.ApplySearch(skipped, "skipped");
	assert(!applied && state.Filter() == "first" && !state.CanLoad());
	applied = state.ApplySearch(current, "current");
	assert(applied && state.Filter() == "current" && state.Loading());
	applied = state.ApplySearch(current, "duplicate");
	assert(!applied && state.Filter() == "current");
	auto update = state.FinishPage(Page(request));
	assert(update.applied && state.Position().offset == 50 && state.Filter() == "current");
	assert(state.ShouldLoad(false));
	request = Begin(state);
	assert(request.searchGeneration == current);
	state.Reset("");
	applied = state.ApplySearch(current, "late");
	assert(!applied && state.Filter().empty());
}

static void
TestOldSearchFailureDoesNotStopNewSearch()
{
	PlaylistPageState state;
	state.Reset("old");
	auto request = Begin(state);
	int32_t generation = state.ScheduleSearch();
	bool applied = state.ApplySearch(generation, "new");
	assert(applied);
	auto update = state.FinishPage(Failure(request, 403));
	assert(update.accepted && update.continueLoading && update.retryDelay == 0);
	assert(!state.SearchFailed() && state.ShouldLoad(false));
	request = Begin(state);
	update = state.FinishPage(Failure(request, 403));
	assert(update.accepted && state.SearchFailed() && !state.ShouldLoad(false));
	generation = state.ScheduleSearch();
	assert(!state.CanLoad());
	applied = state.ApplySearch(generation, "");
	assert(applied && !state.SearchFailed() && state.ShouldLoad(true));
}

static void
TestRetryBudgetAndObsoleteTimers()
{
	PlaylistPageState state;
	state.Reset("search");
	int32_t generation = state.SearchGeneration();
	for (int attempt = 0; attempt < 4; attempt++) {
		auto request = Begin(state);
		auto update = state.FinishPage(Failure(request));
		assert(update.accepted && !update.applied);
		assert(update.retryDelay == (attempt < 3 ? 7000000 : 0));
		if (attempt == 3)
			break;
		assert(state.WaitingRetry() && !state.CanLoad());
		bool retry = state.RetrySearch(generation - 1);
		assert(!retry && state.WaitingRetry());
		retry = state.RetrySearch(generation);
		assert(retry && state.CanLoad());
		retry = state.RetrySearch(generation);
		assert(!retry);
	}
	assert(state.SearchFailed() && !state.WaitingRetry() && !state.Searching());
	generation = state.ScheduleSearch();
	bool applied = state.ApplySearch(generation, "new");
	assert(applied);
	auto update = state.FinishPage(Failure(Begin(state), 500));
	assert(update.retryDelay == 2000000);
	int32_t next = state.ScheduleSearch();
	bool retry = state.RetrySearch(generation);
	assert(!retry && !state.WaitingRetry());
	applied = state.ApplySearch(next, "");
	assert(applied && state.ShouldLoad(true));
}

static PlaylistPageRequest
Head(PlaylistPageState& state, int32_t offset = 0)
{
	auto request = MakePlaylistPageRequest(ResolvePlaylistContentTarget("spotify:show:show"), offset, 50);
	request.headRefresh = true;
	bool started = state.BeginHead(request);
	assert(started && state.HeadRefreshing() && !state.CanLoad());
	return request;
}

static void
TestSuccessResetsRetriesAndDebounceBlocksNewReads()
{
	PlaylistPageState state;
	state.Reset("search");
	auto update = state.FinishPage(Failure(Begin(state), 500));
	assert(update.retryDelay == 2000000);
	bool retry = state.RetrySearch(state.SearchGeneration());
	assert(retry);
	auto request = Begin(state);
	update = state.FinishPage(Page(request, 1, 100));
	assert(update.applied && !state.WaitingRetry());
	retry = state.RetrySearch(state.SearchGeneration());
	assert(!retry);
	for (int attempt = 0; attempt < 3; attempt++) {
		update = state.FinishPage(Failure(Begin(state), 500));
		assert(update.retryDelay == 2000000);
		retry = state.RetrySearch(state.SearchGeneration());
		assert(retry);
	}
	request = Begin(state);
	int32_t generation = state.ScheduleSearch();
	update = state.FinishPage(Page(request, 1, 100));
	assert(update.applied && !state.ShouldLoad(true));
	bool started = state.BeginPage(request);
	assert(!started);
	bool applied = state.ApplySearch(generation, "new");
	assert(applied && state.ShouldLoad(false));
	request = Begin(state);
	generation = state.ScheduleSearch();
	update = state.FinishPage(Failure(request));
	assert(update.accepted && !update.continueLoading && update.retryDelay == 0);
	applied = state.ApplySearch(generation, "");
	assert(applied && state.ShouldLoad(true));
}

static void
TestHeadAndOrdinaryPagesCannotOverlap()
{
	PlaylistPageState state;
	state.Reset("");
	auto page = Begin(state);
	auto head = page;
	head.headRefresh = true;
	bool started = state.BeginHead(head);
	assert(!started);
	auto update = state.FinishPage(Page(page));
	assert(update.applied);
	head = Head(state);
	started = state.BeginPage(page);
	assert(!started);
	bool accepted = state.FinishHeadPage(Page(head, 5));
	assert(accepted);
	head.offset = 10;
	started = state.BeginHead(head);
	assert(!started);
	state.FinishHeadRefresh(55);
	assert(state.CanLoad());
}

static void
TestHeadRefreshAndReload()
{
	PlaylistPageState state;
	state.Reset("search");
	state.RestorePosition({50, 50, false});
	auto old = Head(state);
	state.Reset("search");
	state.RestorePosition({50, 50, false});
	auto current = Head(state);
	bool accepted = state.FinishHeadPage(Failure(old));
	assert(!accepted && state.HeadRefreshing());
	auto page = Page(current, 50, 130);
	accepted = state.FinishHeadPage(page);
	assert(accepted && state.ContinueHead(page, false));
	assert(!state.ContinueHead(page, true));
	auto next = Head(state, 50);
	accepted = state.FinishHeadPage(page);
	assert(!accepted && state.HeadRefreshing());
	page = Page(next, 10, 130);
	accepted = state.FinishHeadPage(page);
	assert(accepted);
	state.FinishHeadRefresh(60);
	assert(!state.HeadRefreshing() && state.Position().offset == 60);
	assert(state.Position().total == 130 && state.ShouldLoad(false));
	auto request = Begin(state);
	assert(request.offset == 60);
}

static void
TestDispatchFailureAndMalformedCounts()
{
	PlaylistPageState state;
	state.Reset("search");
	auto request = Begin(state);
	auto invalid = Page(request);
	invalid.nextOffset = 1;
	auto update = state.FinishPage(invalid);
	assert(update.accepted && !update.applied && state.SearchFailed());
	assert(state.Position().offset == 0 && update.retryDelay == 0);
	state.Reset("");
	request = Begin(state);
	state.DispatchFailed(request);
	assert(!state.Loading() && !state.CanLoad());
	state.RestorePosition({50, 100, true});
	request = Head(state);
	state.DispatchFailed(request);
	assert(!state.HeadRefreshing() && state.CanLoad());
	request = Head(state);
	bool accepted = state.FinishHeadPage(Failure(request));
	assert(accepted && !state.HeadRefreshing() && state.Position().offset == 50);
}

int
main()
{
	TestPageProgressAndEnd();
	TestReloadAndDuplicateReplies();
	TestWrongIdentityAndSameOffsetRetry();
	TestCacheRestoreAndMetadataRestart();
	TestRapidSearchChangesReuseUnfilteredPage();
	TestOldSearchFailureDoesNotStopNewSearch();
	TestRetryBudgetAndObsoleteTimers();
	TestSuccessResetsRetriesAndDebounceBlocksNewReads();
	TestHeadAndOrdinaryPagesCannotOverlap();
	TestHeadRefreshAndReload();
	TestDispatchFailureAndMalformedCounts();
	puts("Playlist page state tests passed.");
}
