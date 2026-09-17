#include "playlist/PlaylistPageController.h"

#include <cassert>
#include <cstdio>
#include <limits>
#include <utility>

#ifdef NDEBUG
#error Playlist page tests require assertions; compile without NDEBUG.
#endif

struct PageTransport {
	struct Request {
		PlaylistPageSource source = PlaylistPageSource::Unsupported;
		std::string id;
		int32_t offset = 0;
		int32_t limit = 50;
		JsonCallback done;
	};
	std::vector<Request> requests;

	auto Content(PlaylistPageSource source)
	{
		return [this, source](const std::string& id, int32_t offset,
			int32_t limit, JsonCallback done) {
			requests.push_back({source, id, offset, limit, std::move(done)});
		};
	}

	PlaylistPageController Controller()
	{
		return PlaylistPageController(
			[this](int32_t offset, int32_t limit, JsonCallback done) {
				requests.push_back({PlaylistPageSource::LikedSongs, "", offset,
					limit, std::move(done)});
			}, Content(PlaylistPageSource::Playlist), Content(PlaylistPageSource::Album),
			Content(PlaylistPageSource::Podcast));
	}

	void Reply(size_t index, bool ok, const nlohmann::json& data)
	{
		auto done = std::move(requests.at(index).done);
		assert(done);
		done(ok, data);
	}
};

static PlaylistPageRequest
Request(const std::string& uri, int32_t offset = 0, int32_t limit = 50)
{
	return MakePlaylistPageRequest(ResolvePlaylistContentTarget(uri), offset, limit);
}

static PlaylistPageResult
Decode(const PlaylistPageRequest& request, const nlohmann::json& data, bool ok = true)
{
	PageTransport transport;
	PlaylistPageResult result;
	int calls = 0;
	// The controller is already destroyed when the delayed response arrives.
	bool started = transport.Controller().Load(request, [&](const PlaylistPageResult& page) {
		result = page;
		calls++;
	});
	assert(started && calls == 0 && transport.requests.size() == 1);
	transport.Reply(0, ok, data);
	assert(calls == 1 && transport.requests.size() == 1);
	return result;
}

static void
TestContentRouting()
{
	const std::vector<std::pair<std::string, PlaylistPageSource>> sources = {
		{"spotify:collection", PlaylistPageSource::LikedSongs},
		{"spotify:playlist:id", PlaylistPageSource::Playlist},
		{"spotify:album:id", PlaylistPageSource::Album},
		{"spotify:show:id", PlaylistPageSource::Podcast}
	};
	for (const auto& source : sources) {
		PageTransport transport;
		bool started = transport.Controller().Load(Request(source.first, 50, 25), nullptr);
		assert(started && transport.requests.size() == 1);
		const auto& sent = transport.requests[0];
		assert(sent.source == source.second && sent.offset == 50 && sent.limit == 25);
		assert(sent.id == (source.second == PlaylistPageSource::LikedSongs ? "" : "id"));
	}
}

static void
TestUnsupportedAndInvalidRequests()
{
	std::vector<PlaylistPageRequest> requests = {Request(""),
		Request("spotify:artist:id"), Request("spotify:audiobook:id"),
		Request("spotify:track:id"), Request("spotify:show:id", -1),
		Request("spotify:show:id", 0, 0), Request("spotify:show:id", 0, -1)};
	auto head = Request("spotify:album:id");
	head.headRefresh = true;
	requests.push_back(head);
	for (const auto& request : requests) {
		PageTransport transport;
		int calls = 0;
		bool started = transport.Controller().Load(request, [&](const PlaylistPageResult&) { calls++; });
		assert(!started && calls == 0 && transport.requests.empty());
	}
}

static void
TestCollectionRetainsSourcePositions()
{
	nlohmann::json track = {{"name", "Song"}, {"uri", "spotify:track:a"},
		{"duration_ms", 125000}, {"artists", {{{"name", "Artist"}, {"uri", "spotify:artist:a"}}}},
		{"album", {{"name", "Album"}, {"uri", "spotify:album:a"}}}};
	auto result = Decode(Request("spotify:collection", 50),
		{{"items", {nullptr, {{"track", nullptr}}, {{"track", track}},
			{{"track", nlohmann::json::array()}}, {{"track", track}}}}, {"total", 100}});
	assert(result.ok && result.responseValid && result.total == 100);
	assert(result.pageCount == 5 && result.nextOffset == 55 && result.rows.size() == 2);
	assert(result.rows[0].number == 53 && result.rows[1].number == 55);
	const auto& row = result.rows[0];
	assert(row.title == "Song" && row.uri == "spotify:track:a" && row.duration == "2:05");
	assert(row.artist == "Artist" && row.artistUri == "spotify:artist:a");
	assert(row.album == "Album" && row.albumUri == "spotify:album:a");
	assert(result.rows[1].uri == row.uri);
}

static void
TestPlaylistEntryCompatibility()
{
	auto result = Decode(Request("spotify:playlist:id"), {{"items", {
		{{"item", {{"name", "New"}, {"uri", "spotify:track:new"}}},
			{"track", {{"name", "Old"}, {"uri", "spotify:track:old"}}}},
		{{"item", nullptr}, {"track", {{"name", "Legacy"}, {"uri", "spotify:track:legacy"}}}},
		{{"item", {{"name", "Episode"}, {"uri", "spotify:episode:e"},
			{"show", {{"name", "Show"}, {"uri", "spotify:show:s"}}}}}}, nullptr}}});
	assert(result.ok && result.rows.size() == 3 && result.pageCount == 4);
	assert(result.rows[0].uri == "spotify:track:new" && result.rows[1].uri == "spotify:track:legacy");
	assert(result.rows[2].uri == "spotify:episode:e" && result.rows[2].number == 3);
	assert(result.rows[2].artist == "Show" && result.rows[2].album == "Show");
	assert(result.rows[2].artistUri == "spotify:show:s" && result.rows[2].albumUri == "spotify:show:s");
}

static void
TestAlbumContextAndMalformedFields()
{
	auto request = Request("spotify:album:parent");
	request.albumName = "Parent";
	auto result = Decode(request, {{"items", {
		{{"name", "A"}, {"uri", "spotify:track:a"}},
		{{"album", {{"name", "Own"}, {"uri", "spotify:album:own"}}}},
		{{"name", nullptr}, {"uri", 7}, {"duration_ms", "broken"},
			{"artists", {7}}, {"album", {{"name", nullptr}}}}, 9}}});
	assert(result.ok && result.rows.size() == 3 && result.nextOffset == 4);
	assert(result.rows[0].album == "Parent" && result.rows[0].albumUri == "spotify:album:parent");
	assert(result.rows[1].album == "Own" && result.rows[1].albumUri == "spotify:album:own");
	const auto& row = result.rows[2];
	assert(row.title == "Unknown" && row.uri.empty() && row.duration == "0:00");
	assert(row.artist == "Unknown" && row.artistUri.empty());
	assert(row.album == "Parent" && row.albumUri == "spotify:album:parent");
}

static void
TestPodcastPlaceholdersAndDescription()
{
	auto request = Request("spotify:show:id", 50);
	request.headRefresh = true;
	auto result = Decode(request, {{"items", {nullptr, 7,
		{{"name", "Episode"}, {"uri", "spotify:episode:a"}, {"duration_ms", 61000},
			{"html_description", "<b>HTML</b>"}, {"description", "Plain"}, {"release_date", "2026-01-02"}},
		{{"html_description", nullptr}, {"description", "Fallback"}}}}});
	assert(result.ok && result.request.headRefresh && result.total == 0);
	assert(result.rows.size() == 4 && result.nextOffset == 54);
	assert(result.rows[0].unavailable && result.rows[0].uri.empty());
	assert(result.rows[1].unavailable && result.rows[1].number == 52);
	assert(result.rows[2].title == "Episode" && result.rows[2].duration == "1:01");
	assert(result.rows[2].description == "<b>HTML</b>" && result.rows[2].date == "2026-01-02");
	assert(result.rows[3].description == "Fallback" && !result.rows[3].unavailable);
}

static void
TestEmptyAndMalformedPages()
{
	for (const auto& uri : {"spotify:collection", "spotify:playlist:id", "spotify:album:id", "spotify:show:id"}) {
		auto result = Decode(Request(uri, 50), {{"items", nlohmann::json::array()}});
		assert(result.ok && result.responseValid && result.rows.empty());
		assert(result.nextOffset == 50 && result.pageCount == 0);
		assert(result.total == (result.request.source == PlaylistPageSource::Podcast ? 0 : -1));
		for (const auto& malformed : std::vector<nlohmann::json>{nullptr,
			nlohmann::json::array(), nlohmann::json::object(), {{"items", nullptr}},
			{{"items", 7}}, {{"items", nlohmann::json::object()}}}) {
			result = Decode(Request(uri), malformed);
			assert(!result.ok && !result.responseValid && result.rows.empty());
			assert(PlaylistPageRetryDelay(result.status, result.retryAfter, 0, result.responseValid) == 0);
		}
	}
}

static void
TestFailuresPreserveClassification()
{
	for (int32_t status : {-1, 0, 401, 403, 404, 408, 425, 429, 500}) {
		auto request = Request("spotify:show:id");
		request.searchGeneration = 7;
		auto result = Decode(request, {{"status", status}, {"retry_after", 12},
			{"items", {{{"name", "Untrusted"}}}}}, false);
		assert(!result.ok && result.responseValid && result.rows.empty());
		assert(result.status == status && result.retryAfter == 12);
		assert(result.request.searchGeneration == 7);
	}
}

static void
TestCopiedContextAndOutOfOrderReplies()
{
	PageTransport transport;
	std::vector<PlaylistPageResult> results;
	auto request = Request("spotify:album:first", 0);
	request.albumName = "First";
	request.searchGeneration = 1;
	request.token = {10, 20};
	auto capture = [&](const PlaylistPageResult& result) { results.push_back(result); };
	bool started = transport.Controller().Load(request, capture);
	assert(started);
	request = Request("spotify:album:second", 50);
	request.albumName = "Second";
	request.searchGeneration = 2;
	request.token = {11, 21};
	started = transport.Controller().Load(request, capture);
	assert(started && results.empty());
	transport.Reply(1, true, {{"items", {{{"name", "B"}}}}});
	transport.Reply(0, true, {{"items", {{{"name", "A"}}}}});
	assert(results.size() == 2);
	assert(results[0].request.offset == 50 && results[0].request.searchGeneration == 2);
	assert(results[0].request.token.generation == 11 && results[0].request.token.requestId == 21);
	assert(results[0].rows[0].album == "Second" && results[0].rows[0].number == 51);
	assert(results[1].request.offset == 0 && results[1].request.searchGeneration == 1);
	assert(results[1].request.token.generation == 10 && results[1].request.token.requestId == 20);
	assert(results[1].rows[0].album == "First" && results[1].rows[0].number == 1);
}

static void
TestOffsetOverflowIsRejected()
{
	auto request = Request("spotify:album:id", std::numeric_limits<int32_t>::max());
	auto result = Decode(request, {{"items", {{{"name", "Overflow"}}}}});
	assert(!result.ok && !result.responseValid && result.rows.empty());
	result = Decode(request, {{"items", nlohmann::json::array()}});
	assert(result.ok && result.nextOffset == request.offset);
}

static void
TestSignedAndUnsignedIntegerBoundaries()
{
	const int32_t minimum = std::numeric_limits<int32_t>::min();
	const int32_t maximum = std::numeric_limits<int32_t>::max();
	for (int32_t number : {minimum, -1, 0, 1, maximum}) {
		auto result = Decode(Request("spotify:show:id"),
			{{"items", nlohmann::json::array()}, {"total", int64_t(number)}});
		assert(result.ok && result.total == number);
		if (number >= 0) {
			result = Decode(Request("spotify:show:id"),
				{{"items", nlohmann::json::array()}, {"total", uint64_t(number)}});
			assert(result.ok && result.total == number);
		}
	}
	for (const auto& number : std::vector<nlohmann::json>{
		std::numeric_limits<uint64_t>::max(), std::numeric_limits<int64_t>::min(),
		std::numeric_limits<int64_t>::max(), int64_t(minimum) - 1,
		int64_t(maximum) + 1, uint64_t(maximum) + 1}) {
		auto result = Decode(Request("spotify:show:id"),
			{{"items", nlohmann::json::array()}, {"total", number}});
		assert(result.ok && result.total == 0);
	}
	auto result = Decode(Request("spotify:show:id"),
		{{"status", uint64_t(429)}, {"retry_after", uint64_t(12)}}, false);
	assert(!result.ok && result.status == 429 && result.retryAfter == 12);
}

static void
TestRetryPolicy()
{
	for (int32_t count = 0; count <= 3; count++) {
		for (int32_t status : {-1, 408, 425, 429, 500}) {
			int64_t expected = count < 3 ? (status == 429 ? 12000000 : 2000000) : 0;
			assert(PlaylistPageRetryDelay(status, 12, count, true) == expected);
			assert(PlaylistPageRetryDelay(status, 12, count, false) == 0);
		}
	}
	for (int32_t status : {0, 200, 400, 401, 403, 404})
		assert(PlaylistPageRetryDelay(status, 12, 0, true) == 0);
	assert(PlaylistPageRetryDelay(429, -1, 0, true) == 2000000);
	assert(PlaylistPageRetryDelay(429, 0, 0, true) == 2000000);
	assert(PlaylistPageRetryDelay(429, 2147483647, 0, true) == 2147483647000000LL);
}

int
main()
{
	TestContentRouting();
	TestUnsupportedAndInvalidRequests();
	TestCollectionRetainsSourcePositions();
	TestPlaylistEntryCompatibility();
	TestAlbumContextAndMalformedFields();
	TestPodcastPlaceholdersAndDescription();
	TestEmptyAndMalformedPages();
	TestFailuresPreserveClassification();
	TestCopiedContextAndOutOfOrderReplies();
	TestOffsetOverflowIsRejected();
	TestSignedAndUnsignedIntegerBoundaries();
	TestRetryPolicy();
	std::puts("Playlist page controller tests passed.");
}
