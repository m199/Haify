#pragma once

#include "PlaylistPageController.h"
#include <Message.h>

// Compatibility bridge to PlaylistWindow's existing row consumers. Wire codes,
// append types and column names are documented in docs/phase-4-verification.md.
BMessage MakePlaylistPageMessage(const PlaylistPageResult& result,
	const std::string& unavailableEpisodeTitle);
// Reads request identity and outcome; rows remain in the existing UI wire format.
// Missing/invalid identity is rejected without changing output.
bool ReadPlaylistPageHeader(const BMessage& message, PlaylistPageResult& result);
BMessage MakePlaylistSearchMessage(bool retry, int32_t generation);
bool ReadPlaylistSearchGeneration(const BMessage& message, int32_t& generation);
