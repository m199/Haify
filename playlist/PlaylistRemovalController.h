#pragma once

#include "PlaylistPageState.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

struct PlaylistRemovalContext {
	std::string playlistId;
	std::string snapshotId;
	PlaylistPagePosition page;
	int32_t rowCount = 0;
	bool owned = false;
	bool otherMutationPending = false;
};

struct PlaylistRemovalCommand {
	int64_t requestId = 0;
	std::string playlistId;
	std::string snapshotId;
	std::vector<std::pair<std::string, int>> items;
	// Nonempty only for a complete, usable local snapshot; otherwise read first.
	std::vector<std::string> knownPlaylistUris;
};

struct PlaylistRemovalResult {
	int64_t requestId = 0;
	std::string playlistId;
	bool ok = false;
	int32_t status = -1;
	bool partialUpdate = false;
};

enum class PlaylistRemovalAction { Ignore, Commit, Rollback, Reload };

struct PlaylistRemovalUpdate {
	PlaylistRemovalAction action = PlaylistRemovalAction::Ignore;
	PlaylistPagePosition page;
	std::vector<int32_t> removedPositions;
	int32_t PositionAfterRemoval(int32_t position) const;
};

// Window-thread domain state. The window alone owns detached BRows/selection,
// performs cache operations, and applies commit or rollback before reloading.
class PlaylistRemovalController {
public:
	// Rejects without changing output or pending state. Tokens are never reused.
	bool Begin(const PlaylistRemovalContext& context,
		const std::vector<std::pair<std::string, int>>& items,
		const std::vector<std::string>& visibleUris, PlaylistRemovalCommand& command);
	bool Pending() const { return fPending.requestId != 0; }
	PlaylistRemovalUpdate Complete(const PlaylistRemovalResult& result,
		const PlaylistPagePosition& page, int32_t remainingRowCount);

private:
	int64_t fNextRequest = 0;
	PlaylistRemovalCommand fPending;
};

using PlaylistRemovalHandler = std::function<void(const PlaylistRemovalCommand&,
	JsonCallback)>;
using PlaylistRemovalCompletion = std::function<void(const PlaylistRemovalResult&)>;

// Adapters borrow the command during dispatch; completion owns a copy and
// captures no controller/window. Adapters must copy any retained request data.
// False means no dispatch/callback; caller must roll its optimistic rows back.
bool DispatchPlaylistRemoval(const PlaylistRemovalCommand& command,
	const PlaylistRemovalHandler& removeAtPositions,
	const PlaylistRemovalHandler& removeFromKnownSnapshot,
	PlaylistRemovalCompletion complete);
