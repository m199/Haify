#include "PlaylistPageState.h"
#include "UiLogic.h"

#include <algorithm>
#include <limits>

namespace {
template<typename Integer>
Integer NextGeneration(Integer value)
{
	return value == std::numeric_limits<Integer>::max() ? 1 : value + 1;
}

bool Matches(const PlaylistPageRequest& expected, const PlaylistPageRequest& actual)
{
	return actual.token.generation > 0 && actual.token.requestId > 0
		&& actual.token.generation == expected.token.generation
		&& actual.token.requestId == expected.token.requestId
		&& actual.source == expected.source && actual.id == expected.id
		&& actual.offset == expected.offset && actual.headRefresh == expected.headRefresh;
}

bool ValidCounts(const PlaylistPageResult& result)
{
	return result.responseValid && result.pageCount >= 0
		&& result.nextOffset == int64_t(result.request.offset) + result.pageCount;
}
}

void
PlaylistPageState::_InvalidateRequests()
{
	fGeneration = NextGeneration(fGeneration);
	fLoading = false;
	fHeadLoading = false;
	fHeadRefreshing = false;
}

void
PlaylistPageState::_ClearSearchFailure()
{
	fRetryCount = 0;
	fWaitingRetry = false;
	fSearchFailed = false;
}

void
PlaylistPageState::Reset(const std::string& filter)
{
	Restart(-1);
	fFilter = filter;
	fSearchGeneration = NextGeneration(fSearchGeneration);
	fSearchPending = false;
	fSearchPaging = !filter.empty();
	_ClearSearchFailure();
}

void
PlaylistPageState::Restart(int32_t total)
{
	_InvalidateRequests();
	fPosition = {0, std::max(int32_t(0), total), total != 0};
}

void
PlaylistPageState::RestorePosition(const PlaylistPagePosition& position)
{
	_InvalidateRequests();
	fPosition = position;
}

void
PlaylistPageState::ReconcileMetadata(int32_t total, int32_t rowCount)
{
	auto resolved = ResolvePlaylistMetadataPageState(total, fPosition.total,
		rowCount, fPosition.offset);
	fPosition.total = resolved.total;
	fPosition.hasMore = resolved.hasMore;
}

void
PlaylistPageState::_AssignToken(PlaylistPageRequest& request)
{
	if (fGeneration == 0)
		fGeneration = 1;
	fRequestId = NextGeneration(fRequestId);
	request.token = {fGeneration, fRequestId};
}

bool
PlaylistPageState::CanLoad() const
{
	return !fLoading && !fHeadRefreshing && fPosition.hasMore
		&& !fSearchPending && !fWaitingRetry;
}

bool
PlaylistPageState::ShouldLoad(bool nearEnd) const
{
	return CanLoad() && (fFilter.empty() ? nearEnd : fSearchPaging);
}

bool
PlaylistPageState::BeginPage(PlaylistPageRequest& request)
{
	if (!CanLoad() || request.limit <= 0
			|| request.source == PlaylistPageSource::Unsupported || request.headRefresh)
		return false;
	request.offset = fPosition.offset;
	request.searchGeneration = request.source == PlaylistPageSource::Podcast
		&& fSearchPaging ? fSearchGeneration : -1;
	_AssignToken(request);
	fPageRequest = request;
	fLoading = true;
	return true;
}

PlaylistPageUpdate
PlaylistPageState::_PageFailure(const PlaylistPageResult& result)
{
	PlaylistPageUpdate update;
	update.accepted = true;
	if (!fSearchPaging || fFilter.empty() || fSearchPending)
		return update;
	if (fPageRequest.searchGeneration != fSearchGeneration) {
		// A read for the previous filter cannot stop or delay the new search.
		update.continueLoading = true;
		return update;
	}
	update.retryDelay = PlaylistPageRetryDelay(result.status, result.retryAfter,
		fRetryCount, result.responseValid && !result.ok);
	fWaitingRetry = update.retryDelay > 0;
	if (fWaitingRetry)
		fRetryCount++;
	else {
		fSearchPaging = false;
		fSearchFailed = true;
	}
	return update;
}

PlaylistPageUpdate
PlaylistPageState::FinishPage(const PlaylistPageResult& result)
{
	if (!fLoading || !Matches(fPageRequest, result.request))
		return {};
	fLoading = false;
	if (!result.ok || !ValidCounts(result))
		return _PageFailure(result);
	if (result.total >= 0)
		fPosition.total = result.total;
	fPosition.offset = result.nextOffset;
	fPosition.hasMore = result.pageCount > 0
		&& (fPosition.total <= 0 || fPosition.offset < fPosition.total);
	if (result.request.source == PlaylistPageSource::Podcast) {
		_ClearSearchFailure();
		if (!fPosition.hasMore)
			fSearchPaging = false;
	}
	PlaylistPageUpdate update;
	update.accepted = true;
	update.applied = true;
	update.continueLoading = true;
	return update;
}

void
PlaylistPageState::DispatchFailed(const PlaylistPageRequest& request)
{
	if (fLoading && Matches(fPageRequest, request)) {
		fLoading = false;
		fPosition.hasMore = false;
		fSearchPaging = false;
	}
	if (fHeadLoading && Matches(fHeadRequest, request)) {
		fHeadLoading = false;
		fHeadRefreshing = false;
	}
}

bool
PlaylistPageState::BeginHead(PlaylistPageRequest& request)
{
	if (fLoading || fHeadLoading || request.source != PlaylistPageSource::Podcast
			|| !request.headRefresh || request.offset < 0 || request.limit <= 0)
		return false;
	if (request.offset == 0 ? fHeadRefreshing
			: (!fHeadRefreshing || request.offset != fHeadNextOffset))
		return false;
	_AssignToken(request);
	fHeadRequest = request;
	fHeadLoading = true;
	fHeadRefreshing = true;
	return true;
}

bool
PlaylistPageState::FinishHeadPage(const PlaylistPageResult& result)
{
	if (!fHeadLoading || !Matches(fHeadRequest, result.request))
		return false;
	fHeadLoading = false;
	if (!result.ok || !ValidCounts(result)) {
		fHeadRefreshing = false;
		return true;
	}
	fPosition.total = result.total;
	fHeadNextOffset = result.nextOffset;
	return true;
}

bool
PlaylistPageState::ContinueHead(const PlaylistPageResult& result, bool reachedKnown) const
{
	return fHeadRefreshing && !reachedKnown && result.pageCount > 0
		&& (fPosition.total <= 0 || result.nextOffset < fPosition.total);
}

void
PlaylistPageState::FinishHeadRefresh(int32_t loadedCount)
{
	fHeadRefreshing = false;
	fPosition.offset = std::max(fPosition.offset, loadedCount);
	fPosition.hasMore = fPosition.total <= 0 || fPosition.offset < fPosition.total;
	if (!fPosition.hasMore)
		fSearchPaging = false;
}

int32_t
PlaylistPageState::ScheduleSearch()
{
	fSearchGeneration = NextGeneration(fSearchGeneration);
	fSearchPending = true;
	fSearchPaging = false;
	_ClearSearchFailure();
	return fSearchGeneration;
}

bool
PlaylistPageState::ApplySearch(int32_t generation, const std::string& filter)
{
	if (!fSearchPending || generation != fSearchGeneration)
		return false;
	fSearchPending = false;
	fFilter = filter;
	fSearchPaging = !filter.empty() && fPosition.hasMore;
	fSearchFailed = false;
	return true;
}

bool
PlaylistPageState::RetrySearch(int32_t generation)
{
	if (generation != fSearchGeneration || !fWaitingRetry || !fSearchPaging)
		return false;
	fWaitingRetry = false;
	return true;
}
