#pragma once

#include "PlaylistPageController.h"

struct PlaylistPagePosition {
	int32_t offset = 0;
	int32_t total = 0;
	bool hasMore = false;
};

struct PlaylistPageUpdate {
	bool accepted = false;
	bool applied = false;
	int64_t retryDelay = 0;
	bool continueLoading = false;
};

// Window-thread state. No API calls, timers, views or persistence.
// Search filters loaded episodes locally; changing search does not invalidate
// an unfiltered page. Reset/replacement invalidates both page and head requests.
class PlaylistPageState {
public:
	void Reset(const std::string& filter);
	void Restart(int32_t total);
	void RestorePosition(const PlaylistPagePosition& position);
	void ReconcileMetadata(int32_t total, int32_t rowCount);
	void SetTotal(int32_t total) { fPosition.total = total; }
	const PlaylistPagePosition& Position() const { return fPosition; }
	bool Loading() const { return fLoading; }
	bool HeadRefreshing() const { return fHeadRefreshing; }

	bool BeginPage(PlaylistPageRequest& request);
	PlaylistPageUpdate FinishPage(const PlaylistPageResult& result);
	void DispatchFailed(const PlaylistPageRequest& request);
	bool BeginHead(PlaylistPageRequest& request);
	bool FinishHeadPage(const PlaylistPageResult& result);
	bool ContinueHead(const PlaylistPageResult& result, bool reachedKnown) const;
	void FinishHeadRefresh(int32_t loadedCount);

	int32_t ScheduleSearch();
	bool ApplySearch(int32_t generation, const std::string& filter);
	bool RetrySearch(int32_t generation);
	bool CanLoad() const;
	bool ShouldLoad(bool nearEnd) const;
	const std::string& Filter() const { return fFilter; }
	int32_t SearchGeneration() const { return fSearchGeneration; }
	bool Searching() const { return fSearchPaging; }
	bool WaitingRetry() const { return fWaitingRetry; }
	bool SearchFailed() const { return fSearchFailed; }

private:
	void _InvalidateRequests();
	void _AssignToken(PlaylistPageRequest& request);
	void _ClearSearchFailure();
	PlaylistPageUpdate _PageFailure(const PlaylistPageResult& result);

	PlaylistPagePosition fPosition;
	int64_t fGeneration = 0;
	int64_t fRequestId = 0;
	PlaylistPageRequest fPageRequest;
	PlaylistPageRequest fHeadRequest;
	bool fLoading = false;
	bool fHeadLoading = false;
	bool fHeadRefreshing = false;
	int32_t fHeadNextOffset = 0;
	std::string fFilter;
	int32_t fSearchGeneration = 0;
	int32_t fRetryCount = 0;
	bool fSearchPending = false;
	bool fSearchPaging = false;
	bool fWaitingRetry = false;
	bool fSearchFailed = false;
};
