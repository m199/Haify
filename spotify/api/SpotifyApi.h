#pragma once

#include <functional>
#include <Locker.h>
#include <map>
#include <memory>
#include <string>
#include <time.h>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

using JsonCallback = std::function<void(bool ok, const nlohmann::json& data)>;
using TokenRefreshCompletion = std::function<void(bool ok)>;
using TokenRefreshHandler = std::function<void(TokenRefreshCompletion completion)>;

struct ApiCacheEntry {
    nlohmann::json data;
    time_t timestamp;
};

class SpotifyApi {
public:
    using RequestCompletion = std::function<void(int status,
        const std::string& body, int retryAfter)>;
    using RequestHandler = std::function<void(const std::string& method,
        const std::string& path, const std::string& body,
        const std::string& contentType, RequestCompletion completion)>;

    explicit        SpotifyApi(const std::string& accessToken);

    void            SetAccessToken(const std::string& token);
    void            SetAccountId(const std::string& accountId);
    void            SetTokenRefreshHandler(TokenRefreshHandler handler);
    void            SetRequestHandler(RequestHandler handler);
    void            ClearSession();

    static int      ResponseStatus(const nlohmann::json& data);
    static int      ResponseRetryAfter(const nlohmann::json& data);
    static std::string ResponseErrorReason(const nlohmann::json& data);
    static bool     IsTemporaryFailure(const nlohmann::json& data);


    void            GetPlaybackState(JsonCallback callback);
    void            GetCurrentlyPlaying(JsonCallback callback);
    void            Play(JsonCallback callback);
    void            PlayTrack(const std::string& trackUri,
                              const std::string& contextUri,
                              JsonCallback callback);
    void            PlayContext(const std::string& contextUri, JsonCallback callback);
    void            Pause(JsonCallback callback);
    void            Next(JsonCallback callback);
    void            Previous(JsonCallback callback);
    void            Seek(int positionMs, JsonCallback callback);
    void            SetVolume(int percent, JsonCallback callback,
                              const std::string& deviceId = "");
    void            GetDevices(JsonCallback callback);
    void            TransferPlayback(const std::string& deviceId, JsonCallback callback);
    void            SetShuffle(bool on, JsonCallback callback);
    void            SetRepeat(const std::string& mode, JsonCallback callback);


    void            GetPlaylists(JsonCallback callback);
    void            InvalidateCachePrefix(const std::string& prefix);
    void            GetCurrentUserProfile(JsonCallback callback);
    void            GetFollowedArtists(const std::string& after, int limit,
                                       JsonCallback callback);
    void            GetPlaylist(const std::string& playlistId, JsonCallback callback);
    void            InvalidatePlaylist(const std::string& playlistId);
    void            GetPlaylistTracks(const std::string& playlistId, int offset,
                                      int limit, JsonCallback callback);
    void            GetTrack(const std::string& trackId, JsonCallback callback);
    void            GetAlbum(const std::string& albumId, JsonCallback callback);
    void            GetAlbumTracks(const std::string& albumId, int offset, int limit,
                                   JsonCallback callback);
    void            GetSavedTracks(int offset, int limit, JsonCallback callback);
    void            GetSavedEpisodes(int offset, int limit, JsonCallback callback);
    void            GetEpisode(const std::string& episodeId, JsonCallback callback);
    void            GetArtist(const std::string& artistId, JsonCallback callback);
    void            GetArtistTopTracks(const std::string& artistId, JsonCallback callback);
    void            GetArtistAlbums(const std::string& artistId, int limit,
                                    JsonCallback callback);


    void            GetRecentlyPlayed(int limit, JsonCallback callback);
    void            GetNewReleases(int limit, JsonCallback callback);
    void            GetTopItems(const std::string& type, int limit, JsonCallback callback);


    void            GetSavedShows(int limit, JsonCallback callback);
    void            GetShow(const std::string& showId, JsonCallback callback);
    void            GetShowEpisodes(const std::string& showId, int offset, int limit,
                                    JsonCallback callback);
    void            InvalidateShowEpisodes(const std::string& showId);
    void            FollowShow(const std::string& showId, JsonCallback callback);
    void            UnfollowShow(const std::string& showId, JsonCallback callback);
    void            CheckFollowingShow(const std::string& showId, JsonCallback callback);
    void            GetSavedAlbums(int limit, JsonCallback callback);
    void            SaveAlbum(const std::string& albumId, JsonCallback callback);
    void            RemoveSavedAlbum(const std::string& albumId,
                                     JsonCallback callback);
    void            CheckSavedAlbums(const std::string& ids,
                                     JsonCallback callback);
    void            GetSavedAudiobooks(int offset, int limit,
                                       JsonCallback callback);
    void            GetAllSavedAudiobooks(JsonCallback callback);
    void            GetAudiobook(const std::string& audiobookId,
                                 JsonCallback callback);
    void            GetAudiobookChapters(const std::string& audiobookId,
                                         int offset, int limit,
                                         JsonCallback callback);
    void            GetChapter(const std::string& chapterId,
                               JsonCallback callback);
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


    void            Search(const std::string& query, const std::string& types,
                           JsonCallback callback);


    void            GetQueue(JsonCallback callback);
    void            AddToQueue(const std::string& uri, JsonCallback callback);


    void            CreatePlaylist(const std::string& name, JsonCallback callback);
    void            RenamePlaylist(const std::string& playlistId,
                                   const std::string& name, JsonCallback callback);
    void            UpdatePlaylistDetails(const std::string& playlistId,
                                          const std::string& name,
                                          const std::string& description,
                                          bool isPublic,
                                          JsonCallback callback);
    void            UnfollowPlaylist(const std::string& playlistId,
                                     JsonCallback callback);


    void            AddTrackToPlaylist(const std::string& playlistId,
                                       const std::string& trackUri,
                                       JsonCallback callback);
    void            RemoveTrackFromPlaylist(const std::string& playlistId,
                                            const std::string& trackUri,
                                            JsonCallback callback);
    void            RemoveItemsFromPlaylist(const std::string& playlistId,
                                            const std::vector<std::string>& uris,
                                            const std::string& snapshotId,
                                            JsonCallback callback);
    void            RemovePlaylistItemsAtPositions(
                                            const std::string& playlistId,
                                            const std::vector<std::pair<std::string,
                                                int>>& items,
                                            const std::string& snapshotId,
                                            JsonCallback callback);
    void            RemovePlaylistItemsFromKnownSnapshot(
                                            const std::string& playlistId,
                                            const std::vector<std::pair<std::string,
                                                int>>& items,
                                            const std::string& snapshotId,
                                            const std::vector<std::string>&
                                                playlistUris,
                                            JsonCallback callback);
    void            ReorderPlaylistItems(const std::string& playlistId,
                                         int rangeStart, int insertBefore,
                                         int rangeLength,
                                         const std::string& snapshotId,
                                         JsonCallback callback);
    void            ReplacePlaylistItems(const std::string& playlistId,
                                         const std::vector<std::string>& uris,
                                         JsonCallback callback);
    void            GetPlaylistImages(const std::string& playlistId,
                                      JsonCallback callback);
    void            UploadPlaylistImage(const std::string& playlistId,
                                        const std::string& base64Jpeg,
                                        JsonCallback callback);

    void            SaveTrack(const std::string& trackId, JsonCallback callback);
    void            RemoveSavedTrack(const std::string& trackId, JsonCallback callback);
    void            CheckSavedTracks(const std::string& ids, JsonCallback callback);


    void            FollowArtist(const std::string& artistId, JsonCallback callback);
    void            UnfollowArtist(const std::string& artistId, JsonCallback callback);
    void            CheckFollowingArtist(const std::string& artistId,
                                         JsonCallback callback);

    std::vector<std::pair<std::string, std::string>>
                    GetCachedPlaylists() const;

private:
    using RawCallback = std::function<void(int status, const std::string& body,
        int retryAfter)>;
    struct PreciseRemovalState;

    void            _Request(const std::string& method, const std::string& path,
                            const std::string& body, RawCallback callback,
                            bool allowRefresh = true,
                            const std::string& contentType = "application/json");
    void            Get(const std::string& path, JsonCallback callback);
    void            _SearchArtistTracksFallback(const std::string& artistId,
                            const std::string& artistName, int offset,
                            nlohmann::json tracks, JsonCallback callback);
    void            _GetPlaylistsPage(int offset, nlohmann::json items,
                            JsonCallback callback);
    void            _GetAllSavedAudiobooksPage(int offset,
                            nlohmann::json items, JsonCallback callback);
    void            _EraseCache(const std::string& path);
    void            _InvalidateCachePrefix(const std::string& prefix);
    std::string     _CacheKey(const std::string& path) const;
    void            Put(const std::string& path, const std::string& body,
                        JsonCallback callback);
    void            Post(const std::string& path, const std::string& body,
                         JsonCallback callback);
    void            Delete(const std::string& path, const std::string& body, JsonCallback callback);
    void            _LibraryRequestBatches(const std::string& method,
                            const std::vector<std::string>& uris, size_t offset,
                            nlohmann::json accumulated, JsonCallback callback);
    void            _FetchPreciseRemovalPage(
                            const std::shared_ptr<PreciseRemovalState>& state);
    void            _ApplyPreciseRemoval(
                            const std::shared_ptr<PreciseRemovalState>& state);
    void            _RestorePreciseRemovalItems(
                            const std::shared_ptr<PreciseRemovalState>& state);
    static bool     _IsValidLibraryUri(const std::string& uri);

    std::string     fAccessToken;
    std::string     fAccountId;
    TokenRefreshHandler fTokenRefreshHandler;
    RequestHandler   fRequestHandler;
    mutable BLocker fLock;
    std::map<std::string, ApiCacheEntry> fCache;
    std::map<std::string, std::vector<JsonCallback>> fPendingGets;
    std::vector<std::pair<std::string, std::string>> fCachedPlaylists;
};
