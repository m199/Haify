#pragma once

#include "SpotifyApiTypes.h"

#include <functional>
#include <Locker.h>
#include <map>
#include <string>
#include <vector>

class SpotifyRequestClient {
public:
    using RequestCompletion = std::function<void(int status,
        const std::string& body, int retryAfter)>;
    using RequestHandler = std::function<void(const std::string& method,
        const std::string& path, const std::string& body,
        const std::string& contentType, RequestCompletion completion)>;
    using RawCallback = std::function<void(int status, const std::string& body,
        int retryAfter)>;

    explicit        SpotifyRequestClient(const std::string& accessToken);

    bool            SetAccessToken(const std::string& token);
    bool            SetAccountId(const std::string& accountId);
    std::string     AccountId() const;
    void            SetTokenRefreshHandler(TokenRefreshHandler handler);
    void            SetRequestHandler(RequestHandler handler);
    void            ClearSession();

    void            Request(const std::string& method, const std::string& path,
                            const std::string& body, RawCallback callback,
                            bool allowRefresh = true,
                            const std::string& contentType = "application/json");
    void            Get(const std::string& path, JsonCallback callback);
    void            Put(const std::string& path, const std::string& body,
                        JsonCallback callback);
    void            Post(const std::string& path, const std::string& body,
                         JsonCallback callback);
    void            Delete(const std::string& path, const std::string& body,
                           JsonCallback callback);
    void            EraseCache(const std::string& path);
    void            InvalidateCachePrefix(const std::string& prefix);

private:
    std::string     _CacheKey(const std::string& path) const;

    std::string     fAccessToken;
    std::string     fAccountId;
    TokenRefreshHandler fTokenRefreshHandler;
    RequestHandler  fRequestHandler;
    mutable BLocker fLock;
    std::map<std::string, ApiCacheEntry> fCache;
    std::map<std::string, std::vector<JsonCallback>> fPendingGets;
};
