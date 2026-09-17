#pragma once

#include "PlaylistPageState.h"
#include "spotify/api/SpotifyApiTypes.h"

#include <functional>

enum class PlaylistWriteKind { Clear, Add };
struct PlaylistWriteContext {
	std::string playlistId;
	std::string snapshotId;
	PlaylistPagePosition page;
	int32_t rowCount = 0;
	bool owned = false;
	bool otherMutationPending = false;
};
struct PlaylistWriteCommand {
	PlaylistWriteKind kind = PlaylistWriteKind::Clear;
	int64_t requestId = 0;
	std::string playlistId;
	std::string uri;
	bool appendVisible = false;
	int32_t appendPosition = -1;
	PlaylistPagePosition optimisticPage;
};
struct PlaylistWriteResult {
	PlaylistWriteKind kind = PlaylistWriteKind::Clear;
	int64_t requestId = 0;
	std::string playlistId;
	bool ok = false;
	int32_t status = -1;
	std::string snapshotId;
};
enum class PlaylistWriteAction { Ignore, Commit, Rollback, Reload };
struct PlaylistWriteUpdate {
	PlaylistWriteAction action = PlaylistWriteAction::Ignore;
	PlaylistPagePosition page;
	std::string snapshotId;
	bool refreshSnapshot = false;
};

// Each operation owns independent state on the window looper; BRows stay in UI.
class PlaylistClearController {
public:
	bool Begin(const PlaylistWriteContext& context, PlaylistWriteCommand& command);
	bool Pending() const { return fPending.requestId > 0; }
	PlaylistWriteUpdate Complete(const PlaylistWriteResult& result);
private:
	int64_t fNextRequest = 0;
	PlaylistWriteContext fBefore;
	PlaylistWriteCommand fPending;
};
class PlaylistAddController {
public:
	bool Begin(const PlaylistWriteContext& context, const std::string& uri,
		PlaylistWriteCommand& command);
	bool Pending() const { return fPending.requestId > 0; }
	PlaylistWriteUpdate Complete(const PlaylistWriteResult& result);
private:
	int64_t fNextRequest = 0;
	PlaylistWriteContext fBefore;
	PlaylistWriteCommand fPending;
};

using PlaylistWriteHandler = std::function<void(const PlaylistWriteCommand&, JsonCallback)>;
using PlaylistWriteCompletion = std::function<void(const PlaylistWriteResult&)>;
// False means no dispatch; callbacks own request identity, never window/row pointers.
bool DispatchPlaylistWrite(const PlaylistWriteCommand& command,
	const PlaylistWriteHandler& handler, PlaylistWriteCompletion complete);
