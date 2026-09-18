#include "playback/PlaybackStartController.h"
#include "spotify/api/PlaybackApi.h"
#include "JsonApiTestSupport.h"

#include <algorithm>
#include <cstdio>
#include <limits>

using nlohmann::json;

struct StartFixture {
	JsonApiTestTransport transport;
	PlaybackApi api{transport.GetHandler(), transport.BodyHandler("PUT"),
		transport.BodyHandler("POST"), transport.CacheHandler()};
	int calls = 0;
	PlaybackStartResult result;

	bool Start(const PlaybackCommand& command, bool shuffle = false)
	{
		return DispatchPlaybackStart(api, command, shuffle,
			[this](const PlaybackStartResult& value) { calls++; result = value; });
	}

	void Finish(size_t index, bool ok = true, int status = 204)
	{
		transport.Reply(index, ok, {{"status", status}});
	}
};

static void
TestItemAndContextStarts()
{
	for (const char* context : {"spotify:album:album", "spotify:playlist:list"}) {
		StartFixture f;
		PlaybackCommand command{"spotify:track:one", context, "remote /+", 1234};
		assert(f.Start(command));
		f.transport.ExpectJson(0, "PUT", "/me/player/play?device_id=remote%20%2F%2B",
			{{"context_uri", context}, {"offset", {{"uri", command.uri}}},
				{"position_ms", 1234}});
		assert(f.calls == 0 && f.transport.requests.size() == 1);
		f.Finish(0);
		assert(f.calls == 1 && f.result.play.accepted && f.result.play.status == 204);
		assert(!f.result.disableShuffle.attempted && !f.result.restoreShuffle.attempted);
	}
	for (const char* uri : {"spotify:playlist:list", "spotify:album:album",
		"spotify:artist:artist", "spotify:collection", "spotify:saved-episodes"}) {
		StartFixture f;
		assert(f.Start({uri}, true));
		f.transport.ExpectJson(0, "PUT", "/me/player/play", {{"context_uri", uri}});
		f.Finish(0);
		assert(f.calls == 1 && f.result.play.accepted);
	}
}

static void
TestEpisodeAndAlbumWithShuffle()
{
	for (const char* uri : {"spotify:track:one", "spotify:episode:one"}) {
		StartFixture f;
		PlaybackCommand command{uri, "spotify:album:album", "device", 2500};
		assert(f.Start(command, true));
		json body = {{"uris", {uri}}, {"position_ms", 2500}};
		if (command.uri == "spotify:track:one") {
			body = {{"context_uri", command.contextUri}, {"offset", {{"uri", uri}}},
				{"position_ms", 2500}};
		}
		f.transport.ExpectJson(0, "PUT", "/me/player/play?device_id=device", body);
		f.Finish(0);
		assert(f.transport.requests.size() == 1 && f.calls == 1);
	}
}

static void
TestBatchBoundariesAndDuplicates()
{
	for (size_t count : {size_t(1), size_t(98), size_t(99), size_t(100), size_t(120)}) {
		StartFixture f;
		PlaybackCommand command{"spotify:track:first", "spotify:playlist:list", "d", 700};
		for (size_t i = 0; i < count; i++)
			command.nextQueueUris.push_back("spotify:track:" + std::to_string(i));
		command.nextQueueUris[0] = command.uri;
		std::vector<std::string> expected{command.uri};
		for (size_t i = 0; i < std::min(count, size_t(99)); i++)
			expected.push_back(command.nextQueueUris[i]);
		assert(f.Start(command));
		f.transport.ExpectJson(0, "PUT", "/me/player/play?device_id=d", {{"uris", expected}});
		f.Finish(0);
		assert(f.calls == 1 && f.result.play.accepted);
		assert(command.nextQueueUris.size() == count);
	}
}

static void
TestAudiobookProvenance()
{
	for (bool parentField : {false, true}) {
		StartFixture f;
		PlaybackCommand command{"spotify:episode:chapter", "spotify:audiobook:book", "d", 890,
			{"spotify:episode:next", "spotify:episode:last"}};
		if (parentField)
			command.parentKind = "audiobook";
		else
			command.primaryOpenUri = "spotify:audiobook:book";
		assert(f.Start(command, true));
		f.transport.ExpectJson(0, "PUT", "/me/player/play?device_id=d",
			{{"uris", {command.uri}}, {"position_ms", 890}});
		f.Finish(0);
		assert(f.calls == 1 && f.transport.requests.size() == 1);
		assert(command.nextQueueUris.size() == 2);
	}
	// A context URI alone does not turn the remaining queue into chapters.
	StartFixture f;
	assert(f.Start({"spotify:episode:first", "spotify:audiobook:book", "", 890,
		{"spotify:episode:next"}}, true));
	f.transport.ExpectJson(0, "PUT", "/me/player/play",
		{{"uris", {"spotify:episode:first", "spotify:episode:next"}}});
	f.Finish(0);
}

static void
TestAudiobookSuppressesShuffleCycle()
{
	for (bool parentField : {false, true}) {
		StartFixture f;
		PlaybackCommand command{"spotify:track:chapter", "spotify:playlist:list", "", 123,
			{"spotify:track:next"}};
		command.parentKind = parentField ? "audiobook" : "";
		command.primaryOpenUri = parentField ? "" : "spotify:audiobook:book";
		assert(f.Start(command, true));
		f.transport.ExpectJson(0, "PUT", "/me/player/play",
			{{"context_uri", command.contextUri}, {"offset", {{"uri", command.uri}}},
				{"position_ms", 123}});
		f.Finish(0);
		assert(!f.result.disableShuffle.attempted && f.transport.requests.size() == 1);
	}
}

static void
TestShuffleSequenceOwnsCommand()
{
	StartFixture f;
	PlaybackCommand command{"spotify:track:one", "spotify:playlist:list", "d /", 456};
	assert(f.Start(command, true));
	command = {"spotify:track:changed", "", "other"};
	f.transport.Expect(0, "PUT", "/me/player/shuffle?state=false&device_id=d%20%2F");
	assert(f.transport.requests.size() == 1 && f.calls == 0);
	f.Finish(0);
	f.transport.ExpectJson(1, "PUT", "/me/player/play?device_id=d%20%2F",
		{{"context_uri", "spotify:playlist:list"}, {"offset", {{"uri", "spotify:track:one"}}},
			{"position_ms", 456}});
	assert(f.transport.requests.size() == 2 && f.calls == 0);
	f.Finish(1);
	f.transport.Expect(2, "PUT", "/me/player/shuffle?state=true&device_id=d%20%2F");
	assert(f.calls == 0);
	f.Finish(2);
	assert(f.calls == 1 && f.result.uri == "spotify:track:one" && f.result.deviceId == "d /");
	assert(f.result.disableShuffle.accepted && f.result.play.accepted);
	assert(f.result.restoreShuffle.accepted && !f.result.usedSingleItemFallback);
}

static void
TestShuffleRestoreAfterPlayFailure()
{
	for (int status : {0, 401, 403, 404, 429, 503}) {
		StartFixture f;
		assert(f.Start({"spotify:track:one", "spotify:playlist:list", "d", 0,
			{"spotify:episode:two"}}, true));
		f.Finish(0);
		f.transport.ExpectJson(1, "PUT", "/me/player/play?device_id=d",
			{{"uris", {"spotify:track:one", "spotify:episode:two"}}});
		f.transport.Reply(1, false, {{"status", status}, {"retry_after", 17}});
		f.transport.Expect(2, "PUT", "/me/player/shuffle?state=true&device_id=d");
		f.Finish(2);
		assert(f.calls == 1 && !f.result.play.accepted && f.result.play.status == status);
		assert(f.result.play.retryAfter == 17 && f.result.restoreShuffle.accepted);
		assert(!f.result.usedSingleItemFallback && f.transport.requests.size() == 3);
	}
}

static void
TestLegacyShuffleFailureFallback()
{
	for (int status : {0, 401, 403, 404, 429, 503}) {
		for (bool played : {false, true}) {
			StartFixture f;
			assert(f.Start({"spotify:track:one", "spotify:playlist:list", "d", 456,
				{"spotify:track:two"}}, true));
			f.transport.Reply(0, false, {{"status", status}, {"retry_after", 23}});
			f.transport.ExpectJson(1, "PUT", "/me/player/play?device_id=d",
				{{"uris", {"spotify:track:one"}}});
			assert(f.calls == 0);
			f.Finish(1, played, played ? 204 : 403);
			assert(f.calls == 1 && f.result.usedSingleItemFallback);
			assert(!f.result.disableShuffle.accepted && f.result.disableShuffle.status == status);
			assert(f.result.disableShuffle.retryAfter == 23 && f.result.play.accepted == played);
			assert(!f.result.restoreShuffle.attempted && f.transport.requests.size() == 2);
		}
	}
}

static void
TestRestoreFailureIsSeparate()
{
	StartFixture f;
	assert(f.Start({"spotify:track:one", "spotify:playlist:list"}, true));
	f.Finish(0);
	f.Finish(1);
	f.transport.Expect(2, "PUT", "/me/player/shuffle?state=true");
	f.Finish(2, false, 503);
	assert(f.calls == 1 && f.result.play.accepted);
	assert(f.result.restoreShuffle.attempted && !f.result.restoreShuffle.accepted);
	assert(f.result.restoreShuffle.status == 503 && f.transport.requests.size() == 3);
}

static void
TestInvalidCommandsHaveNoSideEffects()
{
	StartFixture f;
	for (const char* uri : {"", "spotify:track:", "spotify:unknown:id", "not-a-uri"})
		assert(!f.Start({uri}, true));
	assert(!f.Start({"spotify:track:one", "", "", -1}));
	for (const char* uri : {"", "spotify:album:id", "spotify:episode:"}) {
		PlaybackCommand command{"spotify:track:one"};
		command.nextQueueUris.assign(100, "spotify:track:valid");
		command.nextQueueUris.push_back(uri);
		assert(!f.Start(command));
	}
	assert(f.transport.requests.empty() && f.transport.invalidations.empty() && f.calls == 0);
}

static void
TestMalformedResponseFields()
{
	const std::vector<json> invalid{nullptr, true, "204", 204.0, -10,
		std::numeric_limits<uint64_t>::max()};
	for (const json& value : invalid) {
		StartFixture f;
		assert(f.Start({"spotify:track:one"}));
		f.transport.Reply(0, false, {{"status", value}, {"retry_after", value}});
		assert(f.calls == 1 && !f.result.play.accepted);
		assert(f.result.play.status == -1 && f.result.play.retryAfter == -1);
	}
	for (const json& data : {json(), json::array(), json::object()}) {
		StartFixture f;
		assert(f.Start({"spotify:track:one"}));
		f.transport.Reply(0, true, data);
		assert(f.calls == 1 && f.result.play.accepted && f.result.play.status == -1);
	}
}

static void
TestIndependentDelayedStarts()
{
	StartFixture f;
	std::vector<PlaybackStartResult> results;
	auto done = [&results](const PlaybackStartResult& result) { results.push_back(result); };
	assert(DispatchPlaybackStart(f.api, {"spotify:track:first", "spotify:playlist:list", "a"}, true, done));
	assert(DispatchPlaybackStart(f.api, {"spotify:episode:second", "", "b"}, false, done));
	f.Finish(1);
	f.Finish(0);
	f.transport.ExpectJson(2, "PUT", "/me/player/play?device_id=a",
		{{"context_uri", "spotify:playlist:list"}, {"offset", {{"uri", "spotify:track:first"}}}});
	f.Finish(2);
	f.Finish(3);
	assert(results.size() == 2 && results[0].uri == "spotify:episode:second");
	assert(results[0].deviceId == "b" && !results[0].restoreShuffle.attempted);
	assert(results[1].uri == "spotify:track:first" && results[1].deviceId == "a");
	assert(results[1].restoreShuffle.accepted);
}

static void
TestSynchronousCompletion()
{
	int requests = 0, completions = 0;
	PlaybackApi api({}, [&requests](const std::string&, const std::string&, JsonCallback done) {
		requests++;
		done(true, {{"status", 204}});
	}, {}, {});
	auto done = [&completions](const PlaybackStartResult& result) {
		completions++;
		assert(result.play.accepted && result.restoreShuffle.accepted);
	};
	assert(DispatchPlaybackStart(api, {"spotify:track:one", "spotify:playlist:list"}, true, done));
	assert(requests == 3 && completions == 1);
	assert(DispatchPlaybackStart(api, {"spotify:track:one"}, false, {}));
	assert(requests == 4 && completions == 1);
}

int
main()
{
	TestItemAndContextStarts();
	TestEpisodeAndAlbumWithShuffle();
	TestBatchBoundariesAndDuplicates();
	TestAudiobookProvenance();
	TestAudiobookSuppressesShuffleCycle();
	TestShuffleSequenceOwnsCommand();
	TestShuffleRestoreAfterPlayFailure();
	TestLegacyShuffleFailureFallback();
	TestRestoreFailureIsSeparate();
	TestInvalidCommandsHaveNoSideEffects();
	TestMalformedResponseFields();
	TestIndependentDelayedStarts();
	TestSynchronousCompletion();
	std::puts("Playback start controller tests passed.");
	return 0;
}
