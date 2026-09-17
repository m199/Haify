#include "playlist/PlaylistRemovalController.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <limits>

#ifdef NDEBUG
#error Playlist removal tests require assertions; compile without NDEBUG.
#endif

using Items = std::vector<std::pair<std::string, int>>;

static PlaylistRemovalContext
Context(int32_t rows = 4)
{
	PlaylistRemovalContext context;
	context.playlistId = "list";
	context.snapshotId = "snapshot";
	context.page = {rows, rows, false};
	context.rowCount = rows;
	context.owned = true;
	return context;
}

static PlaylistRemovalResult
Reply(const PlaylistRemovalCommand& command, bool ok = true, int32_t status = 200)
{
	PlaylistRemovalResult result;
	result.requestId = command.requestId;
	result.playlistId = command.playlistId;
	result.ok = ok;
	result.status = status;
	return result;
}

static PlaylistRemovalCommand
Begin(PlaylistRemovalController& controller, const PlaylistRemovalContext& context,
	const Items& items, const std::vector<std::string>& uris = {})
{
	PlaylistRemovalCommand command;
	bool started = controller.Begin(context, items, uris, command);
	assert(started && controller.Pending());
	assert(command.requestId > 0 && command.playlistId == context.playlistId);
	assert(command.items == items && command.snapshotId == context.snapshotId);
	return command;
}

static void
TestCompleteSnapshotRouting()
{
	const std::vector<std::string> uris = {"spotify:track:a", "spotify:episode:b",
		"spotify:track:a", "spotify:track:c"};
	for (int scenario = 0; scenario < 6; scenario++) {
		auto context = Context();
		auto visible = uris;
		if (scenario == 1) context.snapshotId.clear();
		if (scenario == 2) context.page.total = 9;
		if (scenario == 3) context.page.offset = 3;
		if (scenario == 4) visible[1].clear();
		if (scenario == 5) visible.pop_back();
		PlaylistRemovalController controller;
		auto command = Begin(controller, context, {{uris[2], 2}}, visible);
		assert(command.knownPlaylistUris.empty() == (scenario != 0));
		int positionCalls = 0;
		int snapshotCalls = 0;
		int completions = 0;
		auto positional = [&](const PlaylistRemovalCommand& request, JsonCallback done) {
			positionCalls++;
			assert(request.items == command.items);
			done(false, {{"status", 403}});
		};
		auto known = [&](const PlaylistRemovalCommand& request, JsonCallback done) {
			snapshotCalls++;
			assert(request.knownPlaylistUris == uris);
			done(true, {{"snapshot_id", "updated"}});
		};
		bool sent = DispatchPlaylistRemoval(command, positional, known,
			[&](const PlaylistRemovalResult& result) {
				completions++;
				assert(result.requestId == command.requestId);
				assert(result.playlistId == command.playlistId);
				assert(result.ok == (scenario == 0));
				assert(result.status == (scenario == 0 ? -1 : 403));
			});
		assert(sent && completions == 1);
		assert(snapshotCalls == (scenario == 0 ? 1 : 0));
		assert(positionCalls == (scenario == 0 ? 0 : 1));
	}
}

static void
TestAllRemovalMasks()
{
	// Compare the plan with erasing rows from a concrete list, including all rows.
	for (unsigned mask = 1; mask < 256; mask++) {
		std::vector<int> expected;
		Items items;
		for (int position = 7; position >= 0; position--) {
			if ((mask & (1u << position)) != 0)
				items.push_back({"spotify:track:duplicate", position});
		}
		for (int position = 0; position < 8; position++) {
			if ((mask & (1u << position)) == 0)
				expected.push_back(position);
		}
		PlaylistRemovalController controller;
		auto context = Context(8);
		auto command = Begin(controller, context, items);
		auto update = controller.Complete(Reply(command), context.page,
			static_cast<int32_t>(expected.size()));
		assert(update.action == PlaylistRemovalAction::Commit && !controller.Pending());
		assert(update.page.total == static_cast<int32_t>(expected.size()));
		assert(update.page.offset == update.page.total && !update.page.hasMore);
		assert(update.PositionAfterRemoval(-1) == -1);
		for (size_t index = 0; index < expected.size(); index++)
			assert(update.PositionAfterRemoval(expected[index]) == static_cast<int32_t>(index));
	}
}

static void
TestPartialPageAndUnknownTotal()
{
	PlaylistRemovalController controller;
	auto context = Context(5);
	context.page = {8, 12, true};
	auto command = Begin(controller, context, {{"spotify:track:a", 2},
		{"spotify:episode:b", 6}});
	auto update = controller.Complete(Reply(command), context.page, 3);
	assert(update.page.offset == 6 && update.page.total == 10 && update.page.hasMore);
	assert(update.PositionAfterRemoval(7) == 5);

	context.page = {4, 0, true};
	command = Begin(controller, context, {{"spotify:track:a", 1},
		{"spotify:track:b", 4}});
	update = controller.Complete(Reply(command), context.page, 3);
	assert(update.page.offset == 3 && update.page.total == 3 && !update.page.hasMore);
}

static void
TestFailureOutcomes()
{
	for (int status : {-1, 0, 400, 401, 403, 404, 409, 429, 500, 503}) {
		for (bool partial : {false, true}) {
			PlaylistRemovalController controller;
			auto context = Context();
			auto command = Begin(controller, context, {{"spotify:track:a", 0}});
			auto result = Reply(command, false, status);
			result.partialUpdate = partial;
			auto update = controller.Complete(result, context.page, 3);
			assert(update.action == (status == 409 || partial
				? PlaylistRemovalAction::Reload : PlaylistRemovalAction::Rollback));
			assert(update.removedPositions.empty() && !controller.Pending());
		}
	}
}

static void
TestIdentityAndDuplicateResponses()
{
	PlaylistRemovalController controller;
	auto context = Context();
	auto first = Begin(controller, context, {{"spotify:track:a", 1}});
	auto wrong = Reply(first);
	wrong.playlistId = "other";
	assert(controller.Complete(wrong, context.page, 3).action == PlaylistRemovalAction::Ignore);
	wrong = Reply(first);
	wrong.requestId++;
	assert(controller.Complete(wrong, context.page, 3).action == PlaylistRemovalAction::Ignore);
	assert(controller.Pending());
	assert(controller.Complete(Reply(first), context.page, 3).action == PlaylistRemovalAction::Commit);
	assert(controller.Complete(Reply(first), context.page, 3).action == PlaylistRemovalAction::Ignore);
	auto second = Begin(controller, context, {{"spotify:track:a", 1}});
	assert(second.requestId != first.requestId);
	assert(controller.Complete(Reply(first), context.page, 3).action == PlaylistRemovalAction::Ignore);
	assert(controller.Pending());
	assert(controller.Complete(Reply(second, false), context.page, 3).action == PlaylistRemovalAction::Rollback);
}

static void
TestRejectedCommands()
{
	for (int scenario = 0; scenario < 9; scenario++) {
		PlaylistRemovalController controller;
		auto context = Context();
		Items items = {{"spotify:track:a", 0}};
		if (scenario == 0) context.owned = false;
		if (scenario == 1) context.otherMutationPending = true;
		if (scenario == 2) context.playlistId.clear();
		if (scenario == 3) context.rowCount = 0;
		if (scenario == 4) items.clear();
		if (scenario == 5) items[0].first.clear();
		if (scenario == 6) items[0].second = -1;
		if (scenario == 7) items.push_back(items[0]);
		if (scenario == 8) Begin(controller, context, items);
		PlaylistRemovalCommand output;
		output.playlistId = "untouched";
		assert(!controller.Begin(context, items, {}, output));
		assert(output.playlistId == "untouched" && output.requestId == 0);
		assert(controller.Pending() == (scenario == 8));
	}
}

static void
TestCallbackLifetimeAndMalformedStatus()
{
	JsonCallback callback;
	PlaylistRemovalResult actual;
	{
		PlaylistRemovalController controller;
		auto command = Begin(controller, Context(), {{"spotify:track:a", 0}});
		bool sent = DispatchPlaylistRemoval(command,
			[&](const PlaylistRemovalCommand&, JsonCallback done) { callback = done; }, {},
			[&](const PlaylistRemovalResult& result) { actual = result; });
		assert(sent);
		command.playlistId = "changed after dispatch";
	}
	const std::vector<nlohmann::json> statuses = {"409", 409.5, nullptr,
		std::numeric_limits<uint64_t>::max(), std::numeric_limits<int64_t>::min()};
	for (const auto& status : statuses) {
		callback(false, {{"status", status}, {"partial_update", "true"}});
		assert(actual.playlistId == "list" && actual.requestId > 0);
		assert(!actual.ok && actual.status == -1 && !actual.partialUpdate);
	}
	callback(false, {{"status", uint64_t(409)}, {"partial_update", true}});
	assert(actual.status == 409 && actual.partialUpdate);
	callback(false, nlohmann::json::array());
	assert(actual.status == -1 && !actual.partialUpdate);
}

static void
TestUnavailableDispatch()
{
	PlaylistRemovalController controller;
	auto context = Context();
	auto command = Begin(controller, context, {{"spotify:track:a", 0}});
	int calls = 0;
	auto done = [&](const PlaylistRemovalResult&) { calls++; };
	assert(!DispatchPlaylistRemoval(command, {}, {}, done));
	assert(calls == 0 && controller.Pending());
	// The caller reports local dispatch failure through the same completion path.
	assert(controller.Complete(Reply(command, false, -1), context.page, 3).action
		== PlaylistRemovalAction::Rollback);
}

static void
TestPendingPageInvalidation()
{
	PlaylistPageState paging;
	paging.RestorePosition({4, 10, true});
	auto pageRequest = MakePlaylistPageRequest(
		ResolvePlaylistContentTarget("spotify:playlist:list"), 4, 50);
	assert(paging.BeginPage(pageRequest));
	PlaylistRemovalController controller;
	auto context = Context();
	context.page = paging.Position();
	auto command = Begin(controller, context, {{"spotify:track:a", 1}});
	// Same state handoff as PlaylistWindow before it detaches the selected rows.
	paging.RestorePosition(paging.Position());
	PlaylistPageResult latePage;
	latePage.request = pageRequest;
	latePage.ok = true;
	latePage.total = 10;
	latePage.pageCount = 2;
	latePage.nextOffset = 6;
	assert(!paging.FinishPage(latePage).accepted);
	assert(paging.Position().offset == 4 && controller.Pending());
	auto update = controller.Complete(Reply(command), paging.Position(), 3);
	paging.RestorePosition(update.page);
	assert(paging.Position().offset == 3 && paging.Position().total == 9);
	assert(!paging.FinishPage(latePage).accepted);
	assert(paging.ShouldLoad(true));
}

int
main()
{
	TestCompleteSnapshotRouting();
	TestAllRemovalMasks();
	TestPartialPageAndUnknownTotal();
	TestFailureOutcomes();
	TestIdentityAndDuplicateResponses();
	TestRejectedCommands();
	TestCallbackLifetimeAndMalformedStatus();
	TestUnavailableDispatch();
	TestPendingPageInvalidation();
	std::puts("Playlist removal controller tests passed (all 255 removal masks).");
	return 0;
}
