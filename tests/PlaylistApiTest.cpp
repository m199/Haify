#include "JsonApiTestSupport.h"
#include "HaifyDebug.h"
#include "settings/SettingsController.h"
#include "spotify/api/PlaylistApi.h"

#include <cstdio>

// Standalone link substitutes: never link App.cpp or SettingsController.cpp.
bool gIsDebug = false;

std::string
SettingsController::CacheFilePath(const std::string& directory,
	const std::string& fileName, bool createDirectory)
{
	assert(directory == "playlists" && !fileName.empty() && !createDirectory);
	// An unavailable cache path prevents unlink from touching real user files.
	return "";
}

struct PlaylistFixture {
	JsonApiTestTransport transport;
	PlaylistApi api{transport.GetHandler(), transport.BodyHandler("PUT"),
		transport.BodyHandler("POST"), transport.BodyHandler("DELETE"),
		transport.BodyHandler("UPLOAD"), transport.CacheHandler()};
};

static nlohmann::json
Playlist(const std::string& id, const std::string& name,
	const nlohmann::json& owner, bool collaborative = false)
{
	return {{"id", id}, {"name", name}, {"owner", owner},
		{"collaborative", collaborative}};
}

static void
SeedOwnedPlaylist(PlaylistFixture& fixture)
{
	fixture.api.SetAccountId("account");
	fixture.api.GetPlaylists(nullptr);
	fixture.transport.Reply(0, true, {{"items", {Playlist("list", "Old",
		{{"account_id", "account"}})}}, {"total", 1}});
	assert(fixture.api.GetCachedPlaylists().size() == 1);
}

static void
TestPlaylistPagingAndOwnership()
{
	PlaylistFixture fixture;
	fixture.api.SetAccountId("account");
	JsonApiTestResult result;
	auto owned = Playlist("owned", "Mine", {{"account_id", "account"}});
	auto foreign = Playlist("foreign", "Other", {{"id", "other"}});
	auto legacy = Playlist("legacy", "Legacy", {{"id", "account"}});
	auto shared = Playlist("shared", "Shared", {{"id", "other"}}, true);
	fixture.api.GetPlaylists(result.Callback());
	fixture.transport.Expect(0, "GET", "/me/playlists?limit=50&offset=0");
	fixture.transport.Reply(0, true, {{"items", {owned, foreign}}, {"total", 4}});
	assert(result.calls == 0 && fixture.api.GetCachedPlaylists().empty());
	fixture.transport.Expect(1, "GET", "/me/playlists?limit=50&offset=2");
	fixture.transport.Reply(1, true, {{"items", {legacy, shared}}, {"total", 4}});
	result.Expect(true, {{"items", {owned, foreign, legacy, shared}}, {"total", 4}});
	const std::vector<std::pair<std::string, std::string>> writable = {
		{"owned", "Mine"}, {"legacy", "Legacy"}, {"shared", "Shared"}
	};
	assert(fixture.api.GetCachedPlaylists() == writable);
	fixture.api.SetAccountId("account");
	assert(fixture.api.GetCachedPlaylists() == writable);
	fixture.api.SetAccountId("another-account");
	assert(fixture.api.GetCachedPlaylists().empty());
	fixture.api.GetPlaylists(nullptr);
	fixture.transport.Reply(2, true, {{"items", {shared}}, {"total", 1}});
	assert(fixture.api.GetCachedPlaylists().size() == 1);
	fixture.api.ClearSession();
	assert(fixture.api.GetCachedPlaylists().empty());
}

static void
TestFailedPlaylistPageKeepsConfirmedCache()
{
	for (const auto& failure : std::vector<nlohmann::json>{
		{{"status", 429}, {"retry_after", 12}}, {{"items", nullptr}}}) {
		PlaylistFixture fixture;
		SeedOwnedPlaylist(fixture);
		auto confirmed = fixture.api.GetCachedPlaylists();
		JsonApiTestResult result;
		fixture.api.GetPlaylists(result.Callback());
		fixture.transport.Reply(1, true, {{"items", {Playlist("new", "New",
			{{"account_id", "account"}})}}, {"total", 2}});
		assert(result.calls == 0);
		fixture.transport.Reply(2, !failure.contains("status"), failure);
		result.Expect(false, failure);
		assert(fixture.api.GetCachedPlaylists() == confirmed);
		assert(fixture.transport.requests.size() == 3);
	}
}

static void
TestMetadataMutationsPublishOnlyOnSuccess()
{
	for (bool success : {false, true}) {
		PlaylistFixture fixture;
		SeedOwnedPlaylist(fixture);
		JsonApiTestResult renamed;
		const std::string name = "A \"quoted\" name\nline";
		fixture.api.RenamePlaylist("list", name, renamed.Callback());
		fixture.transport.ExpectJson(1, "PUT", "/playlists/list", {{"name", name}});
		assert(fixture.api.GetCachedPlaylists().front().second == "Old");
		nlohmann::json response = {{"status", success ? 204 : 403}};
		fixture.transport.Reply(1, success, response);
		renamed.Expect(success, response);
		assert(fixture.api.GetCachedPlaylists().front().second == (success ? name : "Old"));
		JsonApiTestResult removed;
		fixture.api.UnfollowPlaylist("list", removed.Callback());
		fixture.transport.Expect(2, "DELETE", "/me/library?uris=spotify%3Aplaylist%3Alist");
		assert(fixture.api.GetCachedPlaylists().size() == 1);
		fixture.transport.Reply(2, success, response);
		removed.Expect(success, response);
		assert(fixture.api.GetCachedPlaylists().size() == (success ? 0u : 1u));
		const std::vector<std::string> events = {"GET /me/playlists?limit=50&offset=0",
			"INVALIDATE /me/playlists", "INVALIDATE /playlists/list", "PUT /playlists/list",
			"INVALIDATE /me/playlists", "INVALIDATE /playlists/list",
			"DELETE /me/library?uris=spotify%3Aplaylist%3Alist"};
		assert(fixture.transport.events == events);
	}
}

static void
TestCreateAndUpdateDetails()
{
	PlaylistFixture fixture;
	SeedOwnedPlaylist(fixture);
	JsonApiTestResult created;
	fixture.api.CreatePlaylist("New", created.Callback());
	fixture.transport.ExpectJson(1, "POST", "/me/playlists", {{"name", "New"}, {"public", false}});
	fixture.transport.Reply(1, true, {{"id", "list"}});
	created.Expect(true, {{"id", "list"}});
	assert(fixture.api.GetCachedPlaylists().size() == 1);
	assert(fixture.api.GetCachedPlaylists().front().second == "New");
	JsonApiTestResult updated;
	fixture.api.UpdatePlaylistDetails("list", "Updated", "Description", true, updated.Callback());
	fixture.transport.ExpectJson(2, "PUT", "/playlists/list",
		{{"name", "Updated"}, {"description", "Description"}, {"public", true}});
	fixture.transport.Reply(2, true, {{"status", 204}});
	updated.Expect(true, {{"status", 204}});
	assert(fixture.api.GetCachedPlaylists().front().second == "Updated");
}

static void
TestItemMutationContracts()
{
	PlaylistFixture fixture;
	fixture.api.GetPlaylistTracks("list", 50, 25, nullptr);
	fixture.transport.Expect(0, "GET", "/playlists/list/items?limit=25&offset=50");
	fixture.api.AddTrackToPlaylist("list", "spotify:episode:one", nullptr);
	fixture.transport.ExpectJson(1, "POST", "/playlists/list/items", {{"uris", {"spotify:episode:one"}}});
	fixture.api.RemoveTrackFromPlaylist("list", "spotify:track:one", nullptr);
	fixture.transport.ExpectJson(2, "DELETE", "/playlists/list/items",
		{{"items", {{{"uri", "spotify:track:one"}}}}});
	fixture.api.RemoveItemsFromPlaylist("list", {"spotify:track:one", "",
		"spotify:album:invalid", "spotify:episode:two"}, "snapshot", nullptr);
	fixture.transport.ExpectJson(3, "DELETE", "/playlists/list/items",
		{{"items", {{{"uri", "spotify:track:one"}}, {{"uri", "spotify:episode:two"}}}},
			{"snapshot_id", "snapshot"}});
	fixture.api.ReorderPlaylistItems("list", 2, 8, 3, "snapshot", nullptr);
	fixture.transport.ExpectJson(4, "PUT", "/playlists/list/items",
		{{"range_start", 2}, {"insert_before", 8}, {"range_length", 3}, {"snapshot_id", "snapshot"}});
	fixture.api.ReorderPlaylistItems("list", 0, 2, 0, "", nullptr);
	fixture.transport.ExpectJson(5, "PUT", "/playlists/list/items",
		{{"range_start", 0}, {"insert_before", 2}, {"range_length", 1}});
	assert(fixture.transport.requests.size() == 6);
	assert(fixture.transport.invalidations.size() == 5);
	for (size_t index = 1; index < 6; index++) {
		assert(fixture.transport.events.at(index * 2 - 1) == "INVALIDATE /playlists/list");
		assert(fixture.transport.invalidations.at(index - 1) == "/playlists/list");
	}
}

static void
TestReplaceLimitsAndClearing()
{
	PlaylistFixture fixture;
	std::vector<std::string> uris(100, "spotify:track:one");
	fixture.api.ReplacePlaylistItems("list", uris, nullptr);
	fixture.transport.ExpectJson(0, "PUT", "/playlists/list/items", {{"uris", uris}});
	JsonApiTestResult tooMany;
	uris.push_back("spotify:episode:one");
	fixture.api.ReplacePlaylistItems("list", uris, tooMany.Callback());
	tooMany.Expect(false, {{"status", 400}, {"error", "too_many_playlist_items"}});
	assert(fixture.transport.requests.size() == 1 && fixture.transport.invalidations.size() == 1);
	fixture.api.ReplacePlaylistItems("list", {}, nullptr);
	fixture.transport.ExpectJson(1, "PUT", "/playlists/list/items", {{"uris", nlohmann::json::array()}});
	JsonApiTestResult invalid;
	fixture.api.RemoveItemsFromPlaylist("list", {"", "spotify:album:no"}, "", invalid.Callback());
	invalid.Expect(false, {{"status", 400}, {"error", "no_valid_playlist_items"}});
	assert(fixture.transport.requests.size() == 2 && fixture.transport.invalidations.size() == 2);
}

static std::vector<std::string>
RepeatedPlaylist(size_t size)
{
	std::vector<std::string> uris(size, "spotify:track:other");
	uris.at(0) = "spotify:track:duplicate";
	uris.at(2) = "spotify:track:duplicate";
	uris.at(4) = "spotify:track:duplicate";
	return uris;
}

static void
TestPreciseRemovalPreservesOtherOccurrences(size_t size)
{
	PlaylistFixture fixture;
	auto original = RepeatedPlaylist(size);
	auto desired = original;
	desired.erase(desired.begin() + 2);
	JsonApiTestResult result;
	fixture.api.RemovePlaylistItemsFromKnownSnapshot("list", {{original[2], 2}},
		"snapshot", original, result.Callback());
	assert(fixture.transport.requests.size() == 1 && result.calls == 0);
	if (size <= 101) {
		fixture.transport.ExpectJson(0, "PUT", "/playlists/list/items", {{"uris", desired}});
		fixture.transport.Reply(0, true, {{"snapshot_id", "updated"}});
		result.Expect(true, {{"snapshot_id", "updated"}});
		return;
	}
	fixture.transport.ExpectJson(0, "DELETE", "/playlists/list/items",
		{{"items", {{{"uri", original[2]}}}}, {"snapshot_id", "snapshot"}});
	fixture.transport.Reply(0, true, {{"snapshot_id", "deleted"}});
	fixture.transport.ExpectJson(1, "POST", "/playlists/list/items",
		{{"uris", {original[0]}}, {"position", 0}});
	assert(result.calls == 0 && fixture.transport.requests.size() == 2);
	fixture.transport.Reply(1, true, {{"snapshot_id", "restored-first"}});
	fixture.transport.ExpectJson(2, "POST", "/playlists/list/items",
		{{"uris", {original[4]}}, {"position", 3}});
	assert(result.calls == 0);
	fixture.transport.Reply(2, true, {{"snapshot_id", "restored-last"}});
	result.Expect(true, {{"status", 200}});
	assert(fixture.transport.requests.size() == 3);
}

static void
TestRestoreRunsRespectRequestLimit()
{
	PlaylistFixture fixture;
	std::vector<std::string> original(203, "spotify:track:duplicate");
	JsonApiTestResult result;
	fixture.api.RemovePlaylistItemsFromKnownSnapshot("list", {{original[0], 0}},
		"snapshot", original, result.Callback());
	fixture.transport.Reply(0, true, {{"snapshot_id", "deleted"}});
	for (size_t index = 0; index < 3; index++) {
		std::vector<std::string> run(index == 2 ? 2 : 100, original[0]);
		fixture.transport.ExpectJson(index + 1, "POST", "/playlists/list/items",
			{{"uris", run}, {"position", index * 100}});
		assert(result.calls == 0 && fixture.transport.requests.size() == index + 2);
		fixture.transport.Reply(index + 1, true, {{"snapshot_id", "restored"}});
	}
	result.Expect(true, {{"status", 200}});
	assert(fixture.transport.requests.size() == 4);
}

static void
TestUnsortedSelectionRestoresAdjacentDifferentUris()
{
	PlaylistFixture fixture;
	std::vector<std::string> original(104, "spotify:track:other");
	original[0] = original[2] = original[6] = "spotify:track:a";
	original[1] = original[4] = "spotify:episode:b";
	JsonApiTestResult result;
	fixture.api.RemovePlaylistItemsFromKnownSnapshot("list",
		{{original[4], 4}, {original[0], 0}}, "snapshot", original, result.Callback());
	fixture.transport.ExpectJson(0, "DELETE", "/playlists/list/items",
		{{"items", {{{"uri", "spotify:episode:b"}}, {{"uri", "spotify:track:a"}}}},
			{"snapshot_id", "snapshot"}});
	fixture.transport.Reply(0, true, {{"snapshot_id", "deleted"}});
	fixture.transport.ExpectJson(1, "POST", "/playlists/list/items",
		{{"uris", {"spotify:episode:b", "spotify:track:a"}}, {"position", 0}});
	fixture.transport.Reply(1, true, {{"snapshot_id", "restored-pair"}});
	fixture.transport.ExpectJson(2, "POST", "/playlists/list/items",
		{{"uris", {"spotify:track:a"}}, {"position", 4}});
	assert(result.calls == 0);
	fixture.transport.Reply(2, true, {{"snapshot_id", "restored-last"}});
	result.Expect(true, {{"status", 200}});
	assert(fixture.transport.requests.size() == 3);
}

static void
TestPositionConflictsDoNotMutate()
{
	const std::vector<std::vector<std::pair<std::string, int>>> selections = {
		{{"spotify:track:wrong", 0}}, {{"spotify:track:duplicate", 5}},
		{{"spotify:track:duplicate", 2}, {"spotify:track:duplicate", 2}}
	};
	for (const auto& selection : selections) {
		PlaylistFixture fixture;
		JsonApiTestResult result;
		fixture.api.RemovePlaylistItemsFromKnownSnapshot("list", selection,
			"snapshot", RepeatedPlaylist(5), result.Callback());
		result.Expect(false, {{"status", 409}, {"error", "playlist_position_changed"}});
		assert(fixture.transport.requests.empty());
	}
	PlaylistFixture fixture;
	JsonApiTestResult invalid;
	fixture.api.RemovePlaylistItemsAtPositions("list",
		{{"spotify:track:duplicate", -1}, {"spotify:album:no", 0}}, "", invalid.Callback());
	invalid.Expect(false, {{"status", 400}, {"error", "no_valid_playlist_positions"}});
	assert(fixture.transport.requests.empty() && fixture.transport.invalidations.empty());
}

static nlohmann::json
ItemPage(const std::vector<std::string>& uris, int total, bool legacy = false)
{
	nlohmann::json items = nlohmann::json::array();
	for (const auto& uri : uris)
		items.push_back({{legacy ? "track" : "item", {{"uri", uri}}}});
	return {{"items", items}, {"total", total}};
}

static void
TestMissingSnapshotFetchesAllPages()
{
	PlaylistFixture fixture;
	JsonApiTestResult result;
	fixture.api.RemovePlaylistItemsFromKnownSnapshot("list", {{"spotify:track:b", 1}},
		"", {"spotify:track:untrusted"}, result.Callback());
	fixture.transport.Expect(0, "GET", "/playlists/list/items?limit=50&offset=0");
	fixture.transport.Reply(0, true, ItemPage({"spotify:track:a", "spotify:track:b"}, 3));
	fixture.transport.Expect(1, "GET", "/playlists/list/items?limit=50&offset=2");
	assert(result.calls == 0 && fixture.transport.requests.size() == 2);
	fixture.transport.Reply(1, true, ItemPage({"spotify:episode:c"}, 3, true));
	fixture.transport.ExpectJson(2, "PUT", "/playlists/list/items",
		{{"uris", {"spotify:track:a", "spotify:episode:c"}}});
	fixture.transport.Reply(2, true, {{"snapshot_id", "updated"}});
	result.Expect(true, {{"snapshot_id", "updated"}});
}

static void
TestIncompleteOrMalformedReadDoesNotMutate()
{
	const std::vector<nlohmann::json> responses = {
		{{"items", nlohmann::json::array()}, {"total", 2}},
		{{"items", nullptr}}, nlohmann::json::array(), {{"other", true}}
	};
	for (size_t index = 0; index < responses.size(); index++) {
		PlaylistFixture fixture;
		JsonApiTestResult result;
		fixture.api.RemovePlaylistItemsAtPositions("list", {{"spotify:track:a", 0}},
			"snapshot", result.Callback());
		fixture.transport.Reply(0, true, ItemPage({"spotify:track:a"}, 2));
		fixture.transport.Reply(1, true, responses[index]);
		auto expected = index == 0
			? nlohmann::json({{"status", -1}, {"error", "incomplete_playlist_read"}})
			: responses[index];
		result.Expect(false, expected);
		assert(fixture.transport.requests.size() == 2);
	}
}

static void
TestRemovalFailuresStopFollowupRequests()
{
	for (int status : {-1, 401, 403, 404, 429, 500}) {
		for (size_t failedRequest : {0u, 1u, 2u}) {
			PlaylistFixture fixture;
			JsonApiTestResult result;
			auto original = RepeatedPlaylist(102);
			fixture.api.RemovePlaylistItemsFromKnownSnapshot("list", {{original[2], 2}},
				"snapshot", original, result.Callback());
			for (size_t index = 0; index < failedRequest; index++) {
				fixture.transport.Reply(index, true, {{"snapshot_id", "changed"}});
				assert(result.calls == 0);
			}
			nlohmann::json failure = {{"status", status}, {"body", "failure"}, {"retry_after", 12}};
			fixture.transport.Reply(failedRequest, false, failure);
			if (failedRequest > 0) {
				failure["partial_update"] = true;
				failure["error"] = "duplicate_restore_failed";
			}
			result.Expect(false, failure);
			assert(fixture.transport.requests.size() == failedRequest + 1);
		}
	}
}

static void
TestReadFailuresDoNotTriggerRemoval()
{
	for (int status : {-1, 401, 403, 404, 429, 500}) {
		PlaylistFixture fixture;
		JsonApiTestResult result;
		fixture.api.RemovePlaylistItemsAtPositions("list", {{"spotify:track:a", 0}},
			"snapshot", result.Callback());
		nlohmann::json failure = {{"status", status}, {"retry_after", 12}};
		fixture.transport.Reply(0, false, failure);
		result.Expect(false, failure);
		assert(fixture.transport.requests.size() == 1);
	}
}

int
main()
{
	TestPlaylistPagingAndOwnership();
	TestFailedPlaylistPageKeepsConfirmedCache();
	TestMetadataMutationsPublishOnlyOnSuccess();
	TestCreateAndUpdateDetails();
	TestItemMutationContracts();
	TestReplaceLimitsAndClearing();
	TestPreciseRemovalPreservesOtherOccurrences(5);
	TestPreciseRemovalPreservesOtherOccurrences(101);
	TestPreciseRemovalPreservesOtherOccurrences(102);
	TestRestoreRunsRespectRequestLimit();
	TestUnsortedSelectionRestoresAdjacentDifferentUris();
	TestPositionConflictsDoNotMutate();
	TestMissingSnapshotFetchesAllPages();
	TestIncompleteOrMalformedReadDoesNotMutate();
	TestRemovalFailuresStopFollowupRequests();
	TestReadFailuresDoNotTriggerRemoval();
	std::puts("Playlist API tests passed.");
}
