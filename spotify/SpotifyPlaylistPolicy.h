#pragma once

#include <string>

inline bool
SpotifyPlaylistIsWritable(bool collaborative,
	const std::string& ownerAccountId, const std::string& ownerLegacyId,
	const std::string& currentAccountId)
{
	return collaborative || (!currentAccountId.empty()
		&& (ownerAccountId == currentAccountId
			|| ownerLegacyId == currentAccountId));
}
