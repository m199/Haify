#pragma once

#include "spotify/api/SpotifyApi.h"
#include "settings/SettingsController.h"
#include <cassert>
#include <utility>
#include <vector>

#ifdef NDEBUG
#error Session tests require assertions; compile without NDEBUG.
#endif

extern HaifySettings sessionTestSettings;
extern status_t sessionTestWriteStatus;
extern int sessionTestWrites;
void ResetSessionTestSettings();

struct SessionTestTransport {
	struct Request {
		std::string method, path;
		SpotifyApi::RequestCompletion complete;
	};
	std::vector<Request> requests;

	void Attach(SpotifyApi& api)
	{
		api.SetRequestHandler([this](const std::string& method, const std::string& path,
			const std::string&, const std::string&, SpotifyApi::RequestCompletion complete) {
			requests.push_back({method, path, std::move(complete)});
		});
	}
	void Reply(size_t index, int status, const nlohmann::json& body, int retryAfter = -1)
	{
		auto complete = requests.at(index).complete;
		complete(status, body.dump(), retryAfter);
	}
};
