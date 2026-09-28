#include "playback/PlaybackMetadata.h"
#include "spotify/SpotifyUri.h"

namespace {

PlaybackMetadataField
Retained(const PlaybackMetadataField& field)
{
	return {field.value, PlaybackMetadataSource::Retained};
}


PlaybackMetadataField
ResolveField(const PlaybackMetadataField& reported,
	const PlaybackMetadataField& current, bool preserve)
{
	return preserve && reported.value.empty() ? Retained(current) : reported;
}


void
StoreResolvedPlaybackField(bool optimistic, const PlaybackMetadataField& value,
	PlaybackMetadataField& target)
{
	if (!optimistic || !value.value.empty())
		target = value;
}


bool
ShouldPreserveCurrentNowPlayingMetadata(bool optimistic, bool trackChanged,
	bool hasTrackUri)
{
	return optimistic || (!trackChanged && hasTrackUri);
}


bool
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


bool
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


bool
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

}


PlaybackMetadata
ResolvePlaybackMetadata(const PlaybackMetadata& reported,
	const PlaybackMetadata& current, const PlaybackMetadataContext& context)
{
	PlaybackMetadata result = reported;
	if (context.preserveCurrentArtwork)
		result.artworkUrl = Retained(current.artworkUrl);
	bool hasTrackUri = !context.trackUri.empty();
	bool preserveCurrentMetadata = ShouldPreserveCurrentNowPlayingMetadata(
		context.optimistic, context.trackChanged, hasTrackUri);
	bool preserveCurrentAudiobookContext = ShouldPreserveCurrentAudiobookContext(
		context.optimistic, context.trackChanged, hasTrackUri, current.parentKind.value,
		result.parentKind.value, current.openUri.value,
		result.openUri.value);
	bool carryAudiobookContext = ShouldCarryAudiobookContextAcrossChapterChange(
		context.optimistic, context.trackChanged, context.trackUri, current.parentKind.value,
		result.parentKind.value, current.openUri.value,
		result.openUri.value);
	result.title = ResolveField(
		result.title, current.title, preserveCurrentMetadata);
	result.artist = ResolveField(
		result.artist, current.artist, preserveCurrentMetadata);
	result.albumId = ResolveField(
		result.albumId, current.albumId, preserveCurrentMetadata);
	result.artistId = ResolveField(
		result.artistId, current.artistId, preserveCurrentMetadata);

	bool preserveCurrentItemContext = preserveCurrentMetadata;
	if (preserveCurrentAudiobookContext) {
		result.artist = Retained(current.artist);
		result.itemKind = Retained(current.itemKind);
		result.openUri = Retained(current.openUri);
		result.parentUri = Retained(current.parentUri);
		result.parentKind = Retained(current.parentKind);
		result.showId = Retained(current.showId);
		result.audiobookId = Retained(current.audiobookId);
		preserveCurrentItemContext = false;
	} else if (carryAudiobookContext) {
		result.openUri = Retained(current.openUri);
		result.parentUri = Retained(current.parentUri);
		result.parentKind = Retained(current.parentKind);
		result.audiobookId = Retained(current.audiobookId);
		preserveCurrentItemContext = false;
	}
	result.itemKind = ResolveField(
		result.itemKind, current.itemKind, preserveCurrentItemContext);
	result.openUri = ResolveField(
		result.openUri, current.openUri,
		preserveCurrentItemContext);
	result.parentUri = ResolveField(
		result.parentUri, current.parentUri,
		preserveCurrentItemContext);
	result.parentKind = ResolveField(
		result.parentKind, current.parentKind,
		preserveCurrentItemContext);
	result.showId = ResolveField(
		result.showId, current.showId, preserveCurrentItemContext);
	result.audiobookId = ResolveField(
		result.audiobookId, current.audiobookId,
		preserveCurrentItemContext);
	result.artworkUrl = ResolveField(
		result.artworkUrl, current.artworkUrl, context.optimistic);
	if (result.openUri.value.empty() && hasTrackUri)
		result.openUri = {context.trackUri, PlaybackMetadataSource::ItemUriFallback};
	if (!NowPlayingUsesTrackIds(result.itemKind.value,
			result.openUri.value)) {
		result.albumId = {{}, PlaybackMetadataSource::ClearedForItem};
		result.artistId = {{}, PlaybackMetadataSource::ClearedForItem};
	}
	return result;
}


void
StorePlaybackMetadata(PlaybackMetadata& current,
	const PlaybackMetadata& resolved, bool optimistic)
{
	StoreResolvedPlaybackField(optimistic, resolved.title,
		current.title);
	StoreResolvedPlaybackField(optimistic, resolved.artist,
		current.artist);
	StoreResolvedPlaybackField(optimistic, resolved.albumId,
		current.albumId);
	StoreResolvedPlaybackField(optimistic, resolved.artistId,
		current.artistId);
	StoreResolvedPlaybackField(optimistic, resolved.itemKind,
		current.itemKind);
	StoreResolvedPlaybackField(optimistic, resolved.openUri,
		current.openUri);
	StoreResolvedPlaybackField(optimistic, resolved.parentUri,
		current.parentUri);
	StoreResolvedPlaybackField(optimistic, resolved.parentKind,
		current.parentKind);
	StoreResolvedPlaybackField(optimistic, resolved.showId,
		current.showId);
	StoreResolvedPlaybackField(optimistic, resolved.audiobookId,
		current.audiobookId);
	if (!NowPlayingUsesTrackIds(resolved.itemKind.value,
			resolved.openUri.value)) {
		current.albumId = {{}, PlaybackMetadataSource::ClearedForItem};
		current.artistId = {{}, PlaybackMetadataSource::ClearedForItem};
	}
}
