#pragma once

#include "PlaylistPageController.h"

class SpotifyApi;
class BMessenger;

namespace PlaylistPageRequests {
// The window starts the load; API callbacks own value copies and publish to a
// BMessenger. They retain neither a window pointer nor a controller reference.
bool Load(SpotifyApi& api, const PlaylistPageRequest& request,
	const BMessenger& target);
}
