#pragma once

#include "SpotifyApiTypes.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

class LibraryApi {
public:
    using GetHandler = std::function<void(const std::string& path,
        JsonCallback callback)>;
    using BodyRequestHandler = std::function<void(const std::string& path,
        const std::string& body, JsonCallback callback)>;
    using CacheHandler = std::function<void(const std::string& path)>;

    LibraryApi(GetHandler get, BodyRequestHandler put,
        BodyRequestHandler deleteRequest, CacheHandler eraseCache,
        CacheHandler invalidateCachePrefix);

    void            GetSavedShows(int limit, JsonCallback callback);
    void            InvalidateSavedShows();
    void            FollowShow(const std::string& showId,
                               JsonCallback callback);
    void            UnfollowShow(const std::string& showId,
                                 JsonCallback callback);
    void            CheckFollowingShow(const std::string& showId,
                                       JsonCallback callback);

    void            GetSavedAlbums(int limit, JsonCallback callback);
    void            InvalidateSavedAlbums();
    void            SaveAlbum(const std::string& albumId,
                              JsonCallback callback);
    void            RemoveSavedAlbum(const std::string& albumId,
                                     JsonCallback callback);
    void            CheckSavedAlbums(const std::string& ids,
                                     JsonCallback callback);

    void            GetSavedTracks(int offset, int limit,
                                   JsonCallback callback);
    void            InvalidateSavedTracks();
    void            SaveTrack(const std::string& trackId,
                              JsonCallback callback);
    void            RemoveSavedTrack(const std::string& trackId,
                                     JsonCallback callback);
    void            CheckSavedTracks(const std::string& ids,
                                     JsonCallback callback);

    void            GetSavedEpisodes(int offset, int limit,
                                     JsonCallback callback);
    void            InvalidateSavedEpisodes();

    void            GetSavedAudiobooks(int offset, int limit,
                                       JsonCallback callback);
    void            InvalidateSavedAudiobooks();
    void            GetAllSavedAudiobooks(JsonCallback callback);
    void            SaveAudiobook(const std::string& audiobookId,
                                  JsonCallback callback);
    void            RemoveSavedAudiobook(const std::string& audiobookId,
                                         JsonCallback callback);
    void            CheckSavedAudiobook(const std::string& audiobookId,
                                        JsonCallback callback);

    void            SaveLibraryItems(const std::vector<std::string>& uris,
                                     JsonCallback callback);
    void            RemoveLibraryItems(const std::vector<std::string>& uris,
                                       JsonCallback callback);
    void            CheckLibraryItems(const std::vector<std::string>& uris,
                                      JsonCallback callback);

    void            FollowArtist(const std::string& artistId,
                                 JsonCallback callback);
    void            UnfollowArtist(const std::string& artistId,
                                   JsonCallback callback);
    void            CheckFollowingArtist(const std::string& artistId,
                                         JsonCallback callback);
    void            InvalidateFollowedArtists();

private:
    void            _GetAllSavedAudiobooksPage(int offset,
                            nlohmann::json items, JsonCallback callback);
    void            _LibraryRequestBatches(const std::string& method,
                            const std::vector<std::string>& uris,
                            size_t offset, nlohmann::json accumulated,
                            JsonCallback callback);
    void            _InvalidateLibraryCaches();

    GetHandler      fGet;
    BodyRequestHandler fPut;
    BodyRequestHandler fDelete;
    CacheHandler    fEraseCache;
    CacheHandler    fInvalidateCachePrefix;
};
