#include "JsonApiTestSupport.h"
#include "spotify/api/PlaybackApi.h"
#include "policy/UiLogic.h"

#include <cstdio>

struct PlaybackFixture {
	JsonApiTestTransport transport;
	PlaybackApi api{transport.GetHandler(), transport.BodyHandler("PUT"),
		transport.BodyHandler("POST"), transport.CacheHandler()};
};

static void
TestTrackContextAndPosition()
{
	const std::vector<std::string> contexts = {
		"spotify:album:album", "spotify:playlist:list", "spotify:artist:artist",
		"spotify:show:show", "spotify:audiobook:book", ""
	};
	for (size_t index = 0; index < contexts.size(); index++) {
		for (bool episode : {false, true}) {
			PlaybackFixture fixture;
			std::string uri = episode ? "spotify:episode:one" : "spotify:track:one";
			JsonApiTestResult result;
			fixture.api.PlayTrack(uri, contexts[index], result.Callback(), 42000,
				"target & device");
			nlohmann::json body = {{"uris", {uri}}, {"position_ms", 42000}};
			if (!episode && index < 2) {
				body = {{"context_uri", contexts[index]}, {"offset", {{"uri", uri}}},
					{"position_ms", 42000}};
			}
			assert(fixture.transport.requests.size() == 1 && result.calls == 0);
			fixture.transport.ExpectJson(0, "PUT",
				"/me/player/play?device_id=target%20%26%20device", body);
			fixture.transport.Reply(0, true, {{"status", 204}});
			result.Expect(true, {{"status", 204}});
		}
	}
}

static void
TestImplicitDeviceAndDefaultPosition()
{
	for (int position : {0, -1}) {
		PlaybackFixture fixture;
		fixture.api.PlayTrack("spotify:track:one", "", nullptr, position);
		fixture.transport.ExpectJson(0, "PUT", "/me/player/play",
			{{"uris", {"spotify:track:one"}}});
		assert(fixture.transport.requests.size() == 1);
	}
}

static void
TestQueueOrderAndDuplicates()
{
	PlaybackFixture fixture;
	fixture.api.PlayUris({"spotify:track:first", "", "spotify:track:second",
		"spotify:track:first"}, nullptr, "baron");
	fixture.transport.ExpectJson(0, "PUT", "/me/player/play?device_id=baron",
		{{"uris", {"spotify:track:first", "spotify:track:second",
			"spotify:track:first"}}});
	fixture.api.AddToQueue("spotify:episode:one", nullptr);
	fixture.transport.Expect(1, "POST",
		"/me/player/queue?uri=spotify%3Aepisode%3Aone");
	assert(fixture.transport.requests.size() == 2);
}

static void
TestPlaybackControls()
{
	PlaybackFixture fixture;
	fixture.api.Play(nullptr, "baron");
	fixture.api.PlayContext("spotify:album:one", nullptr, "baron");
	fixture.api.TransferPlayback("baron", nullptr);
	fixture.api.SetShuffle(false, nullptr, "a&b");
	fixture.api.SetShuffle(true, nullptr);
	fixture.api.Seek(12345, nullptr, "a&b");
	fixture.api.Pause(nullptr);
	fixture.api.Next(nullptr);
	fixture.api.Previous(nullptr);
	fixture.api.SetRepeat("context", nullptr);
	fixture.api.SetVolume(37, nullptr, "baron");
	fixture.transport.Expect(0, "PUT", "/me/player/play?device_id=baron");
	fixture.transport.ExpectJson(1, "PUT", "/me/player/play?device_id=baron",
		{{"context_uri", "spotify:album:one"}});
	fixture.transport.ExpectJson(2, "PUT", "/me/player",
		{{"device_ids", {"baron"}}, {"play", true}});
	fixture.transport.Expect(3, "PUT", "/me/player/shuffle?state=false&device_id=a%26b");
	fixture.transport.Expect(4, "PUT", "/me/player/shuffle?state=true");
	fixture.transport.Expect(5, "PUT", "/me/player/seek?position_ms=12345&device_id=a%26b");
	fixture.transport.Expect(6, "PUT", "/me/player/pause");
	fixture.transport.Expect(7, "POST", "/me/player/next");
	fixture.transport.Expect(8, "POST", "/me/player/previous");
	fixture.transport.Expect(9, "PUT", "/me/player/repeat?state=context");
	fixture.transport.Expect(10, "PUT", "/me/player/volume?volume_percent=37&device_id=baron");
	assert(fixture.transport.requests.size() == 11);
}

static void
TestLiveReadsInvalidateBeforeDispatch()
{
	using Read = void (PlaybackApi::*)(JsonCallback);
	const std::vector<std::pair<Read, std::string>> reads = {
		{&PlaybackApi::GetPlaybackState, "/me/player?additional_types=episode"},
		{&PlaybackApi::GetCurrentlyPlaying, "/me/player/currently-playing?additional_types=episode"},
		{&PlaybackApi::GetDevices, "/me/player/devices"},
		{&PlaybackApi::GetQueue, "/me/player/queue"}
	};
	for (const auto& read : reads) {
		PlaybackFixture fixture;
		for (size_t index = 0; index < 2; index++) {
			JsonApiTestResult result;
			(fixture.api.*read.first)(result.Callback());
			assert(result.calls == 0);
			fixture.transport.Expect(index, "GET", read.second);
			assert(fixture.transport.events.at(index * 2) == "INVALIDATE " + read.second);
			assert(fixture.transport.events.at(index * 2 + 1) == "GET " + read.second);
			fixture.transport.Reply(index, true, nlohmann::json::object());
			result.Expect(true, nlohmann::json::object());
		}
		assert(fixture.transport.requests.size() == 2);
	}
}

static void
TestFailuresAreNotRetriedOrReplaced()
{
	for (int status : {-1, 401, 403, 404, 429, 500}) {
		PlaybackFixture fixture;
		JsonApiTestResult result;
		nlohmann::json failure = {{"status", status}, {"body", "failure"},
			{"retry_after", 12}};
		fixture.api.PlayUris({"spotify:track:one"}, result.Callback(), "baron");
		fixture.transport.Reply(0, false, failure);
		result.Expect(false, failure);
		assert(fixture.transport.requests.size() == 1);
	}
}

static void
TestAcceptedPlayDoesNotInventPlaybackState()
{
	PlaybackFixture fixture;
	JsonApiTestResult command;
	fixture.api.PlayTrack("spotify:track:one", "", command.Callback(), 0, "baron");
	fixture.transport.Reply(0, true, {{"status", 204}});
	command.Expect(true, {{"status", 204}});
	JsonApiTestResult state;
	fixture.api.GetPlaybackState(state.Callback());
	fixture.transport.Reply(1, true, nlohmann::json::object());
	state.Expect(true, nlohmann::json::object());
	assert(fixture.transport.requests.size() == 2);
}

static void
TestColdPlaybackStartPresentation()
{
	PlaybackFixture fixture;
	// Opening the prompt, starting librespot and discovering its ID do not
	// establish a playing item. Neither the title preview nor its metadata
	// request should publish a synthetic playing state at this point.
	assert(!ShouldPreviewPlaybackStart(false, ""));
	JsonApiTestResult command;
	fixture.api.PlayTrack("spotify:track:first", "", command.Callback(), 0, "local");
	fixture.transport.Reply(0, true, {{"status", 204}});
	command.Expect(true, {{"status", 204}});
	assert(!ShouldPreviewPlaybackStart(false, ""));
	JsonApiTestResult poll;
	fixture.api.GetPlaybackState(poll.Callback());
	fixture.transport.Reply(1, true, nlohmann::json::object());
	poll.Expect(true, nlohmann::json::object());
	assert(!ShouldPreviewPlaybackStart(false, ""));
	// A selected/loaded but paused item is also not a running playback.
	assert(!ShouldPreviewPlaybackStart(false, "spotify:track:first"));
	assert(!ShouldPreviewPlaybackStart(true, ""));
	// Once playback reports a running item, subsequent track/episode switches
	// keep their existing fast presentation.
	assert(ShouldPreviewPlaybackStart(true, "spotify:track:first"));
	assert(ShouldPreviewPlaybackStart(true, "spotify:episode:first"));
	assert(!ShouldPreviewPlaybackStart(true, "spotify:playlist:context"));
}

int
main()
{
	TestTrackContextAndPosition();
	TestImplicitDeviceAndDefaultPosition();
	TestQueueOrderAndDuplicates();
	TestPlaybackControls();
	TestLiveReadsInvalidateBeforeDispatch();
	TestFailuresAreNotRetriedOrReplaced();
	TestAcceptedPlayDoesNotInventPlaybackState();
	TestColdPlaybackStartPresentation();
	std::puts("Playback API tests passed.");
}
