#pragma once

#include "messages/DragItem.h"
#include "playlist/PlaylistReorderPolicy.h"

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

struct PlaylistMetadataPageState {
	int total = 0;
	bool hasMore = false;
};

struct CachedPageState {
	int offset = 0;
	int total = 0;
	bool hasMore = false;
	bool valid = true;
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
ShouldCarryAudiobookContextAcrossChapterChange(bool optimistic,
	bool trackChanged, const std::string& trackUri,
	const std::string& currentParentKind,
	const std::string& reportedParentKind,
	const std::string& currentOpenUri, const std::string& reportedOpenUri)
{
	return !optimistic && trackChanged
		&& SpotifyItemKindForUri(trackUri) == kSpotifyItemEpisode
		&& currentParentKind == "audiobook"
		&& reportedParentKind.empty()
		&& SpotifyItemKindForUri(currentOpenUri) == kSpotifyItemAudiobook
		&& SpotifyItemKindForUri(reportedOpenUri) != kSpotifyItemShow;
}

// Preview a track switch only while a known item is already playing. Finding
// a device (including a starting librespot) is not a playback confirmation.
inline bool
ShouldPreviewPlaybackStart(bool isPlaying, const std::string& currentTrackUri)
{
	return isPlaying && SpotifyItemIsPlayable(SpotifyItemKindForUri(currentTrackUri));
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
	SpotifyItemKind openKind = SpotifyItemKindForUri(openUri);
	if (openKind == kSpotifyItemShow || openKind == kSpotifyItemAudiobook
			|| openKind == kSpotifyItemEpisode) {
		return kNowPlayingTitleClickOpenUri;
	}
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
	SpotifyItemKind openKind = SpotifyItemKindForUri(openUri);
	if (openKind == kSpotifyItemShow || openKind == kSpotifyItemAudiobook
			|| openKind == kSpotifyItemEpisode) {
		return kNowPlayingSubtitleClickOpenUri;
	}
	if (!artistId.empty())
		return kNowPlayingSubtitleClickShowArtist;
	if (!openUri.empty())
		return kNowPlayingSubtitleClickOpenUri;
	return kNowPlayingSubtitleClickIgnore;
}

inline bool
NowPlayingUsesTrackIds(const std::string& itemKind,
	const std::string& openUri)
{
	SpotifyItemKind effectiveKind = SpotifyItemKindForTypeName(itemKind);
	SpotifyItemKind openKind = SpotifyItemKindForUri(openUri);
	if (effectiveKind == kSpotifyItemEpisode
			|| openKind == kSpotifyItemShow
			|| openKind == kSpotifyItemAudiobook
			|| openKind == kSpotifyItemEpisode) {
		return false;
	}
	return true;
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

inline PlaylistDropAction
ResolvePlaylistDropAction(const std::string& targetPlaylistUri,
	const MessageContracts::DragItem& item, bool targetWritable,
	bool mutationPending)
{
	PlaylistDropAction action = ResolvePlaylistDropAction(targetPlaylistUri,
		item.sourcePlaylist, item.sourceIndex, SpotifyItemTypeName(item.kind),
		item.uri, targetWritable, mutationPending);
	if (item.intent == MessageContracts::DropIntent::Add
			&& action != kPlaylistDropAddPlayableItem)
		return kPlaylistDropIgnore;
	if (item.intent == MessageContracts::DropIntent::Reorder
			&& action != kPlaylistDropReorder)
		return kPlaylistDropIgnore;
	return action;
}

inline bool
DragItemCanCopy(const MessageContracts::DragItem& item)
{
	return item.intent != MessageContracts::DropIntent::Reorder;
}

inline int
ResolvePlaylistDropInsertBefore(int sourceIndex, int targetIndex, int rowCount)
{
	targetIndex = std::max(0, std::min(targetIndex, rowCount));
	if (targetIndex < rowCount && targetIndex > sourceIndex)
		return targetIndex + 1;
	return targetIndex;
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

inline CachedPageState
ResolveCachedTrackPageState(int cachedOffset, int cachedTotal,
	int loadedRowCount)
{
	CachedPageState state;
	state.offset = std::max(cachedOffset, loadedRowCount);
	state.total = std::max(cachedTotal, loadedRowCount);
	state.hasMore = state.total <= 0 || state.offset < state.total;
	return state;
}

inline CachedPageState
ResolveCachedEpisodePageState(int cachedOffset, int cachedTotal,
	int loadedEpisodeCount, bool hasNextOffset)
{
	CachedPageState state;
	state.valid = hasNextOffset || cachedTotal <= loadedEpisodeCount;
	if (!state.valid)
		return state;

	state.total = std::max(cachedTotal, loadedEpisodeCount);
	state.offset = std::max(cachedOffset, loadedEpisodeCount);
	state.hasMore = state.total <= 0 || state.offset < state.total;
	return state;
}
