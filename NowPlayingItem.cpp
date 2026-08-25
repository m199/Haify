#include "NowPlayingItem.h"
#include "UiLogic.h"

#include <Message.h>

namespace {

std::string
JsonString(const nlohmann::json& object, const char* key)
{
	if (!object.is_object() || !object.contains(key)
			|| !object[key].is_string()) {
		return "";
	}
	return object[key].get<std::string>();
}


std::string
SpotifyUri(const char* prefix, const std::string& id)
{
	if (id.empty())
		return "";
	return std::string(prefix) + id;
}


std::string
FirstImageUrl(const nlohmann::json& object)
{
	if (!object.is_object() || !object.contains("images")
			|| !object["images"].is_array() || object["images"].empty()) {
		return "";
	}
	return object["images"][0].value("url", "");
}


void
AddString(BMessage& message, const char* field, const std::string& value)
{
	message.AddString(field, value.c_str());
}


void
MapTrackParent(NowPlayingItem& result, const nlohmann::json& item)
{
	if (item.contains("artists") && item["artists"].is_array()
			&& !item["artists"].empty()) {
		const nlohmann::json& artist = item["artists"][0];
		result.displaySubtitle = JsonString(artist, "name");
		result.artistId = JsonString(artist, "id");
	}

	if (!item.contains("album") || !item["album"].is_object())
		return;

	const nlohmann::json& album = item["album"];
	result.albumId = JsonString(album, "id");
	result.imageUrl = FirstImageUrl(album);
	result.parentKind = "album";
	result.parentUri = JsonString(album, "uri");
	if (result.parentUri.empty())
		result.parentUri = SpotifyUri("spotify:album:", result.albumId);
	result.primaryOpenUri = result.parentUri;
}


void
MapEpisodeParent(NowPlayingItem& result, const nlohmann::json& item)
{
	if (item.contains("audiobook") && item["audiobook"].is_object()) {
		const nlohmann::json& audiobook = item["audiobook"];
		result.audiobookId = JsonString(audiobook, "id");
		result.displaySubtitle = JsonString(audiobook, "name");
		result.parentKind = "audiobook";
		result.parentUri = JsonString(audiobook, "uri");
		if (result.parentUri.empty()) {
			result.parentUri = SpotifyUri("spotify:audiobook:",
				result.audiobookId);
		}
		result.primaryOpenUri = result.parentUri;
		if (result.imageUrl.empty())
			result.imageUrl = FirstImageUrl(audiobook);
		return;
	}

	if (item.contains("show") && item["show"].is_object()) {
		const nlohmann::json& show = item["show"];
		result.showId = JsonString(show, "id");
		result.displaySubtitle = JsonString(show, "name");
		if (result.displaySubtitle.empty())
			result.displaySubtitle = JsonString(show, "publisher");
		result.parentKind = "show";
		result.parentUri = JsonString(show, "uri");
		if (result.parentUri.empty())
			result.parentUri = SpotifyUri("spotify:show:", result.showId);
		result.primaryOpenUri = result.parentUri;
		if (result.imageUrl.empty())
			result.imageUrl = FirstImageUrl(show);
	}
}

}


void
NowPlayingItem::AddToMessage(BMessage& message) const
{
	AddString(message, "title", displayTitle);
	message.AddInt32("duration_ms", durationMs);
	AddString(message, "track_uri", itemUri);
	AddString(message, "artist", displaySubtitle);
	AddString(message, "album_id", albumId);
	AddString(message, "artist_id", artistId);
	AddString(message, "artwork_url", imageUrl);
	AddString(message, kNowPlayingItemKindField, itemKind);
	AddString(message, kNowPlayingPrimaryOpenUriField, primaryOpenUri);
	AddString(message, kNowPlayingParentUriField, parentUri);
	AddString(message, kNowPlayingParentKindField, parentKind);
	AddString(message, kNowPlayingShowIdField, showId);
	AddString(message, kNowPlayingAudiobookIdField, audiobookId);
}


NowPlayingItem
NowPlayingItem::FromSpotifyItem(const nlohmann::json& item,
	const std::string& fallbackType)
{
	NowPlayingItem result;
	if (!item.is_object())
		return result;

	result.displayTitle = JsonString(item, "name");
	result.durationMs = (int32)item.value("duration_ms", 0);
	result.itemUri = JsonString(item, "uri");
	result.itemKind = JsonString(item, "type");
	if (result.itemKind.empty())
		result.itemKind = fallbackType;
	if (result.itemKind.empty()) {
		SpotifyItemKind uriKind = SpotifyItemKindForUri(result.itemUri);
		if (uriKind != kSpotifyItemUnknown)
			result.itemKind = SpotifyItemTypeName(uriKind);
	}
	result.primaryOpenUri = result.itemUri;
	result.imageUrl = FirstImageUrl(item);

	if (result.itemKind == "track")
		MapTrackParent(result, item);
	else if (result.itemKind == "episode" || result.itemKind == "chapter")
		MapEpisodeParent(result, item);

	if (result.primaryOpenUri.empty())
		result.primaryOpenUri = result.parentUri.empty()
			? result.itemUri : result.parentUri;

	return result;
}
