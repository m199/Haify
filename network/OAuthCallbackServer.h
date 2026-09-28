#pragma once

#include "OAuthCallbackRequest.h"

#include <functional>
#include <Locker.h>
#include <string>
#include <OS.h>

using AuthCodeCallback = std::function<void(const std::string& code,
    const std::string& state, const std::string& error)>;

// Local redirect listener for one interactive sign-in. The listener thread
// serves connections until a "GET /callback" arrives, answers anything else
// with 404, and reports exactly one result through the callback.
// Stop() never waits for a remote peer: it shuts down the listener and the
// connection currently being read, and each read is bounded by a timeout.
class OAuthCallbackServer {
public:
                    OAuthCallbackServer(int port, AuthCodeCallback callback);
                    ~OAuthCallbackServer();

    bool            Start();
    void            Stop();

private:
    int             fPort;
    int             fSocket;
    int             fClient;
    thread_id       fThread;
    BLocker         fLock;
    AuthCodeCallback fCallback;

    static int32    _ListenThread(void* data);
    bool            _IsListening(int listener);
    bool            _ServeClient(int listener, int client,
                        OAuthCallbackRequest& result);
};
