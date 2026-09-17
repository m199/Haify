#pragma once

#include "PlaylistReorderController.h"
#include <Message.h>

BMessage MakePlaylistReorderMessage(const PlaylistReorderResult& result);
// Strict internal result reader; malformed messages leave output unchanged.
bool ReadPlaylistReorderMessage(const BMessage& message, PlaylistReorderResult& result);
