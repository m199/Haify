#pragma once

#include "SpotifyApiTypes.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <Locker.h>

class PlaylistApi {
public:
    using GetHandler = std::function<void(const std::string& path,
        JsonCallback callback)>;
    using BodyRequestHandler = std::function<void(const std::string& path,
        const std::string& body, JsonCallback callback)>;
    using CacheHandler = std::function<void(const std::string& path)>;

    PlaylistApi(GetHandler get, BodyRequestHandler put,
        BodyRequestHandler post, BodyRequestHandler deleteRequest,
        BodyRequestHandler uploadImage, CacheHandler invalidateCachePrefix);

    void            SetAccountId(const std::string& accountId);
    void            ClearSession();

    void            GetPlaylists(JsonCallback callback);
    void            GetPlaylist(const std::string& playlistId,
                                JsonCallback callback);
    void            InvalidatePlaylist(const std::string& playlistId);
    void            GetPlaylistTracks(const std::string& playlistId,
                                      int offset, int limit,
                                      JsonCallback callback);

    void            CreatePlaylist(const std::string& name,
                                   JsonCallback callback);
    void            RenamePlaylist(const std::string& playlistId,
                                   const std::string& name,
                                   JsonCallback callback);
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

    std::vector<std::pair<std::string, std::string>>
                    GetCachedPlaylists() const;

private:
    struct PreciseRemovalState;

    void            _GetPlaylistsPage(int offset, nlohmann::json items,
                                      JsonCallback callback);
    void            _FetchPreciseRemovalPage(
                            const std::shared_ptr<PreciseRemovalState>& state);
    void            _ApplyPreciseRemoval(
                            const std::shared_ptr<PreciseRemovalState>& state);
    void            _RestorePreciseRemovalItems(
                            const std::shared_ptr<PreciseRemovalState>& state);
    void            _RemoveCachedPlaylist(const std::string& playlistId);
    void            _UpdateCachedPlaylistName(const std::string& playlistId,
                                              const std::string& name);

    GetHandler      fGet;
    BodyRequestHandler fPut;
    BodyRequestHandler fPost;
    BodyRequestHandler fDelete;
    BodyRequestHandler fUploadImage;
    CacheHandler    fInvalidateCachePrefix;
    mutable BLocker fLock;
    std::string     fAccountId;
    std::vector<std::pair<std::string, std::string>> fCachedPlaylists;
};
