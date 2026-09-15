#pragma once

#include "DiscoverCacheController.h"
#include "DiscoverRowData.h"

#include <nlohmann/json.hpp>
#include <set>

struct DiscoverCacheSnapshot {
	std::string accountId;
	int64_t savedAt = 0;
	// Missing means not loaded; an empty vector is an authoritative empty tab.
	std::array<std::optional<std::vector<DiscoverRowData>>, TAB_COUNT> tabs;
	std::optional<std::set<std::string>> audiobookIds;
	// Explicit tombstones distinguish an invalidated tab from an unloaded one.
	std::set<int32_t> invalidatedTabs;
};

enum class DiscoverCacheReadStatus { Available, Unavailable, Invalid };

struct DiscoverCacheReadResult {
	DiscoverCacheReadRequest request;
	DiscoverCacheReadStatus status = DiscoverCacheReadStatus::Unavailable;
	int32_t ioStatus = 0;
	std::vector<DiscoverRowData> rows;
	std::optional<std::vector<std::string>> audiobookIds;
};

namespace DiscoverCacheDocument {
inline constexpr int32_t Version = 5;
inline constexpr int32_t MaxRows = 500;

// Keys and column order retain the version-5 disk format. Invalid rows are
// skipped; a wrong account/version or missing tab is not an empty snapshot.
DiscoverCacheReadResult ReadTab(const nlohmann::json& document,
	const DiscoverCacheReadRequest& request);
nlohmann::json Encode(const DiscoverCacheSnapshot& snapshot);
void MergeUnloadedTabs(nlohmann::json& document, const nlohmann::json& existing);
std::string SafeAccountName(const std::string& accountId);
}
