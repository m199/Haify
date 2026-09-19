#include "PlaylistRemovalController.h"
#include "PlaylistMutationResponse.h"
#include "policy/UiLogic.h"

#include <algorithm>
#include <limits>
#include <set>

namespace {
bool
ValidItems(const std::vector<std::pair<std::string, int>>& items)
{
	if (items.empty())
		return false;
	std::set<int> positions;
	for (const auto& item : items) {
		if (item.first.empty() || item.second < 0
				|| !positions.insert(item.second).second) {
			return false;
		}
	}
	return true;
}

PlaylistRemovalResult
MapResult(const PlaylistRemovalCommand& command, bool ok,
	const nlohmann::json& data)
{
	PlaylistRemovalResult result;
	result.requestId = command.requestId;
	result.playlistId = command.playlistId;
	result.ok = ok;
	result.status = PlaylistMutationResponse::Status(data);
	if (data.is_object()) {
		auto partial = data.find("partial_update");
		result.partialUpdate = partial != data.end() && partial->is_boolean()
			&& partial->get<bool>();
	}
	return result;
}
}

bool
PlaylistRemovalController::Begin(const PlaylistRemovalContext& context,
	const std::vector<std::pair<std::string, int>>& items,
	const std::vector<std::string>& visibleUris, PlaylistRemovalCommand& command)
{
	if (Pending() || !context.owned || context.otherMutationPending
			|| context.playlistId.empty() || context.rowCount <= 0
			|| items.size() > static_cast<size_t>(context.rowCount)
			|| !ValidItems(items) || fNextRequest == std::numeric_limits<int64_t>::max()) {
		return false;
	}
	PlaylistRemovalCommand pending;
	pending.requestId = ++fNextRequest;
	pending.playlistId = context.playlistId;
	pending.snapshotId = context.snapshotId;
	pending.items = items;
	bool complete = PlaylistHasCompleteSnapshot(context.snapshotId,
		context.page.total, context.rowCount, context.page.offset);
	if (complete && visibleUris.size() == static_cast<size_t>(context.rowCount)
			&& std::all_of(visibleUris.begin(), visibleUris.end(),
				[](const std::string& uri) { return !uri.empty(); })) {
		pending.knownPlaylistUris = visibleUris;
	}
	fPending = pending;
	command = std::move(pending);
	return true;
}

PlaylistRemovalUpdate
PlaylistRemovalController::Complete(const PlaylistRemovalResult& result,
	const PlaylistPagePosition& page, int32_t remainingRowCount)
{
	PlaylistRemovalUpdate update;
	if (!Pending() || result.requestId != fPending.requestId
			|| result.playlistId != fPending.playlistId)
		return update;
	if (!result.ok) {
		update.action = result.status == 409 || result.partialUpdate
			? PlaylistRemovalAction::Reload : PlaylistRemovalAction::Rollback;
		fPending = {};
		return update;
	}
	update.action = PlaylistRemovalAction::Commit;
	for (const auto& item : fPending.items)
		update.removedPositions.push_back(item.second);
	std::sort(update.removedPositions.begin(), update.removedPositions.end());
	update.page = page;
	update.page.offset = std::max(int32_t(0), update.PositionAfterRemoval(page.offset));
	update.page.total = page.total > 0
		? std::max(int32_t(0), page.total - static_cast<int32_t>(fPending.items.size()))
		: std::max(int32_t(0), remainingRowCount);
	update.page.hasMore = update.page.offset < update.page.total;
	fPending = {};
	return update;
}

int32_t
PlaylistRemovalUpdate::PositionAfterRemoval(int32_t position) const
{
	if (position < 0)
		return position;
	auto before = std::lower_bound(removedPositions.begin(), removedPositions.end(), position);
	return position - static_cast<int32_t>(before - removedPositions.begin());
}

bool
DispatchPlaylistRemoval(const PlaylistRemovalCommand& command,
	const PlaylistRemovalHandler& removeAtPositions,
	const PlaylistRemovalHandler& removeFromKnownSnapshot,
	PlaylistRemovalCompletion complete)
{
	if (command.requestId <= 0 || command.playlistId.empty()
			|| !ValidItems(command.items) || !complete)
		return false;
	const auto& dispatch = command.knownPlaylistUris.empty()
		? removeAtPositions : removeFromKnownSnapshot;
	if (!dispatch)
		return false;
	dispatch(command, [command, complete](bool ok, const nlohmann::json& data) {
		complete(MapResult(command, ok, data));
	});
	return true;
}
