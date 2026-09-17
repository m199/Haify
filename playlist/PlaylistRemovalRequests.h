#pragma once

#include "PlaylistRemovalController.h"

class PlaylistApi;
class BMessenger;

namespace PlaylistRemovalRequests {
bool Send(PlaylistApi& api, const PlaylistRemovalCommand& command,
	const BMessenger& target);
}
