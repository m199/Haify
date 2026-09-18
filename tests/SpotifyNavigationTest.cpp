#include "navigation/SpotifyNavigation.h"
#include "spotify/api/ContentApi.h"
#include "JsonApiTestSupport.h"

#include <iostream>
#include <limits>

using nlohmann::json;
using Action = SpotifyNavigationAction;
using Kind = SpotifyShowResolutionKind;

static SpotifyOpenRequest
Show(const std::string& id = "book")
{
	return {"spotify:show:" + id, "Original title", "https://cover/original", false};
}

struct NavigationFixture {
	JsonApiTestTransport transport;
	ContentApi api{transport.GetHandler(), transport.CacheHandler(), transport.CacheHandler()};
	std::vector<SpotifyShowResolution> results;

	void Start(const SpotifyOpenRequest& request)
	{
		bool dispatched = ResolveSpotifyShow(api, request,
			[this](const SpotifyShowResolution& result) { results.push_back(result); });
		assert(dispatched);
	}
};

static void
TestRouting()
{
	const struct { const char* uri = nullptr; Action action = Action::None; } cases[] = {
		{"", Action::None}, {"spotify:artist:a", Action::Artist},
		{"spotify:episode:e", Action::Episode}, {"spotify:track:t", Action::PlayTrack},
		{"spotify:album:a", Action::Collection}, {"spotify:playlist:p", Action::Collection},
		{"spotify:collection", Action::Collection}, {"spotify:show:", Action::Collection},
		{"spotify:chapter:c", Action::Unsupported}, {"https://open.spotify.com/track/t", Action::Unsupported},
		{"spotify:collection:tracks", Action::Unsupported}, {"spotify:unknown:x", Action::Unsupported}
	};
	for (const auto& item : cases) {
		for (bool books : {false, true}) {
			for (bool api : {false, true})
				assert(SelectSpotifyNavigation({item.uri}, books, api) == item.action);
		}
	}
	for (bool books : {false, true}) {
		for (bool api : {false, true}) {
			assert(SelectSpotifyNavigation({"spotify:audiobook:b"}, books, api)
				== (books ? Action::Audiobook : Action::AudiobooksUnavailable));
			for (bool skip : {false, true}) {
				auto request = Show();
				request.skipAudiobookResolution = skip;
				assert(SelectSpotifyNavigation(request, books, api)
					== (books && api && !skip ? Action::ResolveShow : Action::Collection));
			}
		}
	}
}

static void
TestResolutionMapping()
{
	const struct { json data; Kind kind = Kind::ApiAudiobook; const char* uri = nullptr; } cases[] = {
		{{{"uri", "spotify:audiobook:canonical"}}, Kind::ApiAudiobook, "spotify:audiobook:canonical"},
		{{{"uri", "spotify:audiobook:"}}, Kind::ApiAudiobook, "spotify:audiobook:"},
		{json::object(), Kind::SynthesizedAudiobookUri, "spotify:audiobook:book"},
		{{{"uri", "spotify:show:book"}}, Kind::SynthesizedAudiobookUri, "spotify:audiobook:book"},
		{{{"uri", 42}}, Kind::SynthesizedAudiobookUri, "spotify:audiobook:book"},
		{{{"uri", nullptr}}, Kind::SynthesizedAudiobookUri, "spotify:audiobook:book"},
		{json::array(), Kind::LegacyShowAfterMalformedResponse, "spotify:show:book"},
		{nullptr, Kind::LegacyShowAfterMalformedResponse, "spotify:show:book"}
	};
	for (const auto& item : cases) {
		NavigationFixture f;
		f.Start(Show());
		f.transport.Expect(0, "GET", "/audiobooks/book");
		f.transport.Reply(0, true, item.data);
		assert(f.results.size() == 1 && f.transport.requests.size() == 1);
		const auto& result = f.results.front();
		assert(ValidSpotifyShowResolution(result) && result.kind == item.kind);
		assert(result.resolvedUri == item.uri && result.status == -1 && result.retryAfter == -1);
		auto next = ResolvedSpotifyOpenRequest(result);
		assert(next.uri == item.uri && next.title == Show().title);
		assert(next.coverUrl == Show().coverUrl && next.skipAudiobookResolution);
		assert(SelectSpotifyNavigation(next, true, true) != Action::ResolveShow);
		// A capability change before delivery is evaluated on the receiving looper.
		assert(SelectSpotifyNavigation(next, false, true)
			== (item.kind == Kind::LegacyShowAfterMalformedResponse
				? Action::Collection : Action::AudiobooksUnavailable));
		assert(f.transport.invalidations.empty()); // Existing ContentApi cache policy.
	}
}

static void
TestFailuresAndCoverRegression()
{
	for (int status : {-1, 0, 400, 401, 403, 404, 429, 500}) {
		NavigationFixture f;
		f.Start(Show());
		f.transport.Reply(0, false, {{"status", status}, {"retry_after", 19},
			{"uri", "spotify:audiobook:misleading"}});
		const auto& result = f.results.front();
		assert(result.kind == Kind::LegacyShowAfterApiFailure);
		assert(result.status == status && result.retryAfter == 19);
		auto next = ResolvedSpotifyOpenRequest(result);
		assert(next.uri == Show().uri && next.title == Show().title);
		assert(next.coverUrl == Show().coverUrl); // Previously dropped by App's retry message.
		assert(SelectSpotifyNavigation(next, true, true) == Action::Collection);
		assert(f.transport.requests.size() == 1);
	}
	for (const json& value : {json("429"), json(1.5), json(nullptr), json(-2),
			json(std::numeric_limits<uint64_t>::max())}) {
		NavigationFixture f;
		f.Start(Show());
		f.transport.Reply(0, false, {{"status", value}, {"retry_after", value}});
		assert(f.results.front().status == -1 && f.results.front().retryAfter == -1);
	}
}

static void
TestOwnedConcurrentAndSynchronousResults()
{
	NavigationFixture f;
	auto request = Show("first");
	f.Start(request);
	request.title = "changed";
	request.coverUrl.clear();
	f.Start(Show("second"));
	f.transport.Reply(1, false, {{"status", 404}});
	f.transport.Reply(0, true, json::object());
	assert(f.results[0].request.uri == "spotify:show:second");
	assert(f.results[1].resolvedUri == "spotify:audiobook:first");
	assert(f.results[1].request.title == Show().title && f.results[1].request.coverUrl == Show().coverUrl);
	int calls = 0;
	ContentApi immediate([](const std::string& path, JsonCallback done) {
		assert(path == "/audiobooks/book");
		done(true, {{"uri", "spotify:audiobook:book"}});
	}, [](const std::string&) {}, [](const std::string&) {});
	bool dispatched = ResolveSpotifyShow(immediate, Show(), [&calls](const SpotifyShowResolution& result) {
		++calls;
		assert(result.kind == Kind::ApiAudiobook);
	});
	assert(dispatched && calls == 1);
}

static void
TestRejectedRequestsAndResults()
{
	NavigationFixture f;
	std::vector<SpotifyOpenRequest> invalid{{}, {"spotify:show:"}, {"spotify:track:t"},
		{"spotify:show:b", "", "", true}};
	for (const auto& request : invalid) {
		bool dispatched = ResolveSpotifyShow(f.api, request,
			[](const SpotifyShowResolution&) { assert(false); });
		assert(!dispatched);
	}
	assert(!ResolveSpotifyShow(f.api, Show(), {}));
	assert(f.transport.requests.empty());
	SpotifyShowResolution result;
	assert(!ValidSpotifyShowResolution(result));
	result.request = Show();
	result.resolvedUri = Show().uri;
	assert(ValidSpotifyShowResolution(result));
	result.resolvedUri = "spotify:show:other";
	assert(!ValidSpotifyShowResolution(result));
	assert(ResolvedSpotifyOpenRequest(result).uri.empty());
	result.kind = Kind::SynthesizedAudiobookUri;
	result.resolvedUri = "spotify:audiobook:other";
	assert(!ValidSpotifyShowResolution(result));
	result.resolvedUri = "spotify:audiobook:book";
	assert(ValidSpotifyShowResolution(result));
	result.kind = static_cast<Kind>(99);
	assert(!ValidSpotifyShowResolution(result));
}

int
main()
{
	TestRouting();
	TestResolutionMapping();
	TestFailuresAndCoverRegression();
	TestOwnedConcurrentAndSynchronousResults();
	TestRejectedRequestsAndResults();
	std::cout << "Spotify navigation tests passed.\n";
}
