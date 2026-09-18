#include "SpotifySessionTestSupport.h"
#include "spotify/SpotifyCapabilities.h"

#include <iostream>

using nlohmann::json;

struct CapabilityFixture {
	SpotifyApi api{"token"};
	SpotifyCapabilities capabilities{&api};
	SessionTestTransport transport;
	std::vector<AudiobookCapabilityState> results;

	CapabilityFixture() { transport.Attach(api); }
	void Probe(bool force = false)
	{
		capabilities.ProbeAudiobooks([this](AudiobookCapabilityState state) { results.push_back(state); }, force);
	}
};

static json AvailableBooks()
{
	return {{"items", json::array({{{"id", "book"}}})}};
}

static void TestAvailableCacheAndSingleFlight()
{
	CapabilityFixture f;
	f.Probe();
	f.Probe(true);
	assert(f.transport.requests.size() == 1 && f.results.empty());
	assert(f.transport.requests[0].path == "/me/audiobooks?limit=1&offset=0");
	f.transport.Reply(0, 200, AvailableBooks());
	assert(f.results.size() == 2 && f.capabilities.AudiobooksEnabled());
	assert(!f.capabilities.ProbeInFlight());
	f.Probe();
	assert(f.results.size() == 3 && f.transport.requests.size() == 1);
	f.Probe(true);
	assert(f.transport.requests.size() == 2);
	f.transport.Reply(1, 500, json::object());
	assert(f.results.back() == kAudiobookTemporaryError && f.capabilities.AudiobooksEnabled());
	f.Probe();
	f.transport.Reply(2, 403, json::object());
	assert(f.results.back() == kAudiobookForbidden && !f.capabilities.AudiobooksEnabled());
}

static void TestSearchAndMarketRules()
{
	CapabilityFixture f;
	f.Probe();
	f.transport.Reply(0, 200, {{"items", json::array()}});
	assert(f.transport.requests.size() == 2 && f.results.empty());
	assert(f.transport.requests[1].path.find("/search?") == 0);
	f.transport.Reply(1, 200, {{"audiobooks", {{"total", 1}, {"items", json::array()}}}});
	assert(f.capabilities.AudiobooksEnabled());
	f.Probe(true);
	f.transport.Reply(2, 200, {{"items", json::array({{{"restrictions", {{"reason", "market"}}}}})}});
	assert(f.results.back() == kAudiobookUnavailable && !f.capabilities.AudiobooksEnabled());
	assert(f.transport.requests.size() == 3); // No search for an explicit market block.
}

static void TestResetWhileSavedProbePending()
{
	CapabilityFixture f;
	f.Probe();
	f.capabilities.Reset();
	assert(f.results.size() == 1 && f.results.back() == kAudiobookUnknown);
	assert(!f.capabilities.ProbeInFlight());
	f.Probe();
	assert(f.transport.requests.size() == 2);
	f.transport.Reply(0, 200, {{"items", json::array()}});
	assert(f.transport.requests.size() == 2); // Old saved result cannot start a search.
	assert(f.results.size() == 1 && f.capabilities.ProbeInFlight());
	f.transport.Reply(1, 200, AvailableBooks());
	assert(f.results.size() == 2 && f.capabilities.AudiobooksEnabled());
}

static void TestResetWhileSearchPending()
{
	CapabilityFixture f;
	f.Probe();
	f.transport.Reply(0, 200, {{"items", json::array()}});
	assert(f.transport.requests.size() == 2);
	f.capabilities.Reset();
	f.Probe();
	assert(f.transport.requests.size() == 3);
	f.transport.Reply(1, 200, {{"audiobooks", {{"total", 1}}}});
	assert(f.capabilities.AudiobookState() == kAudiobookUnknown);
	assert(f.capabilities.ProbeInFlight() && f.results.size() == 1);
	f.transport.Reply(2, 200, AvailableBooks());
	assert(f.results.size() == 2 && f.capabilities.AudiobooksEnabled());
}

static void TestModeChangeAndSignOut()
{
	CapabilityFixture f;
	f.Probe();
	f.capabilities.SetAudiobookMode(kAudiobookDisabled);
	assert(f.results.size() == 1 && f.results[0] == kAudiobookUnknown);
	f.Probe();
	assert(f.results.back() == kAudiobookUnavailable && f.transport.requests.size() == 1);
	f.transport.Reply(0, 200, AvailableBooks());
	assert(f.capabilities.AudiobookState() == kAudiobookUnavailable && !f.capabilities.AudiobooksEnabled());
	f.capabilities.SetAudiobookMode(kAudiobookEnabled);
	assert(f.capabilities.AudiobooksEnabled());
	f.capabilities.RefreshForSession(false, kAudiobookAuto, {}, true);
	assert(!f.capabilities.AudiobooksEnabled() && f.capabilities.AudiobookState() == kAudiobookUnknown);
	assert(f.transport.requests.size() == 1);
	SpotifyCapabilities unavailable;
	unavailable.ProbeAudiobooks({});
	assert(unavailable.AudiobookState() == kAudiobookTemporaryError && !unavailable.ProbeInFlight());
	CapabilityFixture switched;
	switched.Probe();
	switched.capabilities.SetApi(nullptr);
	switched.transport.Reply(0, 200, AvailableBooks());
	assert(switched.capabilities.AudiobookState() == kAudiobookUnknown);
	assert(switched.results.size() == 1 && switched.results[0] == kAudiobookUnknown);
}

int main()
{
	TestAvailableCacheAndSingleFlight();
	TestSearchAndMarketRules();
	TestResetWhileSavedProbePending();
	TestResetWhileSearchPending();
	TestModeChangeAndSignOut();
	std::cout << "Spotify capability lifecycle tests passed.\n";
}
