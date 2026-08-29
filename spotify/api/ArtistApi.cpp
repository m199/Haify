#include "ArtistApi.h"
#include "SpotifyUrl.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <utility>

static const int kArtistSearchLimit = 10;
static const int kMaxArtistSearchOffset = 40;

static std::string
ArtistTopTracksPath(const std::string& artistId)
{
    return "/artists/" + artistId + "/top-tracks?market=from_token";
}

static bool
HasNonEmptyTracks(const nlohmann::json& data)
{
    return data.contains("tracks") && data["tracks"].is_array()
        && !data["tracks"].empty();
}

static bool
ShouldUseSearchFallback(bool ok, const nlohmann::json& data)
{
    if (ok)
        return true;
    int status = data.is_object() ? data.value("status", 0) : 0;
    return status == 403 || status == 404 || status == 410;
}

static const nlohmann::json*
SearchTrackItems(const nlohmann::json& result)
{
    if (!result.contains("tracks") || !result["tracks"].is_object()
            || !result["tracks"].contains("items")
            || !result["tracks"]["items"].is_array()) {
        return NULL;
    }
    return &result["tracks"]["items"];
}

static std::string
LowercaseCopy(std::string value)
{
    for (char& c : value)
        c = (char)tolower((unsigned char)c);
    return value;
}

static std::string
TrackIsrc(const nlohmann::json& track)
{
    if (track.contains("external_ids") && track["external_ids"].is_object())
        return track["external_ids"].value("isrc", "");
    return "";
}

static bool
TrackHasArtist(const nlohmann::json& track, const std::string& artistId)
{
    if (!track.contains("artists") || !track["artists"].is_array())
        return false;
    for (const auto& artist : track["artists"]) {
        if (artist.is_object() && artist.value("id", "") == artistId)
            return true;
    }
    return false;
}

static bool
IsSameRecording(const nlohmann::json& left, const nlohmann::json& right)
{
    std::string id = left.value("id", "");
    if (!id.empty() && right.value("id", "") == id)
        return true;

    std::string isrc = TrackIsrc(left);
    if (!isrc.empty() && TrackIsrc(right) == isrc)
        return true;

    std::string name = LowercaseCopy(left.value("name", ""));
    int duration = left.value("duration_ms", 0);
    int otherDuration = right.value("duration_ms", 0);
    return !name.empty() && LowercaseCopy(right.value("name", "")) == name
        && duration > 0 && std::abs(duration - otherDuration) <= 2000;
}

static bool
ContainsRecording(const nlohmann::json& tracks, const nlohmann::json& track)
{
    for (const auto& existing : tracks) {
        if (existing.is_object() && IsSameRecording(track, existing))
            return true;
    }
    return false;
}

static void
AddMatchingTracks(const nlohmann::json& items, const std::string& artistId,
    nlohmann::json& tracks)
{
    for (const auto& track : items) {
        if (!track.is_object() || !TrackHasArtist(track, artistId))
            continue;
        if (!ContainsRecording(tracks, track))
            tracks.push_back(track);
        if (tracks.size() >= kArtistSearchLimit)
            break;
    }
}

static bool
ShouldLoadNextFallbackPage(const nlohmann::json* items, int offset,
    const nlohmann::json& tracks)
{
    return items != NULL && items->size() == kArtistSearchLimit
        && offset < kMaxArtistSearchOffset
        && tracks.size() < kArtistSearchLimit;
}

static ArtistTopTracksResult
OfficialTopTracksResult(nlohmann::json tracks)
{
    ArtistTopTracksResult result;
    result.tracks = std::move(tracks);
    result.source = ArtistTopTracksSource::kOfficialTopTracks;
    return result;
}

static ArtistTopTracksResult
FallbackTopTracksResult(nlohmann::json tracks)
{
    ArtistTopTracksResult result;
    result.tracks = std::move(tracks);
    result.source = ArtistTopTracksSource::kSearchFallback;
    return result;
}

ArtistApi::ArtistApi(GetHandler get, CacheEraseHandler eraseCache)
    : fGet(std::move(get)),
      fEraseCache(std::move(eraseCache))
{
}

void
ArtistApi::GetFollowedArtists(const std::string& after, int limit,
    JsonCallback callback)
{
    if (limit < 1)
        limit = 1;
    if (limit > 50)
        limit = 50;
    std::string path = "/me/following?type=artist&limit="
        + std::to_string(limit);
    if (!after.empty())
        path += "&after=" + SpotifyUrlEncode(after);
    fGet(path, callback);
}

void
ArtistApi::GetArtist(const std::string& artistId, JsonCallback callback)
{
    fGet("/artists/" + artistId, callback);
}

void
ArtistApi::GetArtistTopTracks(const std::string& artistId,
    TopTracksCallback callback)
{
    std::string path = ArtistTopTracksPath(artistId);
    fGet(path, [this, artistId, path, callback](bool ok,
            const nlohmann::json& data) {
        if (ok && HasNonEmptyTracks(data)) {
            if (callback)
                callback(true, OfficialTopTracksResult(data["tracks"]), {});
            return;
        }
        if (ok)
            fEraseCache(path);
        if (!ShouldUseSearchFallback(ok, data)) {
            if (callback)
                callback(false, ArtistTopTracksResult(), data);
            return;
        }

        GetArtist(artistId, [this, artistId, callback](bool artistOk,
                const nlohmann::json& artist) {
            if (!artistOk || !artist.is_object()) {
                if (callback)
                    callback(false, ArtistTopTracksResult(), artist);
                return;
            }

            std::string artistName = artist.value("name", "");
            if (artistName.empty()) {
                if (callback)
                    callback(false, ArtistTopTracksResult(),
                        {{"error", "missing_artist_name"}});
                return;
            }

            _SearchTracksFallback(artistId, artistName, 0,
                nlohmann::json::array(), callback);
        });
    });
}

void
ArtistApi::_SearchTracksFallback(const std::string& artistId,
    const std::string& artistName, int offset, nlohmann::json tracks,
    TopTracksCallback callback)
{
    std::string query = "artist:" + artistName;
    std::string path = "/search?q=" + SpotifyUrlEncode(query)
        + "&type=track&limit=" + std::to_string(kArtistSearchLimit)
        + "&offset=" + std::to_string(offset);
    fEraseCache(path);
    fGet(path, [this, artistId, artistName, offset, tracks, callback](
            bool ok, const nlohmann::json& result) mutable {
        if (!ok) {
            if (callback) {
                if (!tracks.empty())
                    callback(true, FallbackTopTracksResult(tracks), {});
                else
                    callback(false, ArtistTopTracksResult(), result);
            }
            return;
        }

        const nlohmann::json* items = SearchTrackItems(result);
        if (items != NULL)
            AddMatchingTracks(*items, artistId, tracks);

        if (ShouldLoadNextFallbackPage(items, offset, tracks)) {
            _SearchTracksFallback(artistId, artistName,
                offset + kArtistSearchLimit, std::move(tracks), callback);
            return;
        }
        if (callback)
            callback(true, FallbackTopTracksResult(tracks), {});
    });
}

void
ArtistApi::GetArtistAlbums(const std::string& artistId, int limit,
    JsonCallback callback)
{
    fGet("/artists/" + artistId + "/albums?include_groups=album,single&limit="
        + std::to_string(limit), callback);
}
