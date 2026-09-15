#pragma once

#include "DiscoverAsyncScope.h"
#include "DiscoverPlaylistMutationController.h"

#include <Messenger.h>

class SpotifyApi;

namespace DiscoverPlaylistRequests {
// Only dispatch requests issued by the window's controller in the current scope.
// Results retain the request identity and API status; workers never own BRows.
void Mutate(SpotifyApi& api, const DiscoverPlaylistMutationRequest& request,
	const DiscoverAsyncToken& token, const BMessenger& target);
void Create(SpotifyApi& api, const std::string& name,
	const DiscoverAsyncToken& token, const BMessenger& target);
void AddItem(SpotifyApi& api, const std::string& playlistId, const std::string& itemUri,
	const DiscoverAsyncToken& token, const BMessenger& target);
}
