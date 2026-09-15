#include "DiscoverRowFactory.h"

#include "spotify/SpotifyUri.h"
#include "spotify/SpotifyPlaylistPolicy.h"
#include "DiscoverTabPolicy.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <utility>

namespace {

std::string
JsonString(const nlohmann::json& object, const char* key, const char* fallback = "")
{
	if (!object.is_object() || !object.contains(key) || !object[key].is_string())
		return fallback;
	return object[key].get<std::string>();
}

int32_t
JsonInt32(const nlohmann::json& object, const char* key)
{
	if (!object.is_object() || !object.contains(key)
			|| !object[key].is_number_integer())
		return 0;
	return object[key].get<int32_t>();
}

std::string
DurationText(int32_t milliseconds)
{
	int32_t seconds = std::max(int32_t(0), milliseconds) / 1000;
	char text[32];
	std::snprintf(text, sizeof(text), "%ld:%02ld", (long)(seconds / 60),
		(long)(seconds % 60));
	return text;
}

std::string
EpisodeProgress(const nlohmann::json& episode, bool showProgress,
	const std::string& doneLabel)
{
	if (!showProgress || !episode.contains("resume_point")
			|| !episode["resume_point"].is_object())
		return "";
	const auto& resume = episode["resume_point"];
	if (resume.contains("fully_played") && resume["fully_played"].is_boolean()
			&& resume["fully_played"].get<bool>())
		return doneLabel;
	return DurationText(JsonInt32(resume, "resume_position_ms"));
}

DiscoverRowData
SavedEpisodeRow(const nlohmann::json& episode, bool showProgress,
	const std::string& doneLabel)
{
	std::string showName;
	std::string showUri;
	if (episode.contains("show") && episode["show"].is_object()) {
		showName = JsonString(episode["show"], "name");
		std::string showId = JsonString(episode["show"], "id");
		showUri = !showId.empty()
			? SpotifyUriForItemKind(kSpotifyItemShow, showId)
			: JsonString(episode["show"], "uri");
	}
	std::string episodeId = JsonString(episode, "id");
	std::string episodeUri = !episodeId.empty()
		? SpotifyUriForItemKind(kSpotifyItemEpisode, episodeId)
		: JsonString(episode, "uri");
	std::string episodeName = JsonString(episode, "name");
	return {{episodeName, showName, JsonString(episode, "release_date"),
		DurationText(JsonInt32(episode, "duration_ms")),
		EpisodeProgress(episode, showProgress, doneLabel)},
		{episodeUri, showUri, "", "", ""},
		{episodeName, showName, "", "", ""}};
}

bool
BuildResolvedAlbumRow(const std::string& uri, const nlohmann::json& item,
	const std::string& name, DiscoverRowData& row)
{
	std::string artist = "Unknown";
	std::string artistUri;
	if (item.contains("artists") && item["artists"].is_array()
			&& !item["artists"].empty() && item["artists"][0].is_object()) {
		artist = JsonString(item["artists"][0], "name", "Unknown");
		artistUri = JsonString(item["artists"][0], "uri");
	}
	row.vals = {name, artist};
	row.uris = {uri, artistUri};
	row.ttls = {name, artist};
	return true;
}

bool
BuildResolvedPodcastRow(const std::string& uri, const nlohmann::json& item,
	const std::string& name, DiscoverRowData& row)
{
	row.vals = {name, JsonString(item, "publisher", "Unknown")};
	row.uris = {uri, ""};
	row.ttls = {name, ""};
	return true;
}

bool
BuildResolvedArtistRow(const std::string& uri, const nlohmann::json& item,
	const std::string& name, DiscoverRowData& row)
{
	std::string genre = "Artist";
	if (item.contains("genres") && item["genres"].is_array()
			&& !item["genres"].empty() && item["genres"][0].is_string()) {
		genre = item["genres"][0].get<std::string>();
	}
	row.vals = {name, genre};
	row.uris = {uri, ""};
	row.ttls = {name, ""};
	return true;
}

bool
BuildResolvedEpisodeRow(const std::string& uri, const nlohmann::json& item,
	const std::string& name, bool showProgress, const std::string& doneLabel,
	DiscoverRowData& row)
{
	if (name.empty())
		return false;
	DiscoverRowData mapped = SavedEpisodeRow(item, showProgress, doneLabel);
	row.vals = std::move(mapped.vals);
	row.uris = std::move(mapped.uris);
	row.ttls = std::move(mapped.ttls);
	// A library delta identifies the requested item, even if the body has another ID.
	row.uris[0] = uri;
	return true;
}

bool
BuildResolvedAudiobookRow(const std::string& uri, const nlohmann::json& item,
	const std::string& name, DiscoverRowData& row)
{
	std::string author;
	if (item.contains("authors") && item["authors"].is_array()
			&& !item["authors"].empty() && item["authors"][0].is_object()) {
		author = JsonString(item["authors"][0], "name");
	}
	row.vals = {name, author};
	row.uris = {uri, ""};
	row.ttls = {name, ""};
	return true;
}

DiscoverRowData
TrackLikeDiscoverRow(const nlohmann::json& item)
{
	std::string name = item.value("name", "Unknown");
	std::string artist = "Unknown";
	std::string artistUri;
	if (item.contains("artists") && item["artists"].is_array()
			&& !item["artists"].empty()) {
		artist = item["artists"][0].value("name", "Unknown");
		artistUri = item["artists"][0].value("uri", "");
	}
	return {{name, artist}, {item.value("uri", ""), artistUri},
		{name, artist}};
}

} // namespace

std::optional<DiscoverRowData>
DiscoverRowFactory::CreatedPlaylist(const nlohmann::json& response, const std::string& requestedName)
{
	nlohmann::json item = response;
	if (response.is_object() && response.contains("body") && response["body"].is_string())
		item = nlohmann::json::parse(response["body"].get<std::string>(), nullptr, false);
	std::string id = JsonString(item, "id");
	if (id.empty())
		return std::nullopt;
	std::string name = JsonString(item, "name", requestedName.c_str());
	std::string owner = "Spotify";
	if (item.contains("owner") && item["owner"].is_object())
		owner = JsonString(item["owner"], "display_name", "Spotify");
	return DiscoverRowData{{name, owner}, {SpotifyUriForItemKind(kSpotifyItemPlaylist, id), ""},
		{name, ""}, true, true};
}

std::optional<std::vector<DiscoverRowData>>
DiscoverRowFactory::PlaylistRows(const nlohmann::json& data, const std::string& accountId)
{
	if (!data.is_object() || !data.contains("items") || !data["items"].is_array())
		return std::nullopt;
	std::vector<DiscoverRowData> rows;
	for (const auto& item : data["items"]) {
		std::string uri = JsonString(item, "uri");
		if (uri.empty())
			continue;
		std::string owner = "Spotify";
		std::string ownerAccountId;
		std::string ownerLegacyId;
		if (item.contains("owner") && item["owner"].is_object()) {
			owner = JsonString(item["owner"], "display_name", "Spotify");
			ownerAccountId = JsonString(item["owner"], "account_id");
			ownerLegacyId = JsonString(item["owner"], "id");
		}
		bool owned = !accountId.empty()
			&& (ownerAccountId == accountId || ownerLegacyId == accountId);
		bool collaborative = item.contains("collaborative") && item["collaborative"].is_boolean()
			&& item["collaborative"].get<bool>();
		bool writable = SpotifyPlaylistIsWritable(collaborative, ownerAccountId,
			ownerLegacyId, accountId);
		std::string name = JsonString(item, "name", "Unknown");
		rows.push_back({{name, owner}, {uri, ""}, {name, ""}, writable, owned});
	}
	return rows;
}

std::optional<std::vector<DiscoverRowData>>
DiscoverRowFactory::SavedEpisodeRows(const nlohmann::json& data,
	bool showProgress, const std::string& doneLabel)
{
	if (!data.is_object() || !data.contains("items") || !data["items"].is_array())
		return std::nullopt;
	std::vector<DiscoverRowData> rows;
	for (const auto& saved : data["items"]) {
		if (!saved.is_object() || !saved.contains("episode")
				|| !saved["episode"].is_object())
			continue;
		const auto& episode = saved["episode"];
		if (JsonString(episode, "type") != "episode")
			continue;
		DiscoverRowData row = SavedEpisodeRow(episode, showProgress, doneLabel);
		if (!row.vals[0].empty()
				&& SpotifyItemKindForUri(row.uris[0]) == kSpotifyItemEpisode
				&& !SpotifyItemIdForUri(row.uris[0]).empty())
			rows.push_back(std::move(row));
	}
	return rows;
}

bool
DiscoverRowFactory::BuildResolvedLibraryRow(int32_t tab, const std::string& uri,
	const nlohmann::json& item, bool showProgress, const std::string& doneLabel, DiscoverRowData& row)
{
	std::string name = JsonString(item, "name");
	if (tab != TAB_SAVED_EPISODES && name.empty())
		name = "Unknown";
	if (tab == TAB_SAVED_ALBUMS)
		return BuildResolvedAlbumRow(uri, item, name, row);
	if (tab == TAB_PODCASTS)
		return BuildResolvedPodcastRow(uri, item, name, row);
	if (tab == TAB_FOLLOWED_ARTISTS)
		return BuildResolvedArtistRow(uri, item, name, row);
	if (tab == TAB_SAVED_EPISODES)
		return BuildResolvedEpisodeRow(uri, item, name, showProgress, doneLabel, row);
	if (tab == TAB_AUDIOBOOKS)
		return BuildResolvedAudiobookRow(uri, item, name, row);
	return false;
}

std::vector<DiscoverRowData>
DiscoverRowFactory::TopTrackRows(const nlohmann::json& data)
{
	std::vector<DiscoverRowData> rows;
	if (!data.contains("items"))
		return rows;
	for (const auto& item : data["items"]) {
		if (item.is_object())
			rows.push_back(TrackLikeDiscoverRow(item));
	}
	return rows;
}

std::vector<DiscoverRowData>
DiscoverRowFactory::TopArtistRows(const nlohmann::json& data)
{
	std::vector<DiscoverRowData> rows;
	if (!data.contains("items"))
		return rows;
	for (const auto& item : data["items"]) {
		if (!item.is_object())
			continue;
		std::string name = item.value("name", "Unknown");
		std::string genre = "Artist";
		if (item.contains("genres") && item["genres"].is_array()
				&& !item["genres"].empty())
			genre = item["genres"][0].get<std::string>();
		rows.push_back({{name, genre}, {item.value("uri", ""), ""},
			{name, ""}});
	}
	return rows;
}

std::vector<DiscoverRowData>
DiscoverRowFactory::NewReleaseRows(const nlohmann::json& data)
{
	std::vector<DiscoverRowData> rows;
	if (!data.contains("albums") || !data["albums"].contains("items"))
		return rows;
	for (const auto& item : data["albums"]["items"]) {
		if (item.is_object())
			rows.push_back(TrackLikeDiscoverRow(item));
	}
	return rows;
}

std::vector<DiscoverRowData>
DiscoverRowFactory::SavedAlbumRows(const nlohmann::json& data)
{
	std::vector<DiscoverRowData> rows;
	if (!data.contains("items"))
		return rows;
	for (const auto& item : data["items"]) {
		if (item.contains("album") && item["album"].is_object())
			rows.push_back(TrackLikeDiscoverRow(item["album"]));
	}
	return rows;
}

std::vector<DiscoverRowData>
DiscoverRowFactory::PodcastRows(const nlohmann::json& data,
	const std::set<std::string>& audiobookIds)
{
	std::vector<DiscoverRowData> rows;
	if (!data.contains("items") || !data["items"].is_array())
		return rows;
	for (const auto& item : data["items"]) {
		if (!item.is_object() || !item.contains("show")
				|| !item["show"].is_object())
			continue;
		const auto& show = item["show"];
		std::string id = JsonString(show, "id");
		std::string uri = JsonString(show, "uri");
		if (id.empty())
			id = SpotifyItemIdForUri(uri);
		if (JsonString(show, "type") != "show"
				|| !PrimaryUriMatchesTab(TAB_PODCASTS, uri)
				|| (!id.empty() && audiobookIds.find(id) != audiobookIds.end())) {
			continue;
		}
		std::string name = show.value("name", "Unknown");
		std::string publisher = show.value("publisher", "Unknown");
		rows.push_back({{name, publisher}, {uri, ""}, {name, ""}});
	}
	return rows;
}

std::vector<DiscoverRowData>
DiscoverRowFactory::FollowedArtistRows(const nlohmann::json& data)
{
	std::vector<DiscoverRowData> rows;
	if (!data.contains("artists") || !data["artists"].is_object()
			|| !data["artists"].contains("items")
			|| !data["artists"]["items"].is_array())
		return rows;
	for (const auto& item : data["artists"]["items"]) {
		if (!item.is_object())
			continue;
		std::string name = item.contains("name") && item["name"].is_string()
			? item["name"].get<std::string>() : "Unknown";
		std::string uri = item.contains("uri") && item["uri"].is_string()
			? item["uri"].get<std::string>() : "";
		std::string genre = "Artist";
		if (item.contains("genres") && item["genres"].is_array()
				&& !item["genres"].empty() && item["genres"][0].is_string())
			genre = item["genres"][0].get<std::string>();
		rows.push_back({{name, genre}, {uri, ""}, {name, ""}});
	}
	return rows;
}

std::vector<DiscoverRowData>
DiscoverRowFactory::AudiobookRows(const nlohmann::json& data)
{
	std::vector<DiscoverRowData> rows;
	if (!data.contains("items") || !data["items"].is_array())
		return rows;
	for (const auto& book : data["items"]) {
		if (!book.is_object() || JsonString(book, "type") != "audiobook")
			continue;
		std::string id = JsonString(book, "id");
		std::string uri = !id.empty()
			? SpotifyUriForItemKind(kSpotifyItemAudiobook, id)
			: JsonString(book, "uri");
		if (!PrimaryUriMatchesTab(TAB_AUDIOBOOKS, uri))
			continue;
		std::string author;
		if (book.contains("authors") && book["authors"].is_array()
				&& !book["authors"].empty()
				&& book["authors"][0].is_object())
			author = JsonString(book["authors"][0], "name");
		std::string name = JsonString(book, "name", "Unknown");
		rows.push_back({{name, author}, {uri, ""}, {name, ""}});
	}
	return rows;
}
