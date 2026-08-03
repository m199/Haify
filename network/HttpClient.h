#pragma once

#include <functional>
#include <map>
#include <string>

using Headers     = std::map<std::string, std::string>;
struct HttpResponse {
                HttpResponse(int status = -1, const std::string& content = "",
                    int retry = -1)
                    : statusCode(status), body(content), retryAfter(retry) {}
    int         statusCode;
    std::string body;
    int         retryAfter;
};
using HttpCallback = std::function<void(const HttpResponse& response)>;

class HttpClient {
public:
    static void Get(const std::string& url, const Headers& headers,
                    HttpCallback callback);
    static void Post(const std::string& url, const Headers& headers,
                     const std::string& body, HttpCallback callback);
    static void Put(const std::string& url, const Headers& headers,
                    const std::string& body, HttpCallback callback);
    static void Delete(const std::string& url, const Headers& headers,
                       const std::string& body, HttpCallback callback);
};
