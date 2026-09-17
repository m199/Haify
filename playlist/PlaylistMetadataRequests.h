#pragma once

#include "PlaylistMetadataController.h"

class BMessenger;
class SpotifyApi;

namespace PlaylistMetadataRequests {
bool RefreshSnapshot(SpotifyApi& api, const std::string& playlistId, const BMessenger& target);
bool Load(SpotifyApi& api, const PlaylistMetadataRequest& request,
	const BMessenger& target);
}
