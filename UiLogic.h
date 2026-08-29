#pragma once

#include "spotify/SpotifyUri.h"
#include "spotify/SpotifyPlaylistPolicy.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

enum PlaylistDropAction {
	kPlaylistDropIgnore = 0,
	kPlaylistDropAddPlayableItem,
	kPlaylistDropReorder
};

struct PlaylistReorderPlan {
	bool shouldMove = false;
	int targetIndex = -1;
	int insertBefore = -1;
};

struct PlaylistMetadataPageState {
	int total = 0;
	bool hasMore = false;
};

enum NowPlayingTitleClickAction {
	kNowPlayingTitleClickIgnore = 0,
	kNowPlayingTitleClickShowAlbum,
	kNowPlayingTitleClickOpenUri
};

enum NowPlayingSubtitleClickAction {
	kNowPlayingSubtitleClickIgnore = 0,
	kNowPlayingSubtitleClickShowArtist,
	kNowPlayingSubtitleClickOpenUri
};

inline SpotifyItemKind
SpotifyEffectiveItemKind(SpotifyItemKind apiKind, const std::string& id,
	const std::set<std::string>& audiobookIds)
{
	if (!id.empty() && audiobookIds.find(id) != audiobookIds.end())
		return kSpotifyItemAudiobook;
	return apiKind;
}

inline bool
NormalizeSearchFilters(bool selectAll, std::vector<bool>& typeSelections)
{
	bool any = std::find(typeSelections.begin(), typeSelections.end(), true)
		!= typeSelections.end();
	if (selectAll || !any) {
		std::fill(typeSelections.begin(), typeSelections.end(), false);
		return true;
	}
	return false;
}

inline bool
ShouldAcceptReportedVolume(int reportedVolume, int targetVolume,
	bool guardActive, int tolerance = 1)
{
	if (!guardActive)
		return true;
	int difference = reportedVolume - targetVolume;
	if (difference < 0)
		difference = -difference;
	return difference <= tolerance;
}

inline int
ResolveTrackChangedProgress(int reportedProgressMs, int currentProgressMs,
	long long elapsedSinceSyncMs, bool sameTrack, bool isPlaying,
	int durationMs)
{
	long long progress = std::max(reportedProgressMs, 0);
	if (sameTrack) {
		long long current = std::max(currentProgressMs, 0);
		if (isPlaying && elapsedSinceSyncMs > 0)
			current += elapsedSinceSyncMs;
		progress = std::max(progress, current);
	}
	if (durationMs > 0)
		progress = std::min(progress, (long long)durationMs);
	return (int)progress;
}

inline bool
ShouldDeferLibrespotTrackChanged(bool sameTrack)
{
	return !sameTrack;
}

inline std::string
ResolvePlaybackArtworkUrl(const std::string& reportedArtworkUrl,
	const std::string& currentArtworkUrl, bool preserveCurrentArtwork)
{
	return preserveCurrentArtwork ? currentArtworkUrl : reportedArtworkUrl;
}

inline std::string
ResolveNowPlayingFallbackField(const std::string& reportedValue,
	const std::string& currentValue, bool preserveCurrentWhenReportedEmpty)
{
	if (preserveCurrentWhenReportedEmpty && reportedValue.empty())
		return currentValue;
	return reportedValue;
}

inline bool
ShouldPreserveCurrentNowPlayingMetadata(bool optimistic, bool trackChanged,
	bool hasTrackUri)
{
	return optimistic || (!trackChanged && hasTrackUri);
}

inline bool
ShouldPreserveCurrentAudiobookContext(bool optimistic, bool trackChanged,
	bool hasTrackUri, const std::string& currentParentKind,
	const std::string& reportedParentKind,
	const std::string& currentOpenUri, const std::string& reportedOpenUri)
{
	return !optimistic && !trackChanged && hasTrackUri
		&& currentParentKind == "audiobook"
		&& reportedParentKind != "audiobook"
		&& SpotifyItemKindForUri(currentOpenUri) == kSpotifyItemAudiobook
		&& SpotifyItemKindForUri(reportedOpenUri) != kSpotifyItemAudiobook;
}

inline bool
ShouldRetryStartupEmptyPlaybackPoll(bool hasItem, bool hadPlaybackState,
	long long nowUs, long long retryUntilUs)
{
	return !hasItem && !hadPlaybackState && nowUs < retryUntilUs;
}

inline bool
ShouldDeferOptimisticPlaybackPoll(bool optimistic, bool guardActive,
	bool hasReportedTrackUri, const std::string& reportedTrackUri,
	const std::string& currentTrackUri,
	const std::string& optimisticSourceTrackUri, bool knownItemState,
	bool hasItem)
{
	if (optimistic || !guardActive || optimisticSourceTrackUri.empty())
		return false;
	if (knownItemState && !hasItem)
		return true;
	return hasReportedTrackUri && currentTrackUri != reportedTrackUri
		&& optimisticSourceTrackUri == reportedTrackUri;
}

inline NowPlayingTitleClickAction
ResolveNowPlayingTitleClickAction(const std::string& albumId,
	const std::string& openUri)
{
	if (!albumId.empty())
		return kNowPlayingTitleClickShowAlbum;
	if (!openUri.empty())
		return kNowPlayingTitleClickOpenUri;
	return kNowPlayingTitleClickIgnore;
}

inline NowPlayingSubtitleClickAction
ResolveNowPlayingSubtitleClickAction(const std::string& artistId,
	const std::string& openUri)
{
	if (!artistId.empty())
		return kNowPlayingSubtitleClickShowArtist;
	if (!openUri.empty())
		return kNowPlayingSubtitleClickOpenUri;
	return kNowPlayingSubtitleClickIgnore;
}

inline std::vector<std::string>
NormalizeStableOrder(const std::vector<std::string>& configured,
	const std::vector<std::string>& known)
{
	std::vector<std::string> result;
	for (const std::string& id : configured) {
		if (std::find(known.begin(), known.end(), id) != known.end()
				&& std::find(result.begin(), result.end(), id) == result.end())
			result.push_back(id);
	}
	for (const std::string& id : known) {
		if (std::find(result.begin(), result.end(), id) == result.end())
			result.push_back(id);
	}
	return result;
}

inline SpotifyItemKind
ResolveDroppedSpotifyItemKind(const std::string& itemType,
	const std::string& uri)
{
	SpotifyItemKind kind = SpotifyItemKindForTypeName(itemType);
	if (kind != kSpotifyItemUnknown)
		return kind;
	return SpotifyItemKindForUri(uri);
}

inline bool
PlaylistCanAcceptDrop(const std::string& targetPlaylistUri,
	bool targetWritable, bool mutationPending)
{
	return !mutationPending && targetWritable
		&& SpotifyItemKindForUri(targetPlaylistUri) == kSpotifyItemPlaylist;
}

inline PlaylistDropAction
ResolvePlaylistDropAction(const std::string& targetPlaylistUri,
	const std::string& sourcePlaylistUri, int sourceIndex,
	const std::string& itemType, const std::string& itemUri,
	bool targetWritable, bool mutationPending)
{
	if (!PlaylistCanAcceptDrop(targetPlaylistUri, targetWritable,
			mutationPending)) {
		return kPlaylistDropIgnore;
	}
	if (sourceIndex >= 0 && targetPlaylistUri == sourcePlaylistUri)
		return kPlaylistDropReorder;
	if (!itemUri.empty() && SpotifyItemCanAddToPlaylist(
			ResolveDroppedSpotifyItemKind(itemType, itemUri))) {
		return kPlaylistDropAddPlayableItem;
	}
	return kPlaylistDropIgnore;
}

inline int
ResolvePlaylistDropInsertBefore(int sourceIndex, int targetIndex, int rowCount)
{
	targetIndex = std::max(0, std::min(targetIndex, rowCount));
	if (targetIndex < rowCount && targetIndex > sourceIndex)
		return targetIndex + 1;
	return targetIndex;
}

inline PlaylistReorderPlan
ResolvePlaylistReorderPlan(int sourceIndex, int rangeLength, int insertBefore,
	int rowCount)
{
	PlaylistReorderPlan plan;
	if (sourceIndex < 0 || rangeLength < 1 || rowCount < 1
			|| sourceIndex + rangeLength > rowCount) {
		return plan;
	}
	plan.insertBefore = std::max(0, std::min(insertBefore, rowCount));
	plan.targetIndex = plan.insertBefore > sourceIndex
		? plan.insertBefore - rangeLength : plan.insertBefore;
	plan.targetIndex = std::max(0, std::min(plan.targetIndex,
		rowCount - rangeLength));
	plan.shouldMove = plan.targetIndex != sourceIndex;
	return plan;
}

inline bool
PlaylistHasCompleteSnapshot(const std::string& snapshotId, int pageTotal,
	int loadedRowCount, int pageOffset)
{
	return !snapshotId.empty() && pageTotal == loadedRowCount
		&& pageOffset >= pageTotal;
}

inline bool
ShouldReloadPlaylistRowsForSnapshot(const std::string& cachedSnapshotId,
	const std::string& remoteSnapshotId)
{
	return !cachedSnapshotId.empty() && remoteSnapshotId != cachedSnapshotId;
}

inline PlaylistMetadataPageState
ResolvePlaylistMetadataPageState(int remoteTotal, int currentTotal,
	int loadedRowCount, int pageOffset)
{
	PlaylistMetadataPageState state;
	state.total = remoteTotal >= 0 ? remoteTotal : currentTotal;
	state.total = std::max(state.total, loadedRowCount);
	state.hasMore = remoteTotal < 0
		? (state.total <= 0 || pageOffset < state.total)
		: pageOffset < state.total;
	return state;
}
