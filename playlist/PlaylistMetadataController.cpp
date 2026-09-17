#include "PlaylistMetadataController.h"

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

void
MapPlaylist(PlaylistMetadataResult& result, const nlohmann::json& data)
{
	result.snapshotId = String(data, "snapshot_id");
	result.description = String(data, "html_description", String(data, "description"));
	auto visibility = data.find("public");
	result.isPublic = visibility != data.end() && visibility->is_boolean()
		&& visibility->get<bool>();
	auto owner = data.find("owner");
	if (owner != data.end())
		result.ownerId = String(*owner, "account_id", String(*owner, "id"));
	// Preserve the existing container precedence and missing-total default.
	auto tracks = data.find("tracks");
	auto items = data.find("items");
	if (tracks != data.end() && tracks->is_object())
		result.total = Integer(*tracks, "total", 0);
	else if (items != data.end() && items->is_object())
		result.total = Integer(*items, "total", 0);
}

PlaylistMetadataResult
MapResult(const PlaylistMetadataRequest& request, bool ok,
	const nlohmann::json& data)
{
	PlaylistMetadataResult result;
	result.request = request;
	result.status = Integer(data, "status", -1);
	result.retryAfter = Integer(data, "retry_after", -1);
	if (!ok)
		return result;
	result.responseValid = data.is_object();
	if (!result.responseValid)
		return result;
	result.ok = true;
	if (request.kind == PlaylistMetadataKind::CurrentUser) {
		result.legacyUserId = String(data, "id");
		result.userId = String(data, "account_id", result.legacyUserId);
		return result;
	}
	const char* fallback = request.kind == PlaylistMetadataKind::Playlist
		? "Playlist" : request.kind == PlaylistMetadataKind::Album ? "Album" : "Podcast";
	result.title = String(data, "name", fallback);
	auto images = data.find("images");
	if (images != data.end() && images->is_array() && !images->empty())
		result.coverUrl = String((*images)[0], "url");
	if (request.kind == PlaylistMetadataKind::Playlist)
		MapPlaylist(result, data);
	return result;
}
}

PlaylistMetadataController::PlaylistMetadataController(Getter playlist,
	Getter album, Getter podcast, ProfileGetter profile)
	: fPlaylist(std::move(playlist)), fAlbum(std::move(album)),
	  fPodcast(std::move(podcast)), fProfile(std::move(profile))
{
}

bool
PlaylistMetadataController::Load(const PlaylistMetadataRequest& request,
	Completion done) const
{
	auto callback = [request, done](bool ok, const nlohmann::json& data) {
		if (done)
			done(MapResult(request, ok, data));
	};
	if (request.kind == PlaylistMetadataKind::CurrentUser) {
		if (!fProfile)
			return false;
		fProfile(callback);
		return true;
	}
	if (request.id.empty())
		return false;
	const Getter* getter = nullptr;
	switch (request.kind) {
		case PlaylistMetadataKind::Playlist: getter = &fPlaylist; break;
		case PlaylistMetadataKind::Album: getter = &fAlbum; break;
		case PlaylistMetadataKind::Podcast: getter = &fPodcast; break;
		default: return false;
	}
	if (!*getter)
		return false;
	(*getter)(request.id, callback);
	return true;
}

bool
PlaylistMetadataState::Apply(const PlaylistMetadataResult& result)
{
	if (!result.ok || !result.responseValid)
		return false;
	if (result.request.kind == PlaylistMetadataKind::CurrentUser) {
		fUserId = result.userId;
		fLegacyUserId = result.legacyUserId;
		return true;
	}
	if (result.request.kind != PlaylistMetadataKind::Playlist)
		return false;
	fOwnerId = result.ownerId;
	UpdateDetails(result.description, result.isPublic);
	return true;
}

void
PlaylistMetadataState::UpdateDetails(const std::string& description, bool isPublic)
{
	fDescription = description;
	fPublic = isPublic;
}

bool
PlaylistMetadataState::IsOwned() const
{
	// Two missing IDs must never grant edit rights.
	return !fOwnerId.empty() && !fUserId.empty()
		&& (fUserId == fOwnerId || fLegacyUserId == fOwnerId);
}
