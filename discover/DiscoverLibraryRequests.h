#pragma once

#include "DiscoverLibraryChangeController.h"
#include "DiscoverLibraryWriteController.h"
#include "DiscoverAsyncScope.h"
#include <Messenger.h>

class SpotifyApi;

namespace DiscoverLibraryRequests {
void Write(SpotifyApi& api, const DiscoverLibraryWriteRequest& request,
	const DiscoverAsyncToken& token, const BMessenger& target);
// The controller owns validity. Callbacks capture a request and BMessenger,
// retain API status, and never dereference the window after dispatch.
void ResolveAddition(SpotifyApi& api, const DiscoverLibraryRequest& request,
	bool showProgress, const std::string& doneLabel, const BMessenger& target);
void CheckMembership(SpotifyApi& api, const DiscoverLibraryRequest& request,
	const BMessenger& target);
}
