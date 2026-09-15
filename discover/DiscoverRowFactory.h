#pragma once

#include "DiscoverRowData.h"

#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <cstdint>

namespace DiscoverRowFactory {

std::optional<DiscoverRowData> CreatedPlaylist(const nlohmann::json& data,
	const std::string& requestedName);

// Account identity is supplied by the request owner; playlist permissions use
// the shared SpotifyPlaylistPolicy. A malformed envelope is not an empty list.
std::optional<std::vector<DiscoverRowData>> PlaylistRows(
	const nlohmann::json& data, const std::string& accountId);

// Map a successful API response body, never a transport/error response.
// nullopt means a malformed page envelope; an empty vector is a valid page
// without displayable episodes. Invalid items are skipped in source order.
// The caller owns localization, paging, load generations and message delivery.
std::optional<std::vector<DiscoverRowData>> SavedEpisodeRows(
	const nlohmann::json& data, bool showProgress, const std::string& doneLabel);

// Remaining endpoints preserve their existing empty/malformed-item behavior.
// API success and pagination remain the caller's responsibility.
bool
BuildResolvedLibraryRow(int32_t tab, const std::string& uri,
	const nlohmann::json& item, bool showProgress, const std::string& doneLabel, DiscoverRowData& row);

std::vector<DiscoverRowData>
TopTrackRows(const nlohmann::json& data);

std::vector<DiscoverRowData>
TopArtistRows(const nlohmann::json& data);

std::vector<DiscoverRowData>
NewReleaseRows(const nlohmann::json& data);

std::vector<DiscoverRowData>
SavedAlbumRows(const nlohmann::json& data);

std::vector<DiscoverRowData>
PodcastRows(const nlohmann::json& data,
	const std::set<std::string>& audiobookIds);

std::vector<DiscoverRowData>
FollowedArtistRows(const nlohmann::json& data);

std::vector<DiscoverRowData>
AudiobookRows(const nlohmann::json& data);

} // namespace DiscoverRowFactory
