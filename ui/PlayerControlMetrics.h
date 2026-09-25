#pragma once

#include "ui/ReplicantHandleMetrics.h"

#include <algorithm>
#include <cmath>

// Font measurements are supplied by the view; these rules need no Haiku runtime.
// Preserve fractional layout units here. Unlike UiScale::Scaled, the existing
// player layout rounds only where each individual rule requires it.
inline float
PlaybackControlHeight(float scale, float lineHeight)
{
	return std::max(20.0f * scale, lineHeight + 8.0f);
}

struct PlayerBarMetrics {
	float controlHeight = 0, buttonWidth = 0, iconSize = 0, timeWidth = 0;
	float outerInset = 0, rowSpacing = 0, barHeight = 0;
	float seekMinWidth = 0, seekPreferredWidth = 0;
	float volumeTopInset = 0, volumeThickness = 0;
	float volumeMinWidth = 0, volumePreferredWidth = 0, volumeMaxWidth = 0;
	float draggerSize = 0, draggerRightInset = 0, draggerBottomInset = 0;
	float trackInfoInset = 0, addButtonInset = 0;
};

inline PlayerBarMetrics
ResolvePlayerBarMetrics(float scale, float lineHeight, float timeTextWidth)
{
	scale = std::max(1.0f, scale);
	PlayerBarMetrics size;
	size.controlHeight = PlaybackControlHeight(scale, lineHeight);
	size.buttonWidth = std::max(24.0f * scale, size.controlHeight + 2.0f);
	size.iconSize = std::max(18.0f * scale, size.controlHeight - 8.0f);
	size.timeWidth = std::max(36.0f * scale, std::ceil(timeTextWidth + 8.0f));
	size.outerInset = std::max(5.0f * scale, std::ceil(lineHeight * 0.28f));
	size.rowSpacing = std::max(3.0f * scale, std::ceil(lineHeight * 0.18f));
	size.barHeight = std::max(62.0f * scale,
		size.controlHeight * 2.0f + size.rowSpacing + size.outerInset * 2.0f + 4.0f);
	size.seekMinWidth = 120.0f * scale;
	size.seekPreferredWidth = 240.0f * scale;
	size.volumeTopInset = std::max(4.0f * scale, std::ceil(lineHeight * 0.22f));
	size.volumeThickness = std::max(4.0f, std::floor(size.controlHeight * 0.20f));
	size.volumeMinWidth = std::max(90.0f * scale, size.controlHeight * 4.0f);
	size.volumePreferredWidth = std::max(110.0f * scale, size.controlHeight * 5.0f);
	size.volumeMaxWidth = std::max(120.0f * scale, size.controlHeight * 5.5f);
	const auto handle = UiScale::ResolveReplicantHandleMetrics(scale);
	size.draggerSize = handle.size;
	size.draggerRightInset = handle.rightInset;
	size.draggerBottomInset = handle.bottomInset;
	size.trackInfoInset = 2.0f * scale;
	size.addButtonInset = 6.0f * scale;
	return size;
}

struct SeekBarMetrics {
	float height = 0, thickness = 0, minWidth = 0, preferredWidth = 0;
};

inline SeekBarMetrics
ResolveSeekBarMetrics(float scale, float lineHeight)
{
	scale = std::max(1.0f, scale);
	float height = PlaybackControlHeight(scale, lineHeight);
	// Standalone sizing preserves the old font-relative widths. PlayerBarView
	// supplies its own horizontal constraints after creating the seek view.
	return {height, std::max(18.0f * scale, height - 6.0f),
		lineHeight * 10.0f * scale, lineHeight * 24.0f * scale};
}
