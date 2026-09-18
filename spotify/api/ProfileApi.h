#pragma once

#include "SpotifyApiTypes.h"

#include <functional>
#include <string>

class ProfileApi {
public:
    using GetHandler = std::function<void(const std::string& path,
        JsonCallback callback)>;
    using CacheHandler = std::function<void(const std::string& path)>;

    explicit        ProfileApi(GetHandler get, CacheHandler eraseCache = {});

    void            GetCurrentUserProfile(JsonCallback callback);
    // Identity refresh must not join a pre-authentication GET for /me.
    void            RefreshCurrentUserProfile(JsonCallback callback);

private:
    GetHandler      fGet;
    CacheHandler    fEraseCache;
};
