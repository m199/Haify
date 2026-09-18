#include "SpotifyResponse.h"

#include <limits>

namespace {
int ResponseInteger(const nlohmann::json& data, const char* field)
{
    if (!data.is_object()) return -1;
    auto value = data.find(field);
    if (value == data.end() || !value->is_number_integer()) return -1;
    if (value->is_number_unsigned()) {
        auto number = value->get<uint64_t>();
        return number <= uint64_t(std::numeric_limits<int>::max()) ? static_cast<int>(number) : -1;
    }
    auto number = value->get<int64_t>();
    return number >= 0 && number <= std::numeric_limits<int>::max() ? static_cast<int>(number) : -1;
}
}

int
SpotifyResponseStatus(const nlohmann::json& data)
{
    return ResponseInteger(data, "status");
}

int
SpotifyResponseRetryAfter(const nlohmann::json& data)
{
    return ResponseInteger(data, "retry_after");
}

std::string
SpotifyResponseErrorReason(const nlohmann::json& data)
{
    if (!data.is_object())
        return "invalid_response";
    if (data.contains("reason") && data["reason"].is_string())
        return data["reason"].get<std::string>();
    if (data.contains("error") && data["error"].is_string())
        return data["error"].get<std::string>();
    if (data.contains("error") && data["error"].is_object()) {
        const auto& error = data["error"];
        if (error.contains("reason") && error["reason"].is_string())
            return error["reason"].get<std::string>();
        if (error.contains("message") && error["message"].is_string())
            return error["message"].get<std::string>();
    }
    if (data.contains("body") && data["body"].is_string()) {
        try {
            nlohmann::json body = nlohmann::json::parse(
                data["body"].get<std::string>());
            return SpotifyResponseErrorReason(body);
        } catch (...) {
        }
    }
    return "spotify_request_failed";
}

bool
SpotifyResponseIsTemporaryFailure(const nlohmann::json& data)
{
    int status = SpotifyResponseStatus(data);
    return status < 0 || status == 401 || status == 408 || status == 425
        || status == 429 || status >= 500;
}
