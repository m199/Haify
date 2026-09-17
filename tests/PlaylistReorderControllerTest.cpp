#include "playlist/PlaylistReorderController.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <limits>
#include <numeric>
#include <vector>

#ifdef NDEBUG
#error Playlist reorder tests require assertions; compile without NDEBUG.
#endif

static PlaylistReorderContext
Context(int32_t rows = 8)
{
	PlaylistReorderContext context;
	context.playlistId = "list";
	context.snapshotId = "before";
	context.rowCount = rows;
	context.owned = true;
	return context;
}

static PlaylistReorderResult
Reply(const PlaylistReorderCommand& command, bool ok = true, int32_t status = 200)
{
	PlaylistReorderResult result;
	result.requestId = command.requestId;
	result.playlistId = command.playlistId;
	result.ok = ok;
	result.status = status;
	result.snapshotId = "after";
	return result;
}

static void
MoveBlock(std::vector<int>& rows, int source, int length, int target)
{
	std::vector<int> block(rows.begin() + source, rows.begin() + source + length);
	rows.erase(rows.begin() + source, rows.begin() + source + length);
	rows.insert(rows.begin() + target, block.begin(), block.end());
}

static std::vector<int>
ExpectedOrder(const std::vector<int>& original, int source, int length, int before)
{
	if (before >= source && before <= source + length)
		return original;
	auto rows = original;
	std::vector<int> block(rows.begin() + source, rows.begin() + source + length);
	rows.erase(rows.begin() + source, rows.begin() + source + length);
	// Original identities determine the destination, independently of target math.
	auto target = std::find(rows.begin(), rows.end(), before);
	rows.insert(target, block.begin(), block.end());
	return rows;
}

static void
CheckMove(int rows, int source, int length, int boundary)
{
	std::vector<int> original(rows);
	std::iota(original.begin(), original.end(), 0);
	int before = std::max(0, std::min(boundary, rows));
	auto expected = ExpectedOrder(original, source, length, before);
	PlaylistReorderController controller;
	PlaylistReorderCommand command;
	command.playlistId = "untouched";
	bool started = controller.Begin(Context(rows), source, length, boundary, command);
	assert(started == (expected != original));
	if (!started) {
		assert(!controller.Pending() && command.playlistId == "untouched");
		return;
	}
	assert(controller.Pending() && command.requestId > 0);
	assert(command.sourceIndex == source && command.rangeLength == length);
	assert(command.insertBefore == before && command.snapshotId == "before");
	auto actual = original;
	MoveBlock(actual, source, length, command.targetIndex);
	assert(actual == expected);
	auto update = controller.Complete(Reply(command, false, 500));
	assert(update.action == PlaylistReorderAction::Rollback && !controller.Pending());
	assert(update.snapshotId.empty() && !update.refreshSnapshot);
	MoveBlock(actual, command.targetIndex, length, update.restoreIndex);
	assert(actual == original);
}

static void
TestAllSmallMovesAndRollbacks()
{
	for (int rows = 1; rows <= 8; rows++) {
		for (int source = 0; source < rows; source++) {
			for (int length = 1; length <= rows - source; length++) {
				for (int boundary = -1; boundary <= rows + 1; boundary++)
					CheckMove(rows, source, length, boundary);
			}
		}
	}
}

static void
TestBoundsAndSelectionSteps()
{
	constexpr int maximum = std::numeric_limits<int>::max();
	assert(!ResolvePlaylistReorderPlan(maximum, 1, 0, maximum).shouldMove);
	assert(!ResolvePlaylistReorderPlan(1, maximum, 0, maximum).shouldMove);
	assert(!ResolvePlaylistReorderPlan(-1, 1, 0, 8).shouldMove);
	assert(!ResolvePlaylistReorderPlan(1, 0, 0, 8).shouldMove);
	assert(!ResolvePlaylistReorderPlan(1, 1, 0, -1).shouldMove);
	auto plan = ResolvePlaylistReorderPlan(maximum - 1, 1, 0, maximum);
	assert(plan.shouldMove && plan.targetIndex == 0);
	plan = ResolvePlaylistReorderPlan(0, 1, maximum, maximum);
	assert(plan.shouldMove && plan.targetIndex == maximum - 1);
	auto step = ResolvePlaylistReorderStep(2, 4, 3, 8, -1);
	assert(step.shouldMove && step.sourceIndex == 2 && step.rangeLength == 3);
	assert(step.insertBefore == 1);
	step = ResolvePlaylistReorderStep(2, 4, 3, 8, 1);
	assert(step.shouldMove && step.insertBefore == 6);
	assert(ResolvePlaylistReorderStep(2, 4, 2, 8, 1).noncontiguous);
	assert(!ResolvePlaylistReorderStep(0, 1, 2, 8, -1).shouldMove);
	assert(!ResolvePlaylistReorderStep(6, 7, 2, 8, 1).shouldMove);
	assert(!ResolvePlaylistReorderStep(2, 4, 3, 8, 0).shouldMove);
	assert(!ResolvePlaylistReorderStep(-1, 4, 3, 8, 1).shouldMove);
	assert(!ResolvePlaylistReorderStep(2, maximum, 3, maximum, 1).shouldMove);
	step = ResolvePlaylistReorderStep(maximum - 2, maximum - 2, 1, maximum, 1);
	assert(step.shouldMove && step.insertBefore == maximum);
}

static PlaylistReorderCommand
Begin(PlaylistReorderController& controller)
{
	PlaylistReorderCommand command;
	assert(controller.Begin(Context(), 2, 2, 7, command));
	return command;
}

static void
TestGatesAndIdentity()
{
	for (int gate = 0; gate < 6; gate++) {
		PlaylistReorderController controller;
		auto context = Context();
		if (gate == 0) context.owned = false;
		if (gate == 1) context.pageLoading = true;
		if (gate == 2) context.otherMutationPending = true;
		if (gate == 3) context.playlistId.clear();
		if (gate == 4) context.rowCount = 0;
		if (gate == 5) Begin(controller);
		PlaylistReorderCommand output;
		output.playlistId = "untouched";
		assert(!controller.Begin(context, 2, 2, 7, output));
		assert(output.playlistId == "untouched" && output.requestId == 0);
		assert(controller.Pending() == (gate == 5));
	}
	PlaylistReorderController controller;
	auto first = Begin(controller);
	auto result = Reply(first);
	result.playlistId = "other";
	assert(controller.Complete(result).action == PlaylistReorderAction::Ignore);
	result = Reply(first);
	result.requestId++;
	assert(controller.Complete(result).action == PlaylistReorderAction::Ignore);
	assert(controller.Pending());
	auto committed = controller.Complete(Reply(first));
	assert(committed.action == PlaylistReorderAction::Commit);
	assert(committed.snapshotId == "after" && !committed.refreshSnapshot);
	assert(!controller.Pending());
	auto second = Begin(controller);
	assert(second.requestId != first.requestId);
	assert(controller.Complete(Reply(first)).action == PlaylistReorderAction::Ignore);
	assert(controller.Pending());
	result = Reply(second);
	result.snapshotId.clear();
	assert(controller.Complete(result).refreshSnapshot);
	assert(controller.Complete(result).action == PlaylistReorderAction::Ignore);
}

static void
TestFailureClassification()
{
	for (int status : {-1, 0, 400, 401, 403, 404, 409, 429, 500}) {
		PlaylistReorderController controller;
		auto command = Begin(controller);
		auto update = controller.Complete(Reply(command, false, status));
		assert(update.action == (status == 409
			? PlaylistReorderAction::Reload : PlaylistReorderAction::Rollback));
		assert(update.restoreIndex == 2 && update.snapshotId.empty());
		assert(!update.refreshSnapshot && !controller.Pending());
	}
}

static void
TestDispatchAndResponseMapping()
{
	JsonCallback callback;
	PlaylistReorderResult actual;
	{
		PlaylistReorderController controller;
		auto command = Begin(controller);
		bool sent = DispatchPlaylistReorder(command,
			[&](const PlaylistReorderCommand& request, JsonCallback done) {
				assert(request.sourceIndex == 2 && request.rangeLength == 2);
				assert(request.insertBefore == 7 && request.snapshotId == "before");
				callback = done;
			}, [&](const PlaylistReorderResult& result) { actual = result; });
		assert(sent);
		command.playlistId = "changed";
	}
	callback(true, {{"snapshot_id", "direct"}});
	assert(actual.ok && actual.snapshotId == "direct" && actual.status == -1);
	assert(actual.playlistId == "list" && actual.requestId > 0);
	callback(true, {{"status", 200}, {"body", "{\"snapshot_id\":\"wrapped\"}"}});
	assert(actual.snapshotId == "wrapped" && actual.status == 200);
	const std::vector<nlohmann::json> noSnapshot = {nullptr, nlohmann::json::array(),
		{{"snapshot_id", 17}}, {{"status", 204}, {"body", ""}},
		{{"body", "malformed"}}, {{"body", "[]"}}};
	for (const auto& data : noSnapshot) {
		callback(true, data);
		assert(actual.ok && actual.snapshotId.empty());
	}
	callback(false, {{"status", uint64_t(409)}, {"snapshot_id", "unconfirmed"}});
	assert(!actual.ok && actual.status == 409 && actual.snapshotId.empty());
	for (const nlohmann::json& status : std::vector<nlohmann::json>{"409", 409.5,
		nullptr, std::numeric_limits<uint64_t>::max(), std::numeric_limits<int64_t>::min()}) {
		callback(false, {{"status", status}});
		assert(actual.status == -1);
	}
}

static void
CheckSelectionMove(int count, unsigned mask, int boundary)
{
	std::vector<int> rows(count), remaining, selected;
	std::iota(rows.begin(), rows.end(), 0);
	int target = 0;
	for (int i : rows) {
		if (mask & (1u << i))
			selected.push_back(i);
		else {
			remaining.push_back(i);
			if (i < boundary) target++;
		}
	}
	auto expected = remaining;
	expected.insert(expected.begin() + target, selected.begin(), selected.end());
	PlaylistReorderController controller;
	PlaylistReorderCommand command;
	bool started = controller.Begin(Context(count), selected, boundary, command);
	assert(started == (rows != expected));
	if (!started) return;
	assert(command.selectedIndices == selected && command.selectionTarget == target);
	int steps = 0;
	for (;;) {
		steps++;
		assert(steps <= static_cast<int>(selected.size()));
		auto plan = ResolvePlaylistReorderPlan(command.sourceIndex, command.rangeLength,
			command.insertBefore, count);
		assert(plan.shouldMove && plan.targetIndex == command.targetIndex);
		MoveBlock(rows, command.sourceIndex, command.rangeLength, plan.targetIndex);
		auto reply = Reply(command);
		reply.snapshotId = "step-" + std::to_string(steps);
		auto update = controller.Complete(reply);
		assert(controller.Complete(reply).action == PlaylistReorderAction::Ignore);
		if (update.action == PlaylistReorderAction::Commit) {
			assert(rows == expected && !controller.Pending() && !update.partialUpdate);
			assert(update.snapshotId == reply.snapshotId);
			break;
		}
		assert(update.action == PlaylistReorderAction::Continue && controller.Pending());
		assert(update.next.requestId > command.requestId);
		assert(update.next.snapshotId == reply.snapshotId);
		command = update.next;
	}
}

static void
TestAllSelectionMoves()
{
	for (int rows = 1; rows <= 8; rows++) {
		for (unsigned mask = 1; mask < (1u << rows); mask++) {
			for (int boundary = -1; boundary <= rows + 1; boundary++)
				CheckSelectionMove(rows, mask, boundary);
		}
	}
	for (const std::vector<int32_t>& indices : std::vector<std::vector<int32_t>>{
		{}, {-1, 2}, {1, 1}, {3, 1}, {1, 8}}) {
		PlaylistReorderController controller;
		PlaylistReorderCommand command;
		assert(!controller.Begin(Context(), indices, 8, command));
		assert(!controller.Pending());
	}
}

static void
TestSelectionFailureAndMissingSnapshot()
{
	const std::vector<int32_t> selected = {0, 2, 5, 7};
	for (int failureStep = 0; failureStep < 4; failureStep++) {
		PlaylistReorderController controller;
		PlaylistReorderCommand command;
		assert(controller.Begin(Context(), selected, 8, command));
		for (int step = 0; step < failureStep; step++) {
			auto update = controller.Complete(Reply(command));
			assert(update.action == PlaylistReorderAction::Continue);
			command = update.next;
		}
		auto update = controller.Complete(Reply(command, false, 429));
		assert(update.action == (failureStep == 0
			? PlaylistReorderAction::Rollback : PlaylistReorderAction::Reload));
		assert(update.restoreIndices == selected && update.partialUpdate == (failureStep > 0));
		assert(!controller.Pending() && update.snapshotId.empty());
		// The UI removes the compacted block and restores each original position.
		std::vector<int> restored = {1, 3, 4, 6};
		for (size_t i = 0; i < selected.size(); i++)
			restored.insert(restored.begin() + update.restoreIndices[i], selected[i]);
		assert((restored == std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7}));
	}
	PlaylistReorderController controller;
	PlaylistReorderCommand command;
	assert(controller.Begin(Context(), selected, 8, command));
	auto reply = Reply(command);
	reply.snapshotId.clear();
	auto update = controller.Complete(reply);
	assert(update.action == PlaylistReorderAction::Reload && update.partialUpdate);
	assert(!controller.Pending());
}

static void
TestUnavailableDispatch()
{
	PlaylistReorderController controller;
	auto command = Begin(controller);
	int calls = 0;
	auto complete = [&](const PlaylistReorderResult&) { calls++; };
	auto dispatch = [&](const PlaylistReorderCommand&, JsonCallback) { calls++; };
	bool sent = DispatchPlaylistReorder(command, {}, complete);
	assert(!sent);
	assert(calls == 0 && controller.Pending());
	assert(controller.Complete(Reply(command, false, -1)).action == PlaylistReorderAction::Rollback);
	command.requestId = 0;
	sent = DispatchPlaylistReorder(command, dispatch, complete);
	assert(!sent);
	assert(calls == 0);
}

int
main()
{
	TestAllSmallMovesAndRollbacks();
	TestBoundsAndSelectionSteps();
	TestGatesAndIdentity();
	TestFailureClassification();
	TestDispatchAndResponseMapping();
	TestAllSelectionMoves();
	TestSelectionFailureAndMissingSnapshot();
	TestUnavailableDispatch();
	std::puts("Playlist reorder controller tests passed.");
	return 0;
}
