#include "ContentApi.h"
#include "SpotifyUrl.h"

#include <algorithm>
#include <utility>

ContentApi::ContentApi(GetHandler get, CacheHandler eraseCache,
    CachePrefixHandler invalidateCachePrefix)
    : fGet(std::move(get)),
      fEraseCache(std::move(eraseCache)),
      fInvalidateCachePrefix(std::move(invalidateCachePrefix))
{
}

void
ContentApi::GetTrack(const std::string& trackId, JsonCallback callback)
{
    fGet("/tracks/" + trackId, callback);
}

void
ContentApi::GetAlbum(const std::string& albumId, JsonCallback callback)
{
    fGet("/albums/" + albumId, callback);
}

void
ContentApi::GetAlbumTracks(const std::string& albumId, int offset, int limit,
    JsonCallback callback)
{
    fGet("/albums/" + albumId + "/tracks?limit=" + std::to_string(limit)
        + "&offset=" + std::to_string(offset), callback);
}

void
ContentApi::GetEpisode(const std::string& episodeId, JsonCallback callback)
{
    fGet("/episodes/" + episodeId, callback);
}

void
ContentApi::Search(const std::string& query, const std::string& types,
    JsonCallback callback)
{
    std::string path = "/search?q=" + SpotifyUrlEncode(query)
        + "&type=" + SpotifyUrlEncode(types) + "&limit=10";
    fEraseCache(path);
    fGet(path, callback);
}

void
ContentApi::GetNewReleases(int limit, JsonCallback callback)
{
    int searchLimit = std::max(1, std::min(limit, 10));
    std::string path = "/search?q=" + SpotifyUrlEncode("tag:new")
        + "&type=album&limit=" + std::to_string(searchLimit);
    fEraseCache(path);
    fGet(path, callback);
}

void
ContentApi::GetRecommendations(const std::string& seedTrackId, int limit,
    JsonCallback callback)
{
    int recommendationLimit = std::max(1, std::min(limit, 100));
    std::string path = "/recommendations?limit="
        + std::to_string(recommendationLimit)
        + "&seed_tracks=" + SpotifyUrlEncode(seedTrackId);
    fEraseCache(path);
    fGet(path, callback);
}

void
ContentApi::InvalidateNewReleases()
{
    fInvalidateCachePrefix("/search?q=tag%3Anew");
}

void
ContentApi::GetTopItems(const std::string& type, int limit,
    JsonCallback callback)
{
    fGet("/me/top/" + type + "?limit=" + std::to_string(limit), callback);
}

void
ContentApi::InvalidateTopItems(const std::string& type)
{
    fInvalidateCachePrefix("/me/top/" + type);
}

void
ContentApi::GetShow(const std::string& showId, JsonCallback callback)
{
    fGet("/shows/" + showId, callback);
}

void
ContentApi::GetShowEpisodes(const std::string& showId, int offset, int limit,
    JsonCallback callback)
{
    fGet("/shows/" + showId + "/episodes?offset=" + std::to_string(offset)
        + "&limit=" + std::to_string(limit), callback);
}

void
ContentApi::InvalidateShowEpisodes(const std::string& showId)
{
    fInvalidateCachePrefix("/shows/" + showId + "/episodes");
}

void
ContentApi::GetAudiobook(const std::string& audiobookId,
    JsonCallback callback)
{
    fGet("/audiobooks/" + audiobookId, callback);
}

void
ContentApi::GetAudiobookChapters(const std::string& audiobookId,
    int offset, int limit, JsonCallback callback)
{
    fGet("/audiobooks/" + audiobookId + "/chapters?offset="
        + std::to_string(offset) + "&limit=" + std::to_string(limit),
        callback);
}

void
ContentApi::GetChapter(const std::string& chapterId, JsonCallback callback)
{
    fGet("/chapters/" + chapterId, callback);
}
