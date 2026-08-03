#include "SpotifyApi.h"
#include "Config.h"
#include "HttpClient.h"
#include "HaifyDebug.h"
#include "SettingsController.h"
#include "UiLogic.h"

#include <algorithm>
#include <Autolock.h>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <Path.h>
#include <set>
#include <utility>
#include <unistd.h>

static std::string
UrlEncode(const std::string& value)
{
    std::string encoded;
    encoded.reserve(value.size() * 3);
    for (unsigned char c : value) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += (char)c;
        } else {
            char buffer[4];
            snprintf(buffer, sizeof(buffer), "%%%02X", c);
            encoded += buffer;
        }
    }
    return encoded;
}

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
    return UrlEncode(uris);
}

static nlohmann::json
MutationResponse(int status, const std::string& body, int retryAfter,
                 bool& ok)
{
    ok = status >= 200 && status < 300;
    if (!ok || body.empty()) {
        return {{"status", status}, {"body", body},
            {"retry_after", retryAfter}};
    }

    try {
        return nlohmann::json::parse(body);
    } catch (...) {
        ok = false;
        return {{"status", status}, {"body", body},
            {"retry_after", retryAfter}, {"error", "invalid_json"}};
    }
}


static bool
AudiobookIds(const std::vector<std::string>& uris, std::string& encodedIds)
{
    if (uris.empty())
        return false;
    const std::string prefix = "spotify:audiobook:";
    std::string ids;
    for (const std::string& uri : uris) {
        if (uri.rfind(prefix, 0) != 0 || uri.size() <= prefix.size())
            return false;
        if (!ids.empty()) ids += ',';
        ids += uri.substr(prefix.size());
    }
    encodedIds = UrlEncode(ids);
    return true;
}

static bool
HaifyLibraryCachePath(BPath& path, bool createDirectories)
{
    std::string file = SettingsController::CacheFilePath("library",
        "liked-songs.json", createDirectories);
    return !file.empty() && path.SetTo(file.c_str()) == B_OK;
}

static bool
HaifyPlaylistCachePath(const std::string& playlistId, BPath& path,
                       bool createDirectories)
{
    std::string file = SettingsController::CacheFilePath("playlists",
        playlistId + ".json", createDirectories);
    return !file.empty() && path.SetTo(file.c_str()) == B_OK;
}

static void
DeleteLikedSongsCache()
{
    BPath path;
    if (HaifyLibraryCachePath(path, false))
        unlink(path.Path());
}

static void
DeletePlaylistCache(const std::string& playlistId)
{
    BPath path;
    if (HaifyPlaylistCachePath(playlistId, path, false))
        unlink(path.Path());
}

SpotifyApi::SpotifyApi(const std::string& accessToken)
    : fAccessToken(accessToken), fLock("Spotify API") {}

void SpotifyApi::SetAccessToken(const std::string& token)
{
    BAutolock lock(&fLock);
    if (fAccessToken != token) {
        fCache.clear();
        fCachedPlaylists.clear();
    }
    fAccessToken = token;
}

void SpotifyApi::SetAccountId(const std::string& accountId)
{
    BAutolock lock(&fLock);
    if (fAccountId == accountId)
        return;
    fAccountId = accountId;
    fCache.clear();
    fCachedPlaylists.clear();
}

void SpotifyApi::SetTokenRefreshHandler(TokenRefreshHandler handler)
{
    BAutolock lock(&fLock);
    fTokenRefreshHandler = std::move(handler);
}

void SpotifyApi::SetRequestHandler(RequestHandler handler)
{
    BAutolock lock(&fLock);
    fRequestHandler = std::move(handler);
}

void SpotifyApi::ClearSession()
{
    BAutolock lock(&fLock);
    fAccessToken.clear();
    fAccountId.clear();
    fCache.clear();
    fPendingGets.clear();
    fCachedPlaylists.clear();
}

int SpotifyApi::ResponseStatus(const nlohmann::json& data)
{
    return data.is_object() ? data.value("status", -1) : -1;
}

int SpotifyApi::ResponseRetryAfter(const nlohmann::json& data)
{
    return data.is_object() ? data.value("retry_after", -1) : -1;
}

std::string SpotifyApi::ResponseErrorReason(const nlohmann::json& data)
{
    if (!data.is_object())
        return "invalid_response";
    if (data.contains("reason") && data["reason"].is_string())
        return data["reason"].get<std::string>();
    if (data.contains("error") && data["error"].is_string())
        return data["error"].get<std::string>();
    if (data.contains("error") && data["error"].is_object()) {
        const auto& error = data["error"];
        if (error.contains("reason") && error["reason"].is_string())
            return error["reason"].get<std::string>();
        if (error.contains("message") && error["message"].is_string())
            return error["message"].get<std::string>();
    }
    if (data.contains("body") && data["body"].is_string()) {
        try {
            nlohmann::json body = nlohmann::json::parse(
                data["body"].get<std::string>());
            return ResponseErrorReason(body);
        } catch (...) {
        }
    }
    return "spotify_request_failed";
}

bool SpotifyApi::IsTemporaryFailure(const nlohmann::json& data)
{
    int status = ResponseStatus(data);
    return status < 0 || status == 401 || status == 408 || status == 425
        || status == 429 || status >= 500;
}

std::vector<std::pair<std::string, std::string>>
SpotifyApi::GetCachedPlaylists() const
{
    BAutolock lock(&fLock);
    return fCachedPlaylists;
}

void SpotifyApi::_Request(const std::string& method, const std::string& path,
    const std::string& body, RawCallback callback, bool allowRefresh,
    const std::string& contentType)
{
    std::string token;
    TokenRefreshHandler refreshHandler;
    RequestHandler requestHandler;
    {
        BAutolock lock(&fLock);
        token = fAccessToken;
        refreshHandler = fTokenRefreshHandler;
        requestHandler = fRequestHandler;
    }

    Headers headers = {{"Authorization", "Bearer " + token}};
    if (!body.empty() && !contentType.empty())
        headers["Content-Type"] = contentType;

    auto complete = [this, method, path, body, callback, allowRefresh,
        contentType, refreshHandler](int status, const std::string& responseBody,
            int retryAfter) {
        if (status < 200 || status >= 300) {
            DEBUG_PRINT("SpotifyApi %s %s -> %d: %.200s\n", method.c_str(),
                path.c_str(), status, responseBody.c_str());
        }
        if (status == 401 && allowRefresh && refreshHandler) {
            refreshHandler([this, method, path, body, callback,
                contentType](bool ok) {
                if (ok)
                    _Request(method, path, body, callback, false, contentType);
                else if (callback)
                    callback(401, "{\"error\":\"token_refresh_failed\"}", -1);
            });
            return;
        }
        if (callback)
            callback(status, responseBody, retryAfter);
    };

    if (requestHandler) {
        requestHandler(method, path, body, contentType, complete);
        return;
    }

    auto response = [complete](const HttpResponse& httpResponse) {
        complete(httpResponse.statusCode, httpResponse.body,
            httpResponse.retryAfter);
    };

    std::string url = std::string(SPOTIFY_API_BASE) + path;
    if (method == "GET")
        HttpClient::Get(url, headers, response);
    else if (method == "POST")
        HttpClient::Post(url, headers, body, response);
    else if (method == "PUT")
        HttpClient::Put(url, headers, body, response);
    else if (method == "DELETE")
        HttpClient::Delete(url, headers, body, response);
    else if (callback)
        callback(-1, "{\"error\":\"unsupported_http_method\"}", -1);
}

void SpotifyApi::_InvalidateCachePrefix(const std::string& prefix)
{
    std::string accountPrefix = _CacheKey("");
    BAutolock lock(&fLock);
    for (auto it = fCache.begin(); it != fCache.end(); ) {
        if (it->first.rfind(accountPrefix + prefix, 0) == 0)
            it = fCache.erase(it);
        else
            ++it;
    }
}

void SpotifyApi::InvalidateCachePrefix(const std::string& prefix)
{
    _InvalidateCachePrefix(prefix);
}

void SpotifyApi::_EraseCache(const std::string& path)
{
    std::string key = _CacheKey(path);
    BAutolock lock(&fLock);
    fCache.erase(key);
}

std::string SpotifyApi::_CacheKey(const std::string& path) const
{
    BAutolock lock(&fLock);
    return (fAccountId.empty() ? "session" : fAccountId) + "|" + path;
}

void SpotifyApi::Get(const std::string& path, JsonCallback callback)
{
    std::string cacheKey = _CacheKey(path);
    {
        BAutolock lock(&fLock);
        auto cached = fCache.find(cacheKey);
        if (cached != fCache.end()
            && time(NULL) - cached->second.timestamp < 3600) {
            nlohmann::json data = cached->second.data;
            lock.Unlock();
            if (callback) callback(true, data);
            return;
        }
        auto pending = fPendingGets.find(cacheKey);
        if (pending != fPendingGets.end()) {
            if (callback) pending->second.push_back(callback);
            return;
        }
        fPendingGets[cacheKey] = {};
        if (callback) fPendingGets[cacheKey].push_back(callback);
    }

    _Request("GET", path, "",
        [this, cacheKey](int status, const std::string& body, int retryAfter) {
            bool ok = false;
            nlohmann::json result;
            if (status >= 200 && status < 300) {
                try {
                    result = body.empty()
                        ? nlohmann::json::object()
                        : nlohmann::json::parse(body);
                    {
                        BAutolock lock(&fLock);
                        fCache[cacheKey] = { result, time(NULL) };
                    }
                    ok = true;
                } catch (...) {
                    result = {{"status", status}, {"error", "invalid_json"}};
                }
            } else {
                result = {{"status", status}, {"body", body},
                    {"retry_after", retryAfter}};
            }
            std::vector<JsonCallback> callbacks;
            {
                BAutolock lock(&fLock);
                auto pending = fPendingGets.find(cacheKey);
                if (pending != fPendingGets.end()) {
                    callbacks.swap(pending->second);
                    fPendingGets.erase(pending);
                }
            }
            for (const JsonCallback& current : callbacks)
                if (current) current(ok, result);
        });
}

void SpotifyApi::Put(const std::string& path, const std::string& body, JsonCallback callback)
{
    _Request("PUT", path, body,
        [path, callback](int status, const std::string& resp, int retryAfter) {
            if (path.rfind("/me/player/volume?", 0) != 0) {
                DEBUG_PRINT("SpotifyApi::Put %s -> %d: %.200s\n",
                    path.c_str(), status, resp.c_str());
            }
            bool ok;
            nlohmann::json result = MutationResponse(status, resp,
                retryAfter, ok);
            if (callback) callback(ok, result);
        });
}

void SpotifyApi::Post(const std::string& path, const std::string& body, JsonCallback callback)
{
    _Request("POST", path, body,
        [callback](int status, const std::string& resp, int retryAfter) {
            bool ok;
            nlohmann::json result = MutationResponse(status, resp,
                retryAfter, ok);
            if (callback) callback(ok, result);
        });
}

void SpotifyApi::GetPlaybackState(JsonCallback callback)  {
    _EraseCache("/me/player?additional_types=episode");
    Get("/me/player?additional_types=episode", callback);
}
void SpotifyApi::GetCurrentlyPlaying(JsonCallback callback) {
    _EraseCache("/me/player/currently-playing?additional_types=episode");
    Get("/me/player/currently-playing?additional_types=episode", callback);
}
void SpotifyApi::Play(JsonCallback callback)              { Put("/me/player/play", "", callback); }
void SpotifyApi::PlayTrack(const std::string& trackUri,
                           const std::string& contextUri,
                           JsonCallback callback)
{
    std::string body;
    bool supportsOffset = !contextUri.empty()
        && contextUri.find("spotify:artist:") != 0
        && contextUri.find("spotify:show:") != 0
        && trackUri.find("spotify:episode:") != 0;
    if (supportsOffset) {
        body = nlohmann::json({
            {"context_uri", contextUri},
            {"offset", {{"uri", trackUri}}}
        }).dump();
    } else {
        body = nlohmann::json({{"uris", {trackUri}}}).dump();
    }
    Put("/me/player/play", body, callback);
}
void SpotifyApi::PlayContext(const std::string& contextUri, JsonCallback callback)
{
    std::string body = nlohmann::json({{"context_uri", contextUri}}).dump();
    Put("/me/player/play", body, callback);
}
void SpotifyApi::Pause(JsonCallback callback)             { Put("/me/player/pause", "", callback); }
void SpotifyApi::Next(JsonCallback callback)              { Post("/me/player/next", "", callback); }
void SpotifyApi::GetQueue(JsonCallback callback) {
    _EraseCache("/me/player/queue");
    Get("/me/player/queue", callback);
}
void SpotifyApi::AddToQueue(const std::string& uri, JsonCallback callback) {
    std::string path = "/me/player/queue?uri=" + UrlEncode(uri);
    Post(path, "", callback);
}
void SpotifyApi::Previous(JsonCallback callback)          { Post("/me/player/previous", "", callback); }
void SpotifyApi::GetDevices(JsonCallback callback)
{
    _EraseCache("/me/player/devices");
    Get("/me/player/devices", callback);
}

void SpotifyApi::TransferPlayback(const std::string& deviceId, JsonCallback callback)
{
    std::string body = nlohmann::json({
        {"device_ids", {deviceId}}, {"play", true}
    }).dump();
    Put("/me/player", body, callback);
}

void SpotifyApi::SetShuffle(bool on, JsonCallback callback)
{
    Put(std::string("/me/player/shuffle?state=") + (on ? "true" : "false"),
        "", callback);
}

void SpotifyApi::SetRepeat(const std::string& mode, JsonCallback callback)
{
    Put("/me/player/repeat?state=" + mode, "", callback);
}

void SpotifyApi::GetPlaylists(JsonCallback callback)
{
    _GetPlaylistsPage(0, nlohmann::json::array(), callback);
}

void SpotifyApi::_GetPlaylistsPage(int offset, nlohmann::json items,
                                   JsonCallback callback)
{
    Get("/me/playlists?limit=50&offset=" + std::to_string(offset),
        [this, offset, items, callback](bool ok,
            const nlohmann::json& data) mutable {
        if (!ok || !data.contains("items") || !data["items"].is_array()) {
            if (callback) callback(false, data);
            return;
        }
        for (const auto& item : data["items"])
            items.push_back(item);
        int count = (int)data["items"].size();
        int total = data.value("total", offset + count);
        if (count > 0 && offset + count < total) {
            _GetPlaylistsPage(offset + count, std::move(items), callback);
            return;
        }

        nlohmann::json result = data;
        result["items"] = items;
        result["total"] = total;
        {
            BAutolock lock(&fLock);
            std::string accountId = fAccountId;
            fCachedPlaylists.clear();
            for (const auto& item : items) {
                if (item.is_object()) {
                    std::string ownerAccountId;
                    std::string ownerLegacyId;
                    if (item.contains("owner") && item["owner"].is_object()) {
                        ownerAccountId = item["owner"].value(
                            "account_id", "");
                        ownerLegacyId = item["owner"].value("id", "");
                    }
                    bool writable = SpotifyPlaylistIsWritable(
                        item.value("collaborative", false), ownerAccountId,
                        ownerLegacyId, accountId);
                    if (writable) {
                        fCachedPlaylists.push_back({
                            item.value("id", ""),
                            item.value("name", "Unknown")
                        });
                    }
                }
            }
        }
        if (callback) callback(true, result);
    });
}

void SpotifyApi::GetCurrentUserProfile(JsonCallback callback)
{
    Get("/me", callback);
}

void SpotifyApi::GetFollowedArtists(const std::string& after, int limit,
                                    JsonCallback callback)
{
    if (limit < 1) limit = 1;
    if (limit > 50) limit = 50;
    std::string path = "/me/following?type=artist&limit="
        + std::to_string(limit);
    if (!after.empty())
        path += "&after=" + UrlEncode(after);
    Get(path, callback);
}

void SpotifyApi::Seek(int positionMs, JsonCallback callback)
{
    Put("/me/player/seek?position_ms=" + std::to_string(positionMs), "", callback);
}

void SpotifyApi::SetVolume(int percent, JsonCallback callback,
                           const std::string& deviceId)
{
    std::string path = "/me/player/volume?volume_percent="
        + std::to_string(percent);
    if (!deviceId.empty())
        path += "&device_id=" + deviceId;
    Put(path, "", callback);
}

void SpotifyApi::Delete(const std::string& path, const std::string& body, JsonCallback callback)
{
    DEBUG_PRINT("SpotifyApi: Delete path: %s\n", path.c_str());
    _Request("DELETE", path, body,
        [callback](int status, const std::string& resp, int retryAfter) {
            DEBUG_PRINT("SpotifyApi: Delete callback status %d, response: %s\n", status, resp.c_str());
            bool ok;
            nlohmann::json result = MutationResponse(status, resp,
                retryAfter, ok);
            if (callback) callback(ok, result);
        });
}

void SpotifyApi::GetPlaylist(const std::string& playlistId, JsonCallback callback)
{
    Get("/playlists/" + playlistId, callback);
}

void SpotifyApi::InvalidatePlaylist(const std::string& playlistId)
{
    _InvalidateCachePrefix("/playlists/" + playlistId);
}

void SpotifyApi::GetPlaylistTracks(const std::string& playlistId, int offset, int limit,
                                   JsonCallback callback)
{
    Get("/playlists/" + playlistId + "/items?limit=" + std::to_string(limit)
        + "&offset=" + std::to_string(offset), callback);
}

void SpotifyApi::GetTrack(const std::string& trackId, JsonCallback callback)
{
    Get("/tracks/" + trackId, callback);
}

void SpotifyApi::GetAlbum(const std::string& albumId, JsonCallback callback)
{
    Get("/albums/" + albumId, callback);
}

void SpotifyApi::GetAlbumTracks(const std::string& albumId, int offset, int limit, JsonCallback callback)
{
    Get("/albums/" + albumId + "/tracks?limit=" + std::to_string(limit)
        + "&offset=" + std::to_string(offset), callback);
}

void SpotifyApi::GetSavedTracks(int offset, int limit, JsonCallback callback)
{
    Get("/me/tracks?limit=" + std::to_string(limit)
        + "&offset=" + std::to_string(offset), callback);
}

void SpotifyApi::GetSavedEpisodes(int offset, int limit, JsonCallback callback)
{
    Get("/me/episodes?limit=" + std::to_string(limit)
        + "&offset=" + std::to_string(offset), callback);
}

void SpotifyApi::GetEpisode(const std::string& episodeId, JsonCallback callback)
{
    Get("/episodes/" + episodeId, callback);
}

void SpotifyApi::GetArtist(const std::string& artistId, JsonCallback callback)
{
    Get("/artists/" + artistId, callback);
}

void SpotifyApi::Search(const std::string& query, const std::string& types, JsonCallback callback)
{
    std::string path = "/search?q=" + UrlEncode(query) + "&type=" + UrlEncode(types)
                     + "&limit=10";
    _EraseCache(path);
    Get(path, callback);
}

void SpotifyApi::GetArtistTopTracks(const std::string& artistId, JsonCallback callback)
{
    std::string path = "/artists/" + artistId
        + "/top-tracks?market=from_token";
    Get(path, [this, artistId, callback](bool ok, const nlohmann::json& data) {
        if (ok && data.contains("tracks") && data["tracks"].is_array()) {
            if (callback)
                callback(true, data);
            return;
        }

        int status = data.is_object() ? data.value("status", 0) : 0;
        if (!ok && status != 403 && status != 404 && status != 410) {
            if (callback)
                callback(false, data);
            return;
        }

        GetArtist(artistId, [this, artistId, callback](bool artistOk,
                const nlohmann::json& artist) {
            if (!artistOk || !artist.is_object()) {
                if (callback)
                    callback(false, artist);
                return;
            }

            std::string artistName = artist.value("name", "");
            if (artistName.empty()) {
                if (callback)
                    callback(false, {{"error", "missing_artist_name"}});
                return;
            }

            _SearchArtistTracksFallback(artistId, artistName, 0,
                nlohmann::json::array(), callback);
        });
    });
}

void SpotifyApi::_SearchArtistTracksFallback(const std::string& artistId,
        const std::string& artistName, int offset, nlohmann::json tracks,
        JsonCallback callback)
{
    std::string query = "artist:" + artistName;
    std::string path = "/search?q=" + UrlEncode(query)
        + "&type=track&limit=10&offset=" + std::to_string(offset);
    _EraseCache(path);
    Get(path, [this, artistId, artistName, offset, tracks, callback](bool ok,
            const nlohmann::json& result) mutable {
        if (!ok) {
            if (callback) {
                if (!tracks.empty())
                    callback(true, {{"tracks", tracks}});
                else
                    callback(false, result);
            }
            return;
        }

        const nlohmann::json* items = NULL;
        if (result.contains("tracks") && result["tracks"].is_object()
                && result["tracks"].contains("items")
                && result["tracks"]["items"].is_array()) {
            items = &result["tracks"]["items"];
        }

        if (items != NULL) {
            for (const auto& track : *items) {
                if (!track.is_object() || !track.contains("artists")
                        || !track["artists"].is_array())
                    continue;

                bool matchesArtist = false;
                for (const auto& itemArtist : track["artists"]) {
                    if (itemArtist.is_object()
                            && itemArtist.value("id", "") == artistId) {
                        matchesArtist = true;
                        break;
                    }
                }
                if (!matchesArtist)
                    continue;

                std::string id = track.value("id", "");
                std::string isrc;
                if (track.contains("external_ids")
                        && track["external_ids"].is_object()) {
                    isrc = track["external_ids"].value("isrc", "");
                }
                std::string name = track.value("name", "");
                for (char& c : name)
                    c = (char)tolower((unsigned char)c);
                int duration = track.value("duration_ms", 0);

                bool duplicate = false;
                for (const auto& existing : tracks) {
                    if (!id.empty() && existing.value("id", "") == id) {
                        duplicate = true;
                        break;
                    }

                    std::string existingIsrc;
                    if (existing.contains("external_ids")
                            && existing["external_ids"].is_object()) {
                        existingIsrc
                            = existing["external_ids"].value("isrc", "");
                    }
                    if (!isrc.empty() && existingIsrc == isrc) {
                        duplicate = true;
                        break;
                    }

                    std::string existingName = existing.value("name", "");
                    for (char& c : existingName)
                        c = (char)tolower((unsigned char)c);
                    int existingDuration = existing.value("duration_ms", 0);
                    if (!name.empty() && existingName == name && duration > 0
                            && std::abs(duration - existingDuration) <= 2000) {
                        duplicate = true;
                        break;
                    }
                }

                if (!duplicate)
                    tracks.push_back(track);
                if (tracks.size() >= 10)
                    break;
            }
        }

        bool hasNextPage = items != NULL && items->size() == 10
            && offset < 40 && tracks.size() < 10;
        if (hasNextPage) {
            _SearchArtistTracksFallback(artistId, artistName, offset + 10,
                std::move(tracks), callback);
        } else if (callback) {
            callback(true, {{"tracks", tracks}});
        }
    });
}

void SpotifyApi::GetArtistAlbums(const std::string& artistId, int limit,
                                  JsonCallback callback)
{
    Get("/artists/" + artistId + "/albums?include_groups=album,single&limit="
        + std::to_string(limit), callback);
}

void SpotifyApi::CreatePlaylist(const std::string& name, JsonCallback callback)
{
    std::string body = nlohmann::json({{"name", name}, {"public", false}}).dump();
    _InvalidateCachePrefix("/me/playlists");
    Post("/me/playlists", body, [this, name, callback](bool ok,
        const nlohmann::json& data) {
        if (ok) {
            std::string id = data.value("id", "");
            if (!id.empty()) {
                BAutolock lock(&fLock);
                fCachedPlaylists.erase(std::remove_if(fCachedPlaylists.begin(),
                    fCachedPlaylists.end(), [&id](const auto& playlist) {
                        return playlist.first == id;
                    }), fCachedPlaylists.end());
                fCachedPlaylists.insert(fCachedPlaylists.begin(), {id, name});
            }
        }
        if (callback) callback(ok, data);
    });
}

void SpotifyApi::RenamePlaylist(const std::string& playlistId,
                                const std::string& name, JsonCallback callback)
{
    std::string body = nlohmann::json({{"name", name}}).dump();
    _InvalidateCachePrefix("/me/playlists");
    _InvalidateCachePrefix("/playlists/" + playlistId);
    Put("/playlists/" + playlistId, body, [this, playlistId, name, callback](
        bool ok, const nlohmann::json& data) {
        if (ok) {
            BAutolock lock(&fLock);
            for (auto& playlist : fCachedPlaylists) {
                if (playlist.first == playlistId) {
                    playlist.second = name;
                    break;
                }
            }
        }
        if (callback) callback(ok, data);
    });
}

void SpotifyApi::UpdatePlaylistDetails(const std::string& playlistId,
                                       const std::string& name,
                                       const std::string& description,
                                       bool isPublic,
                                       JsonCallback callback)
{
    nlohmann::json request = {
        {"name", name},
        {"description", description},
        {"public", isPublic}
    };
    _InvalidateCachePrefix("/me/playlists");
    _InvalidateCachePrefix("/playlists/" + playlistId);
    Put("/playlists/" + playlistId, request.dump(),
        [this, playlistId, name, callback](bool ok,
            const nlohmann::json& data) {
            if (ok) {
                BAutolock lock(&fLock);
                for (auto& playlist : fCachedPlaylists) {
                    if (playlist.first == playlistId) {
                        playlist.second = name;
                        break;
                    }
                }
            }
            if (callback) callback(ok, data);
        });
}

void SpotifyApi::UnfollowPlaylist(const std::string& playlistId,
                                  JsonCallback callback)
{
    _InvalidateCachePrefix("/me/playlists");
    _InvalidateCachePrefix("/playlists/" + playlistId);
    RemoveLibraryItems({"spotify:playlist:" + playlistId},
        [this, playlistId, callback](bool ok, const nlohmann::json& data) {
            if (ok) {
                BAutolock lock(&fLock);
                fCachedPlaylists.erase(std::remove_if(fCachedPlaylists.begin(),
                    fCachedPlaylists.end(), [&playlistId](const auto& playlist) {
                        return playlist.first == playlistId;
                    }), fCachedPlaylists.end());
            }
            if (callback) callback(ok, data);
        });
}

void SpotifyApi::AddTrackToPlaylist(const std::string& playlistId,
                                    const std::string& trackUri,
                                    JsonCallback callback)
{
    std::string body = nlohmann::json({{"uris", {trackUri}}}).dump();
    _InvalidateCachePrefix("/playlists/" + playlistId);
    DeletePlaylistCache(playlistId);
    Post("/playlists/" + playlistId + "/items", body, callback);
}

void SpotifyApi::RemoveTrackFromPlaylist(const std::string& playlistId,
                                         const std::string& trackUri,
                                         JsonCallback callback)
{
    nlohmann::json request;
    request["items"] = nlohmann::json::array();
    request["items"].push_back({{"uri", trackUri}});
    std::string body = request.dump();
    std::string path = "/playlists/" + playlistId + "/items";
    DEBUG_PRINT("SpotifyApi: RemoveTrackFromPlaylist body: %s\n", body.c_str());
    _InvalidateCachePrefix("/playlists/" + playlistId);
    DeletePlaylistCache(playlistId);
    Delete(path, body, callback);
}

void SpotifyApi::RemoveItemsFromPlaylist(const std::string& playlistId,
        const std::vector<std::string>& uris, const std::string& snapshotId,
        JsonCallback callback)
{
    nlohmann::json items = nlohmann::json::array();
    for (const std::string& uri : uris) {
        if (uri.find("spotify:track:") == 0
                || uri.find("spotify:episode:") == 0) {
            items.push_back({{"uri", uri}});
        }
    }
    if (items.empty()) {
        if (callback) callback(false, {{"status", 400},
            {"error", "no_valid_playlist_items"}});
        return;
    }
    nlohmann::json request = {{"items", items}};
    if (!snapshotId.empty())
        request["snapshot_id"] = snapshotId;
    _InvalidateCachePrefix("/playlists/" + playlistId);
    DeletePlaylistCache(playlistId);
    Delete("/playlists/" + playlistId + "/items", request.dump(), callback);
}

struct SpotifyApi::PreciseRemovalState {
    std::string playlistId;
    std::string snapshotId;
    std::vector<std::pair<std::string, int>> requestedItems;
    std::vector<std::string> playlistUris;
    int offset = 0;
    std::vector<std::pair<int, std::vector<std::string>>> restoreRuns;
    size_t restoreIndex = 0;
    JsonCallback callback;
};

void SpotifyApi::RemovePlaylistItemsAtPositions(
        const std::string& playlistId,
        const std::vector<std::pair<std::string, int>>& items,
        const std::string& snapshotId, JsonCallback callback)
{
    std::vector<std::pair<std::string, int>> validItems;
    for (const auto& item : items) {
        if ((item.first.find("spotify:track:") == 0
                || item.first.find("spotify:episode:") == 0)
                && item.second >= 0) {
            validItems.push_back(item);
        }
    }
    if (validItems.empty()) {
        if (callback) callback(false, {{"status", 400},
            {"error", "no_valid_playlist_positions"}});
        return;
    }

    std::shared_ptr<PreciseRemovalState> state(new PreciseRemovalState());
    state->playlistId = playlistId;
    state->snapshotId = snapshotId;
    state->requestedItems = std::move(validItems);
    state->callback = callback;
    InvalidatePlaylist(playlistId);
    DeletePlaylistCache(playlistId);
    _FetchPreciseRemovalPage(state);
}

void SpotifyApi::RemovePlaylistItemsFromKnownSnapshot(
        const std::string& playlistId,
        const std::vector<std::pair<std::string, int>>& items,
        const std::string& snapshotId,
        const std::vector<std::string>& playlistUris, JsonCallback callback)
{
    if (snapshotId.empty() || playlistUris.empty()) {
        RemovePlaylistItemsAtPositions(playlistId, items, snapshotId, callback);
        return;
    }
    std::vector<std::pair<std::string, int>> validItems;
    for (const auto& item : items) {
        if ((item.first.find("spotify:track:") == 0
                || item.first.find("spotify:episode:") == 0)
                && item.second >= 0) {
            validItems.push_back(item);
        }
    }
    if (validItems.empty()) {
        if (callback) callback(false, {{"status", 400},
            {"error", "no_valid_playlist_positions"}});
        return;
    }
    std::shared_ptr<PreciseRemovalState> state(new PreciseRemovalState());
    state->playlistId = playlistId;
    state->snapshotId = snapshotId;
    state->requestedItems = std::move(validItems);
    state->playlistUris = playlistUris;
    state->callback = callback;
    InvalidatePlaylist(playlistId);
    DeletePlaylistCache(playlistId);
    _ApplyPreciseRemoval(state);
}

void SpotifyApi::_FetchPreciseRemovalPage(
        const std::shared_ptr<PreciseRemovalState>& state)
{
    GetPlaylistTracks(state->playlistId, state->offset, 50,
        [this, state](bool ok, const nlohmann::json& data) {
            if (!ok || !data.is_object() || !data.contains("items")
                    || !data["items"].is_array()) {
                if (state->callback)
                    state->callback(false, data);
                return;
            }

            const nlohmann::json& page = data["items"];
            for (const auto& entry : page) {
                const nlohmann::json* item = nullptr;
                if (entry.is_object() && entry.contains("item")
                        && entry["item"].is_object()) {
                    item = &entry["item"];
                } else if (entry.is_object() && entry.contains("track")
                        && entry["track"].is_object()) {
                    item = &entry["track"];
                }

                std::string uri;
                if (item && item->contains("uri") && (*item)["uri"].is_string())
                    uri = (*item)["uri"].get<std::string>();
                state->playlistUris.push_back(uri);
            }

            int pageCount = (int)page.size();
            state->offset += pageCount;
            int total = -1;
            if (data.contains("total")
                    && (data["total"].is_number_integer()
                        || data["total"].is_number_unsigned())) {
                total = data["total"].get<int>();
            }

            if (pageCount > 0 && (total < 0 || state->offset < total)) {
                _FetchPreciseRemovalPage(state);
                return;
            }
            if (total >= 0 && state->offset < total) {
                if (state->callback) {
                    state->callback(false, {{"status", -1},
                        {"error", "incomplete_playlist_read"}});
                }
                return;
            }
            _ApplyPreciseRemoval(state);
        });
}

void SpotifyApi::_ApplyPreciseRemoval(
        const std::shared_ptr<PreciseRemovalState>& state)
{
    std::sort(state->requestedItems.begin(), state->requestedItems.end(),
        [](const auto& left, const auto& right) {
            return left.second < right.second;
        });

    std::set<int> selectedPositions;
    std::set<std::string> affectedUris;
    for (const auto& item : state->requestedItems) {
        if ((size_t)item.second >= state->playlistUris.size()
                || state->playlistUris[item.second] != item.first
                || !selectedPositions.insert(item.second).second) {
            if (state->callback) {
                state->callback(false, {{"status", 409},
                    {"error", "playlist_position_changed"}});
            }
            return;
        }
        affectedUris.insert(item.first);
    }

    std::vector<std::string> desiredUris;
    bool canReplaceAtomically = state->playlistUris.size()
        - selectedPositions.size() <= 100;
    for (size_t position = 0; position < state->playlistUris.size(); position++) {
        if (selectedPositions.find((int)position) != selectedPositions.end())
            continue;
        const std::string& uri = state->playlistUris[position];
        if (uri.find("spotify:track:") != 0
                && uri.find("spotify:episode:") != 0) {
            canReplaceAtomically = false;
            break;
        }
        desiredUris.push_back(uri);
    }
    if (canReplaceAtomically) {
        DEBUG_PRINT("SpotifyApi: precise playlist removal replaces %lu items with %lu items\n",
            (unsigned long)state->playlistUris.size(),
            (unsigned long)desiredUris.size());
        ReplacePlaylistItems(state->playlistId, desiredUris,
            [state](bool ok, const nlohmann::json& data) {
                if (state->callback)
                    state->callback(ok, data);
            });
        return;
    }

    DEBUG_PRINT("SpotifyApi: precise playlist removal uses duplicate restore for %lu items\n",
        (unsigned long)state->playlistUris.size());

    int selectedBefore = 0;
    auto nextSelected = selectedPositions.begin();
    for (size_t position = 0; position < state->playlistUris.size(); position++) {
        while (nextSelected != selectedPositions.end()
                && *nextSelected < (int)position) {
            selectedBefore++;
            nextSelected++;
        }

        const std::string& uri = state->playlistUris[position];
        if (affectedUris.find(uri) == affectedUris.end()
                || selectedPositions.find((int)position)
                    != selectedPositions.end()) {
            continue;
        }

        int finalPosition = (int)position - selectedBefore;
        if (!state->restoreRuns.empty()) {
            auto& run = state->restoreRuns.back();
            if (run.second.size() < 100
                    && run.first + (int)run.second.size() == finalPosition) {
                run.second.push_back(uri);
                continue;
            }
        }
        state->restoreRuns.push_back({finalPosition, {uri}});
    }

    std::vector<std::string> affected(affectedUris.begin(), affectedUris.end());
    RemoveItemsFromPlaylist(state->playlistId, affected, state->snapshotId,
        [this, state](bool ok, const nlohmann::json& data) {
            if (!ok) {
                if (state->callback)
                    state->callback(false, data);
                return;
            }
            _RestorePreciseRemovalItems(state);
        });
}

void SpotifyApi::_RestorePreciseRemovalItems(
        const std::shared_ptr<PreciseRemovalState>& state)
{
    if (state->restoreIndex >= state->restoreRuns.size()) {
        if (state->callback)
            state->callback(true, {{"status", 200}});
        return;
    }

    const auto& run = state->restoreRuns[state->restoreIndex];
    nlohmann::json request = {{"uris", run.second}, {"position", run.first}};
    _InvalidateCachePrefix("/playlists/" + state->playlistId);
    DeletePlaylistCache(state->playlistId);
    Post("/playlists/" + state->playlistId + "/items", request.dump(),
        [this, state](bool ok, const nlohmann::json& data) {
            if (!ok) {
                if (state->callback) {
                    nlohmann::json error = data;
                    error["partial_update"] = true;
                    error["error"] = "duplicate_restore_failed";
                    state->callback(false, error);
                }
                return;
            }
            state->restoreIndex++;
            _RestorePreciseRemovalItems(state);
        });
}

void SpotifyApi::ReorderPlaylistItems(const std::string& playlistId,
        int rangeStart, int insertBefore, int rangeLength,
        const std::string& snapshotId, JsonCallback callback)
{
    nlohmann::json request = {
        {"range_start", rangeStart},
        {"insert_before", insertBefore},
        {"range_length", rangeLength < 1 ? 1 : rangeLength}
    };
    if (!snapshotId.empty())
        request["snapshot_id"] = snapshotId;
    _InvalidateCachePrefix("/playlists/" + playlistId);
    DeletePlaylistCache(playlistId);
    Put("/playlists/" + playlistId + "/items", request.dump(), callback);
}

void SpotifyApi::ReplacePlaylistItems(const std::string& playlistId,
        const std::vector<std::string>& uris, JsonCallback callback)
{
    nlohmann::json validUris = nlohmann::json::array();
    for (const std::string& uri : uris) {
        if (uri.find("spotify:track:") == 0
                || uri.find("spotify:episode:") == 0) {
            validUris.push_back(uri);
        }
    }
    if (validUris.size() > 100) {
        if (callback) callback(false, {{"status", 400},
            {"error", "too_many_playlist_items"}});
        return;
    }
    _InvalidateCachePrefix("/playlists/" + playlistId);
    DeletePlaylistCache(playlistId);
    Put("/playlists/" + playlistId + "/items",
        nlohmann::json({{"uris", validUris}}).dump(), callback);
}

void SpotifyApi::GetPlaylistImages(const std::string& playlistId,
                                   JsonCallback callback)
{
    Get("/playlists/" + playlistId + "/images", callback);
}

void SpotifyApi::UploadPlaylistImage(const std::string& playlistId,
        const std::string& base64Jpeg, JsonCallback callback)
{
    if (base64Jpeg.empty() || base64Jpeg.size() > 256 * 1024) {
        if (callback) callback(false, {{"status", 400},
            {"error", "invalid_playlist_image_size"}});
        return;
    }
    _InvalidateCachePrefix("/playlists/" + playlistId);
    _Request("PUT", "/playlists/" + playlistId + "/images", base64Jpeg,
        [callback](int status, const std::string& body, int retryAfter) {
            if (callback) callback(status >= 200 && status < 300,
                {{"status", status}, {"body", body},
                    {"retry_after", retryAfter}});
        }, true, "image/jpeg");
}

void SpotifyApi::GetRecentlyPlayed(int limit, JsonCallback callback)
{
    Get("/me/player/recently-played?limit=" + std::to_string(limit), callback);
}

void SpotifyApi::GetNewReleases(int limit, JsonCallback callback)
{
    int searchLimit = limit < 1 ? 1 : (limit > 10 ? 10 : limit);
    std::string path = "/search?q=" + UrlEncode("tag:new")
        + "&type=album&limit=" + std::to_string(searchLimit);
    _EraseCache(path);
    Get(path, callback);
}

void SpotifyApi::GetTopItems(const std::string& type, int limit, JsonCallback callback)
{
    Get("/me/top/" + type + "?limit=" + std::to_string(limit), callback);
}

void SpotifyApi::GetSavedShows(int limit, JsonCallback callback)
{
    Get("/me/shows?limit=" + std::to_string(limit), callback);
}

void SpotifyApi::GetShow(const std::string& showId, JsonCallback callback)
{
    Get("/shows/" + showId, callback);
}

void SpotifyApi::GetShowEpisodes(const std::string& showId, int offset, int limit,
                                  JsonCallback callback)
{
    Get("/shows/" + showId + "/episodes?offset=" + std::to_string(offset)
        + "&limit=" + std::to_string(limit), callback);
}

void SpotifyApi::InvalidateShowEpisodes(const std::string& showId)
{
    _InvalidateCachePrefix("/shows/" + showId + "/episodes");
}

void SpotifyApi::FollowShow(const std::string& showId, JsonCallback callback)
{
    SaveLibraryItems({"spotify:show:" + showId}, callback);
}

void SpotifyApi::UnfollowShow(const std::string& showId, JsonCallback callback)
{
    RemoveLibraryItems({"spotify:show:" + showId}, callback);
}

void SpotifyApi::CheckFollowingShow(const std::string& showId, JsonCallback callback)
{
    CheckLibraryItems({"spotify:show:" + showId}, callback);
}

void SpotifyApi::GetSavedAlbums(int limit, JsonCallback callback)
{
    Get("/me/albums?limit=" + std::to_string(limit), callback);
}

void SpotifyApi::GetSavedAudiobooks(int offset, int limit,
                                    JsonCallback callback)
{
    std::string path = "/me/audiobooks?limit=" + std::to_string(limit)
        + "&offset=" + std::to_string(offset);
    _EraseCache(path);
    Get(path, callback);
}

void SpotifyApi::GetAllSavedAudiobooks(JsonCallback callback)
{
    _GetAllSavedAudiobooksPage(0, nlohmann::json::array(), callback);
}

void SpotifyApi::_GetAllSavedAudiobooksPage(int offset, nlohmann::json items,
                                             JsonCallback callback)
{
    const int limit = 50;
    std::string path = "/me/audiobooks?limit=" + std::to_string(limit)
        + "&offset=" + std::to_string(offset);
    _EraseCache(path);
    Get(path, [this, offset, items = std::move(items), callback](bool ok,
            const nlohmann::json& data) mutable {
        if (!ok || !data.is_object() || !data.contains("items")
                || !data["items"].is_array()) {
            if (callback) callback(false, data);
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
        if (callback) callback(true, items);
    });
}

void SpotifyApi::GetAudiobook(const std::string& audiobookId,
                              JsonCallback callback)
{
    Get("/audiobooks/" + audiobookId, callback);
}

void SpotifyApi::GetAudiobookChapters(const std::string& audiobookId,
        int offset, int limit, JsonCallback callback)
{
    Get("/audiobooks/" + audiobookId + "/chapters?offset="
        + std::to_string(offset) + "&limit=" + std::to_string(limit),
        callback);
}

void SpotifyApi::GetChapter(const std::string& chapterId,
                            JsonCallback callback)
{
    Get("/chapters/" + chapterId, callback);
}

void SpotifyApi::SaveAudiobook(const std::string& audiobookId,
                               JsonCallback callback)
{
    _InvalidateCachePrefix("/me/library");
    _InvalidateCachePrefix("/me/audiobooks");
    Put("/me/audiobooks?ids=" + UrlEncode(audiobookId), "", callback);
}

void SpotifyApi::RemoveSavedAudiobook(const std::string& audiobookId,
                                      JsonCallback callback)
{
    _InvalidateCachePrefix("/me/library");
    _InvalidateCachePrefix("/me/audiobooks");
    Delete("/me/audiobooks?ids=" + UrlEncode(audiobookId), "", callback);
}

void SpotifyApi::CheckSavedAudiobook(const std::string& audiobookId,
                                     JsonCallback callback)
{
    std::string path = "/me/audiobooks/contains?ids="
        + UrlEncode(audiobookId);
    _EraseCache(path);
    Get(path, callback);
}

bool SpotifyApi::_IsValidLibraryUri(const std::string& uri)
{
    static const char* prefixes[] = {
        "spotify:track:", "spotify:album:", "spotify:episode:",
        "spotify:show:", "spotify:audiobook:", "spotify:artist:",
        "spotify:user:", "spotify:playlist:"
    };
    for (const char* prefix : prefixes) {
        size_t length = strlen(prefix);
        if (uri.size() > length && uri.compare(0, length, prefix) == 0)
            return true;
    }
    return false;
}

void SpotifyApi::_LibraryRequestBatches(const std::string& method,
        const std::vector<std::string>& uris, size_t offset,
        nlohmann::json accumulated, JsonCallback callback)
{
    if (offset >= uris.size()) {
        if (callback) callback(true, accumulated);
        return;
    }

    size_t end = std::min(offset + (size_t)40, uris.size());
    std::string value;
    for (size_t i = offset; i < end; i++) {
        if (!value.empty()) value += ',';
        value += uris[i];
    }
    std::string path = "/me/library?uris=" + UrlEncode(value);
    if (method == "GET")
        path = "/me/library/contains?uris=" + UrlEncode(value);

    auto complete = [this, method, uris, end, accumulated,
        callback](bool ok, const nlohmann::json& data) mutable {
        if (!ok) {
            if (callback) callback(false, data);
            return;
        }
        if (method == "GET" && data.is_array()) {
            if (!accumulated.is_array()) accumulated = nlohmann::json::array();
            for (const auto& item : data)
                accumulated.push_back(item);
        } else {
            accumulated = data;
        }
        _LibraryRequestBatches(method, uris, end, accumulated, callback);
    };

    if (method == "GET")
        Get(path, complete);
    else if (method == "PUT")
        Put(path, "", complete);
    else
        Delete(path, "", complete);
}

void SpotifyApi::SaveLibraryItems(const std::vector<std::string>& uris,
                                  JsonCallback callback)
{
    if (uris.empty()) {
        if (callback) callback(true, nlohmann::json::object());
        return;
    }
    for (const std::string& uri : uris) {
        if (!_IsValidLibraryUri(uri)) {
            if (callback) callback(false, {{"status", 400},
                {"error", "invalid_library_uri"}, {"uri", uri}});
            return;
        }
    }
    _InvalidateCachePrefix("/me/library");
    _InvalidateCachePrefix("/me/albums");
    _InvalidateCachePrefix("/me/audiobooks");
    _InvalidateCachePrefix("/me/episodes");
    _InvalidateCachePrefix("/me/following");
    _InvalidateCachePrefix("/me/playlists");
    _InvalidateCachePrefix("/me/shows");
    _InvalidateCachePrefix("/me/tracks");
    std::string audiobookIds;
    if (AudiobookIds(uris, audiobookIds)) {
        Put("/me/audiobooks?ids=" + audiobookIds, "", callback);
        return;
    }
    _LibraryRequestBatches("PUT", uris, 0, nlohmann::json::object(), callback);
}

void SpotifyApi::RemoveLibraryItems(const std::vector<std::string>& uris,
                                    JsonCallback callback)
{
    if (uris.empty()) {
        if (callback) callback(true, nlohmann::json::object());
        return;
    }
    for (const std::string& uri : uris) {
        if (!_IsValidLibraryUri(uri)) {
            if (callback) callback(false, {{"status", 400},
                {"error", "invalid_library_uri"}, {"uri", uri}});
            return;
        }
    }
    _InvalidateCachePrefix("/me/library");
    _InvalidateCachePrefix("/me/albums");
    _InvalidateCachePrefix("/me/audiobooks");
    _InvalidateCachePrefix("/me/episodes");
    _InvalidateCachePrefix("/me/following");
    _InvalidateCachePrefix("/me/playlists");
    _InvalidateCachePrefix("/me/shows");
    _InvalidateCachePrefix("/me/tracks");
    std::string audiobookIds;
    if (AudiobookIds(uris, audiobookIds)) {
        Delete("/me/audiobooks?ids=" + audiobookIds, "", callback);
        return;
    }
    _LibraryRequestBatches("DELETE", uris, 0, nlohmann::json::object(), callback);
}

void SpotifyApi::CheckLibraryItems(const std::vector<std::string>& uris,
                                   JsonCallback callback)
{
    if (uris.empty()) {
        if (callback) callback(true, nlohmann::json::array());
        return;
    }
    for (const std::string& uri : uris) {
        if (!_IsValidLibraryUri(uri)) {
            if (callback) callback(false, {{"status", 400},
                {"error", "invalid_library_uri"}, {"uri", uri}});
            return;
        }
    }
    std::string audiobookIds;
    if (AudiobookIds(uris, audiobookIds)) {
        std::string path = "/me/audiobooks/contains?ids=" + audiobookIds;
        _EraseCache(path);
        Get(path, callback);
        return;
    }
    _LibraryRequestBatches("GET", uris, 0, nlohmann::json::array(), callback);
}

void SpotifyApi::SaveAlbum(const std::string& albumId, JsonCallback callback)
{
    SaveLibraryItems({"spotify:album:" + albumId}, callback);
}

void SpotifyApi::RemoveSavedAlbum(const std::string& albumId,
                                  JsonCallback callback)
{
    RemoveLibraryItems({"spotify:album:" + albumId}, callback);
}

void SpotifyApi::CheckSavedAlbums(const std::string& ids, JsonCallback callback)
{
    std::string path = "/me/library/contains?uris=" + SpotifyUris("album", ids);
    _EraseCache(path);
    Get(path, callback);
}

void SpotifyApi::SaveTrack(const std::string& trackId, JsonCallback callback)
{
    DeleteLikedSongsCache();
    SaveLibraryItems({"spotify:track:" + trackId}, callback);
}

void SpotifyApi::RemoveSavedTrack(const std::string& trackId, JsonCallback callback)
{
    DeleteLikedSongsCache();
    RemoveLibraryItems({"spotify:track:" + trackId}, callback);
}

void SpotifyApi::CheckSavedTracks(const std::string& ids, JsonCallback callback)
{
    std::string path = "/me/library/contains?uris=" + SpotifyUris("track", ids);
    _EraseCache(path);
    Get(path, callback);
}

void SpotifyApi::FollowArtist(const std::string& artistId, JsonCallback callback)
{
    SaveLibraryItems({"spotify:artist:" + artistId}, callback);
}

void SpotifyApi::UnfollowArtist(const std::string& artistId, JsonCallback callback)
{
    RemoveLibraryItems({"spotify:artist:" + artistId}, callback);
}

void SpotifyApi::CheckFollowingArtist(const std::string& artistId,
                                      JsonCallback callback)
{
    CheckLibraryItems({"spotify:artist:" + artistId}, callback);
}
