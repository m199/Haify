#pragma once

#include "SpotifyApiTypes.h"

#include <functional>
#include <Locker.h>
#include <map>
#include <memory>
#include <optional>
#include <cstdint>
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
    // Only synchronous request submission may run here, never a wait for I/O.
    bool            DispatchForAccount(const std::string& account,
                        const std::function<void()>& operation);
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
    // Drops the stored value and detaches an in-flight GET, for data a
    // mutation just changed.
    void            EraseCache(const std::string& path);
    // Drops only the stored value; the next Get joins a GET already in
    // flight. For live reads whose in-flight answer is equally current.
    void            ExpireCachedValue(const std::string& path);
    void            InvalidateCachePrefix(const std::string& prefix);

private:
	struct PendingGet {
		std::vector<JsonCallback> callbacks;
	};
	void            _Request(const std::string& method, const std::string& path,
	                    const std::string& body, RawCallback callback, bool allowRefresh,
	                    const std::string& contentType, std::optional<uint64_t> session);
	bool            _SessionMatches(uint64_t session) const;
	void            _NoteRateLimit(int retryAfterSeconds, int64_t nowUs);
	int             _RateLimitRemainingSeconds(int64_t nowUs) const;
	bool            _RefuseWhileRateLimited(const RawCallback& callback) const;
    std::string     _CacheKey(const std::string& path) const;

    std::string     fAccessToken;
    std::string     fAccountId;
    TokenRefreshHandler fTokenRefreshHandler;
    RequestHandler  fRequestHandler;
    mutable BLocker fLock;
    std::map<std::string, ApiCacheEntry> fCache;
    std::map<std::string, std::shared_ptr<PendingGet>> fPendingGets;
	uint64_t        fSessionGeneration = 0;
	// system_time() until which requests are answered locally with 429.
	int64_t         fRateLimitedUntilUs = 0;
};
