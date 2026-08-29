#pragma once

#include "ArtistApi.h"
#include "ContentApi.h"
#include "LibraryApi.h"
#include "PlaybackApi.h"
#include "PlaylistApi.h"
#include "ProfileApi.h"
#include "SpotifyApiTypes.h"
#include "SpotifyRequestClient.h"

#include <string>

class SpotifyApi {
public:
    using RequestCompletion = SpotifyRequestClient::RequestCompletion;
    using RequestHandler = SpotifyRequestClient::RequestHandler;

    explicit        SpotifyApi(const std::string& accessToken);

    void            SetAccessToken(const std::string& token);
    void            SetAccountId(const std::string& accountId);
    void            SetTokenRefreshHandler(TokenRefreshHandler handler);
    void            SetRequestHandler(RequestHandler handler);
    void            ClearSession();

    ArtistApi&      Artists();
    ContentApi&     Content();
    LibraryApi&     Library();
    PlaybackApi&    Playback();
    PlaylistApi&    Playlists();
    ProfileApi&     Profile();

private:
    using RawCallback = SpotifyRequestClient::RawCallback;
    void            _Request(const std::string& method, const std::string& path,
                            const std::string& body, RawCallback callback,
                            bool allowRefresh = true,
                            const std::string& contentType = "application/json");
    void            Get(const std::string& path, JsonCallback callback);
    void            _EraseCache(const std::string& path);
    void            _InvalidateCachePrefix(const std::string& prefix);
    void            Put(const std::string& path, const std::string& body,
                        JsonCallback callback);
    void            Post(const std::string& path, const std::string& body,
                         JsonCallback callback);
    void            Delete(const std::string& path, const std::string& body,
                        JsonCallback callback);
    SpotifyRequestClient fClient;
    ArtistApi       fArtists;
    ContentApi      fContent;
    LibraryApi      fLibrary;
    PlaybackApi     fPlayback;
    PlaylistApi     fPlaylists;
    ProfileApi      fProfile;
};
