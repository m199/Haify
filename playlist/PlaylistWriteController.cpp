#include "PlaylistWriteController.h"
#include "PlaylistMutationResponse.h"

#include <algorithm>
#include <limits>

namespace {
bool CanBegin(const PlaylistWriteContext& context, int64_t next, bool pending)
{
	return !pending && !context.otherMutationPending && context.owned
		&& !context.playlistId.empty() && context.rowCount >= 0
		&& context.page.offset >= 0 && context.page.total >= 0
		&& next < std::numeric_limits<int64_t>::max();
}

PlaylistWriteUpdate Finish(const PlaylistWriteResult& result,
	PlaylistWriteCommand& pending, const PlaylistWriteContext& before)
{
	PlaylistWriteUpdate update;
	if (pending.requestId <= 0 || result.requestId != pending.requestId
			|| result.kind != pending.kind || result.playlistId != pending.playlistId)
		return update;
	update.page = before.page;
	update.snapshotId = before.snapshotId;
	if (result.ok) {
		update.action = PlaylistWriteAction::Commit;
		update.page = pending.optimisticPage;
		if (pending.appendVisible)
			update.page.offset = update.page.total;
		update.page.hasMore = update.page.offset < update.page.total;
		update.snapshotId = result.snapshotId;
		update.refreshSnapshot = result.snapshotId.empty();
	} else {
		update.action = result.status == 409
			? PlaylistWriteAction::Reload : PlaylistWriteAction::Rollback;
		if (update.action == PlaylistWriteAction::Reload)
			update.snapshotId.clear();
	}
	pending = {};
	return update;
}
}

bool
PlaylistClearController::Begin(const PlaylistWriteContext& context,
	PlaylistWriteCommand& command)
{
	if (!CanBegin(context, fNextRequest, Pending())
			|| (context.rowCount == 0 && context.page.total == 0))
		return false;
	fBefore = context;
	fPending.kind = PlaylistWriteKind::Clear;
	fPending.requestId = ++fNextRequest;
	fPending.playlistId = context.playlistId;
	command = fPending;
	return true;
}

PlaylistWriteUpdate
PlaylistClearController::Complete(const PlaylistWriteResult& result)
{
	return Finish(result, fPending, fBefore);
}

bool
PlaylistAddController::Begin(const PlaylistWriteContext& context,
	const std::string& uri, PlaylistWriteCommand& command)
{
	if (!CanBegin(context, fNextRequest, Pending()) || SpotifyItemIdForUri(uri).empty()
			|| !SpotifyItemCanAddToPlaylist(SpotifyItemKindForUri(uri)))
		return false;
	int32_t position = std::max({context.page.total, context.page.offset, context.rowCount});
	if (position == std::numeric_limits<int32_t>::max())
		return false;
	fBefore = context;
	fPending.kind = PlaylistWriteKind::Add;
	fPending.requestId = ++fNextRequest;
	fPending.playlistId = context.playlistId;
	fPending.uri = uri;
	fPending.appendPosition = position;
	fPending.appendVisible = !context.page.hasMore && context.page.offset >= context.page.total;
	fPending.optimisticPage = {context.page.offset, position + 1, true};
	command = fPending;
	return true;
}

PlaylistWriteUpdate
PlaylistAddController::Complete(const PlaylistWriteResult& result)
{
	return Finish(result, fPending, fBefore);
}

bool
DispatchPlaylistWrite(const PlaylistWriteCommand& command,
	const PlaylistWriteHandler& handler, PlaylistWriteCompletion complete)
{
	if (command.requestId <= 0 || command.playlistId.empty() || !handler || !complete)
		return false;
	handler(command, [command, complete](bool ok, const nlohmann::json& data) {
		PlaylistWriteResult result;
		result.kind = command.kind;
		result.requestId = command.requestId;
		result.playlistId = command.playlistId;
		result.ok = ok;
		result.status = PlaylistMutationResponse::Status(data);
		if (ok) result.snapshotId = PlaylistMutationResponse::SnapshotId(data);
		complete(result);
	});
	return true;
}
