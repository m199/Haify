#pragma once

#include "PlaylistContent.h"
#include <algorithm>

// Shared menu decisions for menu bar, header context menu and command guards.
struct PlaylistMenuState {
	bool edit = false;
	bool cover = false;
	bool clear = false;
	bool remove = false;
	bool owned = false;
};
inline PlaylistMenuState
ResolvePlaylistMenuState(const PlaylistContentTarget& target, bool owned,
	bool hasItems, bool mutationPending, bool coverPending, bool deletePending)
{
	PlaylistMenuState state;
	if (target.isCollection || target.kind != kSpotifyItemPlaylist || target.id.empty())
		return state;
	state.owned = owned;
	state.edit = owned && !deletePending;
	state.cover = state.edit && !coverPending;
	state.clear = state.edit && hasItems && !mutationPending;
	state.remove = !mutationPending && !coverPending && !deletePending;
	return state;
}

struct PlaylistHeaderModel {
	SpotifyItemKind kind = kSpotifyItemUnknown;
	bool isPodcast = false;
	bool isAlbum = false;
	bool isLikedSongs = false;
	const char* titlePrefix = "";
	float minimumWidth = 420;
	float minimumHeight = 260;
};
inline PlaylistHeaderModel
ResolvePlaylistHeader(const std::string& uri)
{
	PlaylistHeaderModel model;
	auto target = ResolvePlaylistContentTarget(uri);
	model.kind = target.kind;
	model.isLikedSongs = target.isCollection;
	model.isPodcast = target.kind == kSpotifyItemShow;
	model.isAlbum = target.kind == kSpotifyItemAlbum;
	switch (target.kind) {
		case kSpotifyItemPlaylist: model.titlePrefix = "Playlist: "; break;
		case kSpotifyItemAlbum: model.titlePrefix = "Album: "; break;
		case kSpotifyItemShow: model.titlePrefix = "Podcast: "; break;
		case kSpotifyItemArtist: model.titlePrefix = "Artist: "; break;
		default: break;
	}
	if (model.isPodcast) {
		model.minimumWidth = 560;
		model.minimumHeight = 300;
	}
	return model;
}

struct PodcastHeaderMetrics {
	float infoWidth;
	float titleHeight;
	float searchInfoHeight;
};
inline PodcastHeaderMetrics
ResolvePodcastHeaderMetrics(float lineHeight, float actionWidth, float scaledInfoWidth)
{
	return {std::max(actionWidth, scaledInfoWidth), lineHeight * 3.2f, lineHeight * 1.4f};
}
