#include "LibraryApi.h"
#include "SettingsController.h"
#include "spotify/SpotifyUri.h"
#include "SpotifyUrl.h"

#include <algorithm>
#include <Path.h>
#include <utility>
#include <unistd.h>

static const size_t kLibraryBatchSize = 40;

static std::string
SpotifyUris(const std::string& type, const std::string& ids)
{
    std::string uris;
    size_t start = 0;
    while (start <= ids.size()) {
        size_t end = ids.find(',', start);
        std::string id = ids.substr(start,
            end == std::string::npos ? std::string::npos : end - start);
        if (!id.empty()) {
            if (!uris.empty())
                uris += ',';
            uris += "spotify:" + type + ":" + id;
        }
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return SpotifyUrlEncode(uris);
}

static bool
AudiobookIds(const std::vector<std::string>& uris, std::string& encodedIds)
{
    if (uris.empty())
        return false;
    std::string ids;
    for (const std::string& uri : uris) {
        if (SpotifyItemKindForUri(uri) != kSpotifyItemAudiobook)
            return false;
        std::string id = SpotifyItemIdForUri(uri);
        if (id.empty())
            return false;
        if (!ids.empty())
            ids += ',';
        ids += id;
    }
    encodedIds = SpotifyUrlEncode(ids);
    return true;
}

static bool
HaifyLibraryCachePath(BPath& path, bool createDirectories)
{
    std::string file = SettingsController::CacheFilePath("library",
        "liked-songs.json", createDirectories);
    return !file.empty() && path.SetTo(file.c_str()) == B_OK;
}

static void
DeleteLikedSongsCache()
{
    BPath path;
    if (HaifyLibraryCachePath(path, false))
        unlink(path.Path());
}

LibraryApi::LibraryApi(GetHandler get, BodyRequestHandler put,
    BodyRequestHandler deleteRequest, CacheHandler eraseCache,
    CacheHandler invalidateCachePrefix)
    : fGet(std::move(get)),
      fPut(std::move(put)),
      fDelete(std::move(deleteRequest)),
      fEraseCache(std::move(eraseCache)),
      fInvalidateCachePrefix(std::move(invalidateCachePrefix))
{
}

void
LibraryApi::GetSavedShows(int limit, JsonCallback callback)
{
    fGet("/me/shows?limit=" + std::to_string(limit), callback);
}

void
LibraryApi::InvalidateSavedShows()
{
    fInvalidateCachePrefix("/me/shows");
}

void
LibraryApi::FollowShow(const std::string& showId, JsonCallback callback)
{
    SaveLibraryItems({SpotifyUriForItemKind(kSpotifyItemShow, showId)},
        callback);
}

void
LibraryApi::UnfollowShow(const std::string& showId, JsonCallback callback)
{
    RemoveLibraryItems({SpotifyUriForItemKind(kSpotifyItemShow, showId)},
        callback);
}

void
LibraryApi::CheckFollowingShow(const std::string& showId,
    JsonCallback callback)
{
    CheckLibraryItems({SpotifyUriForItemKind(kSpotifyItemShow, showId)},
        callback);
}

void
LibraryApi::GetSavedAlbums(int limit, JsonCallback callback)
{
    fGet("/me/albums?limit=" + std::to_string(limit), callback);
}

void
LibraryApi::InvalidateSavedAlbums()
{
    fInvalidateCachePrefix("/me/albums");
}

void
LibraryApi::SaveAlbum(const std::string& albumId, JsonCallback callback)
{
    SaveLibraryItems({SpotifyUriForItemKind(kSpotifyItemAlbum, albumId)},
        callback);
}

void
LibraryApi::RemoveSavedAlbum(const std::string& albumId,
    JsonCallback callback)
{
    RemoveLibraryItems({SpotifyUriForItemKind(kSpotifyItemAlbum, albumId)},
        callback);
}

void
LibraryApi::CheckSavedAlbums(const std::string& ids, JsonCallback callback)
{
    std::string path = "/me/library/contains?uris="
        + SpotifyUris("album", ids);
    fEraseCache(path);
    fGet(path, callback);
}

void
LibraryApi::GetSavedTracks(int offset, int limit, JsonCallback callback)
{
    fGet("/me/tracks?limit=" + std::to_string(limit)
        + "&offset=" + std::to_string(offset), callback);
}

void
LibraryApi::InvalidateSavedTracks()
{
    DeleteLikedSongsCache();
    fInvalidateCachePrefix("/me/tracks");
}

void
LibraryApi::SaveTrack(const std::string& trackId, JsonCallback callback)
{
    DeleteLikedSongsCache();
    SaveLibraryItems({SpotifyUriForItemKind(kSpotifyItemTrack, trackId)},
        callback);
}

void
LibraryApi::RemoveSavedTrack(const std::string& trackId,
    JsonCallback callback)
{
    DeleteLikedSongsCache();
    RemoveLibraryItems({SpotifyUriForItemKind(kSpotifyItemTrack, trackId)},
        callback);
}

void
LibraryApi::CheckSavedTracks(const std::string& ids, JsonCallback callback)
{
    std::string path = "/me/library/contains?uris="
        + SpotifyUris("track", ids);
    fEraseCache(path);
    fGet(path, callback);
}

void
LibraryApi::GetSavedEpisodes(int offset, int limit, JsonCallback callback)
{
    fGet("/me/episodes?limit=" + std::to_string(limit)
        + "&offset=" + std::to_string(offset), callback);
}

void
LibraryApi::InvalidateSavedEpisodes()
{
    fInvalidateCachePrefix("/me/episodes");
}

void
LibraryApi::GetSavedAudiobooks(int offset, int limit, JsonCallback callback)
{
    std::string path = "/me/audiobooks?limit=" + std::to_string(limit)
        + "&offset=" + std::to_string(offset);
    fEraseCache(path);
    fGet(path, callback);
}

void
LibraryApi::InvalidateSavedAudiobooks()
{
    fInvalidateCachePrefix("/me/audiobooks");
}

void
LibraryApi::GetAllSavedAudiobooks(JsonCallback callback)
{
    _GetAllSavedAudiobooksPage(0, nlohmann::json::array(), callback);
}

void
LibraryApi::_GetAllSavedAudiobooksPage(int offset, nlohmann::json items,
    JsonCallback callback)
{
    const int limit = 50;
    std::string path = "/me/audiobooks?limit=" + std::to_string(limit)
        + "&offset=" + std::to_string(offset);
    fEraseCache(path);
    fGet(path, [this, offset, items = std::move(items), callback](bool ok,
            const nlohmann::json& data) mutable {
        if (!ok || !data.is_object() || !data.contains("items")
                || !data["items"].is_array()) {
            if (callback)
                callback(false, data);
            return;
        }

        for (const auto& item : data["items"])
            items.push_back(item);
        int count = (int)data["items"].size();
        int nextOffset = offset + count;
        int total = nextOffset;
        if (data.contains("total") && data["total"].is_number_integer())
            total = data["total"].get<int>();
        if (count > 0 && nextOffset < total) {
            _GetAllSavedAudiobooksPage(nextOffset, std::move(items), callback);
            return;
        }
        if (callback)
            callback(true, items);
    });
}

void
LibraryApi::SaveAudiobook(const std::string& audiobookId,
    JsonCallback callback)
{
    fInvalidateCachePrefix("/me/library");
    fInvalidateCachePrefix("/me/audiobooks");
    fPut("/me/audiobooks?ids=" + SpotifyUrlEncode(audiobookId), "",
        callback);
}

void
LibraryApi::RemoveSavedAudiobook(const std::string& audiobookId,
    JsonCallback callback)
{
    fInvalidateCachePrefix("/me/library");
    fInvalidateCachePrefix("/me/audiobooks");
    fDelete("/me/audiobooks?ids=" + SpotifyUrlEncode(audiobookId), "",
        callback);
}

void
LibraryApi::CheckSavedAudiobook(const std::string& audiobookId,
    JsonCallback callback)
{
    std::string path = "/me/audiobooks/contains?ids="
        + SpotifyUrlEncode(audiobookId);
    fEraseCache(path);
    fGet(path, callback);
}

void
LibraryApi::_LibraryRequestBatches(const std::string& method,
    const std::vector<std::string>& uris, size_t offset,
    nlohmann::json accumulated, JsonCallback callback)
{
    if (offset >= uris.size()) {
        if (callback)
            callback(true, accumulated);
        return;
    }

    size_t end = std::min(offset + kLibraryBatchSize, uris.size());
    std::string value;
    for (size_t i = offset; i < end; i++) {
        if (!value.empty())
            value += ',';
        value += uris[i];
    }
    std::string path = "/me/library?uris=" + SpotifyUrlEncode(value);
    if (method == "GET")
        path = "/me/library/contains?uris=" + SpotifyUrlEncode(value);

    auto complete = [this, method, uris, end, accumulated,
        callback](bool ok, const nlohmann::json& data) mutable {
        if (!ok) {
            if (callback)
                callback(false, data);
            return;
        }
        if (method == "GET" && data.is_array()) {
            if (!accumulated.is_array())
                accumulated = nlohmann::json::array();
            for (const auto& item : data)
                accumulated.push_back(item);
        } else {
            accumulated = data;
        }
        _LibraryRequestBatches(method, uris, end, accumulated, callback);
    };

    if (method == "GET")
        fGet(path, complete);
    else if (method == "PUT")
        fPut(path, "", complete);
    else
        fDelete(path, "", complete);
}

void
LibraryApi::_InvalidateLibraryCaches()
{
    fInvalidateCachePrefix("/me/library");
    fInvalidateCachePrefix("/me/albums");
    fInvalidateCachePrefix("/me/audiobooks");
    fInvalidateCachePrefix("/me/episodes");
    fInvalidateCachePrefix("/me/following");
    fInvalidateCachePrefix("/me/playlists");
    fInvalidateCachePrefix("/me/shows");
    fInvalidateCachePrefix("/me/tracks");
}

void
LibraryApi::SaveLibraryItems(const std::vector<std::string>& uris,
    JsonCallback callback)
{
    if (uris.empty()) {
        if (callback)
            callback(true, nlohmann::json::object());
        return;
    }
    for (const std::string& uri : uris) {
        if (!SpotifyLibraryUriIsValid(uri)) {
            if (callback) {
                callback(false, {{"status", 400},
                    {"error", "invalid_library_uri"}, {"uri", uri}});
            }
            return;
        }
    }
    _InvalidateLibraryCaches();
    std::string audiobookIds;
    if (AudiobookIds(uris, audiobookIds)) {
        fPut("/me/audiobooks?ids=" + audiobookIds, "", callback);
        return;
    }
    _LibraryRequestBatches("PUT", uris, 0, nlohmann::json::object(),
        callback);
}

void
LibraryApi::RemoveLibraryItems(const std::vector<std::string>& uris,
    JsonCallback callback)
{
    if (uris.empty()) {
        if (callback)
            callback(true, nlohmann::json::object());
        return;
    }
    for (const std::string& uri : uris) {
        if (!SpotifyLibraryUriIsValid(uri)) {
            if (callback) {
                callback(false, {{"status", 400},
                    {"error", "invalid_library_uri"}, {"uri", uri}});
            }
            return;
        }
    }
    _InvalidateLibraryCaches();
    std::string audiobookIds;
    if (AudiobookIds(uris, audiobookIds)) {
        fDelete("/me/audiobooks?ids=" + audiobookIds, "", callback);
        return;
    }
    _LibraryRequestBatches("DELETE", uris, 0, nlohmann::json::object(),
        callback);
}

void
LibraryApi::CheckLibraryItems(const std::vector<std::string>& uris,
    JsonCallback callback)
{
    if (uris.empty()) {
        if (callback)
            callback(true, nlohmann::json::array());
        return;
    }
    for (const std::string& uri : uris) {
        if (!SpotifyLibraryUriIsValid(uri)) {
            if (callback) {
                callback(false, {{"status", 400},
                    {"error", "invalid_library_uri"}, {"uri", uri}});
            }
            return;
        }
    }
    std::string audiobookIds;
    if (AudiobookIds(uris, audiobookIds)) {
        std::string path = "/me/audiobooks/contains?ids=" + audiobookIds;
        fEraseCache(path);
        fGet(path, callback);
        return;
    }
    _LibraryRequestBatches("GET", uris, 0, nlohmann::json::array(), callback);
}

void
LibraryApi::FollowArtist(const std::string& artistId, JsonCallback callback)
{
    SaveLibraryItems({SpotifyUriForItemKind(kSpotifyItemArtist, artistId)},
        callback);
}

void
LibraryApi::UnfollowArtist(const std::string& artistId, JsonCallback callback)
{
    RemoveLibraryItems({SpotifyUriForItemKind(kSpotifyItemArtist, artistId)},
        callback);
}

void
LibraryApi::CheckFollowingArtist(const std::string& artistId,
    JsonCallback callback)
{
    CheckLibraryItems({SpotifyUriForItemKind(kSpotifyItemArtist, artistId)},
        callback);
}

void
LibraryApi::InvalidateFollowedArtists()
{
    fInvalidateCachePrefix("/me/following");
}
