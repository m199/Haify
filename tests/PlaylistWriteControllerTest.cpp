#include "playlist/PlaylistWriteController.h"

#include <cassert>
#include <cstdio>
#include <limits>

#ifdef NDEBUG
#error Playlist write tests require assertions; compile without NDEBUG.
#endif

static void Check(bool condition) { assert(condition); }

static PlaylistWriteContext
Context()
{
	PlaylistWriteContext context;
	context.playlistId = "list";
	context.snapshotId = "before";
	context.page = {4, 4, false};
	context.rowCount = 4;
	context.owned = true;
	return context;
}

static PlaylistWriteResult
Reply(const PlaylistWriteCommand& command, bool ok, int32_t status)
{
	PlaylistWriteResult result;
	result.kind = command.kind;
	result.requestId = command.requestId;
	result.playlistId = command.playlistId;
	result.ok = ok;
	result.status = status;
	result.snapshotId = ok ? "after" : "";
	return result;
}

static void
TestClear()
{
	for (int status : {200, 401, 403, 409, 429, 500, -1}) {
		PlaylistClearController controller;
		PlaylistWriteCommand command;
		Check(controller.Begin(Context(), command));
		Check(controller.Pending() && command.kind == PlaylistWriteKind::Clear);
		Check(command.optimisticPage.total == 0 && command.optimisticPage.offset == 0);
		Check(!controller.Begin(Context(), command));
		auto result = Reply(command, status == 200, status);
		auto foreign = result;
		foreign.kind = PlaylistWriteKind::Add;
		Check(controller.Complete(foreign).action == PlaylistWriteAction::Ignore);
		foreign = result;
		foreign.requestId++;
		Check(controller.Complete(foreign).action == PlaylistWriteAction::Ignore);
		foreign = result;
		foreign.playlistId = "other";
		Check(controller.Complete(foreign).action == PlaylistWriteAction::Ignore);
		Check(controller.Pending());
		auto update = controller.Complete(result);
		Check(!controller.Pending());
		Check(update.action == (status == 200 ? PlaylistWriteAction::Commit
			: status == 409 ? PlaylistWriteAction::Reload : PlaylistWriteAction::Rollback));
		Check(update.page.total == (status == 200 ? 0 : 4));
		Check(update.page.offset == (status == 200 ? 0 : 4));
		Check(update.snapshotId == (status == 200 ? "after" : status == 409 ? "" : "before"));
		Check(controller.Complete(result).action == PlaylistWriteAction::Ignore);
		PlaylistWriteCommand next;
		Check(controller.Begin(Context(), next) && next.requestId > command.requestId);
		Check(controller.Complete(result).action == PlaylistWriteAction::Ignore);
	}
	PlaylistClearController controller;
	PlaylistWriteCommand command;
	auto empty = Context();
	empty.rowCount = 0;
	empty.page = {};
	Check(!controller.Begin(empty, command));
	// Known items can be unavailable, or not loaded yet.
	empty.page.total = 10;
	Check(controller.Begin(empty, command));
	auto result = Reply(command, true, 200);
	result.snapshotId.clear();
	auto update = controller.Complete(result);
	Check(update.refreshSnapshot && update.snapshotId.empty());
}

static void
TestAdd()
{
	for (bool partial : {false, true}) {
		for (int status : {201, 403, 409, -1}) {
			auto context = Context();
			if (partial) context.page = {4, 20, true};
			PlaylistAddController controller;
			PlaylistWriteCommand command;
			Check(controller.Begin(context, "spotify:episode:item", command));
			Check(command.kind == PlaylistWriteKind::Add && command.appendVisible == !partial);
			Check(command.appendPosition == (partial ? 20 : 4));
			Check(command.optimisticPage.total == (partial ? 21 : 5));
			Check(!controller.Begin(context, "spotify:track:item", command));
			auto result = Reply(command, status == 201, status);
			auto foreign = result;
			foreign.kind = PlaylistWriteKind::Clear;
			Check(controller.Complete(foreign).action == PlaylistWriteAction::Ignore);
			auto update = controller.Complete(result);
			Check(!controller.Pending());
			Check(update.page.offset == (!partial && status == 201 ? 5 : 4));
			Check(update.page.total == context.page.total + (status == 201 ? 1 : 0));
			Check(update.page.hasMore == partial);
			Check(update.action == (status == 201 ? PlaylistWriteAction::Commit
				: status == 409 ? PlaylistWriteAction::Reload : PlaylistWriteAction::Rollback));
			Check(controller.Complete(result).action == PlaylistWriteAction::Ignore);
		}
	}
	PlaylistAddController controller;
	PlaylistWriteCommand command;
	for (const char* uri : {"", "spotify:track:", "spotify:album:item", "https://example.com"})
		Check(!controller.Begin(Context(), uri, command));
	auto context = Context();
	context.page.total = std::numeric_limits<int32_t>::max();
	Check(!controller.Begin(context, "spotify:track:item", command));
	context = Context();
	context.page = {};
	context.rowCount = 0;
	Check(controller.Begin(context, "spotify:track:item", command));
	Check(command.appendVisible && command.appendPosition == 0);
	auto result = Reply(command, true, 201);
	result.snapshotId.clear();
	Check(controller.Complete(result).refreshSnapshot);
}

static void
TestGuards()
{
	for (int scenario = 0; scenario < 6; scenario++) {
		auto context = Context();
		if (scenario == 0) context.owned = false;
		if (scenario == 1) context.otherMutationPending = true;
		if (scenario == 2) context.playlistId.clear();
		if (scenario == 3) context.rowCount = -1;
		if (scenario == 4) context.page.offset = -1;
		if (scenario == 5) context.page.total = -1;
		PlaylistClearController clear;
		PlaylistAddController add;
		PlaylistWriteCommand command;
		Check(!clear.Begin(context, command));
		Check(!add.Begin(context, "spotify:track:item", command));
		Check(!clear.Pending() && !add.Pending());
	}
}

static void
TestDispatch()
{
	for (auto kind : {PlaylistWriteKind::Clear, PlaylistWriteKind::Add}) {
		PlaylistClearController clear;
		PlaylistAddController add;
		PlaylistWriteCommand command;
		Check(kind == PlaylistWriteKind::Clear ? clear.Begin(Context(), command)
			: add.Begin(Context(), "spotify:track:item", command));
		JsonCallback held;
		auto dispatch = [&](const PlaylistWriteCommand& sent, JsonCallback done) {
			Check(sent.kind == kind && sent.playlistId == "list");
			Check(sent.uri == (kind == PlaylistWriteKind::Add ? "spotify:track:item" : ""));
			held = done;
		};
		int completions = 0;
		auto complete = [&](const PlaylistWriteResult& result) {
			Check(result.kind == kind && result.playlistId == "list" && result.requestId == 1);
			Check(result.ok && result.status == 200 && result.snapshotId == "wrapped");
			completions++;
		};
		Check(!DispatchPlaylistWrite(command, {}, complete));
		Check(!DispatchPlaylistWrite(command, dispatch, {}));
		Check(DispatchPlaylistWrite(command, dispatch, complete));
		command.playlistId = "changed after dispatch";
		held(true, {{"status", 200}, {"body", "{\"snapshot_id\":\"wrapped\"}"}});
		Check(completions == 1);
	}
}

int main()
{
	TestClear();
	TestAdd();
	TestGuards();
	TestDispatch();
	std::puts("Playlist clear/add controller tests passed.");
}
