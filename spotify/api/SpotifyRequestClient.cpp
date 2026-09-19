#include "SpotifyRequestClient.h"
#include "app/Config.h"
#include "app/HaifyDebug.h"
#include "HttpClient.h"

#include <Autolock.h>
#include <OS.h>
#include <atomic>
#include <ctime>
#include <initializer_list>
#include <utility>

static bool
IsPlaybackTracePath(const std::string& path)
{
    std::string endpoint = path.substr(0, path.find('?'));
    return endpoint == "/me/player" || endpoint == "/me/player/play"
        || endpoint == "/me/player/shuffle"
        || endpoint == "/me/player/devices"
        || endpoint == "/me/player/currently-playing";
}

static nlohmann::json
PlaybackTraceFields(const nlohmann::json& data,
    std::initializer_list<const char*> fields)
{
    // Missing fields and explicit nulls must remain distinguishable in the log.
    if (!data.is_object())
        return data;
    nlohmann::json result = nlohmann::json::object();
    for (const char* field : fields) {
        auto value = data.find(field);
        if (value != data.end())
            result[field] = *value;
    }
    return result;
}

static nlohmann::json
PlaybackTraceDevice(const nlohmann::json& device)
{
    return PlaybackTraceFields(device, {"id", "name", "type", "is_active",
        "is_restricted", "is_private_session", "volume_percent"});
}

static std::string
PlaybackTraceResponse(const std::string& body)
{
    if (body.empty())
        return "<empty>";
    auto data = nlohmann::json::parse(body, nullptr, false);
    if (data.is_discarded())
        return "<invalid JSON>";
    auto summary = PlaybackTraceFields(data, {"is_playing", "progress_ms",
        "timestamp", "currently_playing_type", "shuffle_state", "error"});
    if (!data.is_object())
        return summary.dump();
    if (data.contains("device"))
        summary["device"] = PlaybackTraceDevice(data["device"]);
    if (data.contains("item")) {
        summary["item"] = PlaybackTraceFields(data["item"],
            {"uri", "type", "is_playable", "is_local", "restrictions"});
    }
    if (data.contains("context"))
        summary["context"] = PlaybackTraceFields(data["context"], {"uri", "type"});
    if (data.contains("devices") && data["devices"].is_array()) {
        summary["devices"] = nlohmann::json::array();
        for (const auto& device : data["devices"])
            summary["devices"].push_back(PlaybackTraceDevice(device));
    }
    return summary.dump();
}

// Trace actual dispatch/completion, including successful empty GETs. A PUT 204
// alone does not show the device/item subsequently reported by Spotify.
static unsigned long long
LogPlaybackDispatch(const std::string& method, const std::string& path,
    const std::string& body)
{
    if (!gIsDebug || !IsPlaybackTracePath(path))
        return 0;
    static std::atomic<unsigned long long> nextId{1};
    unsigned long long id = nextId.fetch_add(1, std::memory_order_relaxed);
    DEBUG_PRINT("Playback trace=%llu t=%lldms SEND %s %s body_bytes=%zu body=%.4096s\n",
        id, static_cast<long long>(system_time() / 1000), method.c_str(),
        path.c_str(), body.size(), body.empty() ? "<empty>" : body.c_str());
    return id;
}

static void
LogPlaybackResponse(unsigned long long id, const std::string& method,
    const std::string& path, int status, const std::string& body)
{
    if (id == 0 || !gIsDebug)
        return;
    std::string summary = status <= 0
        ? nlohmann::json({{"transport_error", body}}).dump()
        : PlaybackTraceResponse(body);
    DEBUG_PRINT("Playback trace=%llu t=%lldms RECV %s %s status=%d body_bytes=%zu summary=%s\n",
        id, static_cast<long long>(system_time() / 1000), method.c_str(),
        path.c_str(), status, body.size(), summary.c_str());
}

static Headers
RequestHeaders(const std::string& token, const std::string& body,
    const std::string& contentType)
{
    Headers headers = {{"Authorization", "Bearer " + token}};
    if (!body.empty() && !contentType.empty())
        headers["Content-Type"] = contentType;
    return headers;
}

static void
LogRequestFailure(const std::string& method, const std::string& path,
    int status, const std::string& responseBody)
{
    if (status < 200 || status >= 300) {
        DEBUG_PRINT("SpotifyApi %s %s -> %d: %.200s\n", method.c_str(),
            path.c_str(), status, responseBody.c_str());
    }
}

static void
DispatchHttpRequest(const std::string& method, const std::string& path,
    const std::string& body, const Headers& headers,
    SpotifyRequestClient::RawCallback callback)
{
    auto response = [callback](const HttpResponse& httpResponse) {
        callback(httpResponse.statusCode, httpResponse.body,
            httpResponse.retryAfter);
    };

    std::string url = std::string(SPOTIFY_API_BASE) + path;
    if (method == "GET")
        HttpClient::Get(url, headers, response);
    else if (method == "POST")
        HttpClient::Post(url, headers, body, response);
    else if (method == "PUT")
        HttpClient::Put(url, headers, body, response);
    else if (method == "DELETE")
        HttpClient::Delete(url, headers, body, response);
    else if (callback)
        callback(-1, "{\"error\":\"unsupported_http_method\"}", -1);
}

static nlohmann::json
MutationResponse(int status, const std::string& body, int retryAfter,
    bool& ok)
{
    ok = status >= 200 && status < 300;
    if (!ok || body.empty()) {
        return {{"status", status}, {"body", body},
            {"retry_after", retryAfter}};
    }

    try {
        return nlohmann::json::parse(body);
    } catch (...) {
        ok = false;
        return {{"status", status}, {"body", body},
            {"retry_after", retryAfter}, {"error", "invalid_json"}};
    }
}

static nlohmann::json
GetResponse(int status, const std::string& body, int retryAfter, bool& ok)
{
    ok = false;
    if (status < 200 || status >= 300)
        return {{"status", status}, {"body", body}, {"retry_after", retryAfter}};
    auto result = body.empty() ? nlohmann::json::object()
        : nlohmann::json::parse(body, nullptr, false);
    if (result.is_discarded())
        return {{"status", status}, {"error", "invalid_json"}};
    ok = true;
    return result;
}

SpotifyRequestClient::SpotifyRequestClient(const std::string& accessToken)
    : fAccessToken(accessToken),
      fLock("Spotify Request Client")
{
}

bool
SpotifyRequestClient::SetAccessToken(const std::string& token)
{
    BAutolock lock(&fLock);
    bool changed = fAccessToken != token;
    if (changed)
        fCache.clear();
    fAccessToken = token;
    return changed;
}

bool
SpotifyRequestClient::SetAccountId(const std::string& accountId)
{
    BAutolock lock(&fLock);
    if (fAccountId == accountId)
        return false;
    fAccountId = accountId;
    fCache.clear();
    fPendingGets.clear();
    fSessionGeneration++;
    return true;
}

std::string
SpotifyRequestClient::AccountId() const
{
    BAutolock lock(&fLock);
    return fAccountId;
}

bool
SpotifyRequestClient::DispatchForAccount(const std::string& account,
    const std::function<void()>& operation)
{
    BAutolock lock(&fLock);
    if (account != fAccountId)
        return false;
    operation();
    return true;
}

void
SpotifyRequestClient::SetTokenRefreshHandler(TokenRefreshHandler handler)
{
    BAutolock lock(&fLock);
    fTokenRefreshHandler = std::move(handler);
}

void
SpotifyRequestClient::SetRequestHandler(RequestHandler handler)
{
    BAutolock lock(&fLock);
    fRequestHandler = std::move(handler);
}

void
SpotifyRequestClient::ClearSession()
{
    BAutolock lock(&fLock);
    fAccessToken.clear();
    fAccountId.clear();
    fCache.clear();
    fPendingGets.clear();
    fSessionGeneration++;
}

bool
SpotifyRequestClient::_SessionMatches(uint64_t session) const
{
    BAutolock lock(&fLock);
    return session == fSessionGeneration;
}

void
SpotifyRequestClient::Request(const std::string& method,
    const std::string& path, const std::string& body, RawCallback callback,
    bool allowRefresh, const std::string& contentType)
{
    _Request(method, path, body, std::move(callback), allowRefresh, contentType, std::nullopt);
}

void
SpotifyRequestClient::_Request(const std::string& method,
    const std::string& path, const std::string& body, RawCallback callback,
    bool allowRefresh, const std::string& contentType, std::optional<uint64_t> expectedSession)
{
    std::string token;
    TokenRefreshHandler refreshHandler;
    RequestHandler requestHandler;
    uint64_t session;
    {
        BAutolock lock(&fLock);
        session = fSessionGeneration;
        if (expectedSession && *expectedSession != session) {
            lock.Unlock();
            if (callback)
                callback(-1, "{\"error\":\"session_changed\"}", -1);
            return;
        }
        token = fAccessToken;
        refreshHandler = fTokenRefreshHandler;
        requestHandler = fRequestHandler;
    }

    unsigned long long traceId = LogPlaybackDispatch(method, path, body);
    auto complete = [this, method, path, body, callback, allowRefresh,
        contentType, refreshHandler, session, traceId](int status, const std::string& responseBody,
            int retryAfter) {
        LogPlaybackResponse(traceId, method, path, status, responseBody);
        if (!_SessionMatches(session)) {
            if (callback)
                callback(-1, "{\"error\":\"session_changed\"}", -1);
            return;
        }
        LogRequestFailure(method, path, status, responseBody);
        if (status == 401 && allowRefresh && refreshHandler) {
            refreshHandler([this, method, path, body, callback,
                contentType, session](bool ok) {
                if (ok) {
                    _Request(method, path, body, callback, false,
                        contentType, session);
                } else if (callback) {
                    callback(401, "{\"error\":\"token_refresh_failed\"}", -1);
                }
            });
            return;
        }
        if (callback)
            callback(status, responseBody, retryAfter);
    };

    if (requestHandler) {
        requestHandler(method, path, body, contentType, std::move(complete));
        return;
    }
    DispatchHttpRequest(method, path, body,
        RequestHeaders(token, body, contentType), std::move(complete));
}

void
SpotifyRequestClient::Get(const std::string& path, JsonCallback callback)
{
    std::string cacheKey;
    std::shared_ptr<PendingGet> request;
    uint64_t session;
    {
        BAutolock lock(&fLock);
        cacheKey = _CacheKey(path);
        session = fSessionGeneration;
        auto cached = fCache.find(cacheKey);
        if (cached != fCache.end()
                && time(NULL) - cached->second.timestamp < 3600) {
            nlohmann::json data = cached->second.data;
            lock.Unlock();
            if (callback)
                callback(true, data);
            return;
        }
        auto pending = fPendingGets.find(cacheKey);
        if (pending != fPendingGets.end()) {
            if (callback)
                pending->second->callbacks.push_back(callback);
            return;
        }
        request = std::make_shared<PendingGet>();
        fPendingGets[cacheKey] = request;
        if (callback)
            request->callbacks.push_back(callback);
    }

    _Request("GET", path, "",
        [this, cacheKey, request](int status, const std::string& body,
            int retryAfter) {
        bool ok;
        nlohmann::json result = GetResponse(status, body, retryAfter, ok);
        std::vector<JsonCallback> callbacks;
        {
            BAutolock lock(&fLock);
            auto pending = fPendingGets.find(cacheKey);
            if (pending != fPendingGets.end() && pending->second == request) {
                if (ok)
                    fCache[cacheKey] = {result, time(NULL)};
                fPendingGets.erase(pending);
            }
            // Invalidation separates old and new readers. Old readers still
            // receive their own result and decide freshness using their context;
            // this response must never populate the cache or consume new waiters.
            callbacks.swap(request->callbacks);
        }
        for (const JsonCallback& current : callbacks) {
            if (current)
                current(ok, result);
        }
    }, true, "application/json", session);
}

void
SpotifyRequestClient::Put(const std::string& path, const std::string& body,
    JsonCallback callback)
{
    Request("PUT", path, body,
        [path, callback](int status, const std::string& resp,
            int retryAfter) {
        if (path.rfind("/me/player/volume?", 0) != 0) {
            DEBUG_PRINT("SpotifyApi::Put %s -> %d: %.200s\n",
                path.c_str(), status, resp.c_str());
        }
        bool ok;
        nlohmann::json result = MutationResponse(status, resp, retryAfter,
            ok);
        if (callback)
            callback(ok, result);
    });
}

void
SpotifyRequestClient::Post(const std::string& path, const std::string& body,
    JsonCallback callback)
{
    Request("POST", path, body,
        [callback](int status, const std::string& resp, int retryAfter) {
        bool ok;
        nlohmann::json result = MutationResponse(status, resp, retryAfter,
            ok);
        if (callback)
            callback(ok, result);
    });
}

void
SpotifyRequestClient::Delete(const std::string& path,
    const std::string& body, JsonCallback callback)
{
    DEBUG_PRINT("SpotifyApi: Delete path: %s\n", path.c_str());
    Request("DELETE", path, body,
        [callback](int status, const std::string& resp, int retryAfter) {
        DEBUG_PRINT("SpotifyApi: Delete callback status %d, response: %s\n",
            status, resp.c_str());
        bool ok;
        nlohmann::json result = MutationResponse(status, resp, retryAfter,
            ok);
        if (callback)
            callback(ok, result);
    });
}

void
SpotifyRequestClient::EraseCache(const std::string& path)
{
    BAutolock lock(&fLock);
    std::string key = _CacheKey(path);
    fCache.erase(key);
    fPendingGets.erase(key);
}

void
SpotifyRequestClient::InvalidateCachePrefix(const std::string& prefix)
{
    BAutolock lock(&fLock);
    std::string accountPrefix = _CacheKey("");
    for (auto it = fCache.begin(); it != fCache.end();) {
        if (it->first.rfind(accountPrefix + prefix, 0) == 0)
            it = fCache.erase(it);
        else
            ++it;
    }
    for (auto it = fPendingGets.begin(); it != fPendingGets.end();) {
        if (it->first.rfind(accountPrefix + prefix, 0) == 0)
            it = fPendingGets.erase(it);
        else
            ++it;
    }
}

std::string
SpotifyRequestClient::_CacheKey(const std::string& path) const
{
    BAutolock lock(&fLock);
    return (fAccountId.empty() ? "session" : fAccountId) + "|" + path;
}
