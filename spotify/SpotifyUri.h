#pragma once

#include <string>

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

inline const char*
SpotifyItemUriPrefix(SpotifyItemKind kind)
{
	switch (kind) {
		case kSpotifyItemTrack: return "spotify:track:";
		case kSpotifyItemEpisode: return "spotify:episode:";
		case kSpotifyItemAlbum: return "spotify:album:";
		case kSpotifyItemShow: return "spotify:show:";
		case kSpotifyItemArtist: return "spotify:artist:";
		case kSpotifyItemAudiobook: return "spotify:audiobook:";
		case kSpotifyItemPlaylist: return "spotify:playlist:";
		default: return "";
	}
}

inline std::string
SpotifyUriForItemKind(SpotifyItemKind kind, const std::string& id)
{
	const char* prefix = SpotifyItemUriPrefix(kind);
	if (id.empty() || prefix[0] == '\0')
		return "";
	return std::string(prefix) + id;
}

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

inline SpotifyItemKind
SpotifyItemKindForTypeName(const std::string& typeName)
{
	if (typeName == "track") return kSpotifyItemTrack;
	if (typeName == "episode") return kSpotifyItemEpisode;
	if (typeName == "album") return kSpotifyItemAlbum;
	if (typeName == "show") return kSpotifyItemShow;
	if (typeName == "artist") return kSpotifyItemArtist;
	if (typeName == "audiobook") return kSpotifyItemAudiobook;
	if (typeName == "playlist") return kSpotifyItemPlaylist;
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
SpotifyLibraryUriIsValid(const std::string& uri)
{
	static const char* prefixes[] = {
		"spotify:track:", "spotify:album:", "spotify:episode:",
		"spotify:show:", "spotify:audiobook:", "spotify:artist:",
		"spotify:user:", "spotify:playlist:"
	};
	for (const char* prefix : prefixes) {
		size_t length = std::string(prefix).size();
		if (uri.size() > length && uri.compare(0, length, prefix) == 0)
			return true;
	}
	return false;
}

inline bool
SpotifyPlaybackContextSupportsOffset(const std::string& itemUri,
	const std::string& contextUri)
{
	return itemUri.find("spotify:track:") == 0
		&& (contextUri.find("spotify:album:") == 0
			|| contextUri.find("spotify:playlist:") == 0);
}
