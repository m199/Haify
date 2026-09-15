#pragma once

#include <SupportDefs.h>

namespace HttpRequestCompletion {

struct WaitResult {
    status_t status = B_OK;
    unsigned int interruptions = 0;
};

// An interrupted join is not request completion. Retry only the wait; the
// HTTP operation itself must not be dispatched again.
template<typename Wait>
WaitResult
WaitUntilFinished(Wait wait)
{
    WaitResult result;
    for (;;) {
        result.status = wait();
        if (result.status != B_INTERRUPTED)
            return result;
        result.interruptions++;
    }
}

inline int
ResponseStatus(status_t nativeStatus, int httpStatus)
{
    if (httpStatus < 100 || httpStatus > 599)
        return -1;
    // libnetservices reports HTTP 404 as B_RESOURCE_NOT_FOUND. Preserve the
    // real HTTP response so existing endpoint fallbacks keep their meaning.
    if (nativeStatus == B_OK
            || (nativeStatus == B_RESOURCE_NOT_FOUND && httpStatus == 404)) {
        return httpStatus;
    }
    return -1;
}

} // namespace HttpRequestCompletion
