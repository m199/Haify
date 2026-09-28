#include "playlist/PlaylistCacheKeys.h"

#include <cstdio>
#include <cstdlib>

static void
CheckCondition(bool condition, const char* expression, const char* function, int line)
{
	if (condition)
		return;
	std::fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, line, function, expression);
	std::abort();
}

#define Check(condition) CheckCondition((condition), #condition, __func__, __LINE__)

static void
CheckIdsCannotLeaveTheCacheDirectory()
{
	Check(PlaylistCacheKeys::IsSafeId("37i9dQZF1DXcBWIGoYBM5M"));
	Check(PlaylistCacheKeys::IsSafeId("liked-songs"));
	Check(!PlaylistCacheKeys::IsSafeId(""));
	Check(!PlaylistCacheKeys::IsSafeId(".."));
	Check(!PlaylistCacheKeys::IsSafeId("../../settings"));
	Check(!PlaylistCacheKeys::IsSafeId("a/b"));
	Check(!PlaylistCacheKeys::IsSafeId("id?market=DE"));
	Check(!PlaylistCacheKeys::IsSafeId(std::string(129, 'a')));
}

static void
CheckUriIdsAreValidated()
{
	Check(SpotifyItemIdForUri("spotify:playlist:abc123") == "abc123");
	Check(SpotifyItemIdForUri("spotify:episode:chapter-one") == "chapter-one");
	Check(SpotifyItemIdForUri("spotify:playlist:../../x").empty());
	Check(SpotifyItemIdForUri("spotify:track:a&b").empty());
	Check(SpotifyItemIdForUri("spotify:collection").empty());
}

static void
CheckAccountsNeverShareADirectory()
{
	Check(PlaylistCacheKeys::AccountDirectory("playlists", "").empty());
	Check(PlaylistCacheKeys::AccountDirectory("", "user").empty());
	Check(PlaylistCacheKeys::AccountDirectory("library", "user_1")
		== "library/user_1");
	// Escaping is reversible: "a.b" and "a_b" must not collide.
	Check(PlaylistCacheKeys::AccountDirectoryName("a.b") == "a~2Eb");
	Check(PlaylistCacheKeys::AccountDirectoryName("a.b")
		!= PlaylistCacheKeys::AccountDirectoryName("a_b"));
	Check(PlaylistCacheKeys::AccountDirectoryName("../x") == "~2E~2E~2Fx");
}

int
main()
{
	CheckIdsCannotLeaveTheCacheDirectory();
	CheckUriIdsAreValidated();
	CheckAccountsNeverShareADirectory();
	std::puts("PlaylistCacheKeysTest passed");
	return 0;
}
