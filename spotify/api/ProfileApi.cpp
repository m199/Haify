#include "ProfileApi.h"

#include <utility>

ProfileApi::ProfileApi(GetHandler get, CacheHandler eraseCache)
    : fGet(std::move(get)), fEraseCache(std::move(eraseCache))
{
}

void
ProfileApi::RefreshCurrentUserProfile(JsonCallback callback)
{
    if (fEraseCache) fEraseCache("/me");
    fGet("/me", callback);
}

void
ProfileApi::GetCurrentUserProfile(JsonCallback callback)
{
    fGet("/me", callback);
}
