#include "OAuthCallbackServer.h"

#include <Autolock.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>

static const char kSuccessPage[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Connection: close\r\n\r\n"
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<title>Haify</title></head><body style='font-family:sans-serif;padding:40px'>"
    "<h2>&#10003; Authentication Successful</h2>"
    "<p>You can close this window and return to Haify.</p>"
    "</body></html>\r\n";

static std::string
UrlDecode(const std::string& value)
{
    std::string decoded;
    decoded.reserve(value.size());
    for (size_t i = 0; i < value.size(); i++) {
        if (value[i] == '+') {
            decoded += ' ';
        } else if (value[i] == '%' && i + 2 < value.size()) {
            char hex[3] = {value[i + 1], value[i + 2], 0};
            char* end = nullptr;
            long byte = strtol(hex, &end, 16);
            if (end && *end == 0) {
                decoded += (char)byte;
                i += 2;
            } else {
                decoded += value[i];
            }
        } else {
            decoded += value[i];
        }
    }
    return decoded;
}

static std::map<std::string, std::string>
ParseQuery(const std::string& query)
{
    std::map<std::string, std::string> values;
    size_t start = 0;
    while (start <= query.size()) {
        size_t end = query.find('&', start);
        std::string part = query.substr(start,
            end == std::string::npos ? std::string::npos : end - start);
        size_t equals = part.find('=');
        std::string key = UrlDecode(part.substr(0, equals));
        std::string value = equals == std::string::npos
            ? "" : UrlDecode(part.substr(equals + 1));
        if (!key.empty())
            values[key] = value;
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return values;
}

OAuthCallbackServer::OAuthCallbackServer(int port, AuthCodeCallback callback)
    : fPort(port), fSocket(-1), fThread(-1), fLock("OAuth callback"),
      fCallback(std::move(callback)) {}

OAuthCallbackServer::~OAuthCallbackServer() {
    Stop();
}

bool OAuthCallbackServer::Start() {
    BAutolock lock(&fLock);
    if (fSocket >= 0 || fThread >= 0)
        return false;

    fSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (fSocket < 0)
        return false;

    int opt = 1;
    setsockopt(fSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)fPort);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(fSocket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fSocket);
        fSocket = -1;
        return false;
    }
    if (listen(fSocket, 1) < 0) {
        close(fSocket);
        fSocket = -1;
        return false;
    }

    fThread = spawn_thread(_ListenThread, "oauth_cb", B_NORMAL_PRIORITY, this);
    if (fThread < 0) {
        close(fSocket);
        fSocket = -1;
        return false;
    }
    resume_thread(fThread);
    return true;
}

void OAuthCallbackServer::Stop() {
    int socket = -1;
    thread_id thread = -1;
    {
        BAutolock lock(&fLock);
        socket = fSocket;
        fSocket = -1;
        thread = fThread;
    }

    if (socket >= 0) {
        shutdown(socket, SHUT_RDWR);
        close(socket);
    }
    if (thread >= 0 && thread != find_thread(nullptr)) {
        status_t result;
        wait_for_thread(thread, &result);
        BAutolock lock(&fLock);
        if (fThread == thread)
            fThread = -1;
    }
}

int32 OAuthCallbackServer::_ListenThread(void* data) {
    auto* self = static_cast<OAuthCallbackServer*>(data);
    int listener;
    {
        BAutolock lock(&self->fLock);
        listener = self->fSocket;
    }

    int client = accept(listener, nullptr, nullptr);
    {
        BAutolock lock(&self->fLock);
        if (self->fSocket == listener) {
            close(listener);
            self->fSocket = -1;
        }
    }

    if (client < 0)
        return 1;

    std::string request;
    char buffer[2048];
    while (request.size() < 16384 && request.find("\r\n\r\n") == std::string::npos) {
        ssize_t bytes = recv(client, buffer, sizeof(buffer), 0);
        if (bytes <= 0)
            break;
        request.append(buffer, (size_t)bytes);
    }

    std::string method;
    std::string target;
    size_t firstSpace = request.find(' ');
    size_t secondSpace = firstSpace == std::string::npos
        ? std::string::npos : request.find(' ', firstSpace + 1);
    if (firstSpace != std::string::npos && secondSpace != std::string::npos) {
        method = request.substr(0, firstSpace);
        target = request.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    }

    size_t question = target.find('?');
    std::string path = target.substr(0, question);
    std::map<std::string, std::string> params = question == std::string::npos
        ? std::map<std::string, std::string>()
        : ParseQuery(target.substr(question + 1));
    std::string code = params["code"];
    std::string state = params["state"];
    std::string error = params["error"];

    if (method != "GET" || path != "/callback")
        error = "invalid_callback_request";
    else if (code.empty() && error.empty())
        error = "missing_authorization_code";

    if (!code.empty()) {
        send(client, kSuccessPage, strlen(kSuccessPage), 0);
    } else {
        std::string errBody =
            "HTTP/1.1 400 Bad Request\r\nContent-Type: text/html; charset=utf-8\r\n"
            "Connection: close\r\n\r\n"
            "<!DOCTYPE html><html><body style='font-family:sans-serif;padding:40px'>"
            "<h2>Sign-in failed</h2><p>Please return to Haify and try again.</p>"
            "</body></html>\r\n";
        send(client, errBody.c_str(), errBody.size(), 0);
    }
    close(client);

    if (self->fCallback)
        self->fCallback(code, state, error);

    return 0;
}
