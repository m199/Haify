#pragma once

#include "PlaylistReorderController.h"

class PlaylistApi;
class BMessenger;

namespace PlaylistReorderRequests {
bool Send(PlaylistApi& api, const PlaylistReorderCommand& command,
	const BMessenger& target);
}
