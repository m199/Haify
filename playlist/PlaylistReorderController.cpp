#include "PlaylistReorderController.h"
#include "PlaylistMutationResponse.h"
#include "PlaylistReorderPositions.h"

#include <limits>
#include <utility>

bool
PlaylistReorderController::Begin(const PlaylistReorderContext& context,
	int32_t sourceIndex, int32_t rangeLength, int32_t insertBefore,
	PlaylistReorderCommand& command)
{
	if (!ResolvePlaylistReorderPlan(sourceIndex, rangeLength,
			insertBefore, context.rowCount).shouldMove)
		return false;
	std::vector<int32_t> indices;
	for (int32_t offset = 0; offset < rangeLength; offset++)
		indices.push_back(sourceIndex + offset);
	return Begin(context, indices, insertBefore, command);
}

bool
PlaylistReorderController::Begin(const PlaylistReorderContext& context,
	const std::vector<int32_t>& indices, int32_t insertBefore,
	PlaylistReorderCommand& command)
{
	if (Pending() || !context.owned || context.pageLoading
			|| context.otherMutationPending || context.playlistId.empty())
		return false;
	auto plan = ResolvePlaylistSelectionReorder(indices, insertBefore, context.rowCount);
	std::vector<int32_t> positions;
	if (!context.visiblePositions.empty()) {
		if (context.visiblePositions.size() != static_cast<size_t>(context.rowCount))
			return false;
		auto mapped = ResolvePlaylistPositionReorder(indices, insertBefore,
			context.visiblePositions, context.loadedEnd, context.total);
		plan.targetIndex = mapped.selectionTarget;
		plan.moves = std::move(mapped.moves);
		positions = std::move(mapped.positions);
	}
	if (plan.moves.empty() || plan.moves.size() > static_cast<uint64_t>(
			std::numeric_limits<int64_t>::max() - fNextRequest))
		return false;
	fPending.playlistId = context.playlistId;
	fPending.selectedIndices = indices;
	fPending.selectionTarget = plan.targetIndex;
	fPending.visiblePositions = std::move(positions);
	fOriginalPositions = context.visiblePositions;
	fMoves = std::move(plan.moves);
	_SetMove(0, context.snapshotId);
	command = fPending;
	return true;
}

void
PlaylistReorderController::_SetMove(size_t step, const std::string& snapshot)
{
	fStep = step;
	fPending.requestId = ++fNextRequest;
	fPending.snapshotId = snapshot;
	const auto& move = fMoves[step];
	fPending.sourceIndex = move.sourceIndex;
	fPending.rangeLength = move.rangeLength;
	fPending.insertBefore = move.insertBefore;
	fPending.targetIndex = move.targetIndex;
}

PlaylistReorderUpdate
PlaylistReorderController::Complete(const PlaylistReorderResult& result)
{
	PlaylistReorderUpdate update;
	if (!Pending() || result.requestId != fPending.requestId
			|| result.playlistId != fPending.playlistId)
		return update;
	update.restoreIndices = fPending.selectedIndices;
	update.restoreIndex = update.restoreIndices.front();
	update.visiblePositions = fOriginalPositions;
	bool more = fStep + 1 < fMoves.size();
	if (result.ok && more && !result.snapshotId.empty()) {
		_SetMove(fStep + 1, result.snapshotId);
		update.action = PlaylistReorderAction::Continue;
		update.next = fPending;
		return update;
	}
	if (result.ok && !more) {
		update.action = PlaylistReorderAction::Commit;
		update.visiblePositions = fPending.visiblePositions;
		update.snapshotId = result.snapshotId;
		update.refreshSnapshot = result.snapshotId.empty();
	} else {
		update.partialUpdate = fStep > 0 || (result.ok && more);
		update.action = result.status == 409 || update.partialUpdate
			? PlaylistReorderAction::Reload : PlaylistReorderAction::Rollback;
	}
	fPending = {};
	fMoves.clear();
	fOriginalPositions.clear();
	return update;
}

bool
DispatchPlaylistReorder(const PlaylistReorderCommand& command,
	const PlaylistReorderHandler& reorder, PlaylistReorderCompletion complete)
{
	if (command.requestId <= 0 || command.playlistId.empty()
			|| command.sourceIndex < 0 || command.rangeLength < 1
			|| command.insertBefore < 0 || command.targetIndex < 0
			|| !reorder || !complete)
		return false;
	reorder(command, [command, complete](bool ok, const nlohmann::json& data) {
		PlaylistReorderResult result;
		result.requestId = command.requestId;
		result.playlistId = command.playlistId;
		result.ok = ok;
		result.status = PlaylistMutationResponse::Status(data);
		if (ok)
			result.snapshotId = PlaylistMutationResponse::SnapshotId(data);
		complete(result);
	});
	return true;
}
