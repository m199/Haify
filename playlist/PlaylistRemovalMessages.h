#pragma once

#include "PlaylistRemovalController.h"
#include <Message.h>

// Internal window result: token + playlist ID required; no row pointers.
BMessage MakePlaylistRemovalMessage(const PlaylistRemovalResult& result);
bool ReadPlaylistRemovalMessage(const BMessage& message, PlaylistRemovalResult& result);
