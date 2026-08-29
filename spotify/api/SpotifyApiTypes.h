#pragma once

#include <functional>
#include <string>
#include <time.h>
#include <nlohmann/json.hpp>

using JsonCallback = std::function<void(bool ok, const nlohmann::json& data)>;
using TokenRefreshCompletion = std::function<void(bool ok)>;
using TokenRefreshHandler
    = std::function<void(TokenRefreshCompletion completion)>;

struct ApiCacheEntry {
    nlohmann::json data;
    time_t timestamp;
};
