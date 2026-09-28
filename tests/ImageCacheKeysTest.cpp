#include "network/ImageCacheKeys.h"

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

// Regression: the former name used only the last 60 characters of the last
// path segment, so these URLs shared one cache file.
static void
CheckDistinctUrlsGetDistinctFiles()
{
	const std::string ids = "ab67616d0000b273aaaaaaaaaaaaaaaaaaaaaaaa"
		"ab67616d0000b273bbbbbbbbbbbbbbbbbbbbbbbb";
	Check(ImageCacheKeys::FileName("https://mosaic.scdn.co/640/" + ids)
		!= ImageCacheKeys::FileName("https://mosaic.scdn.co/300/" + ids));
	Check(ImageCacheKeys::FileName("https://a.example/img/x")
		!= ImageCacheKeys::FileName("https://b.example/img/x"));
	Check(ImageCacheKeys::FileName("https://a.example/x?size=1")
		!= ImageCacheKeys::FileName("https://a.example/x?size=2"));
}

static void
CheckNameIsStableAndSafe()
{
	std::string name = ImageCacheKeys::FileName("https://i.scdn.co/image/ab");
	Check(name == ImageCacheKeys::FileName("https://i.scdn.co/image/ab"));
	Check(name.size() == 20 && name.compare(16, 4, ".img") == 0);
	Check(name.find('/') == std::string::npos);
	// FNV-1a 64 reference value for the empty input.
	Check(ImageCacheKeys::UrlHash("") == 14695981039346656037ULL);
}

static void
CheckPruneLeavesHeadroom()
{
	Check(ImageCacheKeys::PruneTarget(0) == 0);
	Check(ImageCacheKeys::PruneTarget(1000) == 900);
	Check(ImageCacheKeys::PruneTarget(500LL * 1024 * 1024) < 500LL * 1024 * 1024);
}

int
main()
{
	CheckDistinctUrlsGetDistinctFiles();
	CheckNameIsStableAndSafe();
	CheckPruneLeavesHeadroom();
	std::puts("ImageCacheKeysTest passed");
	return 0;
}
