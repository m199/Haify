#pragma once

#include <functional>
#include <Locker.h>
#include <string>
#include <OS.h>

using AuthCodeCallback = std::function<void(const std::string& code,
    const std::string& state, const std::string& error)>;

class OAuthCallbackServer {
public:
                    OAuthCallbackServer(int port, AuthCodeCallback callback);
                    ~OAuthCallbackServer();

    bool            Start();
    void            Stop();

private:
    int             fPort;
    int             fSocket;
    thread_id       fThread;
    BLocker         fLock;
    AuthCodeCallback fCallback;

    static int32    _ListenThread(void* data);
};
