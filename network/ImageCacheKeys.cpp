#include "ImageCacheKeys.h"

uint64_t
ImageCacheKeys::UrlHash(const std::string& url)
{
	uint64_t hash = 14695981039346656037ULL;
	for (unsigned char character : url) {
		hash ^= character;
		hash *= 1099511628211ULL;
	}
	return hash;
}


std::string
ImageCacheKeys::FileName(const std::string& url)
{
	static const char kHex[] = "0123456789abcdef";
	uint64_t hash = UrlHash(url);
	std::string name(16, '0');
	for (int index = 15; index >= 0; index--) {
		name[index] = kHex[hash & 0x0f];
		hash >>= 4;
	}
	return name + ".img";
}


int64_t
ImageCacheKeys::PruneTarget(int64_t limitBytes)
{
	if (limitBytes <= 0)
		return 0;
	return limitBytes - limitBytes / 10;
}
