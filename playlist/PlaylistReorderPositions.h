#pragma once
#include "PlaylistReorderPolicy.h"
#include <utility>

// Display indices identify BRows; Spotify positions also count unavailable items.
// The end boundary is the end of the loaded page, not the end of the playlist.
struct PlaylistPositionReorderPlan {
	int32_t selectionTarget = -1;
	std::vector<PlaylistReorderMove> moves;
	std::vector<int32_t> positions;
};

inline int32_t
PlaylistPositionAfterMove(int32_t position, const PlaylistReorderMove& move)
{
	if (position >= move.sourceIndex && position < move.sourceIndex + move.rangeLength)
		return move.targetIndex + (position - move.sourceIndex);
	if (position >= move.targetIndex && position < move.sourceIndex)
		return position + move.rangeLength;
	if (position >= move.sourceIndex + move.rangeLength && position < move.insertBefore)
		return position - move.rangeLength;
	return position;
}

inline bool
PlaylistPositionsValid(const std::vector<int32_t>& positions, int32_t loadedEnd,
	int32_t total)
{
	if (positions.empty() || loadedEnd < 0 || total < loadedEnd)
		return false;
	int32_t previous = -1;
	for (int32_t position : positions) {
		if (position <= previous || position >= loadedEnd)
			return false;
		previous = position;
	}
	return true;
}

inline PlaylistPositionReorderPlan
ResolvePlaylistPositionReorder(const std::vector<int32_t>& indices, int32_t boundary,
	const std::vector<int32_t>& positions, int32_t loadedEnd, int32_t total)
{
	PlaylistPositionReorderPlan result;
	if (!PlaylistPositionsValid(positions, loadedEnd, total))
		return result;
	const int32_t count = static_cast<int32_t>(positions.size());
	auto visible = ResolvePlaylistSelectionReorder(indices, boundary, count);
	// A drop inside the unchanged selection must not rearrange hidden entries.
	if (visible.moves.empty())
		return result;
	boundary = std::max(int32_t(0), std::min(boundary, count));
	std::vector<int32_t> sourcePositions;
	for (int32_t index : indices)
		sourcePositions.push_back(positions[index]);
	auto server = ResolvePlaylistSelectionReorder(sourcePositions,
		boundary == count ? loadedEnd : positions[boundary], total);
	result.selectionTarget = visible.targetIndex;
	result.moves = std::move(server.moves);
	result.positions = positions;
	for (const auto& move : result.moves) {
		for (int32_t& position : result.positions)
			position = PlaylistPositionAfterMove(position, move);
	}
	std::sort(result.positions.begin(), result.positions.end());
	return result;
}
