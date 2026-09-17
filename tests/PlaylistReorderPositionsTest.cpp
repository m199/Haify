#include "playlist/PlaylistReorderPositions.h"
#include "playlist/PlaylistReorderController.h"
#include <cassert>
#include <cstdio>
#include <numeric>

#ifdef NDEBUG
#error Playlist position tests require assertions; compile without NDEBUG.
#endif
static void Check(bool condition) { assert(condition); }

// Oracle: remove the chosen occurrences, then insert them at the original
// boundary, preserving both groups' order. Identity is independent of the planner.
static std::vector<int32_t>
Expected(const std::vector<int32_t>& rows, const std::vector<int32_t>& selected, int32_t boundary)
{
	std::vector<int32_t> result, moved;
	int32_t destination = 0;
	for (size_t i = 0; i < rows.size(); i++) {
		if (std::find(selected.begin(), selected.end(), rows[i]) != selected.end())
			moved.push_back(rows[i]);
		else {
			result.push_back(rows[i]);
			if (static_cast<int32_t>(i) < boundary) destination++;
		}
	}
	result.insert(result.begin() + destination, moved.begin(), moved.end());
	return result;
}

static void
VerifyPlan(const std::vector<int32_t>& visible, const std::vector<int32_t>& indices,
	int32_t boundary, int32_t loadedEnd)
{
	std::vector<int32_t> selected;
	for (int32_t index : indices) selected.push_back(visible[index]);
	auto expectedVisible = Expected(visible, selected, boundary);
	auto plan = ResolvePlaylistPositionReorder(indices, boundary, visible, loadedEnd, 9);
	if (expectedVisible == visible) {
		Check(plan.moves.empty());
		return;
	}
	Check(!plan.moves.empty());
	std::vector<int32_t> server(9);
	std::iota(server.begin(), server.end(), 0);
	int32_t serverBoundary = boundary == static_cast<int32_t>(visible.size())
		? loadedEnd : visible[boundary];
	auto expectedServer = Expected(server, selected, serverBoundary);
	for (const auto& move : plan.moves) {
		std::vector<int32_t> block(server.begin() + move.sourceIndex,
			server.begin() + move.sourceIndex + move.rangeLength);
		server.erase(server.begin() + move.sourceIndex,
			server.begin() + move.sourceIndex + move.rangeLength);
		server.insert(server.begin() + move.targetIndex, block.begin(), block.end());
	}
	Check(server == expectedServer);
	std::vector<int32_t> displayed, positions;
	for (int32_t position = 0; position < static_cast<int32_t>(server.size()); position++) {
		if (std::find(visible.begin(), visible.end(), server[position]) == visible.end()) continue;
		displayed.push_back(server[position]);
		positions.push_back(position);
	}
	Check(displayed == expectedVisible && positions == plan.positions);
}

static void TestEverySmallVisibilityAndSelection()
{
	for (int32_t loaded = 1; loaded <= 7; loaded++) {
		for (int mask = 1; mask < (1 << loaded); mask++) {
			std::vector<int32_t> visible;
			for (int i = 0; i < loaded; i++)
				if (mask & (1 << i)) visible.push_back(i);
			for (int selection = 1; selection < (1 << visible.size()); selection++) {
				std::vector<int32_t> indices;
				for (size_t i = 0; i < visible.size(); i++)
					if (selection & (1 << i)) indices.push_back(static_cast<int32_t>(i));
				for (int32_t boundary = 0; boundary <= static_cast<int32_t>(visible.size()); boundary++)
					VerifyPlan(visible, indices, boundary, loaded);
			}
		}
	}
}

static void TestControllerAndGuards()
{
	PlaylistReorderContext context;
	context.playlistId = "list";
	context.snapshotId = "before";
	context.rowCount = 4;
	context.owned = true;
	context.visiblePositions = {0, 2, 5, 7};
	context.loadedEnd = 8;
	context.total = 12;
	for (bool success : {false, true}) {
		PlaylistReorderController controller;
		PlaylistReorderCommand command;
		Check(controller.Begin(context, std::vector<int32_t>{0, 2}, 4, command));
		Check(command.sourceIndex == 5 && command.rangeLength == 1);
		Check(command.selectionTarget == 2);
		auto finalPositions = command.visiblePositions;
		while (controller.Pending()) {
			PlaylistReorderResult result;
			result.requestId = command.requestId;
			result.playlistId = command.playlistId;
			result.ok = success;
			result.snapshotId = "next";
			auto update = controller.Complete(result);
			if (update.action == PlaylistReorderAction::Continue) command = update.next;
			else {
				Check(update.action == (success ? PlaylistReorderAction::Commit : PlaylistReorderAction::Rollback));
				Check(update.visiblePositions == (success ? finalPositions : context.visiblePositions));
			}
		}
	}
	for (const auto& invalid : std::vector<std::vector<int32_t>>{{0, 2, 1, 7}, {0, 2, 2, 7}, {-1, 2, 5, 7}, {0, 2, 5, 8}})
		Check(ResolvePlaylistPositionReorder({0}, 4, invalid, 8, 12).moves.empty());
	Check(ResolvePlaylistPositionReorder({0}, 4, context.visiblePositions, 8, 7).moves.empty());
	Check(ResolvePlaylistPositionReorder({4}, 0, context.visiblePositions, 8, 12).moves.empty());
	context.rowCount++;
	PlaylistReorderController controller;
	PlaylistReorderCommand command;
	Check(!controller.Begin(context, std::vector<int32_t>{0}, 3, command));
}

int main()
{
	TestEverySmallVisibilityAndSelection();
	TestControllerAndGuards();
	std::puts("Playlist position mapping tests passed (all visibility/selection masks up to seven loaded items).");
}
