#pragma once

#include "SpotifyApiTypes.h"

#include <string>

int         SpotifyResponseStatus(const nlohmann::json& data);
int         SpotifyResponseRetryAfter(const nlohmann::json& data);
std::string SpotifyResponseErrorReason(const nlohmann::json& data);
bool        SpotifyResponseIsTemporaryFailure(const nlohmann::json& data);
