#pragma once

#include "spotify/api/SpotifyApiTypes.h"

#include <cassert>
#include <utility>
#include <vector>

#ifdef NDEBUG
#error API tests require assertions; compile without NDEBUG.
#endif

// Records the API boundary, with responses delivered explicitly by each test.
// No HTTP client, clock, settings repository or application is constructed.
struct JsonApiTestTransport {
	struct Request {
		std::string method;
		std::string path;
		std::string body;
		JsonCallback callback;
	};

	std::vector<Request> requests;
	std::vector<std::string> invalidations;
	std::vector<std::string> events;

	auto GetHandler()
	{
		return [this](const std::string& path, JsonCallback callback) {
			Record("GET", path, "", std::move(callback));
		};
	}

	auto BodyHandler(const std::string& method)
	{
		return [this, method](const std::string& path,
			const std::string& body, JsonCallback callback) {
			Record(method, path, body, std::move(callback));
		};
	}

	auto CacheHandler()
	{
		return [this](const std::string& path) {
			invalidations.push_back(path);
			events.push_back("INVALIDATE " + path);
		};
	}

	void Record(const std::string& method, const std::string& path,
		const std::string& body, JsonCallback callback)
	{
		requests.push_back({method, path, body, std::move(callback)});
		events.push_back(method + " " + path);
	}

	void Reply(size_t index, bool ok, const nlohmann::json& data)
	{
		// A callback may enqueue another request and reallocate the vector.
		auto callback = std::move(requests.at(index).callback);
		assert(callback);
		callback(ok, data);
	}

	void Expect(size_t index, const std::string& method,
		const std::string& path, const std::string& body = "") const
	{
		const auto& request = requests.at(index);
		assert(request.method == method && request.path == path);
		assert(request.body == body);
	}

	void ExpectJson(size_t index, const std::string& method,
		const std::string& path, const nlohmann::json& body) const
	{
		const auto& request = requests.at(index);
		assert(request.method == method && request.path == path);
		assert(nlohmann::json::parse(request.body) == body);
	}
};

struct JsonApiTestResult {
	int calls = 0;
	bool ok = false;
	nlohmann::json data;

	JsonCallback Callback()
	{
		return [this](bool success, const nlohmann::json& response) {
			calls++;
			ok = success;
			data = response;
		};
	}

	void Expect(bool success, const nlohmann::json& response) const
	{
		assert(calls == 1 && ok == success && data == response);
	}
};
