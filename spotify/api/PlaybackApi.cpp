#include "PlaybackApi.h"
#include "SpotifyUrl.h"
#include "spotify/SpotifyUri.h"

#include <nlohmann/json.hpp>
#include <utility>

PlaybackApi::PlaybackApi(GetHandler get, BodyRequestHandler put,
    BodyRequestHandler post, CacheHandler eraseCache)
    : fGet(std::move(get)),
      fPut(std::move(put)),
      fPost(std::move(post)),
      fEraseCache(std::move(eraseCache))
{
}

void
PlaybackApi::GetPlaybackState(JsonCallback callback)
{
    fEraseCache("/me/player?additional_types=episode");
    fGet("/me/player?additional_types=episode", callback);
}

void
PlaybackApi::GetCurrentlyPlaying(JsonCallback callback)
{
    fEraseCache("/me/player/currently-playing?additional_types=episode");
    fGet("/me/player/currently-playing?additional_types=episode", callback);
}

void
PlaybackApi::Play(JsonCallback callback)
{
    fPut("/me/player/play", "", callback);
}

void
PlaybackApi::PlayTrack(const std::string& trackUri,
    const std::string& contextUri, JsonCallback callback, int positionMs)
{
    bool supportsOffset = SpotifyPlaybackContextSupportsOffset(trackUri,
        contextUri);
    nlohmann::json request;
    if (supportsOffset) {
        request = {
            {"context_uri", contextUri},
            {"offset", {{"uri", trackUri}}}
        };
    } else {
        request = {{"uris", {trackUri}}};
    }
    if (positionMs > 0)
        request["position_ms"] = positionMs;
    fPut("/me/player/play", request.dump(), callback);
}

void
PlaybackApi::PlayUris(const std::vector<std::string>& uris,
    JsonCallback callback)
{
    nlohmann::json request;
    request["uris"] = nlohmann::json::array();
    for (const std::string& uri : uris) {
        if (!uri.empty())
            request["uris"].push_back(uri);
    }
    fPut("/me/player/play", request.dump(), callback);
}

void
PlaybackApi::PlayContext(const std::string& contextUri, JsonCallback callback)
{
    std::string body = nlohmann::json({{"context_uri", contextUri}}).dump();
    fPut("/me/player/play", body, callback);
}

void
PlaybackApi::Pause(JsonCallback callback)
{
    fPut("/me/player/pause", "", callback);
}

void
PlaybackApi::Next(JsonCallback callback)
{
    fPost("/me/player/next", "", callback);
}

void
PlaybackApi::Previous(JsonCallback callback)
{
    fPost("/me/player/previous", "", callback);
}

void
PlaybackApi::Seek(int positionMs, JsonCallback callback,
    const std::string& deviceId)
{
    std::string path = "/me/player/seek?position_ms="
        + std::to_string(positionMs);
    if (!deviceId.empty())
        path += "&device_id=" + SpotifyUrlEncode(deviceId);
    fPut(path, "", callback);
}

void
PlaybackApi::SetVolume(int percent, JsonCallback callback,
    const std::string& deviceId)
{
    std::string path = "/me/player/volume?volume_percent="
        + std::to_string(percent);
    if (!deviceId.empty())
        path += "&device_id=" + deviceId;
    fPut(path, "", callback);
}

void
PlaybackApi::GetDevices(JsonCallback callback)
{
    fEraseCache("/me/player/devices");
    fGet("/me/player/devices", callback);
}

void
PlaybackApi::TransferPlayback(const std::string& deviceId,
    JsonCallback callback)
{
    std::string body = nlohmann::json({
        {"device_ids", {deviceId}},
        {"play", true}
    }).dump();
    fPut("/me/player", body, callback);
}

void
PlaybackApi::SetShuffle(bool on, JsonCallback callback)
{
    fPut(std::string("/me/player/shuffle?state=") + (on ? "true" : "false"),
        "", callback);
}

void
PlaybackApi::SetRepeat(const std::string& mode, JsonCallback callback)
{
    fPut("/me/player/repeat?state=" + mode, "", callback);
}

void
PlaybackApi::GetQueue(JsonCallback callback)
{
    fEraseCache("/me/player/queue");
    fGet("/me/player/queue", callback);
}

void
PlaybackApi::AddToQueue(const std::string& uri, JsonCallback callback)
{
    std::string path = "/me/player/queue?uri=" + SpotifyUrlEncode(uri);
    fPost(path, "", callback);
}

void
PlaybackApi::GetRecentlyPlayed(int limit, JsonCallback callback)
{
    fGet("/me/player/recently-played?limit=" + std::to_string(limit),
        callback);
}
