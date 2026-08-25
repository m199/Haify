#pragma once

#include "NowPlayingFields.h"

#include <SupportDefs.h>

#include <nlohmann/json.hpp>

#include <string>

class BMessage;

struct NowPlayingItem {
	std::string itemUri;
	std::string itemKind;
	std::string displayTitle;
	std::string displaySubtitle;
	std::string imageUrl;
	std::string primaryOpenUri;
	std::string parentUri;
	std::string parentKind;
	std::string albumId;
	std::string artistId;
	std::string showId;
	std::string audiobookId;
	int32 durationMs = 0;

	void AddToMessage(BMessage& message) const;

	static NowPlayingItem FromSpotifyItem(const nlohmann::json& item,
		const std::string& fallbackType);
};
