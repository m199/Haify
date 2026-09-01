#pragma once

#include "SpotifyApiTypes.h"

#include <functional>
#include <string>
#include <vector>

class PlaybackApi {
public:
    using GetHandler = std::function<void(const std::string& path,
        JsonCallback callback)>;
    using BodyRequestHandler = std::function<void(const std::string& path,
        const std::string& body, JsonCallback callback)>;
    using CacheHandler = std::function<void(const std::string& path)>;

    PlaybackApi(GetHandler get, BodyRequestHandler put,
        BodyRequestHandler post, CacheHandler eraseCache);

    void            GetPlaybackState(JsonCallback callback);
    void            GetCurrentlyPlaying(JsonCallback callback);
    void            Play(JsonCallback callback);
    void            PlayTrack(const std::string& trackUri,
                              const std::string& contextUri,
                              JsonCallback callback,
                              int positionMs = 0);
    void            PlayUris(const std::vector<std::string>& uris,
                              JsonCallback callback);
    void            PlayContext(const std::string& contextUri,
                                JsonCallback callback);
    void            Pause(JsonCallback callback);
    void            Next(JsonCallback callback);
    void            Previous(JsonCallback callback);
    void            Seek(int positionMs, JsonCallback callback,
                              const std::string& deviceId = "");
    void            SetVolume(int percent, JsonCallback callback,
                              const std::string& deviceId = "");
    void            GetDevices(JsonCallback callback);
    void            TransferPlayback(const std::string& deviceId,
                                     JsonCallback callback);
    void            SetShuffle(bool on, JsonCallback callback);
    void            SetRepeat(const std::string& mode, JsonCallback callback);
    void            GetQueue(JsonCallback callback);
    void            AddToQueue(const std::string& uri, JsonCallback callback);
    void            GetRecentlyPlayed(int limit, JsonCallback callback);

private:
    GetHandler      fGet;
    BodyRequestHandler fPut;
    BodyRequestHandler fPost;
    CacheHandler    fEraseCache;
};
