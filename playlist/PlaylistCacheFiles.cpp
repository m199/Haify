#include "PlaylistCacheFiles.h"

#include "SettingsController.h"

#include <Autolock.h>
#include <File.h>
#include <Locker.h>
#include <Path.h>
#include <SupportDefs.h>

#include <map>
#include <thread>
#include <time.h>
#include <utility>
#include <unistd.h>

namespace {

BLocker sCacheWriterLock("Haify playlist cache writer");
std::map<std::string, uint64> sCacheWriteGenerations;
uint64 sNextCacheWriteGeneration = 0;

const int32 kLikedSongsCacheVersion = 1;
const int32 kShowCacheVersion = 2;
const time_t kLikedSongsCacheMaxAge = 5 * 60;


const nlohmann::json&
EmptyArray()
{
	static const nlohmann::json kEmpty = nlohmann::json::array();
	return kEmpty;
}


std::string
JsonString(const nlohmann::json& object, const char* key,
	const std::string& fallback = "")
{
	if (!object.is_object())
		return fallback;
	auto value = object.find(key);
	if (value == object.end() || !value->is_string())
		return fallback;
	return value->get<std::string>();
}


int
JsonInt(const nlohmann::json& object, const char* key, int fallback = 0)
{
	if (!object.is_object())
		return fallback;
	auto value = object.find(key);
	if (value == object.end()
			|| (!value->is_number_integer() && !value->is_number_unsigned())) {
		return fallback;
	}
	return value->get<int>();
}


bool
ReadJson(const BPath& path, nlohmann::json& data)
{
	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return false;

	off_t size;
	if (file.GetSize(&size) != B_OK)
		return false;
	if (size <= 0 || size > 50 * 1024 * 1024)
		return false;

	std::string content((size_t)size, '\0');
	if (file.Read(&content[0], (size_t)size) != size)
		return false;

	try {
		data = nlohmann::json::parse(content);
		return true;
	} catch (...) {
		return false;
	}
}


bool
LikedSongsDocumentIsFresh(const nlohmann::json& object)
{
	if (JsonInt(object, "version") < kLikedSongsCacheVersion)
		return false;
	time_t cachedAt = (time_t)JsonInt(object, "cached_at", 0);
	time_t now = time(NULL);
	return cachedAt > 0 && now >= cachedAt
		&& now - cachedAt <= kLikedSongsCacheMaxAge;
}


bool
TrackDocumentIsReadable(const nlohmann::json& data, bool isPlaylist,
	std::string& cachedSnapshotId)
{
	if (!data.contains("tracks") || !data["tracks"].is_array())
		return false;
	if (!isPlaylist)
		return LikedSongsDocumentIsFresh(data);
	cachedSnapshotId = JsonString(data, "snapshot_id");
	return !cachedSnapshotId.empty();
}


bool
ShowDocumentIsReadable(const nlohmann::json& data)
{
	return data.contains("episodes") && data["episodes"].is_array()
		&& JsonInt(data, "version") >= kShowCacheVersion;
}


int32
Total(const nlohmann::json& data)
{
	return JsonInt(data, "total", 0);
}


int32
NextOffset(const nlohmann::json& data, int32 fallback = 0)
{
	return JsonInt(data, "next_offset", fallback);
}


bool
HasNextOffset(const nlohmann::json& data)
{
	return data.is_object() && data.contains("next_offset")
		&& data["next_offset"].is_number_integer();
}


const nlohmann::json&
TrackItems(const nlohmann::json& data)
{
	if (!data.is_object())
		return EmptyArray();
	auto value = data.find("tracks");
	return value != data.end() && value->is_array() ? *value : EmptyArray();
}


const nlohmann::json&
EpisodeItems(const nlohmann::json& data)
{
	if (!data.is_object())
		return EmptyArray();
	auto value = data.find("episodes");
	return value != data.end() && value->is_array() ? *value : EmptyArray();
}

nlohmann::json
NewTrackDocument(bool isPlaylist, int32 total,
	int32 nextOffset, const std::string& snapshotId)
{
	nlohmann::json data;
	data["total"] = total;
	data["next_offset"] = nextOffset;
	if (isPlaylist) {
		data["snapshot_id"] = snapshotId;
	} else {
		data["version"] = kLikedSongsCacheVersion;
		data["cached_at"] = (int)time(NULL);
	}
	data["tracks"] = nlohmann::json::array();
	return data;
}


nlohmann::json
NewShowDocument(int32 total, int32 nextOffset, bool complete)
{
	return {
		{"version", kShowCacheVersion},
		{"total", total},
		{"next_offset", nextOffset},
		{"complete", complete},
		{"episodes", nlohmann::json::array()}
	};
}


void
AddTrackItem(nlohmann::json& data, nlohmann::json item)
{
	data["tracks"].push_back(std::move(item));
}


void
AddEpisodeItem(nlohmann::json& data, nlohmann::json item)
{
	data["episodes"].push_back(std::move(item));
}


void
WriteAsync(const std::string& path, nlohmann::json data)
{
	uint64 generation;
	{
		BAutolock lock(&sCacheWriterLock);
		generation = ++sNextCacheWriteGeneration;
		sCacheWriteGenerations[path] = generation;
	}
	std::thread([path, generation, data = std::move(data)]() mutable {
		std::string serialized = data.dump();
		std::string temporary = path + ".part-" + std::to_string(generation);
		BFile file(temporary.c_str(), B_WRITE_ONLY | B_CREATE_FILE
			| B_ERASE_FILE);
		bool written = file.InitCheck() == B_OK
			&& file.Write(serialized.data(), serialized.size())
				== (ssize_t)serialized.size();
		file.Unset();
		bool current;
		{
			BAutolock lock(&sCacheWriterLock);
			current = sCacheWriteGenerations[path] == generation;
			if (current)
				sCacheWriteGenerations.erase(path);
		}
		if (written && current) {
			unlink(path.c_str());
			rename(temporary.c_str(), path.c_str());
		} else {
			unlink(temporary.c_str());
		}
	}).detach();
}


void
CancelWrite(const std::string& path)
{
	BAutolock lock(&sCacheWriterLock);
	sCacheWriteGenerations[path] = ++sNextCacheWriteGeneration;
}

}

bool
PlaylistCacheFiles::LikedSongsPath(BPath& path, bool createDirectories)
{
	std::string file = SettingsController::CacheFilePath("library",
		"liked-songs.json", createDirectories);
	return !file.empty() && path.SetTo(file.c_str()) == B_OK;
}


bool
PlaylistCacheFiles::PlaylistPath(const std::string& playlistId, BPath& path,
	bool createDirectories)
{
	std::string file = SettingsController::CacheFilePath("playlists",
		playlistId + ".json", createDirectories);
	return !file.empty() && path.SetTo(file.c_str()) == B_OK;
}


bool
PlaylistCacheFiles::ShowPath(const std::string& showId, BPath& path,
	bool createDirectories)
{
	std::string file = SettingsController::CacheFilePath("shows",
		showId + ".json", createDirectories);
	return !file.empty() && path.SetTo(file.c_str()) == B_OK;
}


bool
PlaylistCacheFiles::TrackPath(bool isPlaylist, const std::string& playlistId,
	BPath& path, bool createDirectories)
{
	if (isPlaylist)
		return PlaylistPath(playlistId, path, createDirectories);
	return LikedSongsPath(path, createDirectories);
}


bool
PlaylistCacheFiles::ReadTrackDocument(const BPath& path, bool isPlaylist,
	PlaylistCacheFiles::TrackDocument& document)
{
	nlohmann::json data;
	if (!ReadJson(path, data))
		return false;

	std::string cachedSnapshotId;
	if (!TrackDocumentIsReadable(data, isPlaylist, cachedSnapshotId))
		return false;

	document.total = Total(data);
	document.nextOffset = NextOffset(data);
	document.snapshotId = cachedSnapshotId;
	document.tracks.clear();
	for (const auto& track : TrackItems(data)) {
		if (track.is_object()) {
			document.tracks.push_back(
				PlaylistCacheDocument::TrackFromJson(track));
		}
	}
	return true;
}


bool
PlaylistCacheFiles::ReadTrackDocument(bool isPlaylist,
	const std::string& playlistId, TrackDocument& document)
{
	BPath path;
	if (!TrackPath(isPlaylist, playlistId, path, false))
		return false;
	return ReadTrackDocument(path, isPlaylist, document);
}


bool
PlaylistCacheFiles::ReadShowDocument(const BPath& path,
	PlaylistCacheFiles::ShowDocument& document)
{
	nlohmann::json data;
	if (!ReadJson(path, data) || !ShowDocumentIsReadable(data))
		return false;

	document.total = Total(data);
	document.nextOffset = NextOffset(data);
	document.hasNextOffset = HasNextOffset(data);
	document.episodes.clear();
	for (const auto& episode : EpisodeItems(data)) {
		if (episode.is_object()) {
			document.episodes.push_back(
				PlaylistCacheDocument::EpisodeFromJson(episode));
		}
	}
	return true;
}


bool
PlaylistCacheFiles::ReadShowDocument(const std::string& showId,
	ShowDocument& document)
{
	BPath path;
	if (!ShowPath(showId, path, false))
		return false;
	return ReadShowDocument(path, document);
}


void
PlaylistCacheFiles::WriteTrackDocument(const std::string& path, bool isPlaylist,
	int32 total, int32 nextOffset, const std::string& snapshotId,
	const std::vector<PlaylistCacheDocument::Track>& tracks)
{
	nlohmann::json data = NewTrackDocument(isPlaylist, total, nextOffset,
		snapshotId);
	for (const PlaylistCacheDocument::Track& track : tracks)
		AddTrackItem(data, PlaylistCacheDocument::ToJson(track));
	WriteAsync(path, std::move(data));
}


bool
PlaylistCacheFiles::WriteTrackDocument(bool isPlaylist,
	const std::string& playlistId, int32 total, int32 nextOffset,
	const std::string& snapshotId,
	const std::vector<PlaylistCacheDocument::Track>& tracks)
{
	BPath path;
	if (!TrackPath(isPlaylist, playlistId, path, true))
		return false;
	WriteTrackDocument(path.Path(), isPlaylist, total, nextOffset, snapshotId,
		tracks);
	return true;
}


static void
WriteShowDocumentToPath(const std::string& path, int32 total,
	int32 nextOffset, bool complete,
	const std::vector<PlaylistCacheDocument::Episode>& episodes)
{
	nlohmann::json data = NewShowDocument(total, nextOffset, complete);
	for (const PlaylistCacheDocument::Episode& episode : episodes)
		AddEpisodeItem(data, PlaylistCacheDocument::ToJson(episode));
	WriteAsync(path, std::move(data));
}


void
PlaylistCacheFiles::WriteShowDocument(const std::string& showId, int32 total,
	int32 nextOffset, bool complete,
	const std::vector<PlaylistCacheDocument::Episode>& episodes)
{
	BPath path;
	if (!ShowPath(showId, path, true))
		return;
	WriteShowDocumentToPath(path.Path(), total, nextOffset, complete,
		episodes);
}


void
PlaylistCacheFiles::RemoveLikedSongs()
{
	BPath path;
	if (!LikedSongsPath(path, false))
		return;
	CancelWrite(path.Path());
	unlink(path.Path());
}


void
PlaylistCacheFiles::RemovePlaylist(const std::string& playlistId)
{
	BPath path;
	if (!PlaylistPath(playlistId, path, false))
		return;
	CancelWrite(path.Path());
	unlink(path.Path());
}


void
PlaylistCacheFiles::RemoveShow(const std::string& showId)
{
	BPath path;
	if (!ShowPath(showId, path, false))
		return;
	CancelWrite(path.Path());
	unlink(path.Path());
}
