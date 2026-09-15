#include "DiscoverCacheDocument.h"

#include <cctype>
#include <algorithm>
#include <utility>

namespace {

bool
TabInvalidated(const nlohmann::json& document, const std::string& tab)
{
	auto invalidated = document.find("invalidated_tabs");
	return invalidated != document.end() && invalidated->is_array()
		&& std::find(invalidated->begin(), invalidated->end(), tab) != invalidated->end();
}

bool
Readable(const nlohmann::json& cache, const std::string& account)
{
	return cache.is_object() && cache.contains("version")
		&& cache["version"].is_number_integer() && cache["version"] == DiscoverCacheDocument::Version
		&& cache.contains("account_id") && cache["account_id"].is_string()
		&& cache["account_id"] == account && cache.contains("tabs") && cache["tabs"].is_object();
}

static std::vector<std::string>
DiscoverCacheAudiobookIds(const nlohmann::json& cache)
{
	std::vector<std::string> audiobookIds;
	if (!cache.contains("audiobook_ids")
			|| !cache["audiobook_ids"].is_array()) {
		return audiobookIds;
	}
	for (const auto& id : cache["audiobook_ids"]) {
		if (id.is_string() && !id.get<std::string>().empty())
			audiobookIds.push_back(id.get<std::string>());
	}
	return audiobookIds;
}

static bool
CachedDiscoverRowArraysMatch(int32_t tab, const nlohmann::json& row)
{
	if (!row.is_object() || !row.contains("values")
			|| !row.contains("uris") || !row.contains("titles")
			|| !row["values"].is_array() || !row["uris"].is_array()
			|| !row["titles"].is_array()
			|| row["values"].size() != (size_t)DiscoverTabColumnCount(tab)
			|| row["uris"].size() != (size_t)DiscoverTabColumnCount(tab)
			|| row["titles"].size() != (size_t)DiscoverTabColumnCount(tab)) {
		return false;
	}
	return true;
}

static bool
CachedDiscoverRowPrimaryUriMatches(int32_t tab, const nlohmann::json& row)
{
	return !row["uris"].empty() && row["uris"][0].is_string()
		&& PrimaryUriMatchesTab(tab, row["uris"][0].get<std::string>());
}

static bool
CachedDiscoverRowColumnsAreStrings(int32_t tab, const nlohmann::json& row)
{
	for (size_t column = 0; column < (size_t)DiscoverTabColumnCount(tab); column++) {
		if (!row["values"][column].is_string()
				|| !row["uris"][column].is_string()
				|| !row["titles"][column].is_string()) {
			return false;
		}
	}
	return true;
}

static bool
IsValidCachedDiscoverRow(int32_t tab, const nlohmann::json& row)
{
	return CachedDiscoverRowArraysMatch(tab, row)
		&& CachedDiscoverRowPrimaryUriMatches(tab, row)
		&& CachedDiscoverRowColumnsAreStrings(tab, row);
}

static DiscoverRowData
CachedDiscoverRowFromJson(const nlohmann::json& row)
{
	DiscoverRowData cached;
	for (const auto& value : row["values"])
		cached.vals.push_back(value.get<std::string>());
	for (const auto& value : row["uris"])
		cached.uris.push_back(value.get<std::string>());
	for (const auto& value : row["titles"])
		cached.ttls.push_back(value.get<std::string>());
	cached.writable = !row.contains("writable")
		|| !row["writable"].is_boolean() || row["writable"].get<bool>();
	cached.owned = row.contains("owned") && row["owned"].is_boolean()
		&& row["owned"].get<bool>();
	return cached;
}

static std::vector<DiscoverRowData>
CachedDiscoverRowsFromJson(int32_t tab, const nlohmann::json& rows)
{
	std::vector<DiscoverRowData> cachedRows;
	for (const auto& row : rows) {
		if (cachedRows.size() >= size_t(DiscoverCacheDocument::MaxRows))
			break;
		if (IsValidCachedDiscoverRow(tab, row))
			cachedRows.push_back(CachedDiscoverRowFromJson(row));
	}
	return cachedRows;
}

nlohmann::json
EncodeRows(int32_t tab, const std::vector<DiscoverRowData>& rows)
{
	nlohmann::json result = nlohmann::json::array();
	for (const auto& row : rows) {
		if (result.size() >= DiscoverCacheDocument::MaxRows)
			break;
		nlohmann::json encoded = {{"values", row.vals}, {"uris", row.uris},
			{"titles", row.ttls}, {"writable", row.writable}, {"owned", row.owned}};
		if (IsValidCachedDiscoverRow(tab, encoded))
			result.push_back(std::move(encoded));
	}
	return result;
}

void
MergeInvalidations(nlohmann::json& data, const nlohmann::json& existing)
{
	// Only an explicit fresh tab snapshot may replace an earlier tombstone.
	for (int32_t tab = 0; tab < TAB_COUNT; tab++) {
		std::string id = DiscoverTabId(tab);
		if (TabInvalidated(existing, id) && !data["tabs"].contains(id) && !TabInvalidated(data, id))
			data["invalidated_tabs"].push_back(id);
	}
}

void
MergeAudiobookIds(nlohmann::json& data, const nlohmann::json& existing)
{
	if (!data.contains("audiobook_ids") && existing.contains("audiobook_ids")
			&& existing["audiobook_ids"].is_array()
			&& !TabInvalidated(data, DiscoverTabId(TAB_AUDIOBOOKS)))
		data["audiobook_ids"] = existing["audiobook_ids"];
}

} // namespace

DiscoverCacheReadResult
DiscoverCacheDocument::ReadTab(const nlohmann::json& cache,
	const DiscoverCacheReadRequest& request)
{
	DiscoverCacheReadResult result;
	result.request = request;
	result.status = DiscoverCacheReadStatus::Invalid;
	if (request.tab < 0 || request.tab >= TAB_COUNT || request.accountId.empty()
			|| !Readable(cache, request.accountId))
		return result;
	if (TabInvalidated(cache, DiscoverTabId(request.tab)))
		return result;
	auto found = cache["tabs"].find(DiscoverTabId(request.tab));
	if (found == cache["tabs"].end() || !found->is_array())
		return result;
	result.rows = CachedDiscoverRowsFromJson(request.tab, *found);
	if (cache.contains("audiobook_ids") && cache["audiobook_ids"].is_array())
		result.audiobookIds = DiscoverCacheAudiobookIds(cache);
	result.status = DiscoverCacheReadStatus::Available;
	return result;
}

nlohmann::json
DiscoverCacheDocument::Encode(const DiscoverCacheSnapshot& snapshot)
{
	nlohmann::json document = {{"version", Version}, {"account_id", snapshot.accountId},
		{"saved_at", snapshot.savedAt}, {"tabs", nlohmann::json::object()}};
	document["invalidated_tabs"] = nlohmann::json::array();
	for (int32_t tab = 0; tab < TAB_COUNT; tab++) {
		if (snapshot.invalidatedTabs.count(tab))
			document["invalidated_tabs"].push_back(DiscoverTabId(tab));
		else if (snapshot.tabs[tab])
			document["tabs"][DiscoverTabId(tab)] = EncodeRows(tab, *snapshot.tabs[tab]);
	}
	if (snapshot.audiobookIds)
		document["audiobook_ids"] = *snapshot.audiobookIds;
	return document;
}

void
DiscoverCacheDocument::MergeUnloadedTabs(nlohmann::json& data,
	const nlohmann::json& existing)
{
	if (!data.is_object() || !data.contains("account_id") || !data["account_id"].is_string()
			|| !Readable(existing, data["account_id"].get<std::string>())
			|| !data.contains("tabs") || !data["tabs"].is_object())
		return;
	for (auto tab = existing["tabs"].begin(); tab != existing["tabs"].end(); ++tab) {
		if (!TabInvalidated(data, tab.key()) && !TabInvalidated(existing, tab.key())
				&& !data["tabs"].contains(tab.key()))
			data["tabs"][tab.key()] = tab.value();
	}
	MergeInvalidations(data, existing);
	MergeAudiobookIds(data, existing);
}

std::string
DiscoverCacheDocument::SafeAccountName(const std::string& accountId)
{
	std::string name;
	for (unsigned char character : accountId) {
		if (isalnum(character) || character == '-' || character == '_')
			name += (char)character;
		else
			name += '_';
		if (name.size() >= 96)
			break;
	}
	return name;
}
