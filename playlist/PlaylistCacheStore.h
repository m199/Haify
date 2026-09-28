#pragma once

#include <nlohmann/json.hpp>

#include <string>

// Owns where playlist, Liked Songs and show documents live on disk and orders
// their asynchronous writes against removals.
//
// Keys: <cache>/<kind>/<escaped account>/<id>.json (see PlaylistCacheKeys).
// Without an active account every lookup returns "", so documents are never
// read, written or shared across accounts.
// Owner: SpotifyApi publishes the account whenever the session identity
// changes (SetAccountId/ClearSession).
// Invalidation: Remove() cancels a pending write for the path before the file
// is unlinked, so an older write can no longer resurrect it; SetAccount()
// cancels every pending write. RemoveAccountFiles() runs on sign-out.
namespace PlaylistCacheStore {

extern const char* const kPlaylists;
extern const char* const kLibrary;
extern const char* const kShows;
// File ID of the Liked Songs document inside kLibrary.
extern const char* const kLikedSongsId;

void			SetAccount(const std::string& accountId);
std::string		Account();

// Absolute path of <kind>/<account>/<id>.json, or "" when there is no
// account, the ID is unsafe, or the directory is unavailable.
std::string		FilePath(const std::string& kind, const std::string& id,
					bool createDirectory);

void			WriteAsync(const std::string& path, nlohmann::json data);
void			Remove(const std::string& path);
void			Remove(const std::string& kind, const std::string& id);

void			RemoveAccountFiles(const std::string& accountId);
// Files from before account scoping may belong to any previous account.
void			RemoveLegacyUnscopedFiles();

}
