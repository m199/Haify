#pragma once
#include "PlaylistCoverController.h"
#include <Message.h>

BMessage MakePlaylistCoverResultMessage(const PlaylistCoverResult& result);
bool ReadPlaylistCoverResultMessage(const BMessage& message, PlaylistCoverResult& result);
