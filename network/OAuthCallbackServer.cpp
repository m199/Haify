#include "OAuthCallbackServer.h"

#include <Autolock.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
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

static const char kFailurePage[] =
    "HTTP/1.1 400 Bad Request\r\nContent-Type: text/html; charset=utf-8\r\n"
    "Connection: close\r\n\r\n"
    "<!DOCTYPE html><html><body style='font-family:sans-serif;padding:40px'>"
    "<h2>Sign-in failed</h2><p>Please return to Haify and try again.</p>"
    "</body></html>\r\n";

static const char kNotFoundPage[] =
    "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n"
    "Connection: close\r\n\r\n";

// A browser that opened an idle connection must not hold the listener: each
// read and write on an accepted connection gives up after this long.
static const time_t kClientTimeoutSeconds = 5;

static std::string
ReadHttpRequest(int client)
{
    std::string request;
    char buffer[2048];
    while (request.size() < 16384
        && request.find("\r\n\r\n") == std::string::npos) {
        ssize_t bytes = recv(client, buffer, sizeof(buffer), 0);
        if (bytes < 0 && errno == EINTR)
            continue;
        if (bytes <= 0)
            break;
        request.append(buffer, (size_t)bytes);
    }
    return request;
}

static void
SetCloseOnExec(int descriptor)
{
    int flags = fcntl(descriptor, F_GETFD);
    if (flags >= 0)
        fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC);
}

static void
SetClientTimeouts(int client)
{
    struct timeval timeout{};
    timeout.tv_sec = kClientTimeoutSeconds;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

static void
SendCallbackResponse(int client, const OAuthCallbackRequest& request)
{
    const char* page = kNotFoundPage;
    if (request.isCallback)
        page = request.code.empty() ? kFailurePage : kSuccessPage;
    send(client, page, strlen(page), 0);
}

OAuthCallbackServer::OAuthCallbackServer(int port, AuthCodeCallback callback)
    : fPort(port), fSocket(-1), fClient(-1), fThread(-1),
      fLock("OAuth callback"), fCallback(std::move(callback)) {}

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
    // librespot is started with fork/exec and must not inherit port 8765.
    SetCloseOnExec(fSocket);

    int opt = 1;
    setsockopt(fSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)fPort);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(fSocket, reinterpret_cast<struct sockaddr*>(&addr),
            sizeof(addr)) < 0) {
        close(fSocket);
        fSocket = -1;
        return false;
    }
    if (listen(fSocket, 4) < 0) {
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
        // The listener thread owns and closes the client; shutdown only
        // wakes a blocked recv/send so the join below cannot hang.
        if (fClient >= 0)
            shutdown(fClient, SHUT_RDWR);
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

bool OAuthCallbackServer::_IsListening(int listener) {
    BAutolock lock(&fLock);
    return listener >= 0 && fSocket == listener;
}

// Serves one accepted connection. Returns false when Stop() raced the accept.
bool OAuthCallbackServer::_ServeClient(int listener, int client,
    OAuthCallbackRequest& result) {
    {
        BAutolock lock(&fLock);
        if (fSocket != listener) {
            close(client);
            return false;
        }
        fClient = client;
    }
    SetCloseOnExec(client);
    SetClientTimeouts(client);

    result = ParseOAuthCallbackRequest(ReadHttpRequest(client));
    SendCallbackResponse(client, result);

    BAutolock lock(&fLock);
    fClient = -1;
    close(client);
    return true;
}

int32 OAuthCallbackServer::_ListenThread(void* data) {
    auto* self = static_cast<OAuthCallbackServer*>(data);
    int listener;
    {
        BAutolock lock(&self->fLock);
        listener = self->fSocket;
    }

    OAuthCallbackRequest result;
    while (self->_IsListening(listener)) {
        int client = accept(listener, nullptr, nullptr);
        if (client < 0) {
            if (errno == EINTR || errno == ECONNABORTED)
                continue;
            break;
        }
        if (!self->_ServeClient(listener, client, result))
            break;
        if (result.isCallback)
            break;
    }

    {
        BAutolock lock(&self->fLock);
        if (self->fSocket == listener) {
            close(listener);
            self->fSocket = -1;
        }
    }

    // Stop() before a callback arrived ends the thread without a result.
    if (!result.isCallback)
        return 1;
    if (self->fCallback)
        self->fCallback(result.code, result.state, result.error);
    return 0;
}
