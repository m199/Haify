#pragma once

#include "SpotifyApiTypes.h"

#include <functional>
#include <string>

class ProfileApi {
public:
    using GetHandler = std::function<void(const std::string& path,
        JsonCallback callback)>;

    explicit        ProfileApi(GetHandler get);

    void            GetCurrentUserProfile(JsonCallback callback);

private:
    GetHandler      fGet;
};
