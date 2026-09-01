#include "PlaylistCacheDocument.h"

namespace {

std::string
JsonString(const nlohmann::json& object, const char* key)
{
	if (!object.is_object())
		return "";
	auto value = object.find(key);
	if (value == object.end() || !value->is_string())
		return "";
	return value->get<std::string>();
}


int32
JsonInt32(const nlohmann::json& object, const char* key)
{
	if (!object.is_object())
		return 0;
	auto value = object.find(key);
	if (value == object.end()
			|| (!value->is_number_integer() && !value->is_number_unsigned())) {
		return 0;
	}
	return value->get<int32>();
}

}


nlohmann::json
PlaylistCacheDocument::ToJson(const PlaylistCacheDocument::Track& track)
{
	return {
		{"number", track.number},
		{"title", track.title},
		{"artist", track.artist},
		{"bpm", track.bpm},
		{"key", track.key},
		{"album", track.album},
		{"duration", track.duration},
		{"uri", track.uri},
		{"artist_uri", track.artistUri},
		{"album_uri", track.albumUri}
	};
}


PlaylistCacheDocument::Track
PlaylistCacheDocument::TrackFromJson(const nlohmann::json& data)
{
	Track track;
	track.number = JsonInt32(data, "number");
	track.title = JsonString(data, "title");
	track.artist = JsonString(data, "artist");
	track.bpm = JsonString(data, "bpm");
	track.key = JsonString(data, "key");
	track.album = JsonString(data, "album");
	track.duration = JsonString(data, "duration");
	track.uri = JsonString(data, "uri");
	track.artistUri = JsonString(data, "artist_uri");
	track.albumUri = JsonString(data, "album_uri");
	return track;
}


nlohmann::json
PlaylistCacheDocument::ToJson(const PlaylistCacheDocument::Episode& episode)
{
	return {
		{"number", episode.number},
		{"title", episode.title},
		{"description", episode.description},
		{"date", episode.date},
		{"duration", episode.duration},
		{"uri", episode.uri}
	};
}


PlaylistCacheDocument::Episode
PlaylistCacheDocument::EpisodeFromJson(const nlohmann::json& data)
{
	Episode episode;
	episode.number = JsonInt32(data, "number");
	episode.title = JsonString(data, "title");
	episode.description = JsonString(data, "description");
	episode.date = JsonString(data, "date");
	episode.duration = JsonString(data, "duration");
	episode.uri = JsonString(data, "uri");
	return episode;
}
