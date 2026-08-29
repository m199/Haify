#include "SpotifyRequestClient.h"
#include "Config.h"
#include "HaifyDebug.h"
#include "HttpClient.h"

#include <Autolock.h>
#include <ctime>
#include <utility>

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
    return true;
}

std::string
SpotifyRequestClient::AccountId() const
{
    BAutolock lock(&fLock);
    return fAccountId;
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
}

void
SpotifyRequestClient::Request(const std::string& method,
    const std::string& path, const std::string& body, RawCallback callback,
    bool allowRefresh, const std::string& contentType)
{
    std::string token;
    TokenRefreshHandler refreshHandler;
    RequestHandler requestHandler;
    {
        BAutolock lock(&fLock);
        token = fAccessToken;
        refreshHandler = fTokenRefreshHandler;
        requestHandler = fRequestHandler;
    }

    auto complete = [this, method, path, body, callback, allowRefresh,
        contentType, refreshHandler](int status, const std::string& responseBody,
            int retryAfter) {
        LogRequestFailure(method, path, status, responseBody);
        if (status == 401 && allowRefresh && refreshHandler) {
            refreshHandler([this, method, path, body, callback,
                contentType](bool ok) {
                if (ok) {
                    Request(method, path, body, callback, false,
                        contentType);
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
    std::string cacheKey = _CacheKey(path);
    {
        BAutolock lock(&fLock);
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
                pending->second.push_back(callback);
            return;
        }
        fPendingGets[cacheKey] = {};
        if (callback)
            fPendingGets[cacheKey].push_back(callback);
    }

    Request("GET", path, "",
        [this, cacheKey](int status, const std::string& body,
            int retryAfter) {
        bool ok = false;
        nlohmann::json result;
        if (status >= 200 && status < 300) {
            try {
                result = body.empty()
                    ? nlohmann::json::object()
                    : nlohmann::json::parse(body);
                {
                    BAutolock lock(&fLock);
                    fCache[cacheKey] = {result, time(NULL)};
                }
                ok = true;
            } catch (...) {
                result = {{"status", status}, {"error", "invalid_json"}};
            }
        } else {
            result = {{"status", status}, {"body", body},
                {"retry_after", retryAfter}};
        }
        std::vector<JsonCallback> callbacks;
        {
            BAutolock lock(&fLock);
            auto pending = fPendingGets.find(cacheKey);
            if (pending != fPendingGets.end()) {
                callbacks.swap(pending->second);
                fPendingGets.erase(pending);
            }
        }
        for (const JsonCallback& current : callbacks) {
            if (current)
                current(ok, result);
        }
    });
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
    std::string key = _CacheKey(path);
    BAutolock lock(&fLock);
    fCache.erase(key);
}

void
SpotifyRequestClient::InvalidateCachePrefix(const std::string& prefix)
{
    std::string accountPrefix = _CacheKey("");
    BAutolock lock(&fLock);
    for (auto it = fCache.begin(); it != fCache.end();) {
        if (it->first.rfind(accountPrefix + prefix, 0) == 0)
            it = fCache.erase(it);
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
