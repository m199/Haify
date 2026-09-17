#pragma once
#include "PlaylistCoverController.h"

class PlaylistApi;
class BMessenger;
struct entry_ref;

namespace PlaylistCoverRequests {
bool Send(PlaylistApi& api, const PlaylistCoverCommand& command,
	const entry_ref& file, const BMessenger& target);
}
