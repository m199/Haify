#pragma once
#include "PlaylistWriteController.h"
class BMessenger;
class PlaylistApi;
namespace PlaylistWriteRequests {
bool Send(PlaylistApi& api, const PlaylistWriteCommand& command, const BMessenger& target);
}
