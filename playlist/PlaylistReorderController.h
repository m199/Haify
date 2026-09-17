#pragma once

#include "PlaylistReorderPolicy.h"
#include "spotify/api/SpotifyApiTypes.h"

#include <cstdint>

struct PlaylistReorderContext {
	std::string playlistId;
	std::string snapshotId;
	int32_t rowCount = 0;
	bool owned = false;
	bool pageLoading = false;
	bool otherMutationPending = false;
	std::vector<int32_t> visiblePositions = {};
	int32_t loadedEnd = 0;
	int32_t total = 0;
};

struct PlaylistReorderCommand {
	int64_t requestId = 0;
	std::string playlistId;
	std::string snapshotId;
	int32_t sourceIndex = -1;
	int32_t rangeLength = 0;
	int32_t insertBefore = -1;
	int32_t targetIndex = -1;
	std::vector<int32_t> selectedIndices = {};
	int32_t selectionTarget = -1;
	std::vector<int32_t> visiblePositions = {};
};

struct PlaylistReorderResult {
	int64_t requestId = 0;
	std::string playlistId;
	bool ok = false;
	int32_t status = -1;
	std::string snapshotId;
};

enum class PlaylistReorderAction { Ignore, Continue, Commit, Rollback, Reload };

struct PlaylistReorderUpdate {
	PlaylistReorderAction action = PlaylistReorderAction::Ignore;
	int32_t restoreIndex = -1;
	std::string snapshotId;
	bool refreshSnapshot = false;
	bool partialUpdate = false;
	std::vector<int32_t> restoreIndices = {};
	std::vector<int32_t> visiblePositions = {};
	PlaylistReorderCommand next;
};

// Domain state on the window thread. BRows and selection remain window-owned.
class PlaylistReorderController {
public:
	// Invalid/no-op/busy requests leave both state and output unchanged.
	bool Begin(const PlaylistReorderContext& context, int32_t sourceIndex,
		int32_t rangeLength, int32_t insertBefore, PlaylistReorderCommand& command);
	bool Begin(const PlaylistReorderContext& context, const std::vector<int32_t>& indices,
		int32_t insertBefore, PlaylistReorderCommand& command);
	bool Pending() const { return fPending.requestId != 0; }
	PlaylistReorderUpdate Complete(const PlaylistReorderResult& result);

private:
	void _SetMove(size_t step, const std::string& snapshot);
	int64_t fNextRequest = 0;
	PlaylistReorderCommand fPending;
	std::vector<PlaylistReorderMove> fMoves;
	std::vector<int32_t> fOriginalPositions;
	size_t fStep = 0;
};

using PlaylistReorderHandler = std::function<void(const PlaylistReorderCommand&,
	JsonCallback)>;
using PlaylistReorderCompletion = std::function<void(const PlaylistReorderResult&)>;

// The handler borrows the command only during dispatch. The completion owns
// a copy and never refers to the controller/window. False means no callback.
bool DispatchPlaylistReorder(const PlaylistReorderCommand& command,
	const PlaylistReorderHandler& reorder, PlaylistReorderCompletion complete);
