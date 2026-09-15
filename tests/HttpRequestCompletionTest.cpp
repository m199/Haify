#include "network/HttpRequestCompletion.h"

#include <cassert>
#include <cstdio>

#ifdef NDEBUG
#error HTTP completion tests require assertions; compile without NDEBUG.
#endif

using namespace HttpRequestCompletion;

static void
TestInterruptedWait()
{
    int waits = 0;
    bool workerFinished = false;
    int reportedHttpStatus = 0;
    auto result = WaitUntilFinished([&]() -> status_t {
        waits++;
        if (waits < 3)
            return B_INTERRUPTED;
        workerFinished = true;
        reportedHttpStatus = 204;
        return B_OK;
    });
    assert(result.status == B_OK && result.interruptions == 2);
    assert(waits == 3 && workerFinished);
    assert(ResponseStatus(B_OK, reportedHttpStatus) == 204);
}

static void
TestWaitFailures()
{
    int waits = 0;
    auto result = WaitUntilFinished([&]() -> status_t {
        waits++;
        return waits == 1 ? B_INTERRUPTED : B_BAD_THREAD_ID;
    });
    assert(result.status == B_BAD_THREAD_ID && result.interruptions == 1);
    assert(waits == 2);

    auto completed = WaitUntilFinished([]() -> status_t { return B_OK; });
    assert(completed.status == B_OK && completed.interruptions == 0);
}

static void
TestHttpAndNativeStatus()
{
    const int httpStatuses[] = {200, 204, 401, 403, 404, 429, 500, 503};
    for (int status : httpStatuses)
        assert(ResponseStatus(B_OK, status) == status);
    // Haiku's completed 404 must still reach Spotify's classified fallback.
    assert(ResponseStatus(B_RESOURCE_NOT_FOUND, 404) == 404);
    assert(ResponseStatus(B_RESOURCE_NOT_FOUND, 200) == -1);
    assert(ResponseStatus(B_OK, 0) == -1);
    assert(ResponseStatus(B_OK, -1) == -1);
    assert(ResponseStatus(B_OK, 99) == -1);
    assert(ResponseStatus(B_OK, 600) == -1);
    assert(ResponseStatus(B_SERVER_NOT_FOUND, 0) == -1);
    assert(ResponseStatus(B_BUSY, 0) == -1);
    // Receiving headers does not make an interrupted/failed transfer complete.
    assert(ResponseStatus(B_IO_ERROR, 200) == -1);
    assert(ResponseStatus(B_IO_ERROR, 404) == -1);
}

int
main()
{
    TestInterruptedWait();
    TestWaitFailures();
    TestHttpAndNativeStatus();
    std::puts("HTTP request completion tests passed.");
}
