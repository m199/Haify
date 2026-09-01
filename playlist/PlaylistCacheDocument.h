#pragma once

#include <nlohmann/json.hpp>

#include <SupportDefs.h>

#include <string>

namespace PlaylistCacheDocument {

struct Track {
	int32		number = 0;
	std::string	title;
	std::string	artist;
	std::string	bpm;
	std::string	key;
	std::string	album;
	std::string	duration;
	std::string	uri;
	std::string	artistUri;
	std::string	albumUri;
};

struct Episode {
	int32		number = 0;
	std::string	title;
	std::string	description;
	std::string	date;
	std::string	duration;
	std::string	uri;
};

nlohmann::json	ToJson(const Track& track);
Track			TrackFromJson(const nlohmann::json& data);
nlohmann::json	ToJson(const Episode& episode);
Episode			EpisodeFromJson(const nlohmann::json& data);

}
