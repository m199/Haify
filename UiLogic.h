#pragma once

#include <algorithm>
#include <set>
#include <string>
#include <vector>

enum SpotifyItemKind {
	kSpotifyItemUnknown = 0,
	kSpotifyItemTrack,
	kSpotifyItemEpisode,
	kSpotifyItemAlbum,
	kSpotifyItemShow,
	kSpotifyItemArtist,
	kSpotifyItemAudiobook,
	kSpotifyItemPlaylist
};

inline SpotifyItemKind
SpotifyItemKindForUri(const std::string& uri)
{
	if (uri.find("spotify:track:") == 0) return kSpotifyItemTrack;
	if (uri.find("spotify:episode:") == 0) return kSpotifyItemEpisode;
	if (uri.find("spotify:album:") == 0) return kSpotifyItemAlbum;
	if (uri.find("spotify:show:") == 0) return kSpotifyItemShow;
	if (uri.find("spotify:artist:") == 0) return kSpotifyItemArtist;
	if (uri.find("spotify:audiobook:") == 0) return kSpotifyItemAudiobook;
	if (uri.find("spotify:playlist:") == 0) return kSpotifyItemPlaylist;
	return kSpotifyItemUnknown;
}

inline std::string
SpotifyItemIdForUri(const std::string& uri)
{
	if (SpotifyItemKindForUri(uri) == kSpotifyItemUnknown)
		return "";
	size_t separator = uri.rfind(':');
	if (separator == std::string::npos || separator + 1 >= uri.size())
		return "";
	return uri.substr(separator + 1);
}

inline SpotifyItemKind
SpotifyEffectiveItemKind(SpotifyItemKind apiKind, const std::string& id,
	const std::set<std::string>& audiobookIds)
{
	if (!id.empty() && audiobookIds.find(id) != audiobookIds.end())
		return kSpotifyItemAudiobook;
	return apiKind;
}

inline const char*
SpotifyItemTypeName(SpotifyItemKind kind)
{
	switch (kind) {
		case kSpotifyItemTrack: return "track";
		case kSpotifyItemEpisode: return "episode";
		case kSpotifyItemAlbum: return "album";
		case kSpotifyItemShow: return "show";
		case kSpotifyItemArtist: return "artist";
		case kSpotifyItemAudiobook: return "audiobook";
		case kSpotifyItemPlaylist: return "playlist";
		default: return "unknown";
	}
}

inline const char*
SpotifyLibraryTargetId(SpotifyItemKind kind)
{
	switch (kind) {
		case kSpotifyItemTrack: return "playlists";
		case kSpotifyItemEpisode: return "saved_episodes";
		case kSpotifyItemAlbum: return "saved_albums";
		case kSpotifyItemShow: return "podcasts";
		case kSpotifyItemArtist: return "followed_artists";
		case kSpotifyItemAudiobook: return "audiobooks";
		case kSpotifyItemPlaylist: return "playlists";
		default: return "";
	}
}

inline bool
SpotifyItemCanAddToPlaylist(SpotifyItemKind kind)
{
	return kind == kSpotifyItemTrack || kind == kSpotifyItemEpisode;
}

inline bool
SpotifyItemIsPlayable(SpotifyItemKind kind)
{
	return kind == kSpotifyItemTrack || kind == kSpotifyItemEpisode;
}

inline bool
SpotifyPlaylistIsWritable(bool collaborative,
	const std::string& ownerAccountId, const std::string& ownerLegacyId,
	const std::string& currentAccountId)
{
	return collaborative || (!currentAccountId.empty()
		&& (ownerAccountId == currentAccountId
			|| ownerLegacyId == currentAccountId));
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
