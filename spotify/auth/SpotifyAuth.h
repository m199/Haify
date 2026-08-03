#pragma once

#include <functional>
#include <string>

struct TokenResult {
    bool        success = false;
    int         httpStatus = -1;
    std::string accessToken;
    std::string refreshToken;
    std::string scopes;
    int         expiresIn = 0;
    std::string error;
    std::string errorDescription;
};

using TokenCallback = std::function<void(const TokenResult& result)>;

class SpotifyAuth {
public:
    explicit    SpotifyAuth(const std::string& clientId);


    std::string BuildAuthUrl();
    const std::string& State() const { return fState; }


    void        ExchangeCode(const std::string& code, TokenCallback callback);


    void        RefreshToken(const std::string& refreshToken, TokenCallback callback);

private:
    std::string fClientId;
    std::string fCodeVerifier;
    std::string fState;
};
