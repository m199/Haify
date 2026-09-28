#pragma once

#include "spotify/SpotifyUri.h"

#include <string>

// Pure key rules for the playlist/library/show document cache. They keep
// every cache file inside its account directory and never touch the disk.
namespace PlaylistCacheKeys {

// Cache file IDs follow the Spotify ID rules, so no ID can leave the
// account directory.
inline bool
IsSafeId(const std::string& id)
{
	return SpotifyIdIsSafe(id);
}

// Spotify user names may contain characters such as '.', so the account
// directory escapes them reversibly ("~2E") instead of collapsing them.
inline std::string
AccountDirectoryName(const std::string& accountId)
{
	static const char kHex[] = "0123456789ABCDEF";
	std::string name;
	for (unsigned char character : accountId) {
		if (SpotifyIdCharacterIsSafe(character)) {
			name += (char)character;
		} else {
			name += '~';
			name += kHex[character >> 4];
			name += kHex[character & 0x0f];
		}
	}
	return name;
}

// Relative cache directory for one document kind of one account; empty when
// no account is active, which disables the cache instead of sharing it.
inline std::string
AccountDirectory(const std::string& kind, const std::string& accountId)
{
	if (kind.empty() || accountId.empty())
		return "";
	return kind + "/" + AccountDirectoryName(accountId);
}

}
