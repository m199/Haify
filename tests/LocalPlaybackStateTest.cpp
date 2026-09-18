#include "playback/LibrespotEventState.h"
#include "playback/LocalPlaybackReadiness.h"
#include "playback/LocalPlaybackPresentation.h"
#include "playback/PlaybackStartController.h"
#include "spotify/api/PlaybackApi.h"
#include "JsonApiTestSupport.h"

#include <cstdio>

static void
TestOldFilesAfterRestart()
{
	LibrespotEventState state;
	assert(!state.Accept("100", "old-track", true));
	assert(!state.Accept("100", "old-paused", false));
	assert(state.SetSession(200));
	assert(!state.Accept("100", "old-track", true));
	assert(!state.Accept("", "legacy-track", true));
	assert(!state.Accept("200", "", true));
	assert(state.Accept("200", "new-track", true));
	assert(state.RememberTrack("spotify:track:nine", "opaque-nine"));
	assert(!state.Accept("200", "new-track", true));
	assert(state.Accept("200", "new-playing", false));
	assert(state.PositionAction(true, "opaque-nine", "", "spotify:track:nine")
		== LibrespotPositionAction::ApplyPending);
	assert(!state.Accept("200", "new-playing", false));
}

static void
TestPlayingDoesNotPromoteAnotherTrack()
{
	LibrespotEventState state;
	state.SetSession(200);
	assert(state.RememberTrack("spotify:track:three", "opaque-three"));
	assert(state.PositionAction(true, "opaque-nine", "", "spotify:track:three")
		== LibrespotPositionAction::Refresh);
	assert(state.PositionAction(false, "opaque-nine", "spotify:track:three", "")
		== LibrespotPositionAction::Refresh);
	assert(state.PositionAction(true, "", "spotify:track:three", "spotify:track:three")
		== LibrespotPositionAction::Refresh);
	assert(state.RememberTrack("spotify:track:nine", "opaque-nine"));
	assert(state.PositionAction(true, "opaque-nine", "spotify:track:three", "spotify:track:nine")
		== LibrespotPositionAction::ApplyPending);
	assert(state.PositionAction(false, "opaque-three", "spotify:track:nine", "")
		== LibrespotPositionAction::Refresh);
	assert(state.PositionAction(false, "opaque-nine", "spotify:track:nine", "")
		== LibrespotPositionAction::ApplyCurrent);
}

static void
TestPollAndChapterIdentity()
{
	LibrespotEventState state;
	state.SetSession(200);
	assert(state.RememberTrack("spotify:episode:chapter", "opaque-chapter"));
	// A poll may confirm the item before its playing event is consumed.
	assert(state.PositionAction(true, "opaque-chapter", "spotify:episode:chapter", "")
		== LibrespotPositionAction::ApplyCurrent);
	// After a different poll/command cleared pending metadata, never revive it.
	assert(state.PositionAction(true, "opaque-chapter", "spotify:track:nine", "")
		== LibrespotPositionAction::Refresh);
	assert(!state.RememberTrack("", "missing-uri"));
	assert(!state.RememberTrack("spotify:track:nine", ""));
	assert(!state.RememberTrack("spotify:playlist:list", "list"));
}

static void
TestStopAndProcessReplacement()
{
	LibrespotEventState state;
	state.SetSession(200);
	assert(state.Accept("200", "event", true));
	assert(state.RememberTrack("spotify:track:three", "three"));
	assert(!state.SetSession(200));
	assert(!state.Accept("200", "event", true));
	assert(state.SetSession(300));
	assert(!state.Accept("200", "late-writer", false));
	assert(state.PositionAction(true, "three", "spotify:track:three", "spotify:track:three")
		== LibrespotPositionAction::Refresh);
	assert(state.Accept("300", "event", true));
	assert(state.SetSession(0));
	assert(!state.Accept("300", "after-stop", false));
}

static void
TestTransferGenerationBarrier()
{
	LocalPlaybackReadiness state;
	assert(!state.Ready() && !state.Complete(0));
	state.Begin(100);
	assert(state.Accepts(100) && !state.ReadyAfter(0));
	assert(state.Complete(100) && state.ReadyAfter(0));
	// The old successful transfer cannot release a newly requested local start
	// while its start message is still waiting on the App looper.
	assert(!state.ReadyAfter(100));
	state.Begin(200);
	assert(!state.Complete(100) && !state.ReadyAfter(100));
	assert(state.Complete(200) && state.ReadyAfter(100));
	state.Begin(0);
	assert(!state.Accepts(200) && !state.Complete(200) && !state.Ready());
}

static void
TestSelectedTrackFollowsSuccessfulTransfer()
{
	JsonApiTestTransport transport;
	PlaybackApi api(transport.GetHandler(), transport.BodyHandler("PUT"),
		transport.BodyHandler("POST"), transport.CacheHandler());
	LocalPlaybackReadiness state;
	state.Begin(200);
	PlaybackCommand selected{"spotify:track:nine", "spotify:playlist:list", "local", 0,
		{"spotify:track:ten"}};
	api.TransferPlayback("local", [&state](bool ok, const nlohmann::json&) {
		if (ok)
			state.Complete(200);
	});
	transport.ExpectJson(0, "PUT", "/me/player", {{"device_ids", {"local"}}, {"play", true}});
	// Discovering the device while transfer is pending must keep Track 9 queued.
	assert(!state.ReadyAfter(0) && transport.requests.size() == 1);
	transport.Reply(0, true, {{"status", 204}});
	assert(state.ReadyAfter(0));
	assert(DispatchPlaybackStart(api, selected, false, {}));
	transport.ExpectJson(1, "PUT", "/me/player/play?device_id=local",
		{{"uris", {"spotify:track:nine", "spotify:track:ten"}}});
	transport.Reply(1, true, {{"status", 204}});
	assert(transport.requests.size() == 2 && selected.uri == "spotify:track:nine");
}

static void
TestFailedTransferDoesNotReleaseSelection()
{
	for (int status : {0, 401, 403, 429, 503}) {
		JsonApiTestTransport transport;
		PlaybackApi api(transport.GetHandler(), transport.BodyHandler("PUT"),
			transport.BodyHandler("POST"), transport.CacheHandler());
		LocalPlaybackReadiness state;
		state.Begin(200);
		api.TransferPlayback("local", [&state](bool ok, const nlohmann::json&) {
			if (ok)
				state.Complete(200);
		});
		transport.Reply(0, false, {{"status", status}});
		assert(!state.ReadyAfter(0) && transport.requests.size() == 1);
	}
}

static void
TestTransferDoesNotFlashPreviousTrack()
{
	LocalPlaybackPresentation display;
	display.Begin("spotify:track:nine", 100);
	// Transfer restores Track 3, but the selected command has not been sent yet.
	assert(display.Defer("spotify:track:three", false, true, 200));
	assert(display.Defer("spotify:track:nine", false, true, 300));
	display.Dispatched("spotify:track:nine", 400);
	// Neither an old poll, empty state, paused target nor metadata preview is playback.
	assert(display.Defer("spotify:track:three", false, true, 500));
	assert(display.Defer("", false, false, 600));
	assert(display.Defer("spotify:track:nine", false, false, 700));
	assert(display.Defer("spotify:track:nine", true, true, 800));
	assert(!display.Defer("spotify:track:nine", false, true, 900));
	assert(!display.Active(900));
	// Once confirmed, later normal transitions remain visible.
	assert(!display.Defer("spotify:track:ten", false, true, 1000));
}

static void
TestLocalPresentationCancellationAndTimeout()
{
	LocalPlaybackPresentation display;
	display.Begin("spotify:track:nine", 100);
	display.Cancel();
	assert(!display.Defer("spotify:track:three", false, true, 200));
	display.Begin("spotify:track:nine", 100);
	assert(display.Defer("spotify:track:three", false, true, 15000099));
	assert(!display.Defer("spotify:track:three", false, true, 15000100));
	// A failed/unconfirmed Play must never hide the real state indefinitely.
	display.Begin("spotify:track:nine", 100);
	display.Dispatched("spotify:track:nine", 10000000);
	assert(display.Defer("spotify:track:three", false, true, 24999999));
	assert(!display.Defer("spotify:track:three", false, true, 25000000));
}

static void
TestLocalPresentationReplacementAndContexts()
{
	LocalPlaybackPresentation display;
	display.Begin("spotify:track:nine", 100);
	display.Dispatched("spotify:episode:chapter", 200);
	assert(display.Defer("spotify:track:nine", false, true, 300));
	assert(!display.Defer("spotify:episode:chapter", false, true, 400));
	for (const char* context : {"", "spotify:track:", "spotify:playlist:list"}) {
		display.Begin(context, 100);
		assert(!display.Active(200));
	}
	// Normal starts without the local-device flow do not acquire this hold.
	display.Dispatched("spotify:track:nine", 200);
	assert(!display.Active(300));
}

int
main()
{
	TestOldFilesAfterRestart();
	TestPlayingDoesNotPromoteAnotherTrack();
	TestPollAndChapterIdentity();
	TestStopAndProcessReplacement();
	TestTransferGenerationBarrier();
	TestSelectedTrackFollowsSuccessfulTransfer();
	TestFailedTransferDoesNotReleaseSelection();
	TestTransferDoesNotFlashPreviousTrack();
	TestLocalPresentationCancellationAndTimeout();
	TestLocalPresentationReplacementAndContexts();
	std::puts("Local playback state tests passed.");
	return 0;
}
