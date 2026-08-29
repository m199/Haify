#include "SpotifyUrl.h"

#include <cctype>
#include <cstdio>

std::string
SpotifyUrlEncode(const std::string& value)
{
    std::string encoded;
    encoded.reserve(value.size() * 3);
    for (unsigned char c : value) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += (char)c;
        } else {
            char buffer[4];
            snprintf(buffer, sizeof(buffer), "%%%02X", c);
            encoded += buffer;
        }
    }
    return encoded;
}
