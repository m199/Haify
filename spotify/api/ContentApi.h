#pragma once

#include "SpotifyApiTypes.h"

#include <functional>
#include <string>

class ContentApi {
public:
    using GetHandler = std::function<void(const std::string& path,
        JsonCallback callback)>;
    using CacheHandler = std::function<void(const std::string& path)>;
    using CachePrefixHandler = std::function<void(const std::string& prefix)>;

    ContentApi(GetHandler get, CacheHandler eraseCache,
        CachePrefixHandler invalidateCachePrefix);

    void            GetTrack(const std::string& trackId,
                             JsonCallback callback);
    void            GetAlbum(const std::string& albumId,
                             JsonCallback callback);
    void            GetAlbumTracks(const std::string& albumId, int offset,
                                   int limit, JsonCallback callback);
    void            GetEpisode(const std::string& episodeId,
                               JsonCallback callback);
    void            Search(const std::string& query, const std::string& types,
                           JsonCallback callback);
    void            GetNewReleases(int limit, JsonCallback callback);
    void            GetRecommendations(const std::string& seedTrackId,
                                       int limit, JsonCallback callback);
    void            InvalidateNewReleases();
    void            GetTopItems(const std::string& type, int limit,
                                JsonCallback callback);
    void            InvalidateTopItems(const std::string& type);
    void            GetShow(const std::string& showId,
                            JsonCallback callback);
    void            GetShowEpisodes(const std::string& showId, int offset,
                                    int limit, JsonCallback callback);
    void            InvalidateShowEpisodes(const std::string& showId);
    void            GetAudiobook(const std::string& audiobookId,
                                 JsonCallback callback);
    void            GetAudiobookChapters(const std::string& audiobookId,
                                         int offset, int limit,
                                         JsonCallback callback);
    void            GetChapter(const std::string& chapterId,
                               JsonCallback callback);

private:
    GetHandler      fGet;
    CacheHandler    fEraseCache;
    CachePrefixHandler fInvalidateCachePrefix;
};
