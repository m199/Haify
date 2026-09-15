#include "discover/DiscoverCacheDocument.h"

#include <cassert>
#include <cstdio>

#ifdef NDEBUG
#error Discover cache tests require assertions; compile without NDEBUG.
#endif

using nlohmann::json;

static DiscoverRowData
AlbumRow()
{
	return {{"Album", "Artist"}, {"spotify:album:one", "spotify:artist:one"},
		{"Album", "Artist"}, false, true};
}

static DiscoverCacheSnapshot
Snapshot()
{
	DiscoverCacheSnapshot snapshot;
	snapshot.accountId = "account-a";
	snapshot.savedAt = 12345;
	snapshot.tabs[TAB_SAVED_ALBUMS] = std::vector<DiscoverRowData>{AlbumRow()};
	snapshot.audiobookIds = std::set<std::string>{"book1"};
	return snapshot;
}

static void
TestRoundTripAndIdentity()
{
	json document = DiscoverCacheDocument::Encode(Snapshot());
	DiscoverCacheReadRequest request{"account-a", TAB_SAVED_ALBUMS, 12};
	auto result = DiscoverCacheDocument::ReadTab(document, request);
	assert(result.status == DiscoverCacheReadStatus::Available && result.rows.size() == 1);
	assert(result.request.accountId == "account-a" && result.request.generation == 12);
	assert(result.rows[0].vals == AlbumRow().vals && result.rows[0].uris == AlbumRow().uris);
	assert(result.rows[0].ttls == AlbumRow().ttls && !result.rows[0].writable && result.rows[0].owned);
	assert(result.audiobookIds && *result.audiobookIds == std::vector<std::string>{"book1"});
	request.accountId = "account-b";
	assert(DiscoverCacheDocument::ReadTab(document, request).status == DiscoverCacheReadStatus::Invalid);
	request = {"account-a", TAB_AUDIOBOOKS, 12};
	assert(DiscoverCacheDocument::ReadTab(document, request).status == DiscoverCacheReadStatus::Invalid);
	request.tab = TAB_COUNT;
	assert(DiscoverCacheDocument::ReadTab(document, request).status == DiscoverCacheReadStatus::Invalid);
}

static void
TestMalformedDocuments()
{
	const DiscoverCacheReadRequest request{"account-a", TAB_SAVED_ALBUMS, 1};
	for (const json& invalid : {json(), json::array(), json(42), json::object()})
		assert(DiscoverCacheDocument::ReadTab(invalid, request).status == DiscoverCacheReadStatus::Invalid);
	json document = DiscoverCacheDocument::Encode(Snapshot());
	for (const json& version : {json("5"), json(4), json::array()}) {
		document["version"] = version;
		assert(DiscoverCacheDocument::ReadTab(document, request).status == DiscoverCacheReadStatus::Invalid);
	}
	document = DiscoverCacheDocument::Encode(Snapshot());
	document["account_id"] = 42;
	assert(DiscoverCacheDocument::ReadTab(document, request).status == DiscoverCacheReadStatus::Invalid);
	document = DiscoverCacheDocument::Encode(Snapshot());
	document["audiobook_ids"] = "broken";
	auto result = DiscoverCacheDocument::ReadTab(document, request);
	assert(result.status == DiscoverCacheReadStatus::Available && !result.audiobookIds);
}

static void
TestRowsAndLimits()
{
	const DiscoverCacheReadRequest request{"account-a", TAB_SAVED_ALBUMS, 1};
	json document = DiscoverCacheDocument::Encode(Snapshot());
	json valid = document["tabs"]["saved_albums"][0];
	json shortRow = valid;
	shortRow["titles"] = json::array();
	json wrongUri = valid;
	wrongUri["uris"][0] = "spotify:episode:one";
	json wrongValue = valid;
	wrongValue["values"][0] = 42;
	document["tabs"]["saved_albums"] = {nullptr, shortRow, wrongUri, valid, wrongValue};
	auto result = DiscoverCacheDocument::ReadTab(document, request);
	assert(result.status == DiscoverCacheReadStatus::Available && result.rows.size() == 1);
	document["tabs"]["saved_albums"] = json::array();
	for (int i = 0; i < 510; i++)
		document["tabs"]["saved_albums"].push_back(valid);
	assert(DiscoverCacheDocument::ReadTab(document, request).rows.size() == 500);
	auto snapshot = Snapshot();
	snapshot.tabs[TAB_SAVED_ALBUMS] = std::vector<DiscoverRowData>(510, AlbumRow());
	assert(DiscoverCacheDocument::Encode(snapshot)["tabs"]["saved_albums"].size() == 500);
}

static void
TestMergeKeepsAuthoritativeEmptyTabs()
{
	auto previous = Snapshot();
	previous.tabs[TAB_PODCASTS] = std::vector<DiscoverRowData>{
		{{"Podcast", "Publisher"}, {"spotify:show:one", ""}, {"Podcast", ""}}};
	json old = DiscoverCacheDocument::Encode(previous);
	DiscoverCacheSnapshot replacement;
	replacement.accountId = "account-a";
	replacement.tabs[TAB_SAVED_ALBUMS] = std::vector<DiscoverRowData>{};
	json updated = DiscoverCacheDocument::Encode(replacement);
	DiscoverCacheDocument::MergeUnloadedTabs(updated, old);
	assert(updated["tabs"]["saved_albums"].empty());
	assert(updated["tabs"]["podcasts"].size() == 1);
	assert(updated["audiobook_ids"] == old["audiobook_ids"]);
	assert(DiscoverCacheDocument::ReadTab(updated, {"account-a", TAB_SAVED_ALBUMS, 1}).status
		== DiscoverCacheReadStatus::Available);
	replacement.accountId = "account-b";
	updated = DiscoverCacheDocument::Encode(replacement);
	DiscoverCacheDocument::MergeUnloadedTabs(updated, old);
	assert(!updated["tabs"].contains("podcasts") && !updated.contains("audiobook_ids"));
	assert(DiscoverCacheDocument::SafeAccountName("a/b:c") == "a_b_c");
	assert(DiscoverCacheDocument::SafeAccountName("").empty());
	assert(DiscoverCacheDocument::SafeAccountName(std::string(120, 'a')).size() == 96);
}

static void
TestInvalidationSurvivesPartialWrites()
{
	auto old = DiscoverCacheDocument::Encode(Snapshot());
	DiscoverCacheSnapshot invalidated;
	invalidated.accountId = "account-a";
	invalidated.invalidatedTabs = {TAB_SAVED_ALBUMS, TAB_AUDIOBOOKS};
	auto pending = DiscoverCacheDocument::Encode(invalidated);
	DiscoverCacheDocument::MergeUnloadedTabs(pending, old);
	assert(!pending["tabs"].contains("saved_albums") && !pending.contains("audiobook_ids"));
	DiscoverCacheSnapshot partial;
	partial.accountId = "account-a";
	partial.tabs[TAB_PODCASTS] = std::vector<DiscoverRowData>{};
	auto next = DiscoverCacheDocument::Encode(partial);
	DiscoverCacheDocument::MergeUnloadedTabs(next, pending);
	DiscoverCacheDocument::MergeUnloadedTabs(next, old);
	assert(!next["tabs"].contains("saved_albums") && !next.contains("audiobook_ids"));
	assert(DiscoverCacheDocument::ReadTab(next, {"account-a", TAB_SAVED_ALBUMS, 1}).status
		== DiscoverCacheReadStatus::Invalid);
	auto fresh = DiscoverCacheDocument::Encode(Snapshot());
	DiscoverCacheDocument::MergeUnloadedTabs(fresh, next);
	assert(DiscoverCacheDocument::ReadTab(fresh, {"account-a", TAB_SAVED_ALBUMS, 1}).rows.size() == 1);
	// Even a malformed file containing both rows and a tombstone must reject the rows.
	old["invalidated_tabs"] = {"saved_albums"};
	assert(DiscoverCacheDocument::ReadTab(old, {"account-a", TAB_SAVED_ALBUMS, 1}).status
		== DiscoverCacheReadStatus::Invalid);
}

int
main()
{
	TestInvalidationSurvivesPartialWrites();
	TestRoundTripAndIdentity();
	TestMalformedDocuments();
	TestRowsAndLimits();
	TestMergeKeepsAuthoritativeEmptyTabs();
	std::puts("Discover cache document tests passed.");
}
