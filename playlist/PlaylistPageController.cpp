#include "PlaylistPageController.h"

#include <cstdio>
#include <limits>
#include <utility>

namespace {
std::string
String(const nlohmann::json& data, const char* key,
	const std::string& fallback = "")
{
	if (!data.is_object())
		return fallback;
	auto value = data.find(key);
	return value != data.end() && value->is_string()
		? value->get<std::string>() : fallback;
}

int32_t
Integer(const nlohmann::json& data, const char* key, int32_t fallback)
{
	if (!data.is_object())
		return fallback;
	auto value = data.find(key);
	if (value == data.end() || !value->is_number_integer())
		return fallback;
	// Mixed signed/unsigned JSON comparisons may narrow UINT64_MAX to -1.
	if (value->is_number_unsigned()) {
		auto number = value->get<nlohmann::json::number_unsigned_t>();
		return number <= static_cast<nlohmann::json::number_unsigned_t>(
				std::numeric_limits<int32_t>::max())
			? static_cast<int32_t>(number) : fallback;
	}
	auto number = value->get<nlohmann::json::number_integer_t>();
	return number >= std::numeric_limits<int32_t>::min()
		&& number <= std::numeric_limits<int32_t>::max()
		? static_cast<int32_t>(number) : fallback;
}

std::string
Duration(int32_t milliseconds)
{
	int seconds = milliseconds / 1000;
	char buffer[16];
	snprintf(buffer, sizeof(buffer), "%d:%02d", seconds / 60, seconds % 60);
	return buffer;
}

void
TrackContext(PlaylistPageRow& row, const nlohmann::json& track,
	const PlaylistPageRequest& request)
{
	row.artist = "Unknown";
	if (track.contains("artists") && track["artists"].is_array()
			&& !track["artists"].empty()) {
		row.artist = String(track["artists"][0], "name", "Unknown");
		row.artistUri = String(track["artists"][0], "uri");
	} else if (track.contains("show") && track["show"].is_object()) {
		row.artist = String(track["show"], "name", "Unknown");
		row.artistUri = String(track["show"], "uri");
	}
	if (request.source == PlaylistPageSource::Album) {
		row.album = request.albumName;
		row.albumUri = SpotifyUriForItemKind(kSpotifyItemAlbum, request.id);
	}
	if (track.contains("album") && track["album"].is_object()) {
		row.album = String(track["album"], "name",
			row.album.empty() ? "Unknown" : row.album);
		row.albumUri = String(track["album"], "uri", row.albumUri);
	} else if (track.contains("show") && track["show"].is_object()) {
		row.album = String(track["show"], "name", row.album);
		row.albumUri = String(track["show"], "uri", row.albumUri);
	}
}

const nlohmann::json*
PlayableItem(const nlohmann::json& entry, PlaylistPageSource source)
{
	if (!entry.is_object())
		return nullptr;
	if (source == PlaylistPageSource::Album || source == PlaylistPageSource::Podcast)
		return &entry;
	if (source == PlaylistPageSource::Playlist
			&& entry.contains("item") && entry["item"].is_object()) {
		return &entry["item"];
	}
	if (entry.contains("track") && entry["track"].is_object())
		return &entry["track"];
	return nullptr;
}

void
AppendRow(PlaylistPageResult& result, const nlohmann::json& entry, int32_t number)
{
	const auto* item = PlayableItem(entry, result.request.source);
	bool podcast = result.request.source == PlaylistPageSource::Podcast;
	if (!item && !podcast)
		return;
	PlaylistPageRow row;
	row.number = number;
	row.unavailable = item == nullptr;
	if (item) {
		row.title = String(*item, "name", "Unknown");
		row.uri = String(*item, "uri");
		row.duration = Duration(Integer(*item, "duration_ms", 0));
		if (podcast) {
			row.description = String(*item, "html_description",
				String(*item, "description"));
			row.date = String(*item, "release_date");
		} else {
			TrackContext(row, *item, result.request);
		}
	}
	result.rows.push_back(std::move(row));
}

PlaylistPageResult
MapPage(const PlaylistPageRequest& request, bool ok, const nlohmann::json& data)
{
	PlaylistPageResult result;
	result.request = request;
	result.status = Integer(data, "status", -1);
	result.retryAfter = Integer(data, "retry_after", -1);
	if (!ok)
		return result;
	result.responseValid = data.is_object() && data.contains("items")
		&& data["items"].is_array();
	if (!result.responseValid)
		return result;
	const auto& items = data["items"];
	if (items.size() > static_cast<size_t>(
			std::numeric_limits<int32_t>::max() - request.offset)) {
		result.responseValid = false;
		return result;
	}
	result.ok = true;
	result.total = Integer(data, "total",
		request.source == PlaylistPageSource::Podcast ? 0 : -1);
	result.pageCount = static_cast<int32_t>(items.size());
	result.nextOffset = request.offset + result.pageCount;
	int32_t number = request.offset;
	for (const auto& entry : items)
		AppendRow(result, entry, ++number);
	return result;
}
}

PlaylistPageRequest
MakePlaylistPageRequest(const PlaylistContentTarget& target, int32_t offset, int32_t limit)
{
	PlaylistPageRequest request;
	request.id = target.id;
	request.offset = offset;
	request.limit = limit;
	if (target.isCollection)
		request.source = PlaylistPageSource::LikedSongs;
	else if (target.kind == kSpotifyItemPlaylist)
		request.source = PlaylistPageSource::Playlist;
	else if (target.kind == kSpotifyItemAlbum)
		request.source = PlaylistPageSource::Album;
	else if (target.kind == kSpotifyItemShow)
		request.source = PlaylistPageSource::Podcast;
	return request;
}

int64_t
PlaylistPageRetryDelay(int32_t status, int32_t retryAfter, int32_t retryCount,
	bool responseValid)
{
	bool temporary = status < 0 || status == 408 || status == 425
		|| status == 429 || status >= 500;
	if (!responseValid || !temporary || retryCount >= 3)
		return 0;
	return status == 429 && retryAfter > 0
		? int64_t(retryAfter) * 1000000 : 2000000;
}

PlaylistPageController::PlaylistPageController(CollectionGetter likedSongs,
	ContentGetter playlist, ContentGetter album, ContentGetter podcast)
	: fLikedSongs(std::move(likedSongs)), fPlaylist(std::move(playlist)),
	  fAlbum(std::move(album)), fPodcast(std::move(podcast))
{
}

bool
PlaylistPageController::Load(const PlaylistPageRequest& request, Completion complete) const
{
	if (request.offset < 0 || request.limit <= 0
			|| (request.headRefresh && request.source != PlaylistPageSource::Podcast)) {
		return false;
	}
	auto done = [request, complete](bool ok, const nlohmann::json& data) {
		if (complete)
			complete(MapPage(request, ok, data));
	};
	switch (request.source) {
		case PlaylistPageSource::LikedSongs:
			fLikedSongs(request.offset, request.limit, done);
			break;
		case PlaylistPageSource::Playlist:
			fPlaylist(request.id, request.offset, request.limit, done);
			break;
		case PlaylistPageSource::Album:
			fAlbum(request.id, request.offset, request.limit, done);
			break;
		case PlaylistPageSource::Podcast:
			fPodcast(request.id, request.offset, request.limit, done);
			break;
		default:
			return false;
	}
	return true;
}
