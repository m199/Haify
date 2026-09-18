#include "playback/LibrespotTransferController.h"
#include "spotify/api/PlaybackApi.h"
#include "JsonApiTestSupport.h"

#include <iostream>
#include <limits>

using nlohmann::json;

static json
Devices(const std::string& name = "Haify", const std::string& id = "local")
{
	return {{"devices", json::array({{{"name", name}, {"id", id}, {"is_active", false}}})}};
}

struct Fixture {
	JsonApiTestTransport transport;
	PlaybackApi api{transport.GetHandler(), transport.BodyHandler("PUT"),
		transport.BodyHandler("POST"), transport.CacheHandler()};
	LibrespotTransferController controller;
	std::vector<LibrespotTransferResult> results;

	void Send(const LibrespotTransferRequest& request)
	{
		bool dispatched = DispatchLibrespotTransfer(api, request,
			[this](const LibrespotTransferResult& result) { results.push_back(result); });
		assert(dispatched);
	}

	void Begin(LibrespotTransferMode mode, bool oauth = false)
	{
		controller.Begin(100, mode, oauth);
		Send(controller.Poll("Haify"));
	}

	LibrespotTransferUpdate Advance(bool ok, const json& data)
	{
		transport.Reply(transport.requests.size() - 1, ok, data);
		auto update = controller.Apply(results.back());
		assert(update.accepted);
		if (ValidLibrespotTransferRequest(update.next))
			Send(update.next);
		return update;
	}
};

static void
TestExplicitStart()
{
	Fixture f;
	assert(!ValidLibrespotTransferRequest(f.controller.Poll("Haify")));
	f.Begin(kLibrespotTransferAlways);
	f.transport.Expect(0, "GET", "/me/player/devices");
	assert(f.transport.events.front() == "INVALIDATE /me/player/devices");
	assert(!ValidLibrespotTransferRequest(f.controller.Poll("Haify")));
	// A callback only publishes data; the owner accepts it before any mutation.
	f.transport.Reply(0, true, Devices());
	assert(f.transport.requests.size() == 1 && !f.controller.Readiness().Ready());
	auto update = f.controller.Apply(f.results.back());
	assert(update.accepted && !update.ready && !update.finishOAuth);
	f.Send(update.next);
	f.transport.ExpectJson(1, "PUT", "/me/player",
		{{"device_ids", {"local"}}, {"play", true}});
	assert(!f.controller.Apply(f.results.front()).accepted);
	assert(!f.controller.Readiness().Ready());
	assert(f.Advance(true, {{"status", 204}}).ready);
	assert(f.controller.Readiness().ReadyAfter(99));
	assert(!f.controller.Readiness().ReadyAfter(100));
	assert(!f.controller.Apply(f.results.back()).accepted);
	f.controller.ResetAttempts();
	assert(!ValidLibrespotTransferRequest(f.controller.Poll("Haify")));
	assert(f.transport.requests.size() == 2);
}

static void
TestIdleDecisions()
{
	const struct { json data; bool valid = false; bool transfer = false; } cases[] = {
		{json::object(), true, true}, // Empty successful GET is represented as {}.
		{{{"is_playing", false}, {"device", {{"id", "remote"}}}}, true, true},
		{{{"is_playing", true}, {"device", {{"id", "remote"}}}}, true, false},
		{{{"is_playing", true}, {"device", {{"id", "local"}}}}, true, true},
		{{{"is_playing", true}}, true, false},
		{{{"is_playing", true}, {"device", {{"id", nullptr}}}}, true, false},
		{{{"is_playing", false}, {"device", nullptr}}, true, true},
		{{{"is_playing", "false"}}, false, false},
		{{{"device", 7}}, false, false},
		{{{"device", {{"id", 7}}}}, false, false},
		{json::array(), false, false},
		{nullptr, false, false}
	};
	for (const auto& item : cases) {
		Fixture f;
		f.Begin(kLibrespotTransferIfIdle);
		f.Advance(true, Devices());
		f.transport.Expect(1, "GET", "/me/player?additional_types=episode");
		assert(f.transport.invalidations.back() == "/me/player?additional_types=episode");
		f.Advance(true, item.data);
		assert(f.results.back().responseValid == item.valid);
		assert(f.results.back().shouldTransfer == item.transfer);
		assert(!f.controller.Readiness().Ready());
		assert(f.transport.requests.size() == (item.transfer ? 3U : 2U));
		if (item.transfer) {
			f.transport.ExpectJson(2, "PUT", "/me/player",
				{{"device_ids", {"local"}}, {"play", true}});
			assert(f.Advance(true, {{"status", 204}}).ready);
		}
		assert(!ValidLibrespotTransferRequest(f.controller.Poll("Haify")));
	}
}

static void
TestDiscoveryMapping()
{
	const struct { json data; bool valid = false; std::string id; } cases[] = {
		{Devices("haify"), true, ""},
		{Devices("Haify", ""), true, ""},
		{{{"devices", json::array({{{"name", "Haify"}, {"id", nullptr}},
			{{"name", "Haify"}, {"id", "second"}}})}}, true, ""},
		{{{"devices", json::array({42, {{"name", "remote"}},
			{{"name", "Haify"}, {"id", "first"}},
			{{"name", "Haify"}, {"id", "second"}}})}}, true, "first"},
		{json::object(), false, ""},
		{{{"devices", 42}}, false, ""},
		{{{"devices", json::array({{{"name", 42}}})}}, false, ""},
		{{{"devices", json::array({{{"name", "Haify"}, {"id", 42}}})}}, false, ""},
		{nullptr, false, ""}
	};
	for (const auto& item : cases) {
		Fixture f;
		f.Begin(kLibrespotTransferAlways);
		auto update = f.Advance(true, item.data);
		assert(f.results.back().ok && f.results.back().responseValid == item.valid);
		assert(f.results.back().foundDeviceId == item.id);
		assert(update.retryDelayUs == (item.id.empty() ? 1000000 : 0));
		assert(f.transport.requests.size() == (item.id.empty() ? 1U : 2U));
	}
}

static void
TestFailureProvenance()
{
	for (int stage = 0; stage < 3; ++stage) {
		for (int status : {-1, 0, 200, 401, 403, 429, 500}) {
			Fixture f;
			f.Begin(kLibrespotTransferIfIdle);
			if (stage > 0) f.Advance(true, Devices());
			if (stage > 1) f.Advance(true, json::object());
			auto update = f.Advance(false, {{"status", status}, {"retry_after", 31}});
			assert(!f.results.back().ok && f.results.back().status == status);
			assert(f.results.back().retryAfter == 31);
			assert(!update.ready && !f.controller.Readiness().Ready());
			assert(!ValidLibrespotTransferRequest(update.next));
			assert(update.retryDelayUs == (stage == 0 ? 1000000 : 0));
			assert(f.transport.requests.size() == size_t(stage + 1));
		}
	}
	for (const json& value : {json("429"), json(4.5), json(nullptr),
			json(std::numeric_limits<uint64_t>::max()), json(-2)}) {
		Fixture f;
		f.Begin(kLibrespotTransferAlways);
		f.Advance(false, {{"status", value}, {"retry_after", value}});
		assert(f.results.back().status == -1 && f.results.back().retryAfter == -1);
	}
}

static void
TestRetryBudgetsAndOAuth()
{
	for (bool oauth : {false, true}) {
		Fixture f;
		f.Begin(kLibrespotTransferAlways, oauth);
		int limit = oauth ? 150 : 5;
		for (int i = 1; i <= limit; ++i) {
			auto update = f.Advance(true, {{"devices", json::array()}});
			assert(!update.finishOAuth && !update.ready);
			assert(update.retryDelayUs == (i == limit ? 0 : oauth ? 2000000 : 1000000));
			assert(!f.controller.Apply(f.results.back()).accepted);
			if (i < limit) f.Send(f.controller.Poll("Haify"));
		}
		assert(!ValidLibrespotTransferRequest(f.controller.Poll("Haify")));
		// Authentication completion can restart exhausted discovery.
		f.controller.ResetAttempts();
		f.Send(f.controller.Poll("Haify"));
		assert(f.Advance(true, Devices()).finishOAuth == oauth);
		assert(!f.controller.Readiness().Ready());
		assert(f.Advance(true, {{"status", 204}}).ready);
		assert(f.transport.requests.size() == size_t(limit + 2));
	}
}

static void
TestObsoleteResponses()
{
	for (int stage = 0; stage < 3; ++stage) {
		for (bool stopped : {false, true}) {
			Fixture f;
			f.Begin(kLibrespotTransferIfIdle);
			if (stage > 0) f.Advance(true, Devices());
			if (stage > 1) f.Advance(true, json::object());
			size_t oldRequest = f.transport.requests.size() - 1;
			f.controller.Begin(stopped ? 0 : 200);
			if (!stopped) f.Send(f.controller.Poll("New name"));
			f.transport.Reply(oldRequest, true, stage == 0 ? Devices() : json::object());
			assert(!f.controller.Apply(f.results.back()).accepted);
			assert(!f.controller.Readiness().Ready());
			assert(f.transport.requests.size() == oldRequest + (stopped ? 1 : 2));
		}
	}
	Fixture f;
	f.Begin(kLibrespotTransferAlways);
	f.Advance(true, {{"devices", json::array()}});
	LibrespotTransferResult old = f.results.back();
	f.Send(f.controller.Poll("Haify"));
	assert(!f.controller.Apply(old).accepted);
	f.transport.Reply(1, true, Devices());
	auto wrong = f.results.back();
	wrong.request.deviceName = "other";
	assert(!f.controller.Apply(wrong).accepted);
	assert(f.controller.Apply(f.results.back()).accepted);
}

static void
TestDispatchRejectionAndAuthRefresh()
{
	Fixture f;
	f.Begin(kLibrespotTransferAlways);
	f.transport.Reply(0, true, Devices());
	auto update = f.controller.Apply(f.results.back());
	LibrespotTransferResult rejected;
	rejected.request = update.next;
	// The owner reports unavailable API/process as a failed, unsent step.
	assert(f.controller.Apply(rejected).accepted);
	assert(!f.controller.Readiness().Ready());
	f.controller.ResetAttempts();
	f.Send(f.controller.Poll("Haify"));
	f.controller.ResetAttempts(); // Authentication while GET is in flight.
	assert(!ValidLibrespotTransferRequest(f.controller.Poll("Haify")));
	f.Advance(true, Devices());
	f.controller.ResetAttempts(); // Nor may it duplicate a pending PUT.
	assert(!ValidLibrespotTransferRequest(f.controller.Poll("Haify")));
	assert(!f.controller.Apply(rejected).accepted);
	assert(f.Advance(true, {{"status", 204}}).ready);
}

static void
TestInvalidRequestsAndOwnedCallbacks()
{
	Fixture f;
	LibrespotTransferRequest valid{10, 1, LibrespotTransferStep::FindDevice, "Haify", ""};
	std::vector<LibrespotTransferRequest> invalid = {
		{}, {0, 1, LibrespotTransferStep::FindDevice, "Haify", ""},
		{10, 0, LibrespotTransferStep::FindDevice, "Haify", ""},
		{10, 1, static_cast<LibrespotTransferStep>(99), "", "local"},
		{10, 1, LibrespotTransferStep::FindDevice, "", ""},
		{10, 1, LibrespotTransferStep::Transfer, "", ""},
		{10, 1, LibrespotTransferStep::InspectPlayback, "Haify", "local"}
	};
	bool called = false;
	for (const auto& request : invalid) {
		bool dispatched = DispatchLibrespotTransfer(f.api, request,
			[&called](const LibrespotTransferResult&) { called = true; });
		assert(!dispatched && !called);
	}
	assert(!DispatchLibrespotTransfer(f.api, valid, {}));
	assert(f.transport.requests.empty());
	f.Send(valid);
	valid.deviceName = "changed";
	f.transport.Reply(0, true, Devices());
	assert(f.results.back().request.deviceName == "Haify");
	assert(f.results.back().foundDeviceId == "local");
}

int
main()
{
	TestExplicitStart();
	TestIdleDecisions();
	TestDiscoveryMapping();
	TestFailureProvenance();
	TestRetryBudgetsAndOAuth();
	TestObsoleteResponses();
	TestDispatchRejectionAndAuthRefresh();
	TestInvalidRequestsAndOwnedCallbacks();
	std::cout << "Librespot transfer controller tests passed.\n";
}
