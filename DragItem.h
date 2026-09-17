#pragma once

#include "spotify/SpotifyUri.h"
#include <cstdint>
#include <string>
#include <vector>

namespace MessageContracts {

enum class DropIntent : int32_t { Automatic = 0, Add = 1, Reorder = 2 };

struct DragItem {
	std::string uri;
	SpotifyItemKind kind = kSpotifyItemUnknown;
	std::string sourcePlaylist = "";
	int32_t sourceIndex = -1;
	DropIntent intent = DropIntent::Automatic;
	std::vector<int32_t> sourceIndices = {};
	std::vector<std::string> sourceUris = {};
	std::string sourceSnapshot = "";
};

} // namespace MessageContracts
