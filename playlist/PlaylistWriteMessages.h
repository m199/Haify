#pragma once
#include "PlaylistWriteController.h"
#include <Message.h>

BMessage MakePlaylistWriteMessage(const PlaylistWriteResult& result);
bool ReadPlaylistWriteMessage(const BMessage& message, PlaylistWriteResult& result);
