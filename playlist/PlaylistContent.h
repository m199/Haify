#pragma once

#include "spotify/SpotifyUri.h"

#include <string>

struct PlaylistContentTarget {
	bool isCollection = false;
	SpotifyItemKind kind = kSpotifyItemUnknown;
	std::string id;
};

PlaylistContentTarget ResolvePlaylistContentTarget(const std::string& uri);
