#include "SpotifyResponse.h"

int
SpotifyResponseStatus(const nlohmann::json& data)
{
    return data.is_object() ? data.value("status", -1) : -1;
}

int
SpotifyResponseRetryAfter(const nlohmann::json& data)
{
    return data.is_object() ? data.value("retry_after", -1) : -1;
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
