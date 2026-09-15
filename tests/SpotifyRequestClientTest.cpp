#include "HaifyDebug.h"
#include "spotify/api/SpotifyRequestClient.h"
#include "network/HttpClient.h"

#include <cassert>
#include <cstdio>
#include <utility>

#ifdef NDEBUG
#error Request client tests require assertions; compile without NDEBUG.
#endif

// The standalone fixture does not link App.cpp, which owns this debug flag.
bool gIsDebug = false;

// Link this fixture instead of network/HttpClient.cpp. Every request must pass
// through SetRequestHandler; reaching the network is a test failure.
void HttpClient::Get(const std::string&, const Headers&, HttpCallback) { assert(false); }
void HttpClient::Post(const std::string&, const Headers&, const std::string&, HttpCallback) { assert(false); }
void HttpClient::Put(const std::string&, const Headers&, const std::string&, HttpCallback) { assert(false); }
void HttpClient::Delete(const std::string&, const Headers&, const std::string&, HttpCallback) { assert(false); }

struct Transport {
	std::vector<SpotifyRequestClient::RequestCompletion> completions;
	void Attach(SpotifyRequestClient& client)
	{
		client.SetRequestHandler([this](const std::string&, const std::string&,
			const std::string&, const std::string&, SpotifyRequestClient::RequestCompletion completion) {
			completions.push_back(std::move(completion));
		});
	}
	void Reply(size_t index, int status, const std::string& body, int retry = -1)
	{
		// Copy: a refresh retry can append to completions while this call runs.
		auto completion = completions.at(index);
		completion(status, body, retry);
	}
};

static void
TestInvalidationSeparatesReaders(bool exactPath, bool newCompletesFirst)
{
	SpotifyRequestClient client("token");
	client.SetAccountId("account");
	Transport transport;
	transport.Attach(client);
	int oldCalls = 0, newCalls = 0;
	auto old = [&oldCalls](bool ok, const nlohmann::json& data) {
		assert(ok && data["value"] == "old");
		oldCalls++;
	};
	client.Get("/me/albums", old);
	client.Get("/me/albums", old);
	assert(transport.completions.size() == 1);
	if (exactPath)
		client.EraseCache("/me/albums");
	else
		client.InvalidateCachePrefix("/me/");
	client.Get("/me/albums", [&newCalls](bool ok, const nlohmann::json& data) {
		assert(ok && data["value"] == "new");
		newCalls++;
	});
	assert(transport.completions.size() == 2);
	if (newCompletesFirst)
		transport.Reply(1, 200, R"({"value":"new"})");
	transport.Reply(0, 200, R"({"value":"old"})");
	assert(oldCalls == 2 && newCalls == (newCompletesFirst ? 1 : 0));
	if (!newCompletesFirst)
		transport.Reply(1, 200, R"({"value":"new"})");
	client.Get("/me/albums", [](bool ok, const nlohmann::json& data) {
		assert(ok && data["value"] == "new");
	});
	assert(transport.completions.size() == 2 && newCalls == 1);
	transport.Reply(0, 200, R"({"value":"old"})");
	assert(oldCalls == 2);
}

static void
TestAccountChangeAndSignOut()
{
	SpotifyRequestClient client("token");
	client.SetAccountId("a");
	Transport transport;
	transport.Attach(client);
	int cancelled = 0, current = 0;
	client.Get("/me/albums", [&cancelled](bool ok, const nlohmann::json& data) {
		assert(!ok && data["status"] == -1);
		cancelled++;
	});
	client.SetAccountId("b");
	client.Get("/me/albums", [&current](bool ok, const nlohmann::json& data) {
		assert(ok && data["account"] == "b");
		current++;
	});
	transport.Reply(0, 200, R"({"account":"a"})");
	assert(cancelled == 1 && current == 0);
	transport.Reply(1, 200, R"({"account":"b"})");
	assert(current == 1);
	client.Put("/me/library", "", [&cancelled](bool ok, const nlohmann::json& data) {
		assert(!ok && data["status"] == -1);
		cancelled++;
	});
	client.ClearSession();
	transport.Reply(2, 204, "");
	assert(cancelled == 2 && client.AccountId().empty());
}

static void
TestRefreshDoesNotRetryInAnotherSession()
{
	SpotifyRequestClient client("token");
	client.SetAccountId("a");
	Transport transport;
	transport.Attach(client);
	std::function<void(bool)> refreshed;
	client.SetTokenRefreshHandler([&refreshed](std::function<void(bool)> done) { refreshed = done; });
	int failures = 0;
	client.Post("/me/playlists", "{}", [&failures](bool ok, const nlohmann::json& data) {
		assert(!ok && data["status"] == -1);
		failures++;
	});
	transport.Reply(0, 401, "{}");
	assert(refreshed && failures == 0);
	client.SetAccountId("b");
	refreshed(true);
	assert(transport.completions.size() == 1 && failures == 1);
}

static void
TestFailureClassification()
{
	SpotifyRequestClient client("token");
	Transport transport;
	transport.Attach(client);
	client.Get("/limited", [](bool ok, const nlohmann::json& data) {
		assert(!ok && data["status"] == 429 && data["retry_after"] == 12);
	});
	transport.Reply(0, 429, "limited", 12);
	client.Get("/malformed", [](bool ok, const nlohmann::json& data) {
		assert(!ok && data["status"] == 200 && data["error"] == "invalid_json");
	});
	transport.Reply(1, 200, "broken");
	client.Get("/malformed", [](bool ok, const nlohmann::json& data) {
		assert(ok && data.is_object());
	});
	assert(transport.completions.size() == 3);
	transport.Reply(2, 200, "{}");
}

static void
TestAccountAtDispatch()
{
	SpotifyRequestClient client("token");
	client.SetAccountId("a");
	Transport transport;
	transport.Attach(client);
	bool unexpected = false;
	bool dispatched = client.DispatchForAccount("b", [&unexpected]() { unexpected = true; });
	assert(!dispatched && !unexpected);
	dispatched = client.DispatchForAccount("a", [&client]() { client.Get("/me", nullptr); });
	assert(dispatched);
	assert(transport.completions.size() == 1);
	client.SetAccountId("b");
	dispatched = client.DispatchForAccount("a", [&unexpected]() { unexpected = true; });
	assert(!dispatched && !unexpected);
	transport.Reply(0, 200, "{}");
}

static void
TestMutationFailureClassification()
{
	for (int status : {-1, 0, 401, 403, 404, 429, 500}) {
		SpotifyRequestClient client("token");
		Transport transport;
		transport.Attach(client);
		int calls = 0;
		client.Put("/me/player/play", "{}",
			[&calls, status](bool ok, const nlohmann::json& data) {
				assert(!ok && data["status"] == status);
				assert(data["body"] == "failure" && data["retry_after"] == 12);
				calls++;
			});
		transport.Reply(0, status, "failure", 12);
		assert(calls == 1 && transport.completions.size() == 1);
	}
}

static void
TestAcceptedPlayAndEmptyPlaybackState()
{
	SpotifyRequestClient client("token");
	Transport transport;
	transport.Attach(client);
	int calls = 0;
	client.Put("/me/player/play", R"({"uris":["spotify:track:one"]})",
		[&calls](bool ok, const nlohmann::json& data) {
			assert(ok && data["status"] == 204);
			assert(!data.contains("item") && !data.contains("is_playing"));
			calls++;
		});
	transport.Reply(0, 204, "");
	client.Get("/me/player", [&calls](bool ok, const nlohmann::json& data) {
		assert(ok && data.is_object() && data.empty());
		calls++;
	});
	transport.Reply(1, 204, "");
	assert(calls == 2 && transport.completions.size() == 2);
}

static void
TestRefreshPreservesCommandAndRetriesOnlyOnce()
{
	for (int finalStatus : {204, 401}) {
		SpotifyRequestClient client("old-token");
		std::vector<SpotifyRequestClient::RequestCompletion> completions;
		client.SetRequestHandler([&completions](const std::string& method,
			const std::string& path, const std::string& body,
			const std::string& contentType, SpotifyRequestClient::RequestCompletion complete) {
			assert(method == "PUT" && path == "/me/player/play?device_id=baron");
			assert(body == R"({"uris":["spotify:track:one"]})" && contentType == "application/json");
			completions.push_back(std::move(complete));
		});
		std::function<void(bool)> refreshed;
		int refreshCalls = 0, resultCalls = 0;
		client.SetTokenRefreshHandler([&](std::function<void(bool)> done) {
			refreshCalls++;
			refreshed = std::move(done);
		});
		client.Put("/me/player/play?device_id=baron", R"({"uris":["spotify:track:one"]})",
			[&resultCalls, finalStatus](bool ok, const nlohmann::json& data) {
				assert(ok == (finalStatus == 204) && data["status"] == finalStatus);
				resultCalls++;
			});
		auto first = completions.at(0);
		first(401, "expired", -1);
		assert(refreshCalls == 1 && resultCalls == 0 && completions.size() == 1);
		client.SetAccessToken("new-token");
		refreshed(true);
		assert(completions.size() == 2 && resultCalls == 0);
		auto retry = completions.at(1);
		retry(finalStatus, "", -1);
		assert(refreshCalls == 1 && resultCalls == 1 && completions.size() == 2);
	}
}

int
main()
{
	TestInvalidationSeparatesReaders(false, false);
	TestInvalidationSeparatesReaders(false, true);
	TestInvalidationSeparatesReaders(true, false);
	TestInvalidationSeparatesReaders(true, true);
	TestAccountChangeAndSignOut();
	TestRefreshDoesNotRetryInAnotherSession();
	TestFailureClassification();
	TestAccountAtDispatch();
	TestMutationFailureClassification();
	TestAcceptedPlayAndEmptyPlaybackState();
	TestRefreshPreservesCommandAndRetriesOnlyOnce();
	std::puts("Spotify request client tests passed.");
}
