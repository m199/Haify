#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

struct PlaylistReorderPlan {
	bool shouldMove = false;
	int targetIndex = -1;
	int insertBefore = -1;
};

// insertBefore refers to the original list, targetIndex to the list after
// removing the block. Inserting into the block itself leaves it unchanged.
inline PlaylistReorderPlan
ResolvePlaylistReorderPlan(int sourceIndex, int rangeLength, int insertBefore,
	int rowCount)
{
	PlaylistReorderPlan plan;
	if (sourceIndex < 0 || rangeLength < 1 || rowCount < 1
			|| sourceIndex > rowCount || rangeLength > rowCount - sourceIndex)
		return plan;
	plan.insertBefore = std::max(0, std::min(insertBefore, rowCount));
	if (plan.insertBefore >= sourceIndex
			&& plan.insertBefore <= sourceIndex + rangeLength) {
		plan.targetIndex = sourceIndex;
		return plan;
	}
	plan.targetIndex = plan.insertBefore > sourceIndex
		? plan.insertBefore - rangeLength : plan.insertBefore;
	plan.shouldMove = true;
	return plan;
}

struct PlaylistReorderStep {
	bool shouldMove = false;
	bool noncontiguous = false;
	int sourceIndex = -1;
	int rangeLength = 0;
	int insertBefore = -1;
};

struct PlaylistReorderMove {
	int32_t sourceIndex = -1;
	int32_t rangeLength = 0;
	int32_t insertBefore = -1;
	int32_t targetIndex = -1;
};

struct PlaylistSelectionReorderPlan {
	int32_t targetIndex = -1;
	std::vector<PlaylistReorderMove> moves;
};

// Indices identify occurrences, not URIs: duplicate songs remain distinct.
// Gather runs at the first selected row, then move the block to the drop boundary.
// Each gather leaves all later source indices unchanged.
inline PlaylistSelectionReorderPlan
ResolvePlaylistSelectionReorder(const std::vector<int32_t>& indices,
	int32_t insertBefore, int32_t rowCount)
{
	PlaylistSelectionReorderPlan plan;
	int32_t previous = -1;
	for (int32_t index : indices) {
		if (index <= previous || index >= rowCount)
			return plan;
		previous = index;
	}
	if (indices.empty())
		return plan;
	int32_t boundary = std::max(int32_t(0), std::min(insertBefore, rowCount));
	int32_t countBefore = std::lower_bound(indices.begin(), indices.end(), boundary)
		- indices.begin();
	plan.targetIndex = boundary - countBefore;
	for (size_t start = 0; start < indices.size();) {
		size_t end = start + 1;
		while (end < indices.size() && indices[end] == indices[end - 1] + 1)
			end++;
		int32_t length = static_cast<int32_t>(end - start);
		int32_t destination = indices.front() + static_cast<int32_t>(start);
		auto gather = ResolvePlaylistReorderPlan(indices[start], length, destination, rowCount);
		if (gather.shouldMove)
			plan.moves.push_back({indices[start], length, gather.insertBefore, gather.targetIndex});
		start = end;
	}
	int32_t count = static_cast<int32_t>(indices.size());
	int32_t finalBoundary = plan.targetIndex > indices.front()
		? plan.targetIndex + count : plan.targetIndex;
	auto move = ResolvePlaylistReorderPlan(indices.front(), count, finalBoundary, rowCount);
	if (move.shouldMove)
		plan.moves.push_back({indices.front(), count, move.insertBefore, move.targetIndex});
	return plan;
}

inline PlaylistReorderStep
ResolvePlaylistReorderStep(int first, int last, int selectedCount,
	int rowCount, int delta)
{
	PlaylistReorderStep step;
	if (delta == 0 || first < 0 || last < first || last >= rowCount
			|| selectedCount < 1 || selectedCount > rowCount)
		return step;
	step.noncontiguous = last - first + 1 != selectedCount;
	if (step.noncontiguous || (delta < 0 && first == 0)
			|| (delta > 0 && last == rowCount - 1))
		return step;
	step.shouldMove = true;
	step.sourceIndex = first;
	step.rangeLength = selectedCount;
	step.insertBefore = delta > 0 ? last + 2 : first - 1;
	return step;
}
