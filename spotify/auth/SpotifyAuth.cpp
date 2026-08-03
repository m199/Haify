#include "SpotifyAuth.h"
#include "Config.h"
#include "HttpClient.h"
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>


namespace {

static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define ROR32(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define CH(x,y,z)  (((x)&(y))^(~(x)&(z)))
#define MAJ(x,y,z) (((x)&(y))^((x)&(z))^((y)&(z)))
#define EP0(x)     (ROR32(x,2)^ROR32(x,13)^ROR32(x,22))
#define EP1(x)     (ROR32(x,6)^ROR32(x,11)^ROR32(x,25))
#define SIG0(x)    (ROR32(x,7)^ROR32(x,18)^((x)>>3))
#define SIG1(x)    (ROR32(x,17)^ROR32(x,19)^((x)>>10))

static void sha256_transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t w[64], a,b,c,d,e,f,g,h,t1,t2;
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)block[i*4]<<24)|((uint32_t)block[i*4+1]<<16)
              |((uint32_t)block[i*4+2]<<8)|(uint32_t)block[i*4+3];
    for (int i = 16; i < 64; i++)
        w[i] = SIG1(w[i-2]) + w[i-7] + SIG0(w[i-15]) + w[i-16];

    a=state[0]; b=state[1]; c=state[2]; d=state[3];
    e=state[4]; f=state[5]; g=state[6]; h=state[7];

    for (int i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e,f,g) + K[i] + w[i];
        t2 = EP0(a) + MAJ(a,b,c);
        h=g; g=f; f=e; e=d+t1;
        d=c; c=b; b=a; a=t1+t2;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
    state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
}

static void sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
    uint32_t state[8] = {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
    };
    uint8_t block[64];
    size_t  i     = 0;
    uint64_t bits = (uint64_t)len * 8;

    while (len - i >= 64) {
        sha256_transform(state, data + i);
        i += 64;
    }
    size_t rem = len - i;
    memcpy(block, data + i, rem);
    block[rem++] = 0x80;
    if (rem > 56) {
        memset(block + rem, 0, 64 - rem);
        sha256_transform(state, block);
        rem = 0;
    }
    memset(block + rem, 0, 56 - rem);
    for (int k = 7; k >= 0; k--) { block[56 + (7-k)] = (uint8_t)(bits >> (k*8)); }
    sha256_transform(state, block);

    for (int k = 0; k < 8; k++) {
        out[k*4+0] = (uint8_t)(state[k] >> 24);
        out[k*4+1] = (uint8_t)(state[k] >> 16);
        out[k*4+2] = (uint8_t)(state[k] >>  8);
        out[k*4+3] = (uint8_t)(state[k]);
    }
}


static const char kB64URL[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static std::string base64url_encode(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t b = (uint32_t)data[i] << 16;
        if (i + 1 < len) b |= (uint32_t)data[i+1] << 8;
        if (i + 2 < len) b |= data[i+2];
        out += kB64URL[(b >> 18) & 63];
        out += kB64URL[(b >> 12) & 63];
        if (i + 1 < len) out += kB64URL[(b >> 6) & 63];
        if (i + 2 < len) out += kB64URL[b & 63];
    }
    return out;
}


static std::string url_encode(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += (char)c;
        else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

static bool random_bytes(uint8_t* data, size_t size) {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return false;

    size_t offset = 0;
    while (offset < size) {
        ssize_t bytes = read(fd, data + offset, size - offset);
        if (bytes <= 0) {
            close(fd);
            return false;
        }
        offset += (size_t)bytes;
    }
    close(fd);
    return true;
}

static TokenResult parse_token_response(int status, const std::string& response,
    const std::string& currentRefreshToken)
{
    TokenResult result;
    result.httpStatus = status;
    try {
        auto json = nlohmann::json::parse(response);
        if (status >= 200 && status < 300) {
            result.accessToken = json.value("access_token", "");
            result.refreshToken = json.value("refresh_token", currentRefreshToken);
            result.scopes = json.value("scope", "");
            result.expiresIn = json.value("expires_in", 0);
            result.success = !result.accessToken.empty();
            if (!result.success)
                result.error = "missing_access_token";
        } else {
            result.error = json.value("error", "token_request_failed");
            result.errorDescription = json.value("error_description", "");
        }
    } catch (...) {
        result.error = status >= 200 && status < 300
            ? "invalid_token_response" : "token_request_failed";
        result.errorDescription = response.substr(0, 200);
    }
    return result;
}

}


SpotifyAuth::SpotifyAuth(const std::string& clientId)
    : fClientId(clientId) {}

std::string SpotifyAuth::BuildAuthUrl() {
    uint8_t rnd[32] = {};
    uint8_t state[24] = {};
    if (!random_bytes(rnd, sizeof(rnd)) || !random_bytes(state, sizeof(state)))
        return "";
    fCodeVerifier = base64url_encode(rnd, sizeof(rnd));
    fState = base64url_encode(state, sizeof(state));


    uint8_t hash[32];
    sha256((const uint8_t*)fCodeVerifier.data(), fCodeVerifier.size(), hash);
    std::string challenge = base64url_encode(hash, 32);

    return std::string(SPOTIFY_AUTH_URL)
        + "?client_id="             + fClientId
        + "&response_type=code"
        + "&redirect_uri="          + url_encode(SPOTIFY_REDIRECT)
        + "&scope="                 + url_encode(SPOTIFY_SCOPES)
        + "&state="                 + url_encode(fState)
        + "&code_challenge_method=S256"
        + "&code_challenge="        + challenge;
}

void SpotifyAuth::ExchangeCode(const std::string& code, TokenCallback callback) {
    std::string body =
        "grant_type=authorization_code"
        "&code="          + url_encode(code)
        + "&redirect_uri="  + url_encode(SPOTIFY_REDIRECT)
        + "&client_id="     + url_encode(fClientId)
        + "&code_verifier=" + url_encode(fCodeVerifier);

    Headers headers = {{"Content-Type", "application/x-www-form-urlencoded"}};

    HttpClient::Post(SPOTIFY_TOKEN_URL, headers, body,
        [callback](const HttpResponse& response) {
            if (callback)
                callback(parse_token_response(response.statusCode, response.body, ""));
        });
}

void SpotifyAuth::RefreshToken(const std::string& refreshToken, TokenCallback callback) {
    std::string body =
        "grant_type=refresh_token"
        "&refresh_token=" + url_encode(refreshToken)
        + "&client_id="     + url_encode(fClientId);

    Headers headers = {{"Content-Type", "application/x-www-form-urlencoded"}};

    HttpClient::Post(SPOTIFY_TOKEN_URL, headers, body,
        [callback, refreshToken](const HttpResponse& response) {
            if (callback)
                callback(parse_token_response(response.statusCode, response.body,
                    refreshToken));
        });
}
