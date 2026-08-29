#include "ProfileApi.h"

#include <utility>

ProfileApi::ProfileApi(GetHandler get)
    : fGet(std::move(get))
{
}

void
ProfileApi::GetCurrentUserProfile(JsonCallback callback)
{
    fGet("/me", callback);
}
