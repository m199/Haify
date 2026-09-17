#include "playlist/PlaylistMetadataController.h"

#include <cassert>
#include <cstdio>
#include <limits>
#include <utility>
#include <vector>

#ifdef NDEBUG
#error Playlist metadata tests require assertions; compile without NDEBUG.
#endif

using Kind = PlaylistMetadataKind;
using Json = nlohmann::json;

struct MetadataTransport {
	struct Request {
		Kind kind = Kind::Unsupported;
		std::string id;
		JsonCallback done;
	};
	std::vector<Request> requests;

	auto Getter(Kind kind)
	{
		return [this, kind](const std::string& id, JsonCallback done) {
			requests.push_back({kind, id, std::move(done)});
		};
	}

	PlaylistMetadataController Controller()
	{
		return PlaylistMetadataController(Getter(Kind::Playlist), Getter(Kind::Album),
			Getter(Kind::Podcast), [this](JsonCallback done) {
				requests.push_back({Kind::CurrentUser, "", std::move(done)});
			});
	}

	void Reply(size_t index, bool ok, const Json& data)
	{
		auto done = std::move(requests.at(index).done);
		assert(done);
		done(ok, data);
	}
};

static PlaylistMetadataResult
Decode(Kind kind, const Json& data, bool ok = true)
{
	MetadataTransport transport;
	PlaylistMetadataResult result;
	int calls = 0;
	// Completion remains valid after the temporary controller has gone away.
	bool started = transport.Controller().Load({kind, "id"},
		[&](const PlaylistMetadataResult& value) { result = value; calls++; });
	assert(started && calls == 0 && transport.requests.size() == 1);
	transport.Reply(0, ok, data);
	assert(calls == 1 && transport.requests.size() == 1);
	assert(result.request.kind == kind && result.request.id == "id");
	return result;
}

static void
TestRoutingAndInvalidRequests()
{
	for (Kind kind : {Kind::Playlist, Kind::Album, Kind::Podcast, Kind::CurrentUser}) {
		MetadataTransport transport;
		bool started = transport.Controller().Load({kind, "id"}, nullptr);
		assert(started && transport.requests.size() == 1);
		assert(transport.requests[0].kind == kind);
		assert(transport.requests[0].id == (kind == Kind::CurrentUser ? "" : "id"));
		transport.Reply(0, true, Json::object());
	}
	MetadataTransport transport;
	for (const PlaylistMetadataRequest& request : std::vector<PlaylistMetadataRequest>{
		{Kind::Unsupported, "id"}, {Kind::Playlist, ""}, {Kind::Album, ""},
		{Kind::Podcast, ""}, {static_cast<Kind>(100), "id"}}) {
		bool started = transport.Controller().Load(request,
			[](const PlaylistMetadataResult&) { assert(false); });
		assert(!started && transport.requests.empty());
	}
	PlaylistMetadataController missing(nullptr, nullptr, nullptr, nullptr);
	for (Kind kind : {Kind::Playlist, Kind::Album, Kind::Podcast, Kind::CurrentUser}) {
		bool started = missing.Load({kind, "id"}, nullptr);
		assert(!started);
	}
	bool started = transport.Controller().Load({Kind::CurrentUser, ""}, nullptr);
	assert(started && transport.requests.size() == 1);
}

static void
TestPlaylistFieldsAndPrecedence()
{
	Json data = {{"name", "My list"}, {"snapshot_id", "snapshot"},
		{"html_description", "<b>HTML</b>"}, {"description", "plain"}, {"public", true},
		{"owner", {{"account_id", "account"}, {"id", "legacy"}}},
		{"tracks", {{"total", 70}}}, {"items", {{"total", 80}}},
		{"images", {{{"url", "first"}}, {{"url", "second"}}}}};
	auto result = Decode(Kind::Playlist, data);
	assert(result.ok && result.responseValid);
	assert(result.title == "My list" && result.snapshotId == "snapshot");
	assert(result.description == "<b>HTML</b>" && result.isPublic);
	assert(result.ownerId == "account" && result.total == 70 && result.coverUrl == "first");
	data["owner"]["account_id"] = nullptr;
	data["html_description"] = false;
	data["tracks"] = nullptr;
	result = Decode(Kind::Playlist, data);
	assert(result.ownerId == "legacy" && result.description == "plain" && result.total == 80);
	// Empty strings intentionally do not trigger the legacy fallback.
	data["owner"]["account_id"] = "";
	data["html_description"] = "";
	data["images"][0] = nullptr;
	result = Decode(Kind::Playlist, data);
	assert(result.ownerId.empty() && result.description.empty() && result.coverUrl.empty());
}

static void
TestOptionalFieldsAndBounds()
{
	auto result = Decode(Kind::Playlist, Json::object());
	assert(result.ok && result.title == "Playlist" && result.total == -1);
	assert(!result.isPublic && result.ownerId.empty() && result.snapshotId.empty());
	for (const Json& total : std::vector<Json>{nullptr, "12", 1.5, true,
		std::numeric_limits<uint64_t>::max(), std::numeric_limits<int64_t>::min(),
		int64_t(std::numeric_limits<int32_t>::min()) - 1,
		int64_t(std::numeric_limits<int32_t>::max()) + 1,
		uint64_t(std::numeric_limits<int32_t>::max()) + 1}) {
		result = Decode(Kind::Playlist, {{"tracks", {{"total", total}}},
			{"items", {{"total", 20}}}});
		if (!result.ok || result.total != 0)
			fprintf(stderr, "Invalid total %s: ok=%d, mapped total=%ld\n",
				total.dump().c_str(), result.ok, (long)result.total);
		assert(result.ok && result.total == 0);
	}
	result = Decode(Kind::Playlist, {{"tracks", Json::object()}, {"public", "true"},
		{"name", 4}, {"images", false}, {"owner", Json::array()}});
	assert(result.total == 0 && !result.isPublic && result.title == "Playlist");
	result = Decode(Kind::Playlist, {{"tracks", {{"total", std::numeric_limits<int32_t>::max()}}}});
	assert(result.total == std::numeric_limits<int32_t>::max());
}

static void
TestSignedAndUnsignedIntegerBoundaries()
{
	const int32_t minimum = std::numeric_limits<int32_t>::min();
	const int32_t maximum = std::numeric_limits<int32_t>::max();
	for (int32_t number : {minimum, -1, 0, 1, maximum}) {
		auto result = Decode(Kind::Playlist, {{"items", {{"total", int64_t(number)}}}});
		assert(result.ok && result.total == number);
		if (number >= 0) {
			result = Decode(Kind::Playlist, {{"tracks", {{"total", uint64_t(number)}}}});
			assert(result.ok && result.total == number);
		}
	}
	auto result = Decode(Kind::Playlist,
		{{"status", uint64_t(429)}, {"retry_after", uint64_t(12)}}, false);
	assert(!result.ok && result.status == 429 && result.retryAfter == 12);
	result = Decode(Kind::Playlist, {{"status", std::numeric_limits<uint64_t>::max()},
		{"retry_after", std::numeric_limits<uint64_t>::max()}}, false);
	assert(!result.ok && result.status == -1 && result.retryAfter == -1);
}

static void
TestHeadersAndUserAliases()
{
	for (Kind kind : {Kind::Album, Kind::Podcast}) {
		auto result = Decode(kind, Json::object());
		assert(result.title == (kind == Kind::Album ? "Album" : "Podcast"));
		result = Decode(kind, {{"name", ""}, {"images", {{{"url", "cover"}}}}});
		assert(result.ok && result.title.empty() && result.coverUrl == "cover");
		assert(result.total == -1 && result.ownerId.empty());
	}
	auto result = Decode(Kind::CurrentUser, {{"account_id", "account"}, {"id", "legacy"}});
	assert(result.userId == "account" && result.legacyUserId == "legacy");
	result = Decode(Kind::CurrentUser, {{"account_id", false}, {"id", "legacy"}});
	assert(result.userId == "legacy" && result.legacyUserId == "legacy");
	result = Decode(Kind::CurrentUser, {{"account_id", ""}, {"id", "legacy"}});
	assert(result.userId.empty() && result.legacyUserId == "legacy");
}

static void
TestFailureDistinctions()
{
	for (Kind kind : {Kind::Playlist, Kind::Album, Kind::Podcast, Kind::CurrentUser}) {
		for (int status : {-1, 401, 403, 404, 429, 500}) {
			auto result = Decode(kind, {{"status", status}, {"retry_after", 9},
				{"name", "Do not apply"}}, false);
			assert(!result.ok && result.responseValid && result.status == status);
			assert(result.retryAfter == 9 && result.title.empty() && result.total == -1);
		}
		for (const Json& data : std::vector<Json>{nullptr, true, 12, "text", Json::array()}) {
			auto result = Decode(kind, data);
			assert(!result.ok && !result.responseValid && result.status == -1);
			assert(result.title.empty() && result.ownerId.empty());
		}
	}
	auto result = Decode(Kind::Playlist, {{"status", "429"}, {"retry_after", false}}, false);
	assert(!result.ok && result.status == -1 && result.retryAfter == -1);
}

static void
TestOwnershipInEitherReplyOrder()
{
	for (bool profileFirst : {false, true}) {
		MetadataTransport transport;
		PlaylistMetadataState state;
		auto apply = [&](const PlaylistMetadataResult& result) {
			bool accepted = state.Apply(result);
			assert(accepted);
		};
		bool playlistStarted = transport.Controller().Load({Kind::Playlist, "playlist"}, apply);
		bool profileStarted = transport.Controller().Load({Kind::CurrentUser, ""}, apply);
		assert(playlistStarted && profileStarted);
		Json playlist = {{"owner", {{"id", "legacy"}}}, {"description", "Details"}, {"public", true}};
		Json profile = {{"account_id", "account"}, {"id", "legacy"}};
		transport.Reply(profileFirst ? 1 : 0, true, profileFirst ? profile : playlist);
		assert(!state.IsOwned());
		transport.Reply(profileFirst ? 0 : 1, true, profileFirst ? playlist : profile);
		assert(state.IsOwned() && state.IsPublic() && state.Description() == "Details");
	}
}

static void
TestMissingOwnerDoesNotGrantRights()
{
	PlaylistMetadataState state;
	auto profile = Decode(Kind::CurrentUser, {{"account_id", "account"}});
	bool applied = state.Apply(profile);
	assert(applied && !state.IsOwned());
	for (const std::string& owner : {std::string(), std::string("different"), std::string("account")}) {
		auto playlist = Decode(Kind::Playlist, {{"owner", {{"account_id", owner}}}});
		applied = state.Apply(playlist);
		assert(applied && state.IsOwned() == (owner == "account"));
	}
	profile = Decode(Kind::CurrentUser, {{"account_id", ""}, {"id", "account"}});
	applied = state.Apply(profile);
	assert(applied && !state.IsOwned());
}

static void
TestFailedReadsPreserveState()
{
	PlaylistMetadataState state;
	bool applied = state.Apply(Decode(Kind::Playlist,
		{{"owner", {{"id", "me"}}}, {"description", "Confirmed"}, {"public", true}}));
	assert(applied);
	applied = state.Apply(Decode(Kind::CurrentUser, {{"id", "me"}}));
	assert(applied && state.IsOwned());
	for (const auto& result : {Decode(Kind::Playlist, {{"status", 403}}, false),
		Decode(Kind::CurrentUser, {{"status", 401}}, false),
		Decode(Kind::Playlist, nullptr), Decode(Kind::Album, Json::object())}) {
		applied = state.Apply(result);
		assert(!applied && state.IsOwned() && state.IsPublic());
		assert(state.Description() == "Confirmed");
	}
	state.UpdateDetails("Edited", false);
	assert(state.Description() == "Edited" && !state.IsPublic() && state.IsOwned());
	applied = state.Apply(Decode(Kind::CurrentUser, {{"id", "other"}}));
	assert(applied && !state.IsOwned());
}

int
main()
{
	TestRoutingAndInvalidRequests();
	TestPlaylistFieldsAndPrecedence();
	TestOptionalFieldsAndBounds();
	TestSignedAndUnsignedIntegerBoundaries();
	TestHeadersAndUserAliases();
	TestFailureDistinctions();
	TestOwnershipInEitherReplyOrder();
	TestMissingOwnerDoesNotGrantRights();
	TestFailedReadsPreserveState();
	puts("Playlist metadata controller tests passed.");
}
