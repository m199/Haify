#pragma once

#include "PlaylistMetadataController.h"

#include <Message.h>

BMessage MakePlaylistMetadataMessage(const PlaylistMetadataResult& result);
BMessage MakePlaylistSnapshotMessage(const PlaylistMetadataResult& result);
bool ReadPlaylistSnapshotMessage(const BMessage& message, PlaylistMetadataResult& result);
// Rejects incomplete/wrongly typed results without changing output.
bool ReadPlaylistMetadataMessage(const BMessage& message, PlaylistMetadataResult& result);
BMessage MakePlaylistTitleMessage(const std::string& title);
BMessage MakePlaylistCoverMessage(const std::string& url);
