#include "SpotifyApi.h"

#include <utility>


SpotifyApi::SpotifyApi(const std::string& accessToken)
    : fClient(accessToken),
      fArtists(
        [this](const std::string& path, JsonCallback callback) {
            Get(path, callback);
        },
        [this](const std::string& path) {
            _EraseCache(path);
        }),
      fContent(
        [this](const std::string& path, JsonCallback callback) {
            Get(path, callback);
        },
        [this](const std::string& path) {
            _EraseCache(path);
        },
        [this](const std::string& prefix) {
            _InvalidateCachePrefix(prefix);
        }),
      fLibrary(
        [this](const std::string& path, JsonCallback callback) {
            Get(path, callback);
        },
        [this](const std::string& path, const std::string& body,
                JsonCallback callback) {
            Put(path, body, callback);
        },
        [this](const std::string& path, const std::string& body,
                JsonCallback callback) {
            Delete(path, body, callback);
        },
        [this](const std::string& path) {
            _EraseCache(path);
        },
        [this](const std::string& prefix) {
            _InvalidateCachePrefix(prefix);
        }),
      fPlayback(
        [this](const std::string& path, JsonCallback callback) {
            Get(path, callback);
        },
        [this](const std::string& path, const std::string& body,
                JsonCallback callback) {
            Put(path, body, callback);
        },
        [this](const std::string& path, const std::string& body,
                JsonCallback callback) {
            Post(path, body, callback);
        },
        [this](const std::string& path) {
            _EraseCache(path);
        }),
      fPlaylists(
        [this](const std::string& path, JsonCallback callback) {
            Get(path, callback);
        },
        [this](const std::string& path, const std::string& body,
                JsonCallback callback) {
            Put(path, body, callback);
        },
        [this](const std::string& path, const std::string& body,
                JsonCallback callback) {
            Post(path, body, callback);
        },
        [this](const std::string& path, const std::string& body,
                JsonCallback callback) {
            Delete(path, body, callback);
        },
        [this](const std::string& path, const std::string& body,
                JsonCallback callback) {
            _Request("PUT", path, body,
                [callback](int status, const std::string& responseBody,
                        int retryAfter) {
                    if (callback) {
                        callback(status >= 200 && status < 300,
                            {{"status", status}, {"body", responseBody},
                                {"retry_after", retryAfter}});
                    }
                }, true, "image/jpeg");
        },
        [this](const std::string& prefix) {
            _InvalidateCachePrefix(prefix);
        }),
      fProfile(
        [this](const std::string& path, JsonCallback callback) {
            Get(path, callback);
        })
{
}

void SpotifyApi::SetAccessToken(const std::string& token)
{
    bool changed = fClient.SetAccessToken(token);
    if (changed) {
        fPlaylists.ClearSession();
        fPlaylists.SetAccountId(fClient.AccountId());
    }
}

void SpotifyApi::SetAccountId(const std::string& accountId)
{
    if (!fClient.SetAccountId(accountId))
        return;
    fPlaylists.SetAccountId(accountId);
}

void SpotifyApi::SetTokenRefreshHandler(TokenRefreshHandler handler)
{
    fClient.SetTokenRefreshHandler(std::move(handler));
}

void SpotifyApi::SetRequestHandler(RequestHandler handler)
{
    fClient.SetRequestHandler(std::move(handler));
}

void SpotifyApi::ClearSession()
{
    fClient.ClearSession();
    fPlaylists.ClearSession();
}

ArtistApi& SpotifyApi::Artists()
{
    return fArtists;
}

ContentApi& SpotifyApi::Content()
{
    return fContent;
}

LibraryApi& SpotifyApi::Library()
{
    return fLibrary;
}

PlaybackApi& SpotifyApi::Playback()
{
    return fPlayback;
}

PlaylistApi& SpotifyApi::Playlists()
{
    return fPlaylists;
}

ProfileApi& SpotifyApi::Profile()
{
    return fProfile;
}

void SpotifyApi::_Request(const std::string& method, const std::string& path,
    const std::string& body, RawCallback callback, bool allowRefresh,
    const std::string& contentType)
{
    fClient.Request(method, path, body, callback, allowRefresh, contentType);
}

void SpotifyApi::_InvalidateCachePrefix(const std::string& prefix)
{
    fClient.InvalidateCachePrefix(prefix);
}

void SpotifyApi::_EraseCache(const std::string& path)
{
    fClient.EraseCache(path);
}

void SpotifyApi::Get(const std::string& path, JsonCallback callback)
{
    fClient.Get(path, callback);
}

void SpotifyApi::Put(const std::string& path, const std::string& body, JsonCallback callback)
{
    fClient.Put(path, body, callback);
}

void SpotifyApi::Post(const std::string& path, const std::string& body, JsonCallback callback)
{
    fClient.Post(path, body, callback);
}

void SpotifyApi::Delete(const std::string& path, const std::string& body, JsonCallback callback)
{
    fClient.Delete(path, body, callback);
}
