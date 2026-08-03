#include "HttpClient.h"

#include <DataIO.h>
#include <HttpHeaders.h>
#include <HttpRequest.h>
#include <NetworkAddress.h>
#include <OS.h>
#include <SecureSocket.h>
#include <Url.h>
#include <UrlProtocolRoster.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

using namespace BPrivate::Network;

struct ReqState {
    std::string  url;
    std::string  method;
    Headers      headers;
    std::string  body;
    HttpCallback cb;
};

static std::string
Lowercase(const std::string& value)
{
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char character) { return (char)std::tolower(character); });
    return result;
}

static std::string
Trim(const std::string& value)
{
    size_t start = 0;
    while (start < value.size()
            && std::isspace((unsigned char)value[start])) {
        start++;
    }
    size_t end = value.size();
    while (end > start && std::isspace((unsigned char)value[end - 1]))
        end--;
    return value.substr(start, end - start);
}

static bool
DecodeChunkedBody(const std::string& encoded, std::string& decoded)
{
    size_t offset = 0;
    while (offset < encoded.size()) {
        size_t lineEnd = encoded.find("\r\n", offset);
        if (lineEnd == std::string::npos)
            return false;

        std::string sizeText = encoded.substr(offset, lineEnd - offset);
        size_t extension = sizeText.find(';');
        if (extension != std::string::npos)
            sizeText.erase(extension);
        sizeText = Trim(sizeText);

        errno = 0;
        char* end = nullptr;
        unsigned long long chunkSize = strtoull(sizeText.c_str(), &end, 16);
        if (errno != 0 || end == sizeText.c_str() || *end != '\0'
                || chunkSize > std::numeric_limits<size_t>::max()) {
            return false;
        }

        offset = lineEnd + 2;
        if (chunkSize == 0)
            return true;
        if ((size_t)chunkSize > encoded.size() - offset)
            return false;

        decoded.append(encoded, offset, (size_t)chunkSize);
        offset += (size_t)chunkSize;
        if (offset + 2 > encoded.size()
                || encoded.compare(offset, 2, "\r\n") != 0) {
            return false;
        }
        offset += 2;
    }
    return false;
}

static bool
ParseRawResponse(const std::string& raw, HttpResponse& response)
{
    size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        response.body = "Invalid HTTP response: missing headers";
        return false;
    }

    size_t statusEnd = raw.find("\r\n");
    size_t statusSpace = raw.find(' ');
    if (statusEnd == std::string::npos || statusSpace == std::string::npos
            || statusSpace >= statusEnd) {
        response.body = "Invalid HTTP response: missing status";
        return false;
    }
    response.statusCode = atoi(raw.c_str() + statusSpace + 1);
    if (response.statusCode <= 0) {
        response.statusCode = -1;
        response.body = "Invalid HTTP response status";
        return false;
    }

    bool chunked = false;
    long long contentLength = -1;
    size_t lineStart = statusEnd + 2;
    while (lineStart < headerEnd) {
        size_t lineEnd = raw.find("\r\n", lineStart);
        if (lineEnd == std::string::npos || lineEnd > headerEnd)
            lineEnd = headerEnd;
        size_t colon = raw.find(':', lineStart);
        if (colon != std::string::npos && colon < lineEnd) {
            std::string name = Lowercase(Trim(
                raw.substr(lineStart, colon - lineStart)));
            std::string value = Trim(raw.substr(colon + 1,
                lineEnd - colon - 1));
            if (name == "retry-after")
                response.retryAfter = atoi(value.c_str());
            else if (name == "transfer-encoding"
                    && Lowercase(value).find("chunked") != std::string::npos) {
                chunked = true;
            } else if (name == "content-length") {
                contentLength = strtoll(value.c_str(), nullptr, 10);
            }
        }
        lineStart = lineEnd + 2;
    }

    std::string encodedBody = raw.substr(headerEnd + 4);
    if (chunked) {
        if (!DecodeChunkedBody(encodedBody, response.body)) {
            response.statusCode = -1;
            response.body = "Invalid chunked HTTP response";
            return false;
        }
    } else {
        if (contentLength >= 0
                && (unsigned long long)contentLength > encodedBody.size()) {
            response.statusCode = -1;
            response.body = "Incomplete HTTP response body";
            return false;
        }
        response.body = encodedBody;
        if (contentLength >= 0)
            response.body.resize((size_t)contentLength);
    }
    return true;
}

static bool
WriteAll(BSecureSocket& socket, const std::string& data,
    std::string& error)
{
    size_t offset = 0;
    while (offset < data.size()) {
        ssize_t written = socket.Write(data.data() + offset,
            data.size() - offset);
        if (written == B_INTERRUPTED)
            continue;
        if (written <= 0) {
            error = "HTTPS write failed";
            return false;
        }
        offset += (size_t)written;
    }
    return true;
}

static HttpResponse
RunDeleteRequest(const ReqState& req)
{
    HttpResponse response;
    BUrl url(req.url.c_str(), false);
    if (!url.IsValid() || url.Protocol() != "https") {
        response.body = "DELETE requires a valid HTTPS URL";
        return response;
    }

    int port = url.HasPort() ? url.Port() : 443;
    BNetworkAddress address;
    status_t status = address.SetTo(url.Host().String(), (uint16)port);
    if (status != B_OK) {
        response.body = std::string("Could not resolve host: ")
            + strerror(status);
        return response;
    }

    BSecureSocket socket;
    status = socket.Connect(address, 30000000LL);
    if (status != B_OK) {
        response.body = std::string("HTTPS connection failed: ")
            + strerror(status);
        return response;
    }
    socket.SetTimeout(30000000LL);

    std::string target = url.Path().String();
    if (target.empty())
        target = "/";
    if (url.HasRequest()) {
        target += '?';
        target += url.Request().String();
    }

    std::string request = "DELETE " + target + " HTTP/1.1\r\n";
    request += "Host: ";
    request += url.Authority().String();
    request += "\r\nUser-Agent: Haify\r\n";
    request += "Accept: application/json\r\n";
    request += "Accept-Encoding: identity\r\n";
    request += "Connection: close\r\n";
    for (const auto& header : req.headers) {
        std::string name = Lowercase(header.first);
        if (name == "host" || name == "content-length"
                || name == "connection" || name == "accept-encoding") {
            continue;
        }
        if (header.first.find_first_of("\r\n") != std::string::npos
                || header.second.find_first_of("\r\n") != std::string::npos) {
            response.body = "Invalid HTTP header";
            return response;
        }
        request += header.first + ": " + header.second + "\r\n";
    }
    request += "Content-Length: " + std::to_string(req.body.size())
        + "\r\n\r\n" + req.body;

    if (!WriteAll(socket, request, response.body))
        return response;

    std::string raw;
    char buffer[8192];
    const size_t maxResponseSize = 16 * 1024 * 1024;
    for (;;) {
        ssize_t bytesRead = socket.Read(buffer, sizeof(buffer));
        if (bytesRead == B_INTERRUPTED)
            continue;
        if (bytesRead == 0)
            break;
        if (bytesRead < 0) {
            socket.Disconnect();
            if (!raw.empty() && ParseRawResponse(raw, response))
                return response;
            response.statusCode = -1;
            response.body = "HTTPS read failed";
            return response;
        }
        if ((size_t)bytesRead > maxResponseSize - raw.size()) {
            response.body = "HTTP response is too large";
            return response;
        }
        raw.append(buffer, (size_t)bytesRead);
    }
    socket.Disconnect();

    ParseRawResponse(raw, response);
    return response;
}

static int32
RunRequest(void* data)
{
    auto* req = static_cast<ReqState*>(data);

    if (req->method == "DELETE") {
        HttpResponse response = RunDeleteRequest(*req);
        req->cb(response);
        delete req;
        return 0;
    }

    BMallocIO output;
    BUrl burl(req->url.c_str(), false);
    BUrlRequest* urlReq = BUrlProtocolRoster::MakeRequest(burl, &output);

    if (!urlReq) {
        req->cb({-1, "MakeRequest failed", -1});
        delete req;
        return 1;
    }
    urlReq->SetTimeout(30000000LL);

    BHttpRequest* httpReq = dynamic_cast<BHttpRequest*>(urlReq);
    if (httpReq) {
        httpReq->SetMethod(req->method.c_str());
        httpReq->SetFollowLocation(true);

        BHttpHeaders* headers = new BHttpHeaders();
        for (const auto& header : req->headers)
            headers->AddHeader(header.first.c_str(), header.second.c_str());
        httpReq->AdoptHeaders(headers);

        BMallocIO* body = new BMallocIO();
        if (!req->body.empty()) {
            body->Write(req->body.data(), req->body.size());
            body->Seek(0, SEEK_SET);
        }
        httpReq->AdoptInputData(body, (ssize_t)req->body.size());
    }

    thread_id thread = urlReq->Run();
    status_t exitValue = B_OK;
    if (thread >= 0)
        wait_for_thread(thread, &exitValue);

    int statusCode = -1;
    int retryAfter = -1;
    if (httpReq) {
        const BHttpResult& result
            = static_cast<const BHttpResult&>(urlReq->Result());
        statusCode = (int)result.StatusCode();
        if (result.HasHeaders()) {
            const char* value = result.Headers().HeaderValue("Retry-After");
            if (value)
                retryAfter = atoi(value);
        }
    }

    std::string body((const char*)output.Buffer(), output.BufferLength());
    req->cb({statusCode, body, retryAfter});

    delete urlReq;
    delete req;
    return 0;
}

static void
dispatch(const std::string& url, const std::string& method,
    const Headers& headers, const std::string& body, HttpCallback callback)
{
    auto* req = new ReqState{url, method, headers, body, std::move(callback)};
    thread_id thread = spawn_thread(RunRequest, "http_request", B_LOW_PRIORITY,
        req);
    if (thread < 0) {
        req->cb({-1, "spawn_thread failed", -1});
        delete req;
    } else {
        resume_thread(thread);
    }
}

void
HttpClient::Get(const std::string& url, const Headers& headers, HttpCallback cb)
{
    dispatch(url, "GET", headers, "", std::move(cb));
}

void
HttpClient::Post(const std::string& url, const Headers& headers,
    const std::string& body, HttpCallback cb)
{
    dispatch(url, "POST", headers, body, std::move(cb));
}

void
HttpClient::Put(const std::string& url, const Headers& headers,
    const std::string& body, HttpCallback cb)
{
    dispatch(url, "PUT", headers, body, std::move(cb));
}

void
HttpClient::Delete(const std::string& url, const Headers& headers,
    const std::string& body, HttpCallback cb)
{
    dispatch(url, "DELETE", headers, body, std::move(cb));
}
